cross-build
===========

Scripts for cross-compiling LunaSysMgr for the TouchPad (`topaz`) and TouchPad Go
(`opal`), against webOS 3.0.5 or 3.0.4.

* `build.sh` — run qmake and make for a given MACHINE / WEBOS_ABI pair
* `mk-staging-304.sh` — derive the 3.0.4 link tree from the 3.0.5 one

The repository holds only the source. Everything the build needs besides that — the
extracted device rootfs, the WOCE headers, prebuilt host Qt tools, object files — is
several gigabytes and lives in a **workspace directory outside this checkout**:

    $LUNACE_BUILD_DIR       default: ../lunace-build, alongside this repo

      qt4-host/             Qt 4.8.7 host tools (qmake/moc/uic/rcc) from Ubuntu
                            archive debs, plus a qws/linux-armv7-g++ mkspec that
                            targets the PDK toolchain
      staging/armv7/        webOS 3.0.5 rootfs + WOCE build-support headers
      staging/armv7-304/    the 3.0.4 equivalent, built by mk-staging-304.sh
      sysmgr-build*/        shadow build directories, one per combination

Assembling `qt4-host/` and `staging/armv7/` is described in the main
[README](../README.md#building); it is a one-time job.


Building
--------

    ./cross-build/build.sh                                 topaz, 3.0.5 (default)
    MACHINE=opal ./cross-build/build.sh                     opal, 3.0.5
    WEBOS_ABI=3.0.4 ./cross-build/build.sh                 topaz, 3.0.4
    MACHINE=opal WEBOS_ABI=3.0.4 ./cross-build/build.sh     opal, 3.0.4

`MACHINE` and `WEBOS_ABI` are independent — `MACHINE` selects hardware support,
`WEBOS_ABI` selects which release's libraries and ABI to build against. Both tablets
shipped in 3.0.4 and 3.0.5 flavours, so neither implies the other.

Pass `reconf` first to re-run qmake; any other arguments go to make:

    ./cross-build/build.sh reconf -j$(nproc)

Output lands in `$LUNACE_BUILD_DIR/sysmgr-build[-<machine>][-304]/release-<machine>/`.


The 3.0.4 tree
--------------

    ./cross-build/mk-staging-304.sh                 pull from an attached 3.0.4 device
    ./cross-build/mk-staging-304.sh /path/libs.tar  reuse a tarball pulled earlier

It clones the 3.0.5 tree (hardlinked, so it is nearly free), swaps in the shared
libraries from a real 3.0.4 device, and then fixes two things that otherwise bite:

* glibc's `libc.so` / `libpthread.so` are GNU ld scripts naming their members by
  absolute path, which a cross-link resolves against the *host* root.
* Three headers declare virtuals that 3.0.5 added and 3.0.4 does not have. Because
  they are virtual, every virtual after them shifts a slot, so a call across the
  boundary lands on the wrong function — a SIGSEGV at runtime, never a link error.
  The script comments them out and asserts the match counts, so it fails loudly if
  the build-support headers ever change.

Qt needs no header patching: `QT_NO_ACTION` and `QT_NO_CLIPBOARD` already guard its
side, and `device.pri` defines both when `WEBOS_ABI=3.0.4`.

If you add a library to the link, compare vtable **sizes** before trusting the
result — matching symbol names are not sufficient:

    nm -D -S <3.0.4-lib> | grep _ZTV     # diff against the same from 3.0.5
