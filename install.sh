#!/usr/bin/env bash
set -euo pipefail

VERSION=0.1.0
NAME=xps7390-webcam
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
KVER=${KERNEL_VERSION:-$(uname -r)}
KDIR=${KDIR:-/lib/modules/$KVER/build}

if [[ $EUID -ne 0 ]]; then
  echo "Run this installer as root: sudo ./install.sh" >&2
  exit 1
fi
if [[ ${FORCE:-0} != 1 ]]; then
  "$ROOT/scripts/check-hardware.sh"
fi
if [[ ${KVER%%.*}.${KVER#*.} != 6.18* ]]; then
  echo "This release supports Linux 6.18.x. Found $KVER." >&2
  echo "Set FORCE=1 only if you will test another kernel." >&2
  exit 1
fi
if [[ ! -d "$KDIR" ]]; then
  echo "Kernel build files are missing at $KDIR." >&2
  echo "Install the headers or development package for kernel $KVER." >&2
  exit 1
fi

for command in make cc install depmod modprobe systemctl media-ctl v4l2-ctl; do
  command -v "$command" >/dev/null || {
    echo "Required command not found: $command" >&2
    exit 1
  }
done
if ! modinfo v4l2loopback >/dev/null 2>&1; then
  echo "Install the v4l2loopback kernel module before this driver." >&2
  exit 1
fi

firmware=$ROOT/firmware/intel/ipu/ipu4p_cpd.bin
if [[ ! -f "$firmware" ]]; then
  echo "Firmware is missing." >&2
  echo "Run scripts/extract-firmware.sh with Dell package 4PY3P first." >&2
  exit 1
fi

make -C "$ROOT" KDIR="$KDIR" clean all
make -C "$ROOT/softisp" install PREFIX=/usr/local

if command -v dkms >/dev/null; then
  source_dir=/usr/src/$NAME-$VERSION
  rm -rf "$source_dir"
  mkdir -p "$source_dir"
  cp -a "$ROOT/driver" "$ROOT/Makefile" "$ROOT/dkms.conf" "$source_dir/"
  dkms remove -m "$NAME" -v "$VERSION" --all >/dev/null 2>&1 || true
  dkms add -m "$NAME" -v "$VERSION"
  dkms build -m "$NAME" -v "$VERSION" -k "$KVER"
  dkms install -m "$NAME" -v "$VERSION" -k "$KVER"
else
  make -C "$ROOT" KDIR="$KDIR" modules
  module_dir=/lib/modules/$KVER/updates/xps7390-webcam
  install -d "$module_dir"
  install -m644 "$ROOT/driver/int346f/int346f.ko" "$module_dir/"
  install -m644 "$ROOT/driver/ov01a10/ov01a10-i346f.ko" "$module_dir/"
  install -m644 "$ROOT/driver/ipu4p/intel-ipu4p.ko" "$module_dir/"
  install -m644 "$ROOT/driver/ipu4p/intel-ipu4p-isys.ko" "$module_dir/"
fi

depmod "$KVER"
install -D -m644 "$firmware" /lib/firmware/intel/ipu/ipu4p_cpd.bin
install -D -m755 "$ROOT/scripts/setup-camera.sh" /usr/local/libexec/xps7390-webcam-setup
install -D -m644 "$ROOT/systemd/xps7390-webcam-setup.service" /etc/systemd/system/xps7390-webcam-setup.service
install -D -m644 "$ROOT/systemd/xps7390-webcam.service" /etc/systemd/system/xps7390-webcam.service
cat >/etc/modprobe.d/xps7390-webcam.conf <<'EOF'
blacklist ov01a10
options v4l2loopback video_nr=57 card_label="XPS Front Camera" exclusive_caps=1
EOF

systemctl daemon-reload
systemctl enable xps7390-webcam-setup.service xps7390-webcam.service

echo
echo "Installation is complete. Reboot the computer."
echo "After reboot, applications will find 'XPS Front Camera' at /dev/video57."
if command -v mokutil >/dev/null && mokutil --sb-state 2>/dev/null | grep -qi enabled; then
  echo "Secure Boot is enabled. You must sign and enroll the four kernel modules."
fi
