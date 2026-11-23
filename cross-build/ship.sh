#!/bin/bash
# Refresh the shipped builds in bin/ from a CLEAN build of this tree, topaz and opal.
#
#   ./cross-build/ship.sh
#
# This is the only way a build should reach bin/. It deletes each machine's build
# directory first, so every object is compiled against today's headers. It then
# copies the binaries to bin/ and records their sha256 in bin/LunaSysMgr-LunaCE.clean.
# The webOS CE Doctor's bake refuses a LunaSysMgr that has no matching record.
#
# Why: an incremental build after a header change once shipped objects compiled
# against the old Settings layout. Every phone app crashed LunaSysMgr, and 60 other
# functions read the wrong setting (webOS-Community-Edition issue #16).
# sysmgr.pro now tracks header dependencies, but a release does not rely on that.

set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$(cd "$HERE/.." && pwd)"
WORKSPACE="${LUNACE_BUILD_DIR:-$(cd "$SRC/.." && pwd)/lunace-build}"
RECORD="$SRC/bin/LunaSysMgr-LunaCE.clean"

for machine in topaz opal; do
    if [ "$machine" = topaz ]; then dir="$WORKSPACE/sysmgr-build"; else dir="$WORKSPACE/sysmgr-build-$machine"; fi
    echo "=== $machine: clean build in $dir"
    rm -rf "${dir:?}"
    BUILD_DIR="$dir" MACHINE="$machine" "$HERE/build.sh" -j"$(nproc)"
    cp "$dir/release-$machine/LunaSysMgr" "$SRC/bin/LunaSysMgr-LunaCE-$machine"
done

{
    echo "# Clean builds made by cross-build/ship.sh $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "# from $(git -C "$SRC" describe --always --dirty) (dirty = the commit these binaries go in with)"
    (cd "$SRC/bin" && sha256sum LunaSysMgr-LunaCE-topaz LunaSysMgr-LunaCE-opal)
} > "$RECORD"
cat "$RECORD"
