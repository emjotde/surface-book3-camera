{ self }:
{ config, lib, pkgs, ... }:
let
  cfg = config.hardware.xps7390Webcam;
  modules = config.boot.kernelPackages.callPackage ./driver.nix {
    src = "${self}/driver";
  };
  softisp = pkgs.callPackage ./softisp.nix {
    src = "${self}/softisp";
  };
in {
  options.hardware.xps7390Webcam = {
    enable = lib.mkEnableOption "the Dell XPS 13 7390 2-in-1 IPU4P webcam";
    firmwareFile = lib.mkOption {
      type = lib.types.nullOr lib.types.path;
      default = null;
      description = "Path to the locally extracted ipu4p_cpd.bin firmware file.";
    };
  };

  config = lib.mkIf cfg.enable {
    assertions = [{
      assertion = cfg.firmwareFile != null;
      message = "hardware.xps7390Webcam.firmwareFile must point to ipu4p_cpd.bin";
    }];

    boot.extraModulePackages = [ modules config.boot.kernelPackages.v4l2loopback ];
    boot.blacklistedKernelModules = [ "ov01a10" ];
    boot.kernelModules = [ "v4l2loopback" ];
    boot.extraModprobeConfig = ''
      options v4l2loopback video_nr=57 card_label="XPS Front Camera" exclusive_caps=1
    '';

    hardware.firmware = lib.optional (cfg.firmwareFile != null)
      (pkgs.runCommand "ipu4p-firmware" { } ''
        mkdir -p $out/lib/firmware/intel/ipu
        cp ${cfg.firmwareFile} $out/lib/firmware/intel/ipu/ipu4p_cpd.bin
      '');

    systemd.services.xps7390-webcam-setup = {
      description = "Configure the Dell XPS 13 7390 2-in-1 IPU4P camera";
      wantedBy = [ "multi-user.target" ];
      after = [ "systemd-udev-settle.service" ];
      path = [ pkgs.kmod pkgs.v4l-utils pkgs.coreutils pkgs.gnugrep ];
      serviceConfig = {
        Type = "oneshot";
        RemainAfterExit = true;
        ExecStart = "${self}/scripts/setup-camera.sh";
      };
    };

    systemd.services.xps7390-webcam = {
      description = "Software ISP for the Dell XPS 13 7390 2-in-1 webcam";
      wantedBy = [ "multi-user.target" ];
      requires = [ "xps7390-webcam-setup.service" ];
      after = [ "xps7390-webcam-setup.service" ];
      serviceConfig = {
        ExecStart = "${softisp}/bin/ipu4-softisp /dev/video-ipu4-raw /dev/video57";
        Restart = "always";
        RestartSec = 2;
      };
    };
  };
}
