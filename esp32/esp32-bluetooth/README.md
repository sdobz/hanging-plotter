This example project builds rust onto an esp32 using esp-idf v4.0

Intended to use with the [rust-esp-nix](https://github.com/sdobz/rust-esp-nix) nix shell

See [this blog post](#todo) for details

Example usage:
```
$ git clone https://github.com/sdobz/esp32-hello
$ cd esp32-hello
$ nix-shell shell.nix
<wait for a long long time while we build the world>
[shell] $ ./all.sh /dev/ttyUSB0
...
 (2317) Rust: I, live, again!.
```


## Improvements:
This flashes the bin to 0x10000 which may or may not be accurate. Can building the bootloader be added to this project?

esp-idf is cloned locally. Can this be shared system wide in a sane manner? References are in .cargo/config