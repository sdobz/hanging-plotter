{ pkgs ? import <nixpkgs> {} }:

let
  flutterPkgs = (import (builtins.fetchTarball "https://github.com/babariviere/nixpkgs/archive/flutter-testing.tar.gz")  {});
  unstablePkgs    = (import (builtins.fetchTarball "https://github.com/NixOS/nixpkgs/archive/nixpkgs-unstable.tar.gz")   {});
in
  pkgs.mkShell {
    buildInputs = with pkgs; [
      flutterPkgs.flutter
      unstablePkgs.android-studio
      jdk
      git
    ];

    shellHook=''
      export USE_CCACHE=1
      export ANDROID_JAVA_HOME=${pkgs.jdk.home}
      export ANDROID_HOME=~/.androidsdk
      export FLUTTER_SDK=${flutterPkgs.flutter.unwrapped}
      export _JAVA_AWT_WM_NONREPARENTING=1
    '';
  }
