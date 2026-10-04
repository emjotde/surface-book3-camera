# Surface Book 3 camera support, based on the Dell IPU4P driver

This checkout preserves the history of
[MaximeRivest/xps-13-7390-2-in-1-webcam-linux](https://github.com/MaximeRivest/xps-13-7390-2-in-1-webcam-linux).
The Surface changes are experimental and use the project's existing licenses.
The original Dell documentation remains below the Surface instructions.
See [installation and recovery](docs/surface-install.md) for dependencies,
local signing, kernel updates and rollback, and
[GNOME Camera patches](patches/gnome-snapshot/README.md) for reproducible
application builds. Firmware, signing keys, private captures and build
artifacts are deliberately excluded.
The [Ubuntu build definition](ci/README.md) is preserved but GitHub Actions is
intentionally inactive; publishing the source does not require granting
additional workflow permissions.

## Local Surface Book 3 experiment

The IPU4P controller modules in this checkout have been adapted to compile
against Ubuntu kernel `6.8.0-146-generic`. The port uses Linux 6.8 PCI mapping
and symbol namespace APIs, the older link-frequency helper, and disables
generic CSI-2 metadata capture that this kernel cannot represent.

Both visible-light cameras now capture real packed RAW10 frames using the
stock Ubuntu sensor drivers. The software ISP produces upright YUYV color
video with basic automatic exposure and white balance through v4l2loopback:

| Camera | Raw input | Application output | Virtual device |
| --- | --- | --- | --- |
| OV8865 rear | 3264x2448 | 1632x1224 | `/dev/video50`, Surface Book 3 Rear Camera |
| OV5693 front | 2592x1944 | 1296x972 | `/dev/video51`, Surface Book 3 Front Camera |

Distinct real frames and color output have been captured with V4L2 and
GStreamer. GNOME Camera has also saved real JPEG photographs from both cameras
at their application output sizes through PipeWire, with clean application
shutdown and front/rear/front switching in one window. Both virtual cameras
remain listed while the physical sensors are suspended. Capture starts on
demand and stops when the last streaming client disconnects.
Browser/video-call UI selection is not yet tested. The IR sensor
still fails its I2C probe and is not usable.

The decisive front fix moves the port-specific receiver reset **before**
firmware stream setup. Resetting it afterward broke capture with FIFO
overflow. The stock OV5693 driver works with the corrected startup order;
the earlier separate OV5693 LP11 experiment is not required and is
not included in the working source. A further trial setting analog/MIPI retention
at `0x3023` was also unnecessary and has been removed.

The internal test generator now produces valid 2592x1944 RAW10 frames through
the front capture node without firmware errors. Its original diagnostic
hard-coded 1280x800 output and also started the physical sensor, invalidating
the earlier test. It now uses the routed format and leaves the sensor off.
These are generated pixels, not camera images. Physical sensor color bars
and real front images work with the corrected reset order.

The Surface PHY path is experimental, adapted from
[xiaoland/sfp7-linux-cam-driver](https://github.com/xiaoland/sfp7-linux-cam-driver).
It avoids Dell-specific clock/global reset writes, leaves BB8 untouched,
configures BB10, and uses the front receiver's narrow timing register map
and port-specific reset. A clean boot alone did not fix front capture;
receiver reset ordering was still wrong. Old Dell PHY writes were not the
sole cause. Experimental firmware-source overrides and recovery loops are
not needed and have been removed.
Suspend/resume and physical detach are untested.
Do not run the Dell-specific installer on a Surface.

Secure Boot remains enabled. Both controller modules are signed by an enrolled
local certificate and staged outside the normal module search path at
`/usr/local/lib/surface-camera/6.8.0-146-generic/`. No camera modules are
placed in automatic module-search paths; the bridge services explicitly load
the matching signed modules when they start, including at boot when enabled.
The official Microsoft camera firmware
is authenticated by the controller and installed locally as
`/lib/firmware/intel/ipu/ipu4p_cpd.bin`; it is not distributed in this repository.

Build the Surface components without building the Dell sensor/power drivers:

```sh
make -C driver/ipu4p
make -C softisp ipu4-softisp-surface-rear ipu4-softisp-surface-front libgstsurfaceisp.so
```

Re-sign rebuilt modules with the enrolled local key before installing or
loading them. A rebuild removes the previous module signature.
The installed modules include the front reset-order fix. Original rear-only
baseline modules are preserved under the per-kernel `rear-baseline/`
directory. The older `front-testing/` candidates predate the successful fix;
do not use them for front capture. Check both cameras, service restart and
shutdown before replacing the installed modules with another build.
The controller also reuses an existing firmware graph on reprobe:
Linux 6.8's bridge nodes survive driver removal, and initializing them twice
otherwise fails with `-EEXIST`.
The local bridge services use root-owned installed copies of
`scripts/surface-camera.sh`, the two Surface ISP executables, and
`systemd/surface-camera-{front,rear}.service`. The shared launcher checks the
model, controller and kernel, loads the signed modules, configures the media
graph and selects the sensor by entity name, not its changing subdevice number.
It requires `media-ctl`, `v4l2-ctl`, `udevadm`, `flock`, Ubuntu's `v4l2-relayd`,
GStreamer and the signed Ubuntu v4l2loopback module. Building the source adapter
also requires `pkg-config` and the GStreamer development headers.
Startup is serialized to avoid simultaneous module-load
races. The loopback module creates both devices on its first load; if an
older one-device configuration is already loaded, stop its users and reload
the module before starting these services.
Ubuntu's existing `v4l2-relayd` keeps the virtual-camera writer open and uses
the loopback driver's client-usage events to start and stop its input pipeline.
The installed signed module and Noble relay both use event `0x08000000`;
no replacement loopback kernel module or DKMS build is needed.
`softisp/gstsurfaceisp.c` is a small GStreamer source adapter around the existing
ISP. It starts an ISP child only when capture is requested, transfers complete
YUYV frames over a pipe, and stops and reaps the child when capture ends.
Passing `-` as the ISP output selects this binary stdout mode; the original
direct-loopback output mode remains supported.
Idle output is explicitly black synthetic video, not sensor capture.
The output sink is not clock-synchronised: hardware capture paces real frames,
and idle black frames must not be dropped because their timestamps restart.

Desktop discovery also needs the installed
`scripts/surface-camera-discovery.sh` helper. The units call it after startup,
once the writer has negotiated its capture format, and after shutdown.
PipeWire 1.0 otherwise keeps the loopback's old capture/output capabilities:
a newly started camera can remain unavailable, or a stopped camera can remain
listed as a usable source. The helper refreshes only that virtual video
device's udev discovery; it does not restart PipeWire or interrupt audio.
The WirePlumber 0.4 drop-in hides this controller's raw Bayer capture nodes
from desktop applications without removing direct V4L2 development access.

```sh
sudo systemctl mask v4l2-relayd.service
sudo apt install v4l2-relayd
sudo install -d -m 0755 /usr/local/lib/surface-camera/gstreamer
sudo install -m 0644 softisp/libgstsurfaceisp.so \
    /usr/local/lib/surface-camera/gstreamer/
sudo install -m 0755 softisp/ipu4-softisp-surface-front \
    softisp/ipu4-softisp-surface-rear /usr/local/libexec/
sudo install -m 0755 scripts/surface-camera.sh /usr/local/libexec/surface-camera
sudo install -m 0755 scripts/surface-camera-discovery.sh \
    /usr/local/libexec/surface-camera-discovery
sudo install -D -m 0644 wireplumber/60-surface-camera.lua \
    /etc/wireplumber/main.lua.d/60-surface-camera.lua
sudo install -m 0644 systemd/surface-camera-{front,rear}.service \
    /etc/systemd/system/
sudo systemctl daemon-reload
systemctl --user restart wireplumber
```

Restarting WirePlumber is needed once when installing the rule and briefly
affects desktop audio; normal camera service starts and stops do not require
it. The package's generic example service is masked because the two
Surface-specific services configure their own relay instances.

Ubuntu's GNOME Camera 46.2 also crashes while copying PipeWire preview buffers
([Ubuntu bug 2076315](https://bugs.launchpad.net/bugs/2076315)).
The local `gnome-snapshot_46.2-1ubuntu2+surface2_amd64.deb` was built from the
official Ubuntu source with the guarded `always-copy=true` setting in
`aperture/src/camera.rs`. It copies frames into application-owned memory.
It also rebuilds the source wrapper and format filter when changing cameras:
retargeting the old source kept the previous camera's resolution constraints
and failed negotiation between the front and rear native formats.
The source and original Ubuntu package are retained in the local
`/home/marcinjd/camera-dev/` directory; no package hold is set, so a newer
Ubuntu package can replace this compatibility build.

An additional OpenGL-context crash occurred on Camera shutdown. The installed
`scripts/surface-camera-app.sh` launcher at `/usr/local/bin/snapshot` disables
OpenGL for this application and selects GTK's Cairo renderer. This uses CPU
rendering and leaves other applications' GPU settings unchanged. Both photo
capture and clean shutdown were confirmed with the patched package and this
launcher. Neither application compatibility change manages physical capture; the
bridge services provide that independently of the application.

```sh
sudo apt install ../gnome-snapshot_46.2-1ubuntu2+surface2_amd64.deb
sudo install -m 0755 scripts/surface-camera-app.sh /usr/local/bin/snapshot
```

```sh
sudo systemctl enable --now surface-camera-front surface-camera-rear
```

Leave both bridge services running for ordinary use. Applications select the
corresponding **Surface Book 3 Front Camera** or **Surface Book 3 Rear Camera**,
and the relay controls physical capture. Closing the last client powers the
sensor down without removing the camera from application lists; PipeWire's
normal idle suspension can delay the final stream close.
Opening, capturing, closing, reopening and same-window switching have been
confirmed without manually changing service state.
Both cameras have also recorded and decoded real native-resolution WebM video.
Two concurrent PipeWire clients share capture correctly, and closing one does
not power down the sensor until the other closes.

Use PipeWire for concurrent applications. Two direct V4L2 MMAP consumers failed
buffer allocation with the installed loopback module; read-only V4L2 consumers
did not trigger its stream-usage events. These direct-V4L2 limitations are not
fixed by the adapter.

The driver's independent simultaneous-stream restart limitation remains:
both cameras can initially stream together, but reopening one physical stream
while the other stays active is not
reliable: pending captures can require a shared ISYS power cycle, and the
front receiver can overflow on reopen. Close capture clients before restarting
the bridge services or replacing modules. Simultaneous independent restart
support is unresolved; ordinary single-camera switching works.
For raw-driver development or before the still-untested suspend/detach paths,
stop both bridges explicitly:

```sh
sudo systemctl stop surface-camera-front surface-camera-rear
```

After a kernel update, the launcher deliberately refuses to load until modules
for the new kernel have been built, signed and tested. Manual controller loading
after a clean boot was tested; enabled startup for the final on-demand bridge
setup has not yet been tested by another reboot.

For raw rear capture, enable the CSI2-0 to Capture-0 link, set the OV8865
and CSI2-0 sink to `SBGGR10_1X10/3264x2448`, then select `pBAA` on
the Capture-0 video node. Its stride is 4096 bytes, not the 4080-byte
packed pixel width. Front full-resolution capture uses CSI2-2/Capture-16,
`2592x1944`, and stride 3264. Constant `0xff` buffers are failed captures,
not valid images. Subdevice node numbers can change on reload.

This project enables the internal webcam on the **Dell XPS 13 7390 2-in-1**.
It supports the Ice Lake Intel IPU4P camera controller with PCI ID `8086:8a19`.
The laptop uses an OV01A10 sensor and an INT346F camera power controller.

Mainline Linux does not provide a complete driver for this camera stack.
Without this project, Linux usually shows no internal webcam device.

## Current status

- Real 1280×800 RAW10 capture at 60 frames per second.
- A small software ISP provides 640×400 YUYV video to normal applications.
- Browsers and video-call applications see **XPS Front Camera** at `/dev/video57`.
- Automatic exposure and white balance are basic but functional.
- Stream failure detection and automatic recovery are included.
- Linux 6.18.x is the tested kernel series.
- NixOS is the tested installation path.
- The generic DKMS installer is new and needs testing on other distributions.

This driver is experimental. It currently supports only the exact laptop model above.

## Hardware check

Run this command before installation:

```bash
./scripts/check-hardware.sh
```

The check must find all four items:

- Dell XPS 13 7390 2-in-1
- Intel IPU4P `8086:8a19`
- OV01A10 sensor `OVTI01A0`
- INT346F camera power controller

## Firmware

The Intel camera firmware is proprietary. This repository does not distribute it.
Extract it locally from Dell driver package **4PY3P**:

1. Open the [Dell XPS 13 7390 2-in-1 driver page](https://www.dell.com/support/home/product-support/product/xps-13-7390-2-in-1-laptop/drivers).
2. Download `7390 2in1-win11-A00-4PY3P.CAB`.
3. Install `cabextract` from your distribution.
4. Run:

```bash
./scripts/extract-firmware.sh ~/Downloads/7390\ 2in1-win11-A00-4PY3P.CAB
```

The script verifies the known firmware SHA-256 value. It does not modify the Dell package.

## Install on a systemd distribution

Install these requirements with your distribution package manager:

- C compiler and `make`
- Headers for the running kernel
- `dkms` (recommended)
- `v4l-utils`
- `v4l2loopback`
- `cabextract`
- `pciutils`

Examples of common package names:

| Distribution | Packages |
| --- | --- |
| Debian or Ubuntu | `build-essential linux-headers-$(uname -r) dkms v4l-utils v4l2loopback-dkms cabextract pciutils` |
| Arch Linux | `base-devel linux-headers dkms v4l-utils v4l2loopback-dkms cabextract pciutils` |
| Fedora | `gcc make kernel-devel dkms v4l-utils cabextract pciutils` plus a packaged `v4l2loopback` module |

Then install the driver:

```bash
git clone https://github.com/MaximeRivest/xps-13-7390-2-in-1-webcam-linux.git
cd xps-13-7390-2-in-1-webcam-linux
./scripts/check-hardware.sh
./scripts/extract-firmware.sh ~/Downloads/7390\ 2in1-win11-A00-4PY3P.CAB
sudo ./install.sh
sudo reboot
```

Secure Boot blocks unsigned external modules. Disable Secure Boot or sign all four modules.

## Install on NixOS

First extract `ipu4p_cpd.bin` with the firmware script. Copy it into your NixOS configuration directory.
Then add this repository as a flake input:

```nix
{
  inputs.xps7390-webcam.url =
    "github:MaximeRivest/xps-13-7390-2-in-1-webcam-linux";

  outputs = { nixpkgs, xps7390-webcam, ... }: {
    nixosConfigurations.my-laptop = nixpkgs.lib.nixosSystem {
      system = "x86_64-linux";
      modules = [
        xps7390-webcam.nixosModules.default
        ({ ... }: {
          hardware.xps7390Webcam = {
            enable = true;
            firmwareFile = ./ipu4p_cpd.bin;
          };
        })
      ];
    };
  };
}
```

Rebuild NixOS and reboot.

## Test the camera

Confirm that both services run:

```bash
systemctl status xps7390-webcam-setup.service xps7390-webcam.service
```

View the camera with mpv:

```bash
mpv av://v4l2:/dev/video57 --profile=low-latency --untimed
```

Applications must select **XPS Front Camera**.

## Troubleshooting

Read [`docs/troubleshooting.md`](docs/troubleshooting.md). Include the diagnostic output in bug reports:

```bash
./scripts/diagnose.sh > webcam-diagnostics.txt 2>&1
```

Review the file before you attach it. The report includes the computer model and kernel log.

## Remove the driver

```bash
sudo ./uninstall.sh
sudo reboot
```

## Design and source history

Read [`docs/architecture.md`](docs/architecture.md) and [`docs/provenance.md`](docs/provenance.md).
The kernel modules use GPL-2.0-only code. Each source file keeps its original SPDX identifier.

## Contributing

Test changes on the exact supported model. Report the kernel version and complete diagnostics.
Do not submit Dell or Intel firmware, Windows drivers, or other proprietary files.
