#!/bin/sh
#
# Install LunaCE (or put stock webOS back) on an HP TouchPad over novacom.
#
#   ./install.sh           ask which version to install
#   ./install.sh --lunace  install LunaCE
#   ./install.sh --stock   restore the stock webOS 3.0.5 launcher

set -e

HERE=$(cd "$(dirname "$0")" && pwd)

echo "LunaCE Install"
echo "=============="
echo "Checking for connected TouchPad ..."
if ! novacom -l 2>/dev/null | grep -q "topaz-linux"; then
    echo "Error: No TouchPad detected. Connect via USB and try again."
    exit 1
fi

choose_stock() {
    LUNA_BIN="$HERE/bin/LunaSysMgr-Stock"
    WHAT="stock webOS 3.0.5"
    NEXT="To install LunaCE:     ./install.sh --lunace"
}

choose_lunace() {
    LUNA_BIN="$HERE/bin/LunaSysMgr-LunaCE"
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
