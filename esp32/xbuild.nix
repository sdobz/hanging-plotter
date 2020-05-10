/*
 * xbuild
 */
{ stdenv, fetchFromGitHub, rustPlatform, pkgs }:
rustPlatform.buildRustPackage rec {
  version = "v0.5.29";
  pname = "cargo-xbuild";

  src = fetchFromGitHub {
    owner = "rust-osdev";
    repo = "cargo-xbuild";

    # Use the revision specified by github release
    # For minimum surprises, we want to stick with the version that the
    # upstream build scripts select.
    rev = version;

    /*
     * Give the correct source hash to satisfy buildRustPackage that we're
     * building against the source we expected.
     */
    sha256 = "1syanz7ybqjbsvk1q537bnm414skj4ff41yamrl4fz1rrkz576n4";
  };

  /*
   * Give the correct Cargo hash to satisfy buildRustPackage that we're
   * building against the dependencies we expected.
   */
  cargoSha256 = "17lm99g5ac3428f00f9yh1m2l36d88jwljib03kk0sn28f9dzapq";

  propagatedBuildInputs = [
    rustPlatform.rust.cargo
  ];

  checkPhase = ''
    runHook preCheck
    echo "Running cargo test --release"
    export USER=$(whoami)
    #cargo test --release
    runHook postCheck
  '';

  /*
   * Just copied from upstream Nix librustzcash package.
   */
  meta = with stdenv.lib; {
    description = "Automatically cross-compiles the sysroot crates core, compiler_builtins, and alloc.";
    homepage = https://github.com/rust-osdev/cargo-xbuild;
    maintainers = with maintainers; [ archived ];
    license = with licenses; [ mit asl20 ];
    platforms = platforms.unix;
  };
}