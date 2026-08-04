#!/bin/bash
# Build staging/armv7-304 -- the link tree for webOS 3.0.4.
#
# This is about the OS release, not the hardware: both the TouchPad (topaz) and
# the TouchPad Go (opal) shipped in 3.0.4 and 3.0.5 flavours, and on the Go which
# one you got went by radio. Any 3.0.4 device is a valid source of libraries here,
# and the tree it produces is used for 3.0.4 builds of either machine.
#
# webOS 3.0.4 and 3.0.5 share the same Qt 4.8.0 and the same webOS private
# libraries; 3.0.4 is simply built from a narrower configure (no QAction, no
# QClipboard) and predates a handful of additions such as libqpalm's
# setAdvancedGestures(). So the 3.0.4 tree is the existing 3.0.5 staging tree with
# its shared libraries swapped for the ones off a real device. Headers and static
# libraries (librolegen.a, libserviceinstall.a, the WOCE build-support overlay)
# are not on the device at all and carry over unchanged.
#
# Usage:
#   ./cross-build/mk-staging-304.sh                 pull from an attached 3.0.4 device
#   ./cross-build/mk-staging-304.sh /path/libs.tar  reuse a tarball pulled earlier
#
# The tarball is just `tar cf - /usr/lib /lib` taken on the device. Both trees live
# in the build workspace -- $LUNACE_BUILD_DIR, default ../lunace-build.

set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$(cd "$HERE/.." && pwd)"
WORKSPACE="${LUNACE_BUILD_DIR:-$(cd "$SRC/.." && pwd)/lunace-build}"

SRC_TREE="$WORKSPACE/staging/armv7"
DST_TREE="$WORKSPACE/staging/armv7-304"
TARBALL="$1"

[ -d "$SRC_TREE" ] || { echo "Error: $SRC_TREE (the 3.0.5 staging tree) is missing." >&2; exit 1; }

if [ -z "$TARBALL" ]; then
    novacom -l 2>/dev/null | grep -q -- "-linux" || {
        echo "Error: no device attached, and no tarball given." >&2
        exit 1
    }
    # Take the libraries only from a device actually running 3.0.4 -- pulling a
    # 3.0.5 rootfs in here would silently produce a tree identical to the 3.0.5 one.
    DEV_VERSION=$(novacom run file://bin/cat -- /etc/palm-build-info 2>/dev/null \
                  | sed -n 's/^PRODUCT_VERSION_STRING=//p' | tr -d '\r')
    case "$DEV_VERSION" in
        *3.0.4*) echo "Source device is running $DEV_VERSION." ;;
        "")      echo "Error: could not read the webOS version off the device." >&2; exit 1 ;;
        *)       echo "Error: attached device runs '$DEV_VERSION', not 3.0.4." >&2; exit 1 ;;
    esac
    TARBALL="$WORKSPACE/devlibs-304.tar"
    echo "Collecting /usr/lib and /lib from the device ..."
    TMPSH=$(mktemp)
    cat > "$TMPSH" <<'EOF'
#!/bin/sh
rm -f /media/internal/devlibs.tar
tar cf /media/internal/devlibs.tar /usr/lib /lib 2>/dev/null
EOF
    novacom put file:///tmp/mk-staging-tar.sh < "$TMPSH"
    rm -f "$TMPSH"
    novacom run file://bin/sh -- /tmp/mk-staging-tar.sh
    novacom get file:///media/internal/devlibs.tar > "$TARBALL"
    novacom run file://bin/rm -- -f /media/internal/devlibs.tar /tmp/mk-staging-tar.sh
fi

[ -s "$TARBALL" ] || { echo "Error: $TARBALL is empty or missing." >&2; exit 1; }

echo "Cloning $SRC_TREE -> $DST_TREE ..."
rm -rf "$DST_TREE"
# Hardlink the clone so this costs almost nothing; every file replaced below is
# unlinked first, so the 3.0.5 tree is never written through.
cp -al "$SRC_TREE" "$DST_TREE"

echo "Overlaying the 3.0.4 shared libraries ..."
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
tar xf "$TARBALL" -C "$STAGE"

replaced=0
added=0
while IFS= read -r rel; do
    src="$STAGE/$rel"
    dst="$DST_TREE/$rel"
    [ -f "$src" ] || continue                 # skip directories and dangling links
    mkdir -p "$(dirname "$dst")"
    if [ -e "$dst" ]; then replaced=$((replaced+1)); else added=$((added+1)); fi
    rm -f "$dst"                              # break the hardlink, never write through
    cp "$src" "$dst"
done < <(cd "$STAGE" && find usr/lib lib -type f 2>/dev/null)

# Symlinks (libc.so.6 -> libc-2.8.so and friends) have to come across too.
while IFS= read -r rel; do
    target=$(readlink "$STAGE/$rel")
    rm -f "$DST_TREE/$rel"
    ln -s "$target" "$DST_TREE/$rel"
done < <(cd "$STAGE" && find usr/lib lib -type l 2>/dev/null)

# glibc ships libc.so/libpthread.so as GNU ld scripts naming their members by
# absolute path (/lib/libc.so.6, ...). A cross-link resolves those against the host
# root and fails. The 3.0.5 tree was already patched to bare names so that -L finds
# them; the device copies we just laid down are the stock absolute ones, so redo it.
scripts=0
for f in $(grep -rl 'GNU ld script' "$DST_TREE/usr/lib" "$DST_TREE/lib" 2>/dev/null); do
    if grep -qE '(GROUP|INPUT|AS_NEEDED)[^)]*/(usr/)?lib/' "$f"; then
        sed -i -E 's#(^|[( ])/(usr/)?lib/#\1#g' "$f"
        scripts=$((scripts+1))
    fi
done

# The build-support headers describe webOS 3.0.5. Where 3.0.5 added a virtual to a
# class the 3.0.4 libraries also define, every virtual declared after it shifts by a
# slot, and calls across the boundary land on the wrong function -- a SIGSEGV, not a
# link error, so nothing catches it until the device crashes. Drop those declarations
# so the vtables line up with the libraries we just installed. Verified against
# `nm -S` vtable sizes: media::MediaPlayer 308 vs 316, Palm::WebViewClient 180 vs 200.
#
# Qt's equivalents (QAction, QClipboard) need no patching -- its headers already
# guard them with QT_NO_ACTION / QT_NO_CLIPBOARD, which device.pri defines when
# WEBOS_ABI=3.0.4.
drop_decl() {   # drop_decl <file> <expected count> <grep -E pattern>
    file="$DST_TREE/usr/include/$1"; want="$2"; pat="$3"
    [ -f "$file" ] || { echo "Error: expected header $file" >&2; exit 1; }
    got=$(grep -cE "$pat" "$file" || true)
    if [ "$got" != "$want" ]; then
        echo "Error: $1 matched $got declarations for '$pat', expected $want." >&2
        echo "       The build-support headers changed; re-check the vtable diff." >&2
        exit 1
    fi
    sed -i -E "s#^([[:space:]]*)($pat)#\1// [not in webOS 3.0.4] \2#" "$file"
    echo "  $1: dropped $got declaration(s)"
}

echo "Patching headers to match the 3.0.4 ABI ..."
drop_decl media/MediaPlayer.h 2 'virtual .*(getHttpparameters|setHttpparameters)\(.*= 0;'
drop_decl media/MediaPlayer.h 1 'virtual void httpparametersChanged\(\)'
drop_decl webkit/palmwebviewclient.h 5 'virtual .*(createSensor|destroySensor|getSupportedSensors|setSensorRate|startSensor)\('

echo "Done: $replaced libraries replaced, $added added, $scripts ld scripts de-absolutised"
echo "Build with:  WEBOS_ABI=3.0.4 [MACHINE=opal] ./cross-build/build.sh"
