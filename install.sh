#!/bin/sh
#
# Install a LunaCE build onto an HP TouchPad over novacom.
#
#   ./install.sh                     install a build found automatically
#   ./install.sh path/to/LunaSysMgr  install a specific binary
#   ./install.sh --restore           restore the most recent backup
#
# The current /usr/bin/LunaSysMgr is backed up to /media/internal before it is
# replaced, so there is always a way back. The UI is restarted rather than the
# device rebooted; open cards survive neither.

set -e

DEVICE_BINARY="/usr/bin/LunaSysMgr"
DEVICE_STAGING="/media/internal/LunaSysMgr.staged"
DEVICE_BACKUP_DIR="/media/internal"
DEVICE_IMAGE_DIR="/usr/palm/sysmgr/images/launcher3"
REMOTE_SCRIPT="/tmp/lunace-install-step.sh"

HERE=$(cd "$(dirname "$0")" && pwd)

say() { echo "$@"; }
die() { echo "Error: $@" >&2; exit 1; }

# novacom does not pass a quoted compound command through as a single argument,
# so anything with pipes, tests or redirection is pushed as a script and run.
# Reads the script from stdin; echoes whatever it printed on the device.
remote_sh() {
    _tmp=$(mktemp)
    cat > "$_tmp"
    novacom put "file://$REMOTE_SCRIPT" < "$_tmp" >/dev/null 2>&1
    rm -f "$_tmp"
    novacom run -- file:///bin/sh "$REMOTE_SCRIPT" 2>/dev/null | tr -d '\r'
}

command -v novacom >/dev/null 2>&1 || die "novacom is not on PATH."

say "Checking for a connected TouchPad ..."
novacom -l 2>/dev/null | grep -q "topaz-linux" \
    || die "No TouchPad detected. Connect it by USB, check that novacomd is running, and try again."

# --- restore ----------------------------------------------------------------

if [ "$1" = "--restore" ]; then
    BACKUP=$(remote_sh <<EOF
ls -1 $DEVICE_BACKUP_DIR/LunaSysMgr.backup-* 2>/dev/null | tail -1
EOF
)
    [ -n "$BACKUP" ] || die "No backup found in $DEVICE_BACKUP_DIR."
    say "Restoring $BACKUP ..."
    remote_sh <<EOF
/sbin/stop LunaSysMgr >/dev/null 2>&1
sleep 5
/bin/mount -o remount,rw /
/bin/cp "$BACKUP" $DEVICE_BINARY
/bin/chmod 755 $DEVICE_BINARY
/bin/mount -o remount,ro /
/sbin/start LunaSysMgr >/dev/null 2>&1
EOF
    say "Restored. The UI is restarting; give it about half a minute."
    exit 0
fi

# --- locate the binary ------------------------------------------------------

if [ -n "$1" ]; then
    BINARY="$1"
    [ -f "$BINARY" ] || die "$BINARY does not exist."
else
    for candidate in \
        "$HERE/bin/LunaSysMgr" \
        "$HERE/release-topaz/LunaSysMgr" \
        "$HERE"/../lunace-build/*/release-topaz/LunaSysMgr
    do
        if [ -f "$candidate" ]; then BINARY="$candidate"; break; fi
    done
    [ -n "$BINARY" ] || die "No LunaSysMgr build found. Pass one: ./install.sh path/to/LunaSysMgr"
fi

case "$(file -b "$BINARY" 2>/dev/null)" in
    *"ELF 32-bit"*ARM*) : ;;
    *) die "$BINARY is not an ARM binary. Was it built for the host by mistake?" ;;
esac

say "Installing: $BINARY"

# --- stage and verify before touching the running system --------------------

say "Copying to the device ..."
novacom put "file://$DEVICE_STAGING" < "$BINARY"

LOCAL_SUM=$(md5sum "$BINARY" | cut -d' ' -f1)
DEVICE_SUM=$(remote_sh <<EOF
/usr/bin/md5sum $DEVICE_STAGING | cut -d' ' -f1
EOF
)
[ "$LOCAL_SUM" = "$DEVICE_SUM" ] \
    || die "Transfer verification failed ($LOCAL_SUM != $DEVICE_SUM). Nothing has been changed."
say "Transfer verified ($LOCAL_SUM)."

# Launcher artwork this build expects; skipped if this tree does not carry it.
STAGED_ASSETS=""
for asset in tab-add-icon.png tab-delete-icon.png; do
    if [ -f "$HERE/images/launcher3/$asset" ]; then
        novacom put "file:///media/internal/$asset" < "$HERE/images/launcher3/$asset"
        STAGED_ASSETS="$STAGED_ASSETS $asset"
    fi
done

# --- swap -------------------------------------------------------------------

say "Stopping the UI, backing up, and installing ..."

ASSET_COPY=""
for asset in $STAGED_ASSETS; do
    ASSET_COPY="$ASSET_COPY
/bin/cp /media/internal/$asset $DEVICE_IMAGE_DIR/$asset"
done

RESULT=$(remote_sh <<EOF
/sbin/stop LunaSysMgr >/dev/null 2>&1
sleep 5                                     # the old binary stays busy briefly
/bin/mount -o remount,rw /
if [ -e $DEVICE_BINARY ]; then
    /bin/cp $DEVICE_BINARY $DEVICE_BACKUP_DIR/LunaSysMgr.backup-$(date +%Y%m%d-%H%M%S)
fi
/bin/cp $DEVICE_STAGING $DEVICE_BINARY
/bin/chmod 755 $DEVICE_BINARY$ASSET_COPY
/usr/bin/md5sum $DEVICE_BINARY | cut -d' ' -f1
/bin/mount -o remount,ro /
/sbin/start LunaSysMgr >/dev/null 2>&1
EOF
)

INSTALLED_SUM=$(echo "$RESULT" | tail -1)
if [ "$LOCAL_SUM" != "$INSTALLED_SUM" ]; then
    die "The installed binary does not match what was sent ($INSTALLED_SUM).
The UI has been restarted regardless. Run './install.sh --restore' to go back."
fi

say ""
say "Installed and verified. The launcher takes about half a minute to come back."
say "A backup of the previous LunaSysMgr is in $DEVICE_BACKUP_DIR."
say "If anything is wrong:  ./install.sh --restore"
