{ pkgs ? import <nixpkgs> {} }:

let
  # , stdenv, fetchurl, makeWrapper, buildFHSUserEnv
  stdenv = pkgs.stdenv;
  fetchurl = pkgs.fetchurl;
  fetchzip = pkgs.fetchzip;
  buildFHSUserEnv = pkgs.buildFHSUserEnv;
  makeWrapper = pkgs.makeWrapper;
  lib = pkgs.lib;

  fhsEnv = buildFHSUserEnv {
    name = "esp32-toolchain-env";
    targetPkgs = pkgs: with pkgs; [ zlib libusb1 ];
    runScript = "";
  };
  toolHashes = {
    "xtensa-esp32-elf" = "0yay11fxq5qy65pn63rpfnhfgilv6393r46z6nmhr2mxdhf5g8nc";
    # "xtensa-esp32s2-elf"
    "esp32ulp-elf" = "1z3h76ksfiygz4cv5djgjhjpgqlzl9h3krxb0vv14fd36pw2m735";
    # "esp32s2ulp-elf"
    "openocd-esp32" = "16x6rg7w0byrjg0997123rmfg5amhvm1vlqdky0r84zgchqi2r7d";
  };
  version = "v4.2-dev";

  tools = let
    toolInfoFile = fetchurl {
      url = "https://raw.githubusercontent.com/espressif/esp-idf/${version}/tools/tools.json";
      sha256 = "1p0s85dccl988ahjf006mzrnrc2wfqsf0xkds02z0m7g3z25cq8b";
    };
    toolInfo = builtins.fromJSON (builtins.readFile toolInfoFile);
    filteredTools = builtins.filter (tool: builtins.hasAttr tool.name toolHashes) toolInfo.tools;
    
    fetchTool = tool:
      let
        fileInfo = (builtins.elemAt tool.versions 0).linux-amd64;
      in {
        name = tool.name;
        src = fetchzip {
          url = fileInfo.url;
          sha256 = toolHashes.${tool.name};
        };
      };
  in
    builtins.map fetchTool filteredTools;
in

pkgs.runCommand "esp32-toolchain" {
  buildInputs = [ makeWrapper ];
  meta = with stdenv.lib; {
    description = "ESP32 toolchain";
    homepage = https://docs.espressif.com/projects/esp-idf/en/stable/get-started/linux-setup.html;
    license = licenses.gpl3;
  };
} ''
${lib.strings.concatStrings (builtins.map ({name, src}: ''
mkdir -p $out/tools
TOOLDIR=$out/tools/${name}
cp -r ${src} $TOOLDIR
chmod u+w $TOOLDIR/bin
for FILE in $(ls $TOOLDIR/bin); do
  FILE_PATH="$TOOLDIR/bin/$FILE"
  if [[ -x $FILE_PATH ]]; then
    mv $FILE_PATH $FILE_PATH-unwrapped
    makeWrapper ${fhsEnv}/bin/esp32-toolchain-env $FILE_PATH --add-flags "$FILE_PATH-unwrapped"
  fi
done
chmod u-w $TOOLDIR/bin
'') tools)}
''
