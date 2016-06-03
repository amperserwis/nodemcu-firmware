#!/bin/sh

if [ ! -e ../tools/update_spiffs.sh ]; then
  echo Must run from the tools directory
  exit 1
fi

git clone https://github.com/pellepl/spiffs

cp spiffs/src/*.[ch] ../app/spiffs

rm -fr spiffs
