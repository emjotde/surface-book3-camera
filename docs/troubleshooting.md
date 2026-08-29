# Troubleshooting

## No XPS Front Camera device

Run:

```bash
systemctl status xps7390-webcam-setup.service
journalctl -b -u xps7390-webcam-setup.service
ls -l /dev/video57 /dev/video-ipu4-raw
```

Confirm that `v4l2loopback` is installed. Confirm that the firmware exists at:

```text
/lib/firmware/intel/ipu/ipu4p_cpd.bin
```

## Module does not load

Check the kernel log:

```bash
sudo dmesg | grep -Ei 'ipu4|ov01a10|int346f|firmware'
```

This release supports Linux 6.18.x. Kernel interfaces can change between releases.
Build failures on another kernel require a compatibility patch.

Secure Boot rejects unsigned modules. Check it with:

```bash
mokutil --sb-state
```

## White or damaged image

The software ISP detects the known padded-frame failure and restarts automatically.
Check its restart count and log:

```bash
systemctl status xps7390-webcam.service
journalctl -b -u xps7390-webcam.service
```

Restart the relay once:

```bash
sudo systemctl restart xps7390-webcam.service
```

Reboot if firmware stream teardown failed repeatedly.

## Image is too dark or too bright

The software ISP uses simple automatic exposure and gray-world white balance.
It does not yet provide the image quality of a complete libcamera pipeline.
Test in normal indoor light. Direct sunlight can saturate this small sensor.

## Application cannot select the camera

Select `XPS Front Camera`, not an `Intel IPU4P ISYS Capture` node.
The application-facing device is `/dev/video57`.

Close other camera applications. Some applications keep the video device open after hiding their window.
