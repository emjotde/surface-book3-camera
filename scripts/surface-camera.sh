#!/usr/bin/env bash
set -euo pipefail

if (( EUID != 0 )); then
    echo "Run this camera relay through a surface-camera service" >&2
    exit 1
fi
if [[ $(< /sys/class/dmi/id/product_name) != "Surface Book 3" ]]; then
    echo "This experimental configuration is only tested on Surface Book 3" >&2
    exit 1
fi

camera="${1:-rear}"
case "$camera" in
    rear)
        port=0 capture_index=0 sensor_name="ov8865 3-0010"
        width=3264 height=2448 output=50 label="Surface Book 3 Rear Camera"
        output_width=1632 output_height=1224 front=false
        ;;
    front)
        port=2 capture_index=16 sensor_name="ov5693 2-0036"
        width=2592 height=1944 output=51 label="Surface Book 3 Front Camera"
        output_width=1296 output_height=972 front=true
        ;;
    *)
        echo "Unknown camera: $camera (expected front or rear)" >&2
        exit 1
        ;;
esac

exec 9>/run/lock/surface-camera-startup.lock
flock 9

module_dir="/usr/local/lib/surface-camera/$(uname -r)"
if [[ ! -f "$module_dir/intel-ipu4p.ko" ||
      ! -f "$module_dir/intel-ipu4p-isys.ko" ]]; then
    echo "No tested camera modules installed for kernel $(uname -r)" >&2
    exit 1
fi
if [[ $(< /sys/bus/pci/devices/0000:00:05.0/vendor) != 0x8086 ||
      $(< /sys/bus/pci/devices/0000:00:05.0/device) != 0x8a19 ]]; then
    echo "Unexpected camera controller" >&2
    exit 1
fi

modprobe ipu_bridge
modprobe videobuf2_v4l2
modprobe videobuf2_dma_sg
if [[ ! -d /sys/module/intel_ipu4p ]]; then
    insmod "$module_dir/intel-ipu4p.ko"
fi
if [[ ! -L /sys/bus/pci/devices/0000:00:05.0/driver ||
      $(basename "$(readlink -f /sys/bus/pci/devices/0000:00:05.0/driver)") != intel-ipu4p ]]; then
    echo "Camera controller did not bind; inspect the kernel journal" >&2
    exit 1
fi
if [[ ! -d /sys/module/intel_ipu4p_isys ]]; then
    insmod "$module_dir/intel-ipu4p-isys.ko" \
        combo_port_cfg=0x3895 surface_phy=1 dsettle_ovr=880 csettle_ovr=1400
fi
udevadm settle
modprobe v4l2loopback video_nr=50,51 \
    card_label="Surface Book 3 Rear Camera,Surface Book 3 Front Camera" \
    exclusive_caps=1,1
if [[ ! -f "/sys/class/video4linux/video$output/name" ||
      $(< "/sys/class/video4linux/video$output/name") != "$label" ]]; then
    echo "Expected virtual $camera camera at /dev/video$output is unavailable" >&2
    exit 1
fi

media-ctl -d /dev/media0 -l \
    "\"Intel IPU4P CSI2 $port\":1 -> \"Intel IPU4P ISYS Capture $capture_index\":0 [1]"
media-ctl -d /dev/media0 -V \
    "\"$sensor_name\":0 [fmt:SBGGR10_1X10/${width}x${height}]"
media-ctl -d /dev/media0 -V \
    "\"Intel IPU4P CSI2 $port\":0 [fmt:SBGGR10_1X10/${width}x${height}]"
capture="$(media-ctl -d /dev/media0 -e "Intel IPU4P ISYS Capture $capture_index")"
sensor="$(media-ctl -d /dev/media0 -e "$sensor_name")"
if [[ "$camera" == front ]]; then
    v4l2-ctl -d "$sensor" --set-ctrl=test_pattern=0
fi
flock -u 9
exec 9>&-
export GST_PLUGIN_PATH="/usr/local/lib/surface-camera/gstreamer${GST_PLUGIN_PATH:+:$GST_PLUGIN_PATH}"
export GST_DEBUG="${GST_DEBUG:-2}"
exec /usr/bin/v4l2-relayd \
    -i "surfaceisp front=$front capture-device=$capture sensor-device=$sensor ! queue" \
    -s "videotestsrc pattern=black num-buffers=2" \
    -o "appsrc name=appsrc caps=video/x-raw,format=YUY2,width=$output_width,height=$output_height,framerate=30/1,pixel-aspect-ratio=1/1,interlace-mode=progressive,colorimetry=bt601 ! videoconvert ! queue ! v4l2sink name=v4l2sink device=/dev/video$output sync=false"
