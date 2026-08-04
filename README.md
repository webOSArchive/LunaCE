LunaCE
======

LunaCE is the community-maintained version of **LunaSysMgr**, the process that *is*
the webOS user interface: card view, launcher, status bar, virtual keyboard,
notifications and lock screen all live inside it.

This tree targets **webOS 3.0.5 on the HP TouchPad**, and contains the LunaCE lineage
through 5.0.0 plus newer launcher work — app groups, renameable tabs, and adding and
removing tabs.

It also builds for the **TouchPad Go** (`opal`) — same webOS 3.0.5, different hardware
support. See [The TouchPad Go](#touchpad-go) below.


Credits
-------

LunaCE exists because of a long chain of people who kept webOS alive after HP walked
away:

* **[Herrie82](https://github.com/Herrie82)** — maintainer of the LunaCE lineage this
  tree is built on. The 4.x/5.0.0 work (wave launcher, mini cards, card zoom and stack
  spread gestures, infinite card cycling, fluid gestures, status bar search and version,
  the tablet keymaps, and a great deal more) comes from his repository.
* **[webOS Internals](https://github.com/webos-internals)** — Preware, the patch and
  package infrastructure, and the reverse-engineering groundwork that made community
  webOS development possible at all.
* **webOS Ports / WOCE** — the Community Edition project that first got HP's
  LunaSysMgr building outside HP.
* **Hewlett-Packard / Palm** — the original source, released under the Apache License
  2.0 in 2012.

Individual features carry their contributors' names in the history; `git log` is the
authoritative credit list.

Project background: http://webos-ports.org/wiki/About_LunaCE


Beyond stock LunaCE
-------------------

* **App groups (folders).** Drag an icon onto the *centre* of another and hold briefly
  to create a group, or to add to an existing one; the outer edge of an icon still
  means "reorder next to it". A group renders as a mini-grid tile and opens in an
  overlay panel: tap a member to launch it, press and hold one to pop it back out onto
  the page, tap the title to rename the group. A group dissolves when one member is
  left.
* **Renameable tabs.** Long-press a tab to rename it, or drive it from the bus:

      luna-send -n 1 palm://com.palm.systemmanager/systemUi \
        '{"launchertitlechange":{"id":2,"label":"Games"}}'

  (`id` is the 0-based tab position.) Names survive reboots.
* **Add and remove tabs.** Long-press the empty part of the tab strip for a "+", and
  long-press a tab beyond the stock four for a trash icon. The first four tabs are
  protected, since other parts of webOS expect them to exist; the ceiling is six.

Groups and tab names are stored in the existing save format under
`/var/luna/preferences/launcher3/` with no save-version bump, so an older LunaSysMgr
reading these files skips what it doesn't understand instead of wiping the layout.


Building
--------

This is a cross-compile for ARM. It will not build against a modern desktop Qt:
LunaSysMgr needs webOS 3.0.5's own Qt 4.8 plus a pile of closed-source HP libraries,
so the recipe is "assemble a staging tree, then point qmake at it".

### 1. Toolchain

The Palm PDK ships the compiler this code expects (CodeSourcery arm-2009q1, gcc 4.3.3):

    /opt/PalmPDK/arm-gcc/bin/arm-none-linux-gnueabi-gcc

Those are 32-bit x86 binaries, so a 64-bit host needs i386 multiarch
(`sudo dpkg --add-architecture i386 && sudo apt-get install libc6:i386`).

### 2. Staging tree (libraries + headers)

Two halves, both required:

* **Libraries** come from a real device image. Extract the rootfs from the webOS
  Doctor for 3.0.5 (`webosdoctorp305hstnhwifi.jar`); the `scripts/unpack-doctor-rootfs`
  helper in [woce/woce-build](https://github.com/woce/woce-build) does it. Afterwards,
  rewrite the GNU ld scripts that carry absolute paths (`libc.so`, `libpthread.so`),
  and copy `libc_nonshared.a` and `libpthread_nonshared.a` in from the PDK sysroot —
  a runtime image doesn't ship them, and the link fails without them.
* **Headers** come from [woce/build-support](https://github.com/woce/build-support),
  which carries what was never on the device: Piranha, WebKitLuna's palm API,
  sysmgr-ipc, ime, hid, napp, and Palm's Qt 4.8 headers.

Lay them out as `<staging>/usr/include` and `<staging>/usr/lib`.

### 3. Qt host tools and mkspec

You need Qt 4.8's `qmake`, `moc`, `uic` and `rcc` built for the *host*. Palm's own Qt
tarball has vanished from `downloads.help.palm.com`, but survives in the
[HP webOS open-source archive](https://archive.org/details/hp-webos-oss) if you want
the authentic ones; otherwise stock Qt 4.8.7 host tools work. Put a `qt.conf` beside
`qmake` pointing `Headers` and `Libraries` at the staging tree.

Add an mkspec named `qws/linux-armv7-g++` (copy `qws/linux-armv6-g++` and repoint the
tools at `arm-none-linux-gnueabi-*`). The *name* matters — `sysmgr.pro` keys off it to
pull in `device.pri`. Two settings it must carry:

    QMAKE_CFLAGS   += -O2 -marm -march=armv7-a -mtune=cortex-a8 -mfpu=neon -mfloat-abi=softfp
    QMAKE_CXXFLAGS += -O2 -marm -march=armv7-a -mtune=cortex-a8 -mfpu=neon -mfloat-abi=softfp
    QMAKE_LIBS_OPENGL = -lGLESv2 -lEGL      # the TouchPad is GLES; qmake would default to -lGL

### 4. Build

`cross-build/build.sh` wraps the whole thing once the staging tree exists. It expects
the workspace beside this checkout (override with `LUNACE_BUILD_DIR`):

    ./cross-build/build.sh reconf -j$(nproc)          # topaz
    MACHINE=opal ./cross-build/build.sh -j$(nproc)    # TouchPad Go

See [cross-build/README.md](cross-build/README.md). By hand it is:

    export PATH=/opt/PalmPDK/arm-gcc/bin:<qt-host-tools>:$PATH
    export STAGING_INCDIR=<staging>/usr/include
    export STAGING_LIBDIR=<staging>/usr/lib
    export MACHINE=topaz          # or opal, for the TouchPad Go

    qmake <path-to-this-source>/sysmgr.pro -spec qws/linux-armv7-g++ \
        "QMAKE_LFLAGS += -L$STAGING_LIBDIR -L<staging>/lib"
    make -j$(nproc)

The result is `release-topaz/LunaSysMgr`. It should require no more than `GLIBC_2.4`
and `GLIBCXX_3.4.10`, which a stock 3.0.5 TouchPad provides:

    arm-none-linux-gnueabi-readelf -V release-topaz/LunaSysMgr | grep -oE 'GLIBC[^ ]*' | sort -uV

### A build check worth keeping

`moc` is silent about one mistake that is very expensive in a UI process: a plain
`public:` / `protected:` inside a `Q_SLOTS` block *ends the block*, so every slot below
it stops being a slot. Its `connect()`s then fail at runtime with nothing but a console
warning — in practice, taps stop working while everything still compiles. Check the
generated moc, not just the build:

    grep -c 'case [0-9]*: _t->slotIconActivatedTap(' release-topaz/.moc/moc_dimensionslauncher.cpp

A registered slot appears in moc's `qt_metacall` dispatch. `0` means it isn't one.


<a name="touchpad-go"></a>The TouchPad Go
-----------

The Go (`opal`) runs the same webOS 3.0.5 and takes the same staging tree; it needs
its own binary only because the hardware support is compiled in:

    MACHINE=topaz|opal        which tablet (HostArmTopaz vs HostArmOpal)

What does differ between devices is the closed-source libraries themselves, and the
linker cannot catch it. Where a build adds a **virtual** method to a shared class,
every virtual declared after it shifts a slot, so a call across that boundary lands
on the wrong function — a SIGSEGV at runtime, never a link error. Compare vtable
*sizes* against the device's own libraries before trusting a build; matching symbol
names are not enough:

    nm -D -S <device-lib> | grep _ZTV   # against the same from the staging tree

Individual symbols vary too: the Go's `libqpalm` has no `setAdvancedGestures`, which
`Preferences.cpp` resolves with `dlsym` for that reason. A weak declaration does not
work as a guard here — on ARM the address of a weak function is its PLT stub, which
is never null, so the call goes through to a null GOT entry.

When a device does crash-loop, `/var/log/reports/librdx/` holds rdxd minicore
reports: registers, per-thread backtraces, symbolized against the binary.


Installing
----------

You do not have to build anything to try this. `bin/` carries prebuilt binaries:

* `bin/LunaSysMgr-LunaCE` — this tree, built for topaz
* `bin/LunaSysMgr-Stock` — HP's original webOS 3.0.5 launcher, to go back

`install.sh` pushes one over novacom and restarts the UI rather than rebooting:

    ./install.sh           # asks which one you want
    ./install.sh --lunace
    ./install.sh --stock

The device must be connected by USB with novacomd running. Restarting LunaSysMgr takes
the UI down for roughly half a minute and does not preserve open cards.

If you have built your own, copy it over the matching file in `bin/` first.


Copyright and License Information
---------------------------------

Unless otherwise specified, all content, including all source code files and
documentation files in this repository are:

Copyright (c) 2008-2012 Hewlett-Packard Development Company, L.P.

Unless otherwise specified or set forth in the NOTICE file, all content,
including all source code files and documentation files in this repository are:
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this content except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
