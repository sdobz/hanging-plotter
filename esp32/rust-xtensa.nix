{ stdenv, fetchFromGitHub, pkgs }:
let
  llvm-xtensa = (pkgs.callPackage ./llvm-xtensa.nix {});

  nixpkgs-mozilla = pkgs.fetchFromGitHub {
      owner = "mozilla";
      repo = "nixpkgs-mozilla";
      # commit from: 2020-2-19
      rev = "e912ed483e980dfb4666ae0ed17845c4220e5e7c";
      sha256 = "08fvzb8w80bkkabc1iyhzd15f4sm7ra10jn32kfch5klgl0gj3j3";
   };
in
with import "${nixpkgs-mozilla.out}/rust-overlay.nix" pkgs pkgs;
stdenv.mkDerivation rec {
  name = "rust-xtensa";
  version = "25ae59a82487b8249b05a78f00a3cc35d9ac9959";

  src = fetchFromGitHub {
    owner = "MabezDev";
    repo = "rust-xtensa";
    rev  = "${version}";
    fetchSubmodules = true;
    sha256 = "1xr8rayvvinf1vahzfchlkpspa5f2nxic1j2y4dgdnnzb3rkvkg5";
  };

   buildInputs = [
     pkgs.python3
     pkgs.cmake
     pkgs.ninja
     latest.rustChannels.stable.rust
   ];

  phases = [ "unpackPhase" "configurePhase" "buildPhase" "installPhase" "fixupPhase" ];

  configureFlags = [
    "--llvm-root=${llvm-xtensa}"
  ];

  buildPhase = ''
    python x.py build
  '';

  installPhase = ''
    python x.py install
  '';

  meta = with stdenv.lib; {
    description = "rust xtensa";
    homepage = https://github.com/espressif/llvm-project;
    license = licenses.apache2;
  };
}