{ pkgs ? import <nixpkgs> { } }:
let
  esp-idf = (pkgs.callPackage ./esp-idf.nix { });
  esp32-toolchain = (pkgs.callPackage ./esp32-toolchain.nix { });
in pkgs.mkShell {
  buildInputs = [ esp-idf esp32-toolchain ];
  shellHook = ''
    export IDF_PATH="${esp-idf.outPath}"
    export NIX_CFLAGS_LINK=-lncurses
    export PATH=$PATH:$IDF_PATH/tools
  '';
}
