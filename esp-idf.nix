{ stdenv, fetchFromGitHub, pkgs, makeWrapper }:

let
  version = "v4.1-dev";

  pypkgs = python-packages: with python-packages; [
    pyserial
    click
    cryptography
    future
    pyparsing
    pyelftools
    setuptools
  ];
  python = pkgs.python2.withPackages pypkgs;

in stdenv.mkDerivation rec {
  name = "esp-idf";

  src = fetchFromGitHub {
    owner = "espressif";
    repo = "esp-idf";
    rev  = "${version}";
    fetchSubmodules = true;
    sha256 = "0d1iqxz1jqz3rrk2c5dq33wp1v71d9190wv3bnigxlp5kcsj0j1w";
  };

  buildInputs = [
    python
  ];

  propagatedBuildInputs = [
    pkgs.cmake
    pkgs.ninja
    pkgs.gcc
    pkgs.git
    pkgs.ncurses
    pkgs.flex
    pkgs.bison
    pkgs.gperf
    pkgs.ccache
    python
  ];

  phases = [ "unpackPhase" "installPhase" "fixupPhase" ];

  installPhase = ''
    cp -r . $out
  '';

  meta = with stdenv.lib; {
    description = "ESP IDF";
    homepage = https://docs.espressif.com/projects/esp-idf/en/stable/get-started/linux-setup.html;
    license = licenses.gpl3;
  };
}