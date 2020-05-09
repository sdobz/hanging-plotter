{ stdenv, fetchFromGitHub, pkgs, rustPlatform, cacert }:
let
  llvm-xtensa = (pkgs.callPackage ./llvm-xtensa.nix {});
  lib = stdenv.lib;

  nixpkgs-mozilla = pkgs.fetchFromGitHub {
    owner = "mozilla";
    repo = "nixpkgs-mozilla";
    # commit from: 2020-2-19
    rev = "e912ed483e980dfb4666ae0ed17845c4220e5e7c";
    sha256 = "08fvzb8w80bkkabc1iyhzd15f4sm7ra10jn32kfch5klgl0gj3j3";
  };

  rust = {
    toRustTarget = platform: with platform.parsed; let
      cpu_ = {
        "armv7a" = "armv7";
        "armv7l" = "armv7";
        "armv6l" = "arm";
      }.${cpu.name} or platform.rustc.arch or cpu.name;
    in platform.rustc.config
      or "${cpu_}-${vendor.name}-${kernel.name}${lib.optionalString (abi.name != "unknown") "-${abi.name}"}";
  };

  pkgsBuildBuild = pkgs;
  pkgsBuildHost = pkgs;
  pkgsBuildTarget = pkgs;


  inherit (stdenv.lib) optionals optional optionalString;

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

  # rustc complains about modified source files otherwise
  dontUpdateAutotoolsGnuConfigScripts = true;

  stripDebugList = [ "bin" ];

  # fetchcargo = pkgs.callPackage <nixpkgs/pkgs/build-support/rust/fetchcargo.nix> {};
  # cargoDeps = fetchcargo {
  #   inherit name;
  #   sourceRoot = null;
  #   srcs = null;
  #   patches = [];
  #   sha256 = "0000000000000000000000000000000000000000000000000000";
  # };
  
  # postUnpack = ''
  #   unpackFile "$cargoDeps"
  #   cargoDepsCopy=$(stripHash $(basename $cargoDeps))
  #   ls
  # '';

  RUSTFLAGS = "-Ccodegen-units=10";
  CARGO_HTTP_CAINFO = "${cacert}/etc/ssl/certs/ca-bundle.crt";
  SSL_CERT_FILE = "${cacert}/etc/ssl/certs/ca-bundle.crt";
    # We need rust to build rust. If we don't provide it, configure will try to download it.
  # Reference: https://github.com/rust-lang/rust/blob/master/src/bootstrap/configure.py
  configureFlags = let
    setBuild  = "--set=target.${rust.toRustTarget stdenv.buildPlatform}";
    setHost   = "--set=target.${rust.toRustTarget stdenv.hostPlatform}";
    setTarget = "--set=target.${rust.toRustTarget stdenv.targetPlatform}";
    ccForBuild  = "${pkgsBuildBuild.targetPackages.stdenv.cc}/bin/${pkgsBuildBuild.targetPackages.stdenv.cc.targetPrefix}cc";
    cxxForBuild = "${pkgsBuildBuild.targetPackages.stdenv.cc}/bin/${pkgsBuildBuild.targetPackages.stdenv.cc.targetPrefix}c++";
    ccForHost  = "${pkgsBuildHost.targetPackages.stdenv.cc}/bin/${pkgsBuildHost.targetPackages.stdenv.cc.targetPrefix}cc";
    cxxForHost = "${pkgsBuildHost.targetPackages.stdenv.cc}/bin/${pkgsBuildHost.targetPackages.stdenv.cc.targetPrefix}c++";
    ccForTarget  = "${pkgsBuildTarget.targetPackages.stdenv.cc}/bin/${pkgsBuildTarget.targetPackages.stdenv.cc.targetPrefix}cc";
    cxxForTarget = "${pkgsBuildTarget.targetPackages.stdenv.cc}/bin/${pkgsBuildTarget.targetPackages.stdenv.cc.targetPrefix}c++";

    rustPkgs = latest.rustChannels.stable;
  in [
    "--release-channel=stable"
    "--enable-local-rebuild"
    "--set=build.rustc=${rustPkgs.rustc}/bin/rustc"
    "--set=build.cargo=${rustPkgs.cargo}/bin/cargo"
    "--set=build.rustfmt=${pkgs.rustfmt}/bin/rustfmt"
    "--enable-rpath"
    "--enable-vendor"
    "--build=${rust.toRustTarget stdenv.buildPlatform}"
    "--host=${rust.toRustTarget stdenv.hostPlatform}"
    "--target=${rust.toRustTarget stdenv.targetPlatform}"
    "--llvm-root=${llvm-xtensa}"

    "${setBuild}.cc=${ccForBuild}"
    "${setHost}.cc=${ccForHost}"
    "${setTarget}.cc=${ccForTarget}"

    "${setBuild}.linker=${ccForBuild}"
    "${setHost}.linker=${ccForHost}"
    "${setTarget}.linker=${ccForTarget}"

    "${setBuild}.cxx=${cxxForBuild}"
    "${setHost}.cxx=${cxxForHost}"
    "${setTarget}.cxx=${cxxForTarget}"
  ] ++ optionals stdenv.isLinux [
    "--enable-profiler" # build libprofiler_builtins
  ];

  postConfigure = ''
    substituteInPlace Makefile \
      --replace 'BOOTSTRAP_ARGS :=' 'BOOTSTRAP_ARGS := --jobs $(NIX_BUILD_CORES)'
  '';
  postPatch = ''
    patchShebangs src/etc

    rm -rf src/llvm

    # Fix the configure script to not require curl as we won't use it
    sed -i configure \
      -e '/probe_need CFG_CURL curl/d'
    # Useful debugging parameter
    export VERBOSE=1
  '';

  nativeBuildInputs = with pkgs; [
    file python3 rustc cmake
    which libffi removeReferencesTo pkgconfig
  ];
  
  buildInputs = [
    pkgs.openssl
    llvm-xtensa
  ];

  # rustc unfortunately needs cmake to compile llvm-rt but doesn't
  # use it for the normal build. This disables cmake in Nix.
  dontUseCmakeConfigure = true;

  outputs = [ "out" "man" "doc" ];
  setOutputFlags = false;

  configurePlatforms = [];

  requiredSystemFeatures = [ "big-parallel" ];

  meta = with stdenv.lib; {
    description = "rust xtensa";
    homepage = https://github.com/espressif/llvm-project;
    license = licenses.apache;
  };
}