{ lib, stdenv, src }:

stdenv.mkDerivation {
  pname = "ipu4-softisp";
  version = "0.1.0";
  inherit src;

  buildPhase = "make";
  installPhase = "make install PREFIX=$out";

  meta = {
    description = "Simple RAW10 software ISP for the XPS 7390 webcam";
    license = lib.licenses.gpl2Only;
    platforms = [ "x86_64-linux" ];
  };
}
