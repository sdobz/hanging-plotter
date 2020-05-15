#!/usr/bin/env bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

ELF=$(find ./build -maxdepth 1 -type f -regex ".*\.elf")

: ${TTY:=/dev/ttyUSB0}]

if [ ! -f $ELF ]; then
    echo "Could not find ELF"
    exit 1
fi

print_addrs() { 
    local s=$1 regex=$2
    echo "===== $0 ====="
    while [[ $s =~ $regex ]]; do
        xtensa-esp32-elf-addr2line -pfiaC -e "$ELF" ${BASH_REMATCH[1]}
        s=${s#*"${BASH_REMATCH[1]}"}
    done
    echo "=============="
}

while read line
do
  echo $line;
  if [[ $line =~ "Backtrace:" ]] ; then
    print_addrs "$line" '(0x[a-f0-9]{8}):'
  fi
done < <(miniterm.py --raw $TTY 115200)
