#!/usr/bin/env bash

export RUSTFLAGS=--sysroot=./target/sysroot
cargo check --message-format=json
