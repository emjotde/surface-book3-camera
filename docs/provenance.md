# Sources and licenses

- Base: [MaximeRivest's Dell IPU4P driver](https://github.com/MaximeRivest/xps-13-7390-2-in-1-webcam-linux);
  original Git history is retained.
- IPU4P modules: mainline Intel IPU6 code with IPU4 hardware definitions from
  Intel's `linux-intel-lts` `4.19/base` branch.
- Surface PHY reference: [xiaoland/sfp7-linux-cam-driver](https://github.com/xiaoland/sfp7-linux-cam-driver).
- Inherited Dell sensor/power modules: mainline OV01A10 and INT3472 code;
  not used by the Surface installation.
- Kernel code retains its SPDX identifiers and GPL-2.0-only licensing.
  GNOME Camera patches retain GPL-3.0-or-later licensing.

Hardware register values are included; Windows drivers are not.
Surface firmware comes from Microsoft's driver package; acquisition and hashes
are documented in [installation](surface-install.md).
Firmware, signing material and private captures must not be committed.
