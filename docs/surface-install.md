# Installation and recovery

For Surface Book 3 on Ubuntu 24.04; tested kernel: `6.8.0-146-generic`.
Keep Secure Boot enabled. There is no automatic DKMS rebuild.

## Dependencies

Run from the repository root. Mask the generic relay before installing it:

```sh
sudo systemctl mask v4l2-relayd.service
sudo apt-get install build-essential pkg-config linux-headers-"$(uname -r)" \
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
    gstreamer1.0-tools gstreamer1.0-plugins-base gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad gstreamer1.0-pipewire \
    v4l-utils v4l2loopback-utils v4l2-relayd mokutil msitools
modinfo v4l2loopback
```

Use Ubuntu's signed loopback module, not a replacement DKMS build. If missing,
install `linux-modules-extra-"$(uname -r)"`. Stock OV5693/OV8865 drivers are used.

## Firmware

Download Microsoft's [Surface Book 3 package](https://download.microsoft.com/download/6dc0552d-48b4-416f-8137-3b9a845cc283/SurfaceBook3_Win11_22621_25.013.34389.0.msi).
Extract with `msiextract` outside the checkout. Verify these SHA256 values:

| File | SHA256 |
| --- | --- |
| MSI | `3a579be7653a7cc8ccb27c198612e07a376185e9e6af2b64235a6a02af1c3b95` |
| `SurfaceUpdate/camera/cpd_component_signed.bin` | `ff2c36cc81a5c726508b22970c2e2538ff06107dc5a72c93401403c227e5157f` |

```sh
sudo install -D -m 0644 /path/to/extracted/SurfaceUpdate/camera/cpd_component_signed.bin \
    /lib/firmware/intel/ipu/ipu4p_cpd.bin
```

Firmware is proprietary; do not commit or redistribute it.

## Build and sign

Build as a regular user:

```sh
make -C driver/ipu4p
make -C softisp CFLAGS="-O2 -Wall -Wextra -Werror" \
    ipu4-softisp-surface-front ipu4-softisp-surface-rear libgstsurfaceisp.so
```

Use your own enrolled MOK certificate and protected private key:

```sh
key=/path/to/MOK.priv
cert=/path/to/MOK.der
sudo mokutil --test-key "$cert"
for module in intel-ipu4p intel-ipu4p-isys; do
    sudo /usr/src/linux-headers-"$(uname -r)"/scripts/sign-file sha256 \
        "$key" "$cert" "driver/ipu4p/$module.ko"
    modinfo -F signer "driver/ipu4p/$module.ko"
    modinfo -F vermagic "driver/ipu4p/$module.ko"
done
```

Rebuilds remove signatures. Check the kernel match before installing; never
commit signing material. A new machine needs its own MOK enrollment and reboot.

## Deploy

Close camera clients and stop existing bridge services before replacing files.
Back up the signed modules, installed programs, plugin, units, WirePlumber rule,
Snapshot wrapper and Camera package. Preserve any working baseline backups.

```sh
sudo install -d -m 0755 /usr/local/libexec \
    /usr/local/lib/surface-camera/"$(uname -r)" \
    /usr/local/lib/surface-camera/gstreamer
sudo install -m 0644 driver/ipu4p/intel-ipu4p{,-isys}.ko \
    /usr/local/lib/surface-camera/"$(uname -r)"/
sudo install -m 0755 softisp/ipu4-softisp-surface-{front,rear} /usr/local/libexec/
sudo install -m 0644 softisp/libgstsurfaceisp.so /usr/local/lib/surface-camera/gstreamer/
sudo install -m 0755 scripts/surface-camera.sh /usr/local/libexec/surface-camera
sudo install -m 0755 scripts/surface-camera-discovery.sh /usr/local/libexec/surface-camera-discovery
sudo install -D -m 0644 wireplumber/60-surface-camera.lua \
    /etc/wireplumber/main.lua.d/60-surface-camera.lua
sudo install -m 0644 systemd/surface-camera-{front,rear}.service /etc/systemd/system/
sudo systemctl daemon-reload
systemctl --user restart wireplumber
sudo systemctl enable --now surface-camera-front surface-camera-rear
```

Restart WirePlumber only on first installation or rule changes; it interrupts
audio briefly. Install the [GNOME Camera fixes](../patches/gnome-snapshot/README.md).

## Check

```sh
systemctl is-active surface-camera-front surface-camera-rear
systemctl is-enabled surface-camera-front surface-camera-rear v4l2-relayd
cat /sys/bus/i2c/drivers/ov5693/*/power/runtime_status
cat /sys/bus/i2c/drivers/ov8865/*/power/runtime_status
```

Expect active/enabled Surface bridges, a masked generic relay and suspended
sensors while idle. Take photos, switch cameras, record video and close the app;
sensors should suspend again. Idle black output is synthetic, not proof of capture.

## Updates and rollback

New kernels need matching rebuilt, signed and tested modules. The launcher
refuses missing kernel-specific modules; keep the previous working kernel.
Do not copy old modules into a new kernel directory or disable Secure Boot.

For same-kernel driver replacement, close clients and stop both bridges,
replace the signed modules, then run:

```sh
sudo rmmod intel_ipu4p_isys intel_ipu4p
sudo systemctl start surface-camera-front surface-camera-rear
```

Do not force removal if busy. User-space-only updates need no module removal.
For rollback, restore the backed-up files, reload systemd and restart the
bridges; reload changed modules only for the matching kernel.
Restore the original Camera package with `apt-get install --allow-downgrades
/path/to/original.deb`. Remove `/usr/local/bin/snapshot` only if reverting its
rendering workaround. Reboots require explicit authorization.
