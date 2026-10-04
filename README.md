# Surface Book 3 cameras on Linux

Experimental front and rear camera support for **Ubuntu 24.04**, tested on
kernel **6.8.0-146-generic** with Secure Boot enabled.

| Camera | Application output | Device |
| --- | --- | --- |
| Front OV5693 | 1296x972 YUYV | `/dev/video51` |
| Rear OV8865 | 1632x1224 YUYV | `/dev/video50` |

Both cameras remain discoverable while powered off. Ubuntu's `v4l2-relayd`
starts capture when needed and stops it after the last client closes.
GNOME Camera photos, video, front/rear switching and shared PipeWire capture
work with the [application patches](patches/gnome-snapshot/README.md).

## Install

Follow [installation and recovery](docs/surface-install.md). This requires
locally extracted Microsoft firmware and signed controller modules.
**Do not run the inherited Dell installer on a Surface.**

After installation, leave the idle bridges enabled:

```sh
sudo systemctl enable --now surface-camera-front surface-camera-rear
```

Select **Surface Book 3 Front Camera** or **Surface Book 3 Rear Camera** in
applications. Closing the last client suspends the sensor after PipeWire's
normal idle delay; the camera stays listed.

## Limitations

- IR capture does not work.
- Reopening one physical camera while the other streams is unreliable.
- Concurrent applications should use PipeWire; direct V4L2 multi-client
  allocation and read-only demand detection have limitations.
- Final boot startup, suspend/resume, detach and browser calls are untested.
- Kernel updates require rebuilding, signing and testing matching modules.

## Documentation

[Installation](docs/surface-install.md) |
[Troubleshooting](docs/troubleshooting.md) |
[Design](docs/architecture.md) |
[Sources and licenses](docs/provenance.md) |
[Build recipe](ci/README.md)

Based on [MaximeRivest's Dell IPU4P driver](https://github.com/MaximeRivest/xps-13-7390-2-in-1-webcam-linux),
with Surface PHY work from [xiaoland's Surface Pro 7 driver](https://github.com/xiaoland/sfp7-linux-cam-driver).
Upstream history and licenses are preserved. Dell-specific files are inherited,
not validated by this Surface port; use the upstream repository for Dell instructions.
Firmware, signing keys, private captures and build artifacts are not distributed.
