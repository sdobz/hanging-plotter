#!/usr/bin/env bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

export RUSTFLAGS=--sysroot=$DIR/target/sysroot
cargo check --message-format=json
