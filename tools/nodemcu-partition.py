#!/usr/bin/env python
#
# ESP8266 LFS Loader Utility
#
# Copyright (C) 2019 Terry Ellison, NodeMCU Firmware Community Project. drawing
# heavily from and including content from esptool.py with full acknowledgement
# under GPL 2.0, with said content: Copyright (C) 2014-2016 Fredrik Ahlberg, Angus
# Gratton, Espressif Systems  (Shanghai) PTE LTD, other contributors as noted.
# https://github.com/espressif/esptool
#
# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; either version 2 of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc., 51 Franklin
# Street, Fifth Floor, Boston, MA 02110-1301 USA.

import os
import sys
sys.path.append(os.path.realpath(os.path.dirname(__file__) + '/toolchains/'))
import esptool

import io
import tempfile
import shutil

from pprint import pprint

import argparse
import gzip
import copy
import inspect
import struct
import string

__version__     = '1.0'
__program__     = 'nodemcu-partition.py'
ROM0_Seg        =   0x010000
FLASH_PAGESIZE  =   0x001000
FLASH_BASE_ADDR = 0x40200000
PARTITION_TYPES = {
       4: 'RF_CAL',
       5: 'PHY_DATA',
       6: 'SYSTEM_PARAMETER',
     101: 'EAGLEROM',
     102: 'IROM0TEXT',
     103: 'LFS0',
     104: 'LFS1',
     105: 'TLSCERT',
     106: 'SPIFFS0',
     107: 'SPIFFS1'}

MAX_PT_SIZE = 20*3
FLASH_SIG          = 0xfafaa150
FLASH_SIG_MASK     = 0xfffffff0
FLASH_SIG_ABSOLUTE = 0x00000001
WORDSIZE           = 4
WORDBITS           = 32

PACK_INT    = struct.Struct("<I")

class FatalError(RuntimeError):
    def __init__(self, message):
        RuntimeError.__init__(self, message)

    def WithResult(message, result):
        message += " (result was %s)" % hexify(result)
        return FatalError(message)

def load_PT(data, args):
    """
    Load the Flash copy of the Partition Table from the first segment of the IROM0
    segment, that is at 0x10000.  If nececessary the LFS partition is then correctly
    positioned and adjusted according to the optional start and len arguments.

    The (possibly) updated PT is then returned with the LFS sizing.
    """
    pt = [PACK_INT.unpack_from(data,4*i)[0] for i in range(0, MAX_PT_SIZE)]
    n, flash_used_end, rewrite = 0, 0, False
    LFSaddr, LFSsize = None, None

    # The partition table format is a set of 3*uint32 fields (type, addr, size), 
    # with the last slot being an end marker (0,size,0) where size is the size of 
    # the firmware image.

    pt_map = dict()
    for i in range(0,MAX_PT_SIZE,3):
        if pt[i] == 0:
            n = i // 3
            break
        elif pt[i] in PARTITION_TYPES:
            pt_map[PARTITION_TYPES[pt[i]]] = i       
        else:
            raise FatalError("Unknown partition type: %u" % pt[i]) 

    flash_used_end = pt[3*n+1]
 
    if not ('IROM0TEXT' in pt_map and 'LFS0' in pt_map):
        raise FatalError("Partition table must contain IROM0 and LFS segments")

    i = pt_map['IROM0TEXT']
    if pt[i+2] == 0:
        pt[i+2] = (flash_used_end - FLASH_BASE_ADDR) - pt[i+1]

    j = pt_map['LFS0']
    if args.la is not None: 
        pt[j+1] = args.la
    elif pt[j+1] == 0:
        pt[j+1] = pt[i+1] + pt[i+2]

    if args.ls is not None:
        pt[j+2] = args.ls
    elif pt[j+2] == 0:
        pt[j+2] = 0x10000

    k = pt_map['SPIFFS0']
    if args.sa is not None: 
        pt[k+1] = args.sa
    elif pt[k+1] == 0:
        pt[k+1] = pt[j+1] + pt[j+2]

    if args.ss is not None:
        pt[k+2] = args.ss

    LFSaddr, LFSsize = pt[j+1], pt[j+2]
    print ('\nDump of Partition Table\n')

    for i in range(0,3*n,3):
        print ('%-18s  0x%06x  0x%06x' % (PARTITION_TYPES[pt[i]], pt[i+1], pt[i+2]))

    return pt, pt_map, n

def relocate_lfs(data, addr, size):
    """
    The unpacked LFS image comprises the relocatable image itself, followed by a bit
    map (one bit per word) flagging if the corresponding word of the image needs
    relocating.  The image and bitmap are enumerated with any addresses being
    relocated by the LFS base address.  (Note that the PIC format of addresses is word
    aligned and so first needs scaling by the wordsize.)
    """
    addr += FLASH_BASE_ADDR
    w = [PACK_INT.unpack_from(data,WORDSIZE*i)[0] \
            for i in range(0, len(data) // WORDSIZE)]
    flash_sig, flash_size = w[0], w[1]

    assert ((flash_sig & FLASH_SIG_MASK) == FLASH_SIG and
            (flash_sig & FLASH_SIG_ABSOLUTE) == 0 and
             flash_size % WORDSIZE == 0)

    flash_size //= WORDSIZE
    flags_size = (flash_size + WORDBITS - 1) // WORDBITS

    assert (WORDSIZE*flash_size <= size and
            len(data) == WORDSIZE*(flash_size + flags_size))

    image,flags,j    = w[0:flash_size], w[flash_size:], 0

    for i in range(0,len(image)):
        if i % WORDBITS == 0:
            flag_word = flags[j]
            j += 1
        if (flag_word & 1) == 1:
            o = image[i]
            image[i] = WORDSIZE*image[i] + addr
        flag_word >>= 1

    return ''.join([PACK_INT.pack(i) for i in image])

def main():

    def arg_auto_int(x):
        ux = x.upper()
        if "MB" in ux:
            return int(ux[:ux.index("MB")]) * 1024 * 1024
        elif "KB" in ux:
            return int(ux[:ux.index("KB")]) * 1024
        else:
            return int(ux, 0)

    print('%s V%s' %(__program__, __version__))

    # ---------- process the arguments ---------- #

    a = argparse.ArgumentParser(
        description='%s V%s - ESP8266 NodeMCU Loader Utility' % 
                     (__program__, __version__),
        prog='esplfs')
    a.add_argument('--port', '-p', help='Serial port device')
    a.add_argument('--baud', '-b',  type=arg_auto_int,
        help='Serial port baud rate used when flashing/reading')
    a.add_argument('--lfs-addr', '-la', dest="la", type=arg_auto_int,
        help='(Overwrite) start address of LFS partition')
    a.add_argument('--lfs-size', '-ls', dest="ls", type=arg_auto_int,
        help='(Overwrite) length of LFS partition')
    a.add_argument('--lfs-file', '-lf', dest="lf", help='LFS image file')
    a.add_argument('--spiffs-addr', '-sa', dest="sa", type=arg_auto_int,
        help='(Overwrite) start address of SPIFFS partition')
    a.add_argument('--spiffs-size', '-ss', dest="ss", type=arg_auto_int,
        help='(Overwrite) length of SPIFFS partition')
    a.add_argument('--spiffs-file', '-sf', dest="sf", help='SPIFFS image file')

    arg = a.parse_args()

    if arg.lf is not None:
        if not os.path.exists(arg.lf):
            raise FatalError("LFS image %s does not exist" % arg.lf)

    if arg.sf is not None:
        if not os.path.exists(arg.sf):
           raise FatalError("SPIFFS image %s does not exist" % arg.sf)

    base = [] if arg.port is None else ['--port',arg.port]
    if arg.baud is not None: base.extend(['--baud',arg.baud])

    # ---------- Use esptool to read the PT ---------- #

    tmpdir  = tempfile.mkdtemp()
    pt_file = tmpdir + '/pt.dmp'
    espargs = base+['--after', 'no_reset', 'read_flash', '--no-progress',
                    str(ROM0_Seg), str(FLASH_PAGESIZE), pt_file]
    esptool.main(espargs)

    with open(pt_file,"rb") as f:
        data = f.read()

    pt, pt_map, n = load_PT(data, arg)
    n = n+1

    odata = ''.join([PACK_INT.pack(pt[i]) for i in range(0,3*n)]) + \
            "\xFF" * len(data[3*4*n:])

    # ---------- If the PT has changed then use esptool to rewrite it ---------- #

    if odata != data:
        print("PT updated")
        pt_file = tmpdir + '/opt.dmp'
        with open(pt_file,"wb") as f:
            f.write(odata)
        espargs = base+['--after', 'no_reset', 'write_flash', '--no-progress',
                        str(ROM0_Seg), pt_file]
        esptool.main(espargs)

    if arg.lf is not None:
        i     = pt_map['LFS0']
        la,ls = pt[i+1], pt[i+2]

        # ---------- Read and relocate the LFS image ---------- #
    
        with gzip.open(arg.lf) as f:
            lfs = f.read()
            lfs = relocate_lfs(lfs, la, ls)

        # ---------- Write to a temp file and use esptool to write it to flash ---------- #

        img_file = tmpdir + '/lfs.img'
        espargs = base + ['write_flash', str(la), img_file]
        with open(img_file,"wb") as f:
            f.write(lfs)
        esptool.main(espargs)

    if arg.sf is not None:
        sa = pt[pt_map['SPIFFS0']+1]

        # ---------- Write to a temp file and use esptool to write it to flash ---------- #

        spiffs_file = arg.sf
        espargs = base + ['', str(sa), spiffs_file]
        esptool.main(espargs)

    # ---------- Clean up temp directory ---------- #

    espargs = base + ['--after', 'hard_reset', 'flash_id']
    esptool.main(espargs)

    shutil.rmtree(tmpdir)

def _main():
        main()

if __name__ == '__main__':
    _main()
