#!/usr/bin/env bash
set -euo pipefail

product=$(cat /sys/class/dmi/id/product_name 2>/dev/null || true)
vendor=$(cat /sys/class/dmi/id/sys_vendor 2>/dev/null || true)

if [[ "$vendor" != *Dell* || "$product" != "XPS 13 7390 2-in-1" ]]; then
  echo "Unsupported computer: ${vendor:-unknown} ${product:-unknown}" >&2
  echo "This driver only supports the Dell XPS 13 7390 2-in-1." >&2
  exit 1
fi

if ! command -v lspci >/dev/null; then
  echo "lspci is required." >&2
  exit 1
fi

if ! lspci -n | grep -qi '8086:8a19'; then
  echo "Intel IPU4P device 8086:8a19 was not found." >&2
  exit 1
fi

if [[ ! -e /sys/bus/acpi/devices/OVTI01A0:00 ]]; then
  echo "OV01A10 camera sensor OVTI01A0 was not found." >&2
  exit 1
fi

if [[ ! -e /sys/bus/acpi/devices/INT346F:00 ]]; then
  echo "INT346F camera power controller was not found." >&2
  exit 1
fi

echo "Supported camera hardware found."
echo "  Computer: $vendor $product"
echo "  IPU:      Intel 8086:8a19"
echo "  Sensor:   OV01A10"
echo "  Power:    INT346F"
