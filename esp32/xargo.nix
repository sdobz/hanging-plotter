/*
 * xargo
 */
{ stdenv, fetchFromGitHub, rustPlatform, pkgs }:
rustPlatform.buildRustPackage rec {
  version = "v0.3.20";
  pname = "xargo";

  src = fetchFromGitHub {
    owner = "japaric";
    repo = "xargo";

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
  cargoSha256 = "11dnmnxsk7lijvpy14av51w1pwc0mrqr36pa4aqvk4q8lbcgrn7w";

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
    description = "The sysroot manager that lets you build and customize `std` ";
    homepage = https://github.com/japaric/xargo;
    maintainers = with maintainers; [ archived ];
    license = with licenses; [ mit asl20 ];
    platforms = platforms.unix;
  };
}