self: super:
let
    llvm-xtensa = (super.callPackage ./llvm-xtensa.nix {});
    fetchCargoTarball = super.callPackage <unstable/pkgs/build-support/rust/fetchCargoTarball.nix> {};
    lib = super.lib;
    lists = lib.lists;

    toRustTarget = platform: with platform.parsed; let
        cpu_ = {
            "armv7a" = "armv7";
            "armv7l" = "armv7";
            "armv6l" = "arm";
        }.${cpu.name} or platform.rustc.arch or cpu.name;
        in platform.rustc.config
        or "${cpu_}-${vendor.name}-${kernel.name}${lib.optionalString (abi.name != "unknown") "-${abi.name}"}";

    bootstrapPlatform = rec {
        rust = super.callPackage <unstable/pkgs/development/compilers/rust/binary.nix> rec {
            # Noted while installing out of band
            # https://static.rust-lang.org/dist/2020-03-12/rust-std-beta-x86_64-unknown-linux-gnu.tar.xz
            # https://static.rust-lang.org/dist/2020-03-12/rustc-beta-x86_64-unknown-linux-gnu.tar.xz
            # https://static.rust-lang.org/dist/2020-03-12/cargo-beta-x86_64-unknown-linux-gnu.tar.xz
            # https://static.rust-lang.org/dist/2020-01-31/rustfmt-nightly-x86_64-unknown-linux-gnu.tar.xz

            # noted by inspecting https://static.rust-lang.org/dist/2020-03-12
            # version = "1.42.0";
            # version = "nightly";
            version = "beta";

            platform = toRustTarget super.stdenv.hostPlatform;
            versionType = "bootstrap";

            src = super.fetchurl {
                url = "https://static.rust-lang.org/dist/2020-03-12/rust-${version}-${platform}.tar.gz";
                # sha256 = "0llhg1xsyvww776d1wqaxaipm4f566hw1xyy778dhcwakjnhf7kx"; # 1.42.0
                # sha256 = "0jhggcwr852c4cqb4qv9a9c6avnjrinjnyzgfi7sx7n1piyaad43"; # nightly
                sha256 = "1cv402wp9dx6dqd9slc8wqsqkrb7kc66n0bkkmvgjx01n1jhv7n5"; # beta
            };
        };
    };

in
{
    rustc-xtensa = (super.rustc.override {
        rustPlatform = bootstrapPlatform;
    }).overrideAttrs ( old: rec {
        pname = "rustc-xtensa";

        llvmSharedForBuild = llvm-xtensa;
        llvmSharedForHost = llvm-xtensa;
        llvmSharedForTarget = llvm-xtensa;
        llvmShared = llvm-xtensa;
        patches = [];

        nativeBuildInputs = old.nativeBuildInputs; # ++ [ super.breakpointHook ];

        src = super.fetchFromGitHub {
            owner = "MabezDev";
            repo = "rust-xtensa";
            # rust 1.42++
            rev  = "25ae59a82487b8249b05a78f00a3cc35d9ac9959";
            fetchSubmodules = true;
            sha256 = "1xr8rayvvinf1vahzfchlkpspa5f2nxic1j2y4dgdnnzb3rkvkg5";
        };
        
        configureFlags = 
            (lists.remove "--enable-llvm-link-shared"
            (lists.remove "--release-channel=stable" old.configureFlags)) ++ [
            "--set=build.rustfmt=${super.rustfmt}/bin/rustfmt"
            "--llvm-root=${llvm-xtensa}"
            "--experimental-targets=Xtensa"
            "--release-channel=beta"
        ];

        cargoDeps = fetchCargoTarball {
            inherit pname;
            inherit src;
            sourceRoot = null;
            srcs = null;
            patches = [];
            sha256 = "0z4mb33f72ik8a1k3ckbg3rf6p0403knx5mlagib0fs2gdswg9w5";
        };

        postConfigure = ''
            ${old.postConfigure}
            unpackFile "$cargoDeps"
            mv $(stripHash $cargoDeps) vendor
            export VERBOSE=1
        '';
    });
}
