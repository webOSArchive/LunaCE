#!/bin/sh
#
# Install or remove LunaCE on a tablet running webOS 3.0.4.
#
#   ./install-304.sh             ask what to do
#   ./install-304.sh --lunace    install LunaCE
#   ./install-304.sh --stock     put the device's own stock launcher back
#
# webOS 3.0.4 needs its own binary. Its Qt 4.8.0 is configured without QAction,
# and its libmedia-api and libWebKitLuna predate virtuals that 3.0.5 added, so a
# 3.0.5 build has vtables that do not line up: it dies on launch or segfaults soon
# after. That is a property of the OS release, not the hardware -- both the
# TouchPad (topaz) and the TouchPad Go (opal) shipped in 3.0.4 and 3.0.5 flavours,
# and on the Go which one you got went by radio. So this checks the version rather
# than the model, and installs the build matching whichever tablet is attached.
# Use ./install.sh for 3.0.5 devices.
#
# Uninstall restores the stock LunaSysMgr this script copied aside before its
# first install, so it puts back that device's own binary rather than a generic one.

set -e

HERE=$(cd "$(dirname "$0")" && pwd)
BACKUP=/var/luna/LunaSysMgr-stock-3.0.4

echo "LunaCE Install (webOS 3.0.4)"
echo "============================"
echo "Looking for a connected tablet ..."

# novacom reports e.g. "topaz-linux" or "opal-linux"; the prefix is the MACHINE
# the binary has to have been built for.
MACHINE=$(novacom -l 2>/dev/null | sed -n 's/.*[[:space:]]\([a-z0-9]*\)-linux.*/\1/p' | head -1)
[ -n "$MACHINE" ] || { echo "Error: No tablet detected. Connect via USB and try again."; exit 1; }

VERSION=$(novacom run file://bin/cat -- /etc/palm-build-info 2>/dev/null \
          | sed -n 's/^PRODUCT_VERSION_STRING=//p' | tr -d '\r')
case "$VERSION" in
    *3.0.4*) ;;
    "")      echo "Error: could not read the webOS version off the device."; exit 1 ;;
    *)       echo "Error: this device reports '$VERSION', not 3.0.4."
             echo "       For a 3.0.5 device use ./install.sh instead."
             exit 1 ;;
esac
echo "Found a '$MACHINE' tablet running $VERSION."

choose_stock() {
    ACTION=stock
    WHAT="the stock webOS 3.0.4 launcher"
    NEXT="To install LunaCE:     ./install-304.sh --lunace"
}

choose_lunace() {
    ACTION=lunace
    LUNA_BIN="$HERE/bin/LunaSysMgr-LunaCE-$MACHINE-3.0.4"
    WHAT="LunaCE"
    NEXT="To go back to stock:   ./install-304.sh --stock"
}

case "$1" in
    --stock)  choose_stock ;;
    --lunace) choose_lunace ;;
    *)
        echo ""
        echo "  1) LunaCE  - app groups, renameable tabs, add and remove tabs"
        echo "  2) Stock   - the launcher this tablet shipped with"
        echo ""
        printf "Which would you like to install? (1/2): "
        read -r choice
        case "$choice" in
            2) choose_stock ;;
            *) choose_lunace ;;
        esac
        ;;
esac

if [ "$ACTION" = lunace ]; then
    [ -f "$LUNA_BIN" ] || {
        echo "Error: no LunaCE build for '$MACHINE' on webOS 3.0.4."
        echo "       Expected $LUNA_BIN."
        echo "       Build one with: WEBOS_ABI=3.0.4 MACHINE=$MACHINE ./build.sh"
        exit 1
    }
else
    # Restoring: the on-device backup is authoritative. Only fall back to a shipped
    # binary if this script was never the one that installed LunaCE here.
    if novacom run file://usr/bin/test -- -s "$BACKUP" >/dev/null 2>&1; then
        LUNA_BIN=""
    elif [ -f "$HERE/bin/LunaSysMgr-Stock-$MACHINE-3.0.4" ]; then
        LUNA_BIN="$HERE/bin/LunaSysMgr-Stock-$MACHINE-3.0.4"
        echo "No on-device backup found; using the stock binary shipped with this repo."
    else
        echo "Error: no stock launcher to restore."
        echo "       Expected a backup at $BACKUP on the device, or"
        echo "       $HERE/bin/LunaSysMgr-Stock-$MACHINE-3.0.4 here."
        echo "       Recover with a webOS Doctor for this tablet."
        exit 1
    fi
fi

echo ""
echo "Installing $WHAT ..."
novacom run -- file:///sbin/stop LunaSysMgr
sleep 3                                   # the old binary stays busy for a moment
novacom run file://bin/mount -- -o remount,rw /

if [ "$ACTION" = lunace ]; then
    # Keep the launcher the device came with, once, before the first overwrite.
    if novacom run file://usr/bin/test -- -s "$BACKUP" >/dev/null 2>&1; then
        echo "Stock launcher already backed up at $BACKUP."
    else
        echo "Backing up the stock launcher to $BACKUP ..."
        novacom run file://bin/cp -- /usr/bin/LunaSysMgr "$BACKUP"
        novacom run file://usr/bin/test -- -s "$BACKUP" >/dev/null 2>&1 || {
            echo "Error: backup failed, refusing to overwrite the stock launcher."
            novacom run file://bin/mount -- -o remount,ro /
            novacom run -- file:///sbin/start LunaSysMgr
            exit 1
        }
    fi
    novacom put file:///usr/bin/LunaSysMgr < "$LUNA_BIN"

    # launcher artwork the LunaCE build expects
    for asset in tab-add-icon.png tab-delete-icon.png; do
        if [ -f "$HERE/images/launcher3/$asset" ]; then
            novacom put "file:///usr/palm/sysmgr/images/launcher3/$asset" < "$HERE/images/launcher3/$asset"
        fi
    done
else
    if [ -n "$LUNA_BIN" ]; then
        novacom put file:///usr/bin/LunaSysMgr < "$LUNA_BIN"
    else
        echo "Restoring from $BACKUP ..."
        novacom run file://bin/cp -- "$BACKUP" /usr/bin/LunaSysMgr
    fi
fi

novacom run file://bin/chmod -- 750 /usr/bin/LunaSysMgr
novacom run file://bin/mount -- -o remount,ro /

echo "Restarting the launcher ..."
novacom run -- file:///sbin/start LunaSysMgr

echo ""
echo "Done - $WHAT is installed. The launcher takes about half a minute to come back."
echo "$NEXT"
