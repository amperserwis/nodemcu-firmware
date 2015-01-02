#ifndef __FLASH_API_H__
#define __FLASH_API_H__
#include "ets_sys.h"
typedef struct __attribute__((packed))
{
    uint8_t unknown0;
    uint8_t unknown1;
    enum
    {
        MODE_QIO = 0,
        MODE_QOUT = 1,
        MODE_DIO = 2,
        MODE_DOUT = 15,
    } mode : 8;
    enum
    {
        SPEED_40MHZ = 0,
        SPEED_26MHZ = 1,
        SPEED_20MHZ = 2,
        SPEED_80MHZ = 15,
    } speed : 4;
    enum
    {
        SIZE_4MBIT = 0,
        SIZE_2MBIT = 1,
        SIZE_8MBIT = 2,
        SIZE_16MBIT = 3,
        SIZE_32MBIT = 4,
    } size : 4;
} SPIFlashInfo;

uint32_t flash_get_size_byte();
uint16_t flash_get_sec_num();
#endif // __FLASH_API_H__
