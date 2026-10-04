# GNOME Camera fixes

For Ubuntu Noble `gnome-snapshot 46.2-1ubuntu2`. The GPL-3.0-or-later patch:

- Enables `always-copy` to fix [preview crashes](https://bugs.launchpad.net/bugs/2076315).
- Rebuilds the source/filter when switching between camera resolutions.

The launcher uses Cairo to avoid an OpenGL shutdown crash. Capture remains
managed by the bridge services.

Enable Ubuntu `deb-src` sources, then build as a regular user:

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

Ubuntu's source supplies vendored Rust dependencies. Keep the original `.deb`
for rollback, set no package hold, and recheck the patch before upgrading to
newer source.
