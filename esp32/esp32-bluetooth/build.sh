#!/usr/bin/env bash

source setenv.sh

# export V=1
make -j6 app && \
./bindgen.sh && \
cargo xbuild --release && \
$IDF_PATH/components/esptool_py/esptool/esptool.py \
	--chip esp32 \
	elf2image \
	--flash_mode "dio" \
	--flash_freq "40m" \
	--flash_size "2MB" \
	-o $TARGET_DIR/esp32-hello.bin \
	$TARGET_DIR/esp32-hello
