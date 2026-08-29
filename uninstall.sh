#!/usr/bin/env bash
set -euo pipefail

if [[ $EUID -ne 0 ]]; then
  echo "Run this script as root: sudo ./uninstall.sh" >&2
  exit 1
fi

systemctl disable --now xps7390-webcam.service xps7390-webcam-setup.service 2>/dev/null || true
rm -f /etc/systemd/system/xps7390-webcam.service
rm -f /etc/systemd/system/xps7390-webcam-setup.service
rm -f /etc/modprobe.d/xps7390-webcam.conf
rm -f /usr/local/libexec/xps7390-webcam-setup
rm -f /usr/local/bin/ipu4-softisp
rm -f /lib/firmware/intel/ipu/ipu4p_cpd.bin
rm -rf /lib/modules/*/updates/xps7390-webcam
if command -v dkms >/dev/null; then
  dkms remove -m xps7390-webcam -v 0.1.0 --all || true
fi
rm -rf /usr/src/xps7390-webcam-0.1.0
depmod -a
systemctl daemon-reload
echo "Removed the XPS webcam driver. Reboot the computer."
