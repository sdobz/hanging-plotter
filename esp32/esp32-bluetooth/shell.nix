let
  pythonPackageOverrides = self: super: {
    pyparsing = super.buildPythonPackage rec {
      pname = "pyparsing";
      version = "2.3.1";
      doCheck = false;
      src = super.fetchPypi {
        inherit pname version;
        sha256 = "0yk6xl885b91dmlhlsap7x78hk2rdr879fln9anbq6k4ca42djb6";
      };
    };
  };

  idf-package-overlay = self: super: {
    python2 = super.python2.override {
      packageOverrides = pythonPackageOverrides;
    };
  };

  pkgs = import (builtins.fetchTarball {
    # https://releases.nixos.org/nixos/unstable/nixos-20.09pre223023.fce7562cf46
    name = "nixos-unstable-2020-04-30";
    url = "https://github.com/nixos/nixpkgs/archive/fce7562cf46727fdaf801b232116bc9ce0512049.tar.gz";
    sha256 = "14rvi69ji61x3z88vbn17rg5vxrnw2wbnanxb7y0qzyqrj7spapx";
  }) {
    overlays = [
      idf-package-overlay
    ];
  };

  rust-esp = pkgs.callPackage (builtins.fetchTarball {
    name = "rust-esp-nix";
    url = "https://github.com/sdobz/rust-esp-nix/archive/791e35c4822a7bdb91a2fbf7323e64255b640bd0.tar.gz";
    sha256 = "0qp3myqpnprf7wfxxvnxpkhs3rg1d85cd9zynrhva7clgs3axnn4";
  }) {};
in
pkgs.mkShell {
    buildInputs = [ 
      rust-esp.xbuild
      rust-esp.bindgen
      rust-esp.rustc
      rust-esp.cargo
      rust-esp.esp-idf
      rust-esp.esp32-toolchain
      pkgs.rustfmt
    ];

    shellHook = ''
${rust-esp.env}

if ! [ -d esp-idf ]; then
  mkdir -p esp-idf
  pushd esp-idf > /dev/null
  git init
  git remote add origin https://github.com/espressif/esp-idf
  git fetch --depth 1 origin 0a03a55c1eb44a354c9ad5d91d91da371fe23f84
  git checkout FETCH_HEAD
  git submodule update --init --recursive
  popd > /dev/null
fi
IDF_PATH=$(pwd)/esp-idf
    '';
}
