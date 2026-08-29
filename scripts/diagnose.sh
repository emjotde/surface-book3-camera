#!/usr/bin/env bash
set -u

echo '== Computer =='
printf 'Vendor: '; cat /sys/class/dmi/id/sys_vendor 2>/dev/null || true
printf 'Product: '; cat /sys/class/dmi/id/product_name 2>/dev/null || true
printf 'Kernel: '; uname -r

echo
echo '== IPU PCI device =='
lspci -nn 2>/dev/null | grep -i '8086:8a19' || true

echo
echo '== Modules =='
lsmod | grep -E 'ipu4|ipu_bridge|int346|ov01|v4l2loopback' || true

echo
echo '== Devices =='
ls -l /dev/video57 /dev/video-ipu4-raw 2>&1 || true

if command -v v4l2-ctl >/dev/null; then
  echo
echo '== V4L2 devices =='
  v4l2-ctl --list-devices 2>&1 || true
fi

echo
echo '== Services =='
systemctl status xps7390-webcam-setup.service xps7390-webcam.service --no-pager 2>&1 || true

echo
echo '== Service log =='
journalctl -b -u xps7390-webcam-setup.service -u xps7390-webcam.service --no-pager 2>&1 || true

echo
echo '== Kernel log =='
if dmesg >/dev/null 2>&1; then
  dmesg | grep -Ei 'ipu4|ov01a10|int346f|firmware|v4l2' || true
else
  echo 'Run this script as root to include the kernel log.'
fi
