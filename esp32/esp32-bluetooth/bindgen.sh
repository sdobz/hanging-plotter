#!/usr/bin/env bash

set -e

source setenv.sh

cd esp32-sys

COMPS=$IDF_PATH/components
SYSROOT=$IDF_TOOLS_PATH/tools/xtensa-esp32-elf/xtensa-esp32-elf/

BINDGEN=bindgen
CLANG_FLAGS="\
	--sysroot=$SYSROOT \
    -I../build/include \
	-D__bindgen \
	-target xtensa \
	-x c"

for INC in $(find $COMPS -type d -regex ".*/include"); do
	# echo $INC
	CLANG_FLAGS+=" -I$INC"
done

function generate_bindings ()
{
    declare -r crate=$1

    cd "$crate"

	LIBCLANG_PATH="$LIBCLANG_PATH" \
	"$BINDGEN" \
		--use-core \
		--no-layout-tests \
		--blacklist-type ble_gatt_chr_def \
		$BINDGEN_FLAGS \
		--output src/bindings.rs \
		src/bindings.h \
		-- $CLANG_FLAGS

	rustfmt src/bindings.rs
}

generate_bindings $@
