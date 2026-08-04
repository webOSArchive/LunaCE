#!/bin/bash
# Cross-build LunaCE (LunaSysMgr) for the HP TouchPad (topaz) or TouchPad Go (opal),
# against webOS 3.0.5.
#
#   ./cross-build/build.sh                  topaz (default)
#   MACHINE=opal ./cross-build/build.sh     opal
#
# MACHINE picks the hardware support: HostArmTopaz vs HostArmOpal, and which
# rendering options are enabled.
#
# Pass "reconf" as the first argument to re-run qmake; anything else goes to make.
#
# The source tree is this repository. Everything else lives in a workspace outside
# it, because it is many gigabytes of extracted device images and prebuilt host
# tools that have no business in version control:
#
#   $LUNACE_BUILD_DIR (default: ../lunace-build, alongside this checkout)
#     qt4-host/            Qt 4.8.7 host tools (qmake/moc/uic/rcc) from Ubuntu
#                          archive debs, plus a custom qws/linux-armv7-g++ mkspec
#                          targeting the PDK toolchain
#     staging/armv7/       webOS 3.0.5 rootfs (from the webOS Doctor) overlaid with
#                          the WOCE build-support headers (Piranha, WebKitLuna,
#                          sysmgr-ipc, Qt, ...)
#     sysmgr-build[-<machine>]/   shadow build dirs, one per machine
#
# See cross-build/README.md for how to assemble the workspace.
#
# Toolchain: /opt/PalmPDK/arm-gcc (CodeSourcery arm-2009q1, gcc 4.3.3)

set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="${SRC:-$(cd "$HERE/.." && pwd)}"
WORKSPACE="${LUNACE_BUILD_DIR:-$(cd "$SRC/.." && pwd)/lunace-build}"

PDK_TC=/opt/PalmPDK/arm-gcc
QT_HOST="$WORKSPACE/qt4-host/usr/lib/x86_64-linux-gnu/qt4/bin"

export MACHINE="${MACHINE:-topaz}"

# topaz keeps the paths it has always used, so an existing build stays valid.
if [ "$MACHINE" = topaz ]; then
    BUILD_DEFAULT="$WORKSPACE/sysmgr-build"
else
    BUILD_DEFAULT="$WORKSPACE/sysmgr-build-$MACHINE"
fi
STAGING="${STAGING:-$WORKSPACE/staging/armv7}"

[ -d "$WORKSPACE" ] || {
    echo "Error: build workspace $WORKSPACE is missing." >&2
    echo "       Set LUNACE_BUILD_DIR, or see cross-build/README.md." >&2
    exit 1
}
[ -x "$QT_HOST/qmake" ] || { echo "Error: no host qmake at $QT_HOST/qmake." >&2; exit 1; }
[ -d "$STAGING" ] || { echo "Error: staging tree $STAGING is missing." >&2; exit 1; }

export PATH="$PDK_TC/bin:$QT_HOST:$PATH"
export STAGING_INCDIR="$STAGING/usr/include"
export STAGING_LIBDIR="$STAGING/usr/lib"

BUILD_DIR="${BUILD_DIR:-$BUILD_DEFAULT}"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# qmake reads Qt's header/library roots from the qt.conf beside it, so it has to name
# the tree this build is against. Write it on EVERY run, not just when we run qmake
# ourselves: a qmake-generated Makefile re-runs qmake behind our back whenever
# sysmgr.pro (or an included .pri) is newer, and that re-run picks up whatever qt.conf
# says at the time -- a stale one quietly produces a binary that segfaults on boot.
cat > "$QT_HOST/qt.conf" <<EOF
[Paths]
Prefix = $STAGING/usr
Headers = $STAGING/usr/include
Libraries = $STAGING/usr/lib
Binaries = $QT_HOST
Data = $WORKSPACE/qt4-host/usr/share/qt4
EOF

if [ ! -f Makefile ] || [ "$1" = "reconf" ]; then
    "$QT_HOST/qmake" "$SRC/sysmgr.pro" \
        -spec qws/linux-armv7-g++ \
        "LIBS += -Wl,-rpath-link,$STAGING_LIBDIR -Wl,-rpath-link,$STAGING/lib" \
        "QMAKE_LFLAGS += -L$STAGING_LIBDIR -L$STAGING/lib"
    [ "$1" = "reconf" ] && shift
fi

make "$@"

# Guard against a class of silent breakage: an access specifier accidentally placed
# inside a Q_SLOTS block ends the block, so every slot below it stops being a slot and
# its connect()s fail at RUNTIME with only a console warning (this once killed every
# tap in the launcher). A registered slot appears as a "case N:" in moc's dispatch.
MOC_LAUNCHER="$BUILD_DIR/release-$MACHINE/.moc/moc_dimensionslauncher.cpp"
if [ -f "$MOC_LAUNCHER" ]; then
    for required_slot in slotIconActivatedTap slotTabActivatedTap slotTabActivatedTapAndHold; do
        if ! grep -q "case [0-9]*: _t->${required_slot}(" "$MOC_LAUNCHER"; then
            echo "ERROR: '${required_slot}' is NOT registered as a slot by moc." >&2
            echo "       Check for a stray public:/protected:/private: inside a Q_SLOTS block." >&2
            exit 1
        fi
    done
    echo "moc check: launcher slots registered ($(grep -c 'case [0-9]*:' "$MOC_LAUNCHER") total)"
fi

echo "Built $BUILD_DIR/release-$MACHINE/LunaSysMgr  (MACHINE=$MACHINE)"
