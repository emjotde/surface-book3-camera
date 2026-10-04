# GNOME Camera compatibility patches

These patches preserve the application changes used with the Surface bridges.
They apply to Ubuntu Noble's `gnome-snapshot 46.2-1ubuntu2`, whose upstream
source is GNOME Snapshot 46.2. The patched local package version is
`46.2-1ubuntu2+surface2`; no package hold is required.

`surface-camera.patch` contains two changes:

- Enable `always-copy` on sources that support it, avoiding the PipeWire
  preview frame-copy crash described in Ubuntu bug
  [2076315](https://bugs.launchpad.net/bugs/2076315).
- Rebuild the camera source and its format filter when changing cameras.
  Retargeting the previous source kept its old resolution constraints.

The original application is GPL-3.0-or-later. These modifications use the same
license; the source headers and Ubuntu package licensing remain unchanged.
The patch does not contain vendored dependencies or generated application
binaries.

Enable Ubuntu's `deb-src` software sources before downloading the source and
installing its declared build dependencies. Run the build as a regular user:

```sh
sudo apt-get build-dep gnome-snapshot
apt-get source gnome-snapshot=46.2-1ubuntu2
cd gnome-snapshot-46.2
patch --dry-run -p1 < /path/to/surface-book3-camera/patches/gnome-snapshot/surface-camera.patch
patch -p1 < /path/to/surface-book3-camera/patches/gnome-snapshot/surface-camera.patch
dpkg-buildpackage -b -us -uc
sudo apt-get install ../gnome-snapshot_46.2-1ubuntu2+surface2_amd64.deb
sudo install -m 0755 /path/to/surface-book3-camera/scripts/surface-camera-app.sh \
    /usr/local/bin/snapshot
```

The source package supplies vendored Rust dependencies, allowing the package
build itself to run offline after its declared build dependencies are installed.
The patch includes the two local Debian changelog entries so the package
version matches the tested build.

The launcher uses Cairo rendering to avoid a separate OpenGL-context crash
on application shutdown. It does not change rendering for other applications
or control capture. Ubuntu's standard `v4l2-relayd` handles capture independently.

Recheck these fixes when upgrading GNOME Camera. Do not blindly apply this
version-specific patch to newer upstream source. To roll back the application,
install the saved original Ubuntu `.deb` with `--allow-downgrades` and remove
only `/usr/local/bin/snapshot` if abandoning the rendering workaround.
