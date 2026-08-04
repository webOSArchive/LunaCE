#!/bin/bash
# Cross-build LunaCE (LunaSysMgr) for the HP TouchPad (topaz) or TouchPad Go (opal),
# against either webOS release.
#
#   ./cross-build/build.sh                                 topaz, webOS 3.0.5 (default)
#   MACHINE=opal ./cross-build/build.sh                     opal, webOS 3.0.5
#   WEBOS_ABI=3.0.4 ./cross-build/build.sh                 topaz, webOS 3.0.4
#   MACHINE=opal WEBOS_ABI=3.0.4 ./cross-build/build.sh     opal, webOS 3.0.4
#
# MACHINE and WEBOS_ABI are independent. MACHINE picks the hardware support
# (HostArmTopaz vs HostArmOpal, and which rendering options are enabled);
# WEBOS_ABI picks which release's libraries and ABI we build against. Both tablets
# shipped in 3.0.4 and 3.0.5 flavours -- on the Go it went by radio -- so neither
# one implies the other.
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
#     staging/armv7-304/   the same tree with the shared libraries replaced by 3.0.4
#                          ones off a device and a few headers edited to match --
#                          see mk-staging-304.sh
#     sysmgr-build[-<machine>][-304]/   shadow build dirs, one per combination
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
export WEBOS_ABI="${WEBOS_ABI:-3.0.5}"

case "$WEBOS_ABI" in
    3.0.4) STAGING_DEFAULT="$WORKSPACE/staging/armv7-304"; SUFFIX="-304" ;;
    3.0.5) STAGING_DEFAULT="$WORKSPACE/staging/armv7";     SUFFIX="" ;;
    *) echo "Error: WEBOS_ABI must be 3.0.4 or 3.0.5, got '$WEBOS_ABI'." >&2; exit 1 ;;
esac

# topaz/3.0.5 keeps the paths it has always used, so an existing build stays valid.
if [ "$MACHINE" = topaz ]; then
    BUILD_DEFAULT="$WORKSPACE/sysmgr-build$SUFFIX"
else
    BUILD_DEFAULT="$WORKSPACE/sysmgr-build-$MACHINE$SUFFIX"
fi
STAGING="${STAGING:-$STAGING_DEFAULT}"

[ -d "$WORKSPACE" ] || {
    echo "Error: build workspace $WORKSPACE is missing." >&2
    echo "       Set LUNACE_BUILD_DIR, or see cross-build/README.md." >&2
    exit 1
}
[ -x "$QT_HOST/qmake" ] || { echo "Error: no host qmake at $QT_HOST/qmake." >&2; exit 1; }
[ -d "$STAGING" ] || {
    echo "Error: staging tree $STAGING is missing." >&2
    [ "$WEBOS_ABI" = 3.0.4 ] && echo "       Build it with cross-build/mk-staging-304.sh." >&2
    exit 1
}

export PATH="$PDK_TC/bin:$QT_HOST:$PATH"
export STAGING_INCDIR="$STAGING/usr/include"
export STAGING_LIBDIR="$STAGING/usr/lib"

BUILD_DIR="${BUILD_DIR:-$BUILD_DEFAULT}"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

if [ ! -f Makefile ] || [ "$1" = "reconf" ]; then
    # qmake reads Qt's header/library roots from the qt.conf beside it, which would
    # otherwise pin every build to whichever staging tree was configured last -- and
    # silently leave the 3.0.5 libraries on a 3.0.4 link line as a fallback.
    cat > "$QT_HOST/qt.conf" <<EOF
[Paths]
Prefix = $STAGING/usr
Headers = $STAGING/usr/include
Libraries = $STAGING/usr/lib
Binaries = $QT_HOST
Data = $WORKSPACE/qt4-host/usr/share/qt4
EOF
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

echo "Built $BUILD_DIR/release-$MACHINE/LunaSysMgr  (MACHINE=$MACHINE WEBOS_ABI=$WEBOS_ABI)"
