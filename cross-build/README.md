cross-build
===========

Scripts for cross-compiling LunaSysMgr for the TouchPad (`topaz`) and TouchPad Go
(`opal`), against webOS 3.0.5.

* `build.sh` — run qmake and make for a given MACHINE

The repository holds only the source. Everything the build needs besides that — the
extracted device rootfs, the WOCE headers, prebuilt host Qt tools, object files — is
several gigabytes and lives in a **workspace directory outside this checkout**:

    $LUNACE_BUILD_DIR       default: ../lunace-build, alongside this repo

      qt4-host/             Qt 4.8.7 host tools (qmake/moc/uic/rcc) from Ubuntu
                            archive debs, plus a qws/linux-armv7-g++ mkspec that
                            targets the PDK toolchain
      staging/armv7/        webOS 3.0.5 rootfs + WOCE build-support headers
      sysmgr-build*/        shadow build directories, one per machine

Assembling `qt4-host/` and `staging/armv7/` is described in the main
[README](../README.md#building); it is a one-time job.


Building
--------

    ./cross-build/build.sh                  topaz (default)
    MACHINE=opal ./cross-build/build.sh     opal

`MACHINE` selects hardware support: `HostArmTopaz` vs `HostArmOpal`, and which
rendering options are enabled. Nothing else differs between the two builds.

Pass `reconf` first to re-run qmake; any other arguments go to make:

    ./cross-build/build.sh reconf -j$(nproc)

Output lands in `$LUNACE_BUILD_DIR/sysmgr-build[-<machine>]/release-<machine>/`.


Libraries and vtables
---------------------

The binary is linked against the device's own closed-source libraries, and those
have moved between webOS releases in a way the linker cannot catch: where a release
adds a **virtual** method to a shared class, every virtual declared after it shifts
a slot, so a call across the boundary lands on the wrong function. That is a
SIGSEGV at runtime, never a link error — a build with zero unresolved symbols still
crash-loops.

So if you retarget the staging tree, or add a library to the link, compare vtable
**sizes** before trusting the result. Matching symbol names are not sufficient:

    nm -D -S <device-lib> | grep _ZTV     # diff against the same from staging

Two symbols are known to vary between devices rather than releases:
`setAdvancedGestures` and `HHardware_getDisplaySize` are absent from some libqpalm
builds (the TouchPad Go's, for one). LunaCE resolves the first with `dlsym` and does
not use the second. Note that a weak declaration is *not* a workable guard here: on
ARM the address of a weak function is its PLT stub, which is never null.
