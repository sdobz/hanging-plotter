{ pkgs ? import <nixpkgs> {} }:
let 
  unstable = import <unstable> {};
in
  pkgs.mkShell {
    buildInputs = [ 
      pkgs.arduino
      unstable.pkgs.pulseview
    ];
    shellHook = ''
set -e
export ARDUINO_DIR=${pkgs.arduino-core}/share/arduino
export _JAVA_AWT_WM_NONREPARENTING=1
export PATH="$PATH:$ARDUINO_DIR"
    '';
}
