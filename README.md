# Linux webcam driver for the Dell XPS 13 7390 2-in-1

This project enables the internal webcam on the **Dell XPS 13 7390 2-in-1**.
It supports the Ice Lake Intel IPU4P camera controller with PCI ID `8086:8a19`.
The laptop uses an OV01A10 sensor and an INT346F camera power controller.

Mainline Linux does not provide a complete driver for this camera stack.
Without this project, Linux usually shows no internal webcam device.

## Current status

- Real 1280×800 RAW10 capture at 60 frames per second.
- A small software ISP provides 640×400 YUYV video to normal applications.
- Browsers and video-call applications see **XPS Front Camera** at `/dev/video57`.
- Automatic exposure and white balance are basic but functional.
- Stream failure detection and automatic recovery are included.
- Linux 6.18.x is the tested kernel series.

This driver is experimental. It currently supports only the exact laptop model above.

## Hardware check

Run this command before installation:

```bash
./scripts/check-hardware.sh
```

The check must find all four items:

- Dell XPS 13 7390 2-in-1
- Intel IPU4P `8086:8a19`
- OV01A10 sensor `OVTI01A0`
- INT346F camera power controller

## Firmware

The Intel camera firmware is proprietary. This repository does not distribute it.
Extract it locally from Dell driver package **4PY3P**:

1. Open the [Dell XPS 13 7390 2-in-1 driver page](https://www.dell.com/support/home/product-support/product/xps-13-7390-2-in-1-laptop/drivers).
2. Download `7390 2in1-win11-A00-4PY3P.CAB`.
3. Install `cabextract` from your distribution.
4. Run:

```bash
./scripts/extract-firmware.sh ~/Downloads/7390\ 2in1-win11-A00-4PY3P.CAB
```

The script verifies the known firmware SHA-256 value. It does not modify the Dell package.

## Install on a systemd distribution

Install these requirements with your distribution package manager:

- C compiler and `make`
- Headers for the running kernel
- `dkms` (recommended)
- `v4l-utils`
- `v4l2loopback`
- `cabextract`
- `pciutils`

Examples of common package names:

| Distribution | Packages |
| --- | --- |
| Debian or Ubuntu | `build-essential linux-headers-$(uname -r) dkms v4l-utils v4l2loopback-dkms cabextract pciutils` |
| Arch Linux | `base-devel linux-headers dkms v4l-utils v4l2loopback-dkms cabextract pciutils` |
| Fedora | `gcc make kernel-devel dkms v4l-utils cabextract pciutils` plus a packaged `v4l2loopback` module |

Then install the driver:

```bash
git clone https://github.com/MaximeRivest/xps-13-7390-2-in-1-webcam-linux.git
cd xps-13-7390-2-in-1-webcam-linux
./scripts/check-hardware.sh
./scripts/extract-firmware.sh ~/Downloads/7390\ 2in1-win11-A00-4PY3P.CAB
sudo ./install.sh
sudo reboot
```

Secure Boot blocks unsigned external modules. Disable Secure Boot or sign all four modules.

## Install on NixOS

First extract `ipu4p_cpd.bin` with the firmware script. Copy it into your NixOS configuration directory.
Then add this repository as a flake input:

```nix
{
  inputs.xps7390-webcam.url =
    "github:MaximeRivest/xps-13-7390-2-in-1-webcam-linux";

  outputs = { nixpkgs, xps7390-webcam, ... }: {
    nixosConfigurations.my-laptop = nixpkgs.lib.nixosSystem {
      system = "x86_64-linux";
      modules = [
        xps7390-webcam.nixosModules.default
        ({ ... }: {
          hardware.xps7390Webcam = {
            enable = true;
            firmwareFile = ./ipu4p_cpd.bin;
          };
        })
      ];
    };
  };
}
```

Rebuild NixOS and reboot.

## Test the camera

Confirm that both services run:

```bash
systemctl status xps7390-webcam-setup.service xps7390-webcam.service
```

View the camera with mpv:

```bash
mpv av://v4l2:/dev/video57 --profile=low-latency --untimed
```

Applications must select **XPS Front Camera**.

## Troubleshooting

Read [`docs/troubleshooting.md`](docs/troubleshooting.md). Include the diagnostic output in bug reports:

```bash
./scripts/diagnose.sh > webcam-diagnostics.txt 2>&1
```

Review the file before you attach it. The report includes the computer model and kernel log.

## Remove the driver

```bash
sudo ./uninstall.sh
sudo reboot
```

## Design and source history

Read [`docs/architecture.md`](docs/architecture.md) and [`docs/provenance.md`](docs/provenance.md).
The kernel modules use GPL-2.0-only code. Each source file keeps its original SPDX identifier.

## Contributing

Test changes on the exact supported model. Report the kernel version and complete diagnostics.
Do not submit Dell or Intel firmware, Windows drivers, or other proprietary files.
