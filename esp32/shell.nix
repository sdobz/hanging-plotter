{ pkgs ? import <nixpkgs> {} }:
let 
  esp-idf = (pkgs.callPackage ./esp-idf.nix {});
  esp32-toolchain = (pkgs.callPackage ./esp32-toolchain.nix {});
  # rust-xtensa = (pkgs.callPackage ./rust-xtensa.nix {});
  nixpkgs-mozilla = pkgs.fetchFromGitHub {
      owner = "mozilla";
      repo = "nixpkgs-mozilla";
      # commit from: 2020-2-19
      rev = "e912ed483e980dfb4666ae0ed17845c4220e5e7c";
      sha256 = "08fvzb8w80bkkabc1iyhzd15f4sm7ra10jn32kfch5klgl0gj3j3";
   };
in
with import "${nixpkgs-mozilla.out}/rust-overlay.nix" pkgs pkgs;
  pkgs.mkShell {
    buildInputs = [ 
      esp-idf
      esp32-toolchain
      latest.rustChannels.stable.rust
      pkgs.cargo-generate
      pkgs.esptool
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
