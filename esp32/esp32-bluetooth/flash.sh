#!/usr/bin/env bash

source setenv.sh

esptool.py \
  --chip esp32 --port $1 --baud 1500000 --before default_reset \
  --after hard_reset write_flash -z --flash_mode dio --flash_freq 40m \
  --flash_size detect \
  0x10000 $TARGET_DIR/esp32-hello.bin
