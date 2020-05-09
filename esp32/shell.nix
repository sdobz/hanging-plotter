{ pkgs ? import <nixpkgs> {} }:
let 
  esp-idf = (pkgs.callPackage ./esp-idf.nix {});
  esp32-toolchain = (pkgs.callPackage ./esp32-toolchain.nix {});
  #rust-xtensa = (pkgs.callPackage ./rust-xtensa.nix {});

  unstable = import <unstable> {
    overlays = [ (import ./rust-overlay.nix) ];
  };
in pkgs.mkShell {
  
    buildInputs = [ 
      esp-idf
      esp32-toolchain
      #rust-xtensa
      unstable.rustc-xtensa
      unstable.cargo
    ];

    shellHook = ''
set -e

export IDF_PATH=${esp-idf}
export IDF_TOOLS_PATH=${esp32-toolchain}

export CFLAGS_COMPILE="-Wno-error=incompatible-pointer-types -Wno-error=implicit-function-declaration"
export OPENOCD_SCRIPTS=$IDF_TOOLS_PATH/tools/openocd-esp32/share/openocd/scripts
export NIX_CFLAGS_LINK=-lncurses
export PATH=$PATH:$IDF_PATH/tools:$IDF_PATH/components/esptool_py/esptool:$IDF_TOOLS_PATH/tools/esp32ulp-elf/bin:$IDF_TOOLS_PATH/tools/openocd-esp32/bin:$IDF_TOOLS_PATH/tools/xtensa-esp32-elf/bin
    '';
}
