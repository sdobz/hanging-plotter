#!/usr/bin/env bash

set -e

: ${TTY:=/dev/ttyUSB0}]

./build.sh
./flash.sh $TTY
./console.sh $TTY
