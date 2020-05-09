let
  pkgs = import <unstable> {
    overlays = [
      (import ./rust-overlay.nix)
    ];
  };
in let
  esp-idf = (pkgs.callPackage ./esp-idf.nix {});
  esp32-toolchain = (pkgs.callPackage ./esp32-toolchain.nix {});
  llvm-xtensa = (pkgs.callPackage ./llvm-xtensa.nix {});
  stdenv = pkgs.stdenv;
  rust = pkgs.rust;

  nixpkgs-mozilla = pkgs.fetchFromGitHub {
    owner = "mozilla";
    repo = "nixpkgs-mozilla";
    # commit from: 2020-2-19
    rev = "e912ed483e980dfb4666ae0ed17845c4220e5e7c";
    sha256 = "08fvzb8w80bkkabc1iyhzd15f4sm7ra10jn32kfch5klgl0gj3j3";
  };
  moz-pkgs = import <unstable> {
    overlays = [
      (import "${nixpkgs-mozilla.out}/rust-overlay.nix")
    ];
  };

  # rustPlatfromFromBinary = {version, date, sha256}:
  #   (rust.makeRustPlatform
  #   (pkgs.callPackage <unstable/pkgs/development/compilers/rust/binary.nix> rec {
  #     inherit version;

  #     platform = rust.toRustTarget stdenv.hostPlatform;
  #     versionType = date;

  #     src = pkgs.fetchurl {
  #       url = "https://static.rust-lang.org/dist/${date}/rust-${version}-${platform}.tar.gz";
  #       inherit sha256;
  #     };
  #   }));
  # nightlyRustPlatform = rustPlatfromFromBinary {
  #   version = "nightly";
  #   date = "2020-03-12";
  #   sha256 = "0jhggcwr852c4cqb4qv9a9c6avnjrinjnyzgfi7sx7n1piyaad43";
  # };

  nightlyRustPlatform =
    let
      nightly = (moz-pkgs.rustChannelOf {
        date = "2020-03-12";
        channel = "nightly";
      });
      # include rust-src component
      # https://github.com/mozilla/nixpkgs-mozilla/issues/51#issuecomment-386079415
      rust = nightly.rust.override {
        extensions = [
          "rust-std"
          "rust-src"
        ];
      };
    in
    pkgs.makeRustPlatform {
      rustc = rust;
      cargo = rust;
    };

  xargo = pkgs.callPackage ./xargo.nix {
    rustPlatform = nightlyRustPlatform;
  };
in
pkgs.mkShell {

    buildInputs = [ 
      esp-idf
      esp32-toolchain
      xargo
      # bindgen
      pkgs.rustc-xtensa
      # pkgs.rustup
    ];

    shellHook = ''
set -e

# rustup toolchain link xtensa "${pkgs.rustc-xtensa}"


export XARGO_RUST_SRC="${pkgs.rust-xtensa-src}/src"
export LLVM_XTENSA="${llvm-xtensa}"
export LIBCLANG_PATH="${llvm-xtensa}/lib"

export IDF_PATH=${esp-idf}
export IDF_TOOLS_PATH=${esp32-toolchain}

# ln -s $IDF_PATH ./esp32-hello/esp-idf

export CFLAGS_COMPILE="-Wno-error=incompatible-pointer-types -Wno-error=implicit-function-declaration"
export OPENOCD_SCRIPTS=$IDF_TOOLS_PATH/tools/openocd-esp32/share/openocd/scripts
export NIX_CFLAGS_LINK=-lncurses
export PATH=$PATH:~/.cargo/bin:$IDF_PATH/tools:$IDF_PATH/components/esptool_py/esptool:$IDF_TOOLS_PATH/tools/esp32ulp-elf/bin:$IDF_TOOLS_PATH/tools/openocd-esp32/bin:$IDF_TOOLS_PATH/tools/xtensa-esp32-elf/bin
    '';
}
