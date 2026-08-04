#!/bin/sh
#
# Install LunaCE (or put stock webOS back) on an HP TouchPad or TouchPad Go,
# over novacom.
#
#   ./install.sh           ask which version to install
#   ./install.sh --lunace  install LunaCE
#   ./install.sh --stock   restore the stock webOS 3.0.5 launcher
#
# Both tablets run webOS 3.0.5 but need their own binary, since the hardware
# support is compiled in. novacom says which one is attached ("topaz-linux" or
# "opal-linux"), so this picks the matching build rather than asking. It also
# checks the release the device reports: on 3.0.4 the vtables in Qt,
# libmedia-api and libWebKitLuna do not line up with a 3.0.5 build, which
# crash-loops rather than failing outright -- worth catching before overwriting
# a working launcher.

set -e

HERE=$(cd "$(dirname "$0")" && pwd)

echo "LunaCE Install"
echo "=============="
echo "Looking for a connected tablet ..."

MACHINE=$(novacom -l 2>/dev/null | sed -n 's/.*[[:space:]]\([a-z0-9]*\)-linux.*/\1/p' | head -1)
case "$MACHINE" in
    topaz) TABLET="TouchPad" ;;
    opal)  TABLET="TouchPad Go" ;;
    "")    echo "Error: No tablet detected. Connect via USB and try again."; exit 1 ;;
    *)     echo "Error: unsupported device '$MACHINE-linux'."; exit 1 ;;
esac

VERSION=$(novacom run file://bin/cat -- /etc/palm-build-info 2>/dev/null \
          | sed -n 's/^PRODUCT_VERSION_STRING=//p' | tr -d '\r')
case "$VERSION" in
    *3.0.5*) ;;
    "")      echo "Error: could not read the webOS version off the device."; exit 1 ;;
    *)       echo "Error: this $TABLET reports '$VERSION'; LunaCE needs webOS 3.0.5."; exit 1 ;;
esac

echo "Found a $TABLET running $VERSION."

choose_stock() {
    LUNA_BIN="$HERE/bin/LunaSysMgr-Stock-$MACHINE"
    WHAT="stock webOS 3.0.5"
    NEXT="To install LunaCE:     ./install.sh --lunace"
}

choose_lunace() {
    LUNA_BIN="$HERE/bin/LunaSysMgr-LunaCE-$MACHINE"
    WHAT="LunaCE"
    NEXT="To go back to stock:   ./install.sh --stock"
}

case "$1" in
    --stock)  choose_stock ;;
    --lunace) choose_lunace ;;
    *)
        echo ""
        echo "  1) LunaCE  - app groups, renameable tabs, add and remove tabs"
        echo "  2) Stock   - the original HP webOS 3.0.5 launcher"
        echo ""
        printf "Which would you like to install? (1/2): "
        read -r choice
        case "$choice" in
            2) choose_stock ;;
            *) choose_lunace ;;
        esac
        ;;
esac

[ -f "$LUNA_BIN" ] || { echo "Error: $LUNA_BIN is missing."; exit 1; }

echo ""
echo "Installing $WHAT ..."
novacom run -- file:///sbin/stop LunaSysMgr
sleep 3                                   # the old binary stays busy for a moment
novacom run file://bin/mount -- -o remount,rw /
novacom put file:///usr/bin/LunaSysMgr < "$LUNA_BIN"

# launcher artwork the LunaCE build expects
for asset in tab-add-icon.png tab-delete-icon.png; do
    if [ -f "$HERE/images/launcher3/$asset" ]; then
        novacom put "file:///usr/palm/sysmgr/images/launcher3/$asset" < "$HERE/images/launcher3/$asset"
    fi
done

novacom run file://bin/mount -- -o remount,ro /

echo "Restarting the launcher ..."
novacom run -- file:///sbin/start LunaSysMgr

echo ""
echo "Done - $WHAT is installed. The launcher takes about half a minute to come back."
echo "$NEXT"
