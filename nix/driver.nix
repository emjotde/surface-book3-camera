{ lib, stdenv, kernel, src }:

stdenv.mkDerivation {
  pname = "xps7390-webcam-modules";
  version = "0.1.0-${kernel.version}";
  inherit src;

  hardeningDisable = [ "pic" ];
  nativeBuildInputs = kernel.moduleBuildDependencies;
  makeFlags = [ "KDIR=${kernel.dev}/lib/modules/${kernel.modDirVersion}/build" ];

  buildPhase = ''
    runHook preBuild
    make -C int346f $makeFlags
    make -C ov01a10 $makeFlags
    make -C ipu4p $makeFlags
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    destination=$out/lib/modules/${kernel.modDirVersion}/extra
    mkdir -p $destination
    cp int346f/int346f.ko ov01a10/ov01a10-i346f.ko \
      ipu4p/intel-ipu4p.ko ipu4p/intel-ipu4p-isys.ko $destination/
    runHook postInstall
  '';

  meta = {
    description = "IPU4P webcam modules for the Dell XPS 13 7390 2-in-1";
    license = lib.licenses.gpl2Only;
    platforms = [ "x86_64-linux" ];
  };
}
