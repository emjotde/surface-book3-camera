# Troubleshooting

## Camera missing or capture fails

```sh
systemctl status surface-camera-front surface-camera-rear
journalctl -b -u surface-camera-front -u surface-camera-rear
ls -l /dev/video50 /dev/video51
sudo dmesg | grep -Ei 'ipu4|ov5693|ov8865|firmware'
```

Check firmware at `/lib/firmware/intel/ipu/ipu4p_cpd.bin`, signed modules under
`/usr/local/lib/surface-camera/$(uname -r)/`, and the installed WirePlumber rule.
Choose a **Surface Book 3** camera, not a raw **Intel IPU4P ISYS Capture** node.
Stopped bridges remove usable cameras from application lists.

## After a kernel update

Build and sign matching modules; see [installation](surface-install.md).
Never load modules for another kernel or disable Secure Boot to bypass errors.

## Camera crash or switching failure

Ubuntu Camera 46.2 needs the [compatibility patches](../patches/gnome-snapshot/README.md)
and Cairo launcher. Close clients before restarting both bridges together.
Independent physical-stream restart while the other camera runs is unreliable.

## Camera stays on

Close all capture clients and allow PipeWire's idle suspension to finish:

```sh
cat /sys/bus/i2c/drivers/ov5693/*/power/runtime_status
cat /sys/bus/i2c/drivers/ov8865/*/power/runtime_status
```

Both should read `suspended` when idle. For development or untested
suspend/detach paths, stop both bridges explicitly.

Exposure and white balance are basic; use normal indoor lighting.
Review logs before sharing them, and never attach private images or firmware.
