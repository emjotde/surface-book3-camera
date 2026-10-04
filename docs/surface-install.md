# Surface installation and recovery

This is the tested Ubuntu 24.04, Linux `6.8.0-146-generic` Surface Book 3
configuration. It is not a generic IPU4 installer. Do not use the Dell-specific
setup script on a Surface. Secure Boot stays enabled; there is no DKMS
installation or automatic rebuild on kernel updates yet.

## Dependencies

Install build prerequisites before building. Mask the relay package's generic
sample service before installing it; the Surface units run their own instances.

```sh
sudo systemctl mask v4l2-relayd.service
sudo apt-get install build-essential pkg-config linux-headers-"$(uname -r)" \
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
    gstreamer1.0-tools gstreamer1.0-plugins-base gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad gstreamer1.0-pipewire \
    v4l-utils v4l2loopback-utils v4l2-relayd mokutil msitools
modinfo v4l2loopback
```

The tested loopback is Ubuntu's signed kernel module, not an alternate DKMS
build. If it is missing, install `linux-modules-extra-"$(uname -r)"` from Ubuntu
and check its signature before continuing. The stock OV5693 and OV8865 sensor
drivers are used unchanged.

## Microsoft firmware

Acquire firmware locally from Microsoft's Surface Book 3 driver package.
Firmware is not distributed here. The tested package is:

```text
https://download.microsoft.com/download/6dc0552d-48b4-416f-8137-3b9a845cc283/SurfaceBook3_Win11_22621_25.013.34389.0.msi
```

Its SHA256 is
`3a579be7653a7cc8ccb27c198612e07a376185e9e6af2b64235a6a02af1c3b95`.
Extract it with `msiextract` into a separate local directory, not the source
repository. The camera firmware member is
`SurfaceUpdate/camera/cpd_component_signed.bin`, SHA256
`ff2c36cc81a5c726508b22970c2e2538ff06107dc5a72c93401403c227e5157f`.

Check the downloaded MSI and extracted firmware against those hashes, then
install the firmware:

```sh
sudo install -D -m 0644 /path/to/extracted/SurfaceUpdate/camera/cpd_component_signed.bin \
    /lib/firmware/intel/ipu/ipu4p_cpd.bin
```

The controller authenticates this firmware. Do not substitute a test image or
publish the Microsoft package or extracted firmware with this repository.

## Build and sign locally

Build as an ordinary user:

```sh
make -C driver/ipu4p
make -C softisp CFLAGS="-O2 -Wall -Wextra -Werror" \
    ipu4-softisp-surface-front ipu4-softisp-surface-rear libgstsurfaceisp.so
```

Use your own locally enrolled signing certificate. Never copy the private key
into a checkout, commit it, or upload it. This machine already has an enrolled
key at the following root-protected paths; another machine needs its own
key generation and MOK enrollment, with user-authorized reboot.

```sh
sudo mokutil --test-key /var/lib/camera-dev-signing/MOK.der
sudo /usr/src/linux-headers-"$(uname -r)"/scripts/sign-file sha256 \
    /var/lib/camera-dev-signing/MOK.priv \
    /var/lib/camera-dev-signing/MOK.der driver/ipu4p/intel-ipu4p.ko
sudo /usr/src/linux-headers-"$(uname -r)"/scripts/sign-file sha256 \
    /var/lib/camera-dev-signing/MOK.priv \
    /var/lib/camera-dev-signing/MOK.der driver/ipu4p/intel-ipu4p-isys.ko
modinfo -F signer driver/ipu4p/intel-ipu4p.ko
modinfo -F signer driver/ipu4p/intel-ipu4p-isys.ko
```

Every rebuild removes the previous signature. Verify the module `vermagic`
matches the running kernel before installation.

## Install or upgrade

Close camera applications first. Confirm both sensors are suspended and no
`ipu4-softisp-surface-*` worker remains. The bridge units should be stopped
before replacing their programs or modules:

```sh
sudo systemctl stop surface-camera-front surface-camera-rear
```

Before replacing anything, make a root-protected, versioned local backup of:

- `/usr/local/lib/surface-camera/$(uname -r)/`, including both signed modules.
- `/usr/local/lib/surface-camera/gstreamer/libgstsurfaceisp.so`.
- `/usr/local/libexec/surface-camera`, `surface-camera-discovery`, and both ISP
  executables.
- Both `/etc/systemd/system/surface-camera-*.service` units.
- `/etc/wireplumber/main.lua.d/60-surface-camera.lua`.
- `/usr/local/bin/snapshot` and the current Camera `.deb`.

Keep the original `rear-baseline/` backup intact. Never use the obsolete
`front-testing/` modules for front capture.

Install the signed controller modules outside the normal module search path:

```sh
sudo install -d -m 0755 /usr/local/lib/surface-camera/"$(uname -r)"
sudo install -m 0644 driver/ipu4p/intel-ipu4p.ko driver/ipu4p/intel-ipu4p-isys.ko \
    /usr/local/lib/surface-camera/"$(uname -r)"/
```

Use the deployment commands in the top-level README to install the ISP, plugin,
launcher, discovery helper, WirePlumber rule and systemd units. On first
installation, restart the user's WirePlumber once; this briefly affects audio.
Do not restart it on ordinary application opens or closes.

Installing modules does not replace modules already loaded into the kernel.
For a tested same-kernel driver upgrade, after all camera clients and bridge
services are stopped, remove only this controller's modules before starting
the bridges again:

```sh
sudo rmmod intel_ipu4p_isys intel_ipu4p
sudo systemctl daemon-reload
sudo systemctl enable --now surface-camera-front surface-camera-rear
```

If removal fails, investigate open clients and the kernel journal rather than
forcing removal. A reboot is an alternative only with explicit authorization.
User-space-only changes do not require module removal.

Install the application compatibility package using
[the preserved GNOME Camera patches](../patches/gnome-snapshot/README.md).
Keep the saved original Ubuntu package for rollback; do not place a package
hold on the local compatibility build.

## Verify normal operation

```sh
systemctl is-active surface-camera-front surface-camera-rear
systemctl is-enabled surface-camera-front surface-camera-rear
systemctl is-enabled v4l2-relayd
cat /sys/bus/i2c/drivers/ov5693/*/power/runtime_status
cat /sys/bus/i2c/drivers/ov8865/*/power/runtime_status
```

Both Surface bridges should be active and enabled, the generic relay masked,
and both physical sensors suspended while idle. PipeWire should advertise
exactly the two Surface virtual cameras, not this controller's raw capture
nodes. Open Camera, take a real photo, switch front/rear/front, record video,
and close it. Sensors should suspend after the final client closes and
PipeWire's normal idle suspension completes. An idle synthetic black frame
alone is not proof of working capture.

The supported application sharing path is PipeWire. Direct V4L2 MMAP
multi-client allocation and read-only demand detection remain limitations
of the installed loopback module. Independent restart of one physical camera
while the other stays active remains a driver limitation.

Boot startup, suspend/resume, detach and browser/video-call interfaces need
separate validation; they are not established by successful Camera captures.

## Kernel updates and rollback

The launcher loads modules only from the directory matching `uname -r`.
After a kernel update it deliberately fails instead of loading an old module.
Build against the new kernel headers, adapt source if its APIs changed, sign
the new modules, and test both cameras before treating the new kernel as
supported. Do not copy old `.ko` files into a new kernel directory or disable
Secure Boot to bypass failures. Keep the previous working kernel available
in GRUB until the new one is verified.

For rollback, close clients and stop both bridge services. Restore the backed-up
programs, plugin, units, rule and signed modules for the current kernel; reload
systemd. If the loaded driver changed, remove its modules as described above
before restarting the bridges. Do not restore modules built for another
kernel. If needed, boot the previous working kernel with user authorization.

For application rollback, install the saved Ubuntu package with
`apt-get install --allow-downgrades /path/to/original.deb`. Remove the local
`/usr/local/bin/snapshot` wrapper only if rolling back its rendering workaround.
Keep the idle bridges running afterward so cameras remain discoverable.
