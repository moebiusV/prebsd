{ lib, stdenvNoCC, fetchFromGitHub, curl, python3 }:

stdenvNoCC.mkDerivation rec {
  pname = "prebsd";
  version = "0.1";

  src = fetchFromGitHub {
    owner = "moebiusV";
    repo = "prebsd";
    rev = "v${version}";
    hash = "";
  };

  buildInputs = [ curl python3 ];
  # recommended (optional): filsys, v7unix-toolchain, simh

  meta = with lib; {
    description = "Fetch and run Research Unix V4-V7 and 32V on SIMH";
    homepage = "https://github.com/moebiusV/prebsd";
    license = licenses.isc;
    maintainers = [ ];
    platforms = platforms.unix;
  };
}
