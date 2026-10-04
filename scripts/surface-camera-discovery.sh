#!/usr/bin/env bash
set -euo pipefail

if [[ $# != 1 && $# != 2 ]] ||
   [[ $# == 2 && ! "$2" =~ ^[1-9][0-9]*$ ]]; then
    echo "Usage: surface-camera-discovery <50|51> [relay-pid]" >&2
    exit 1
fi
case "$1" in
    50|51) ;;
    *)
        echo "Unexpected Surface camera video number: $1" >&2
        exit 1
        ;;
esac

if [[ $# == 2 ]]; then
    while ! result=$(v4l2-ctl -d "/dev/video$1" --get-fmt-video 2>&1); do
        if ! kill -0 "$2" 2>/dev/null; then
            echo "Camera relay exited before its output became ready: $result" >&2
            exit 1
        fi
        sleep 0.1
    done
elif [[ ! -e "/sys/class/video4linux/video$1" ]]; then
    exit 0
fi

# PipeWire 1.0 otherwise retains capabilities after the writer opens or closes.
udevadm trigger --action=remove "/sys/class/video4linux/video$1"
udevadm trigger --action=add "/sys/class/video4linux/video$1"
udevadm settle
