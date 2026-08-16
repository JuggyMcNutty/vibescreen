#!/bin/bash

RELEASES_DIR=./releases/guppyscreen
rm -rf $RELEASES_DIR
mkdir -p $RELEASES_DIR

ASSET_NAME=$1

# CROSS_COMPILE has to be set, and the toolchain on PATH, or this silently
# invokes the host strip. That does not fail loudly on a foreign binary, it just
# leaves it unstripped, which shipped a 7.5MB binary where 6.4MB was expected.
STRIP="${CROSS_COMPILE}strip"
READELF="${CROSS_COMPILE}readelf"

for bin in ./build/bin/guppyscreen ./build/bin/kd_graphic_mode; do
    [ -f "$bin" ] || continue
    "$STRIP" "$bin" || { echo "ERROR: $STRIP failed on $bin" >&2; exit 1; }

    # Verify rather than trust. The host strip returning 0 is exactly how this
    # went unnoticed.
    if "$READELF" -S "$bin" 2>/dev/null | grep -qE '\.symtab'; then
        echo "ERROR: $bin still has a symbol table after $STRIP." >&2
        echo "Is CROSS_COMPILE set and the toolchain on PATH?" >&2
        exit 1
    fi
done
cp ./build/bin/guppyscreen $RELEASES_DIR/guppyscreen
cp -r ./k1/k1_mods $RELEASES_DIR
cp -r ./k1/scripts $RELEASES_DIR
cp -r ./themes $RELEASES_DIR
cp ./installer.sh $RELEASES_DIR
cp ./update.sh $RELEASES_DIR
if [ -f ./custom_upgrade.sh ]; then
    cp ./custom_upgrade.sh $RELEASES_DIR
fi
cp reinstall-creality.sh $RELEASES_DIR
cp -r ./debian $RELEASES_DIR
cp ./build/bin/kd_graphic_mode $RELEASES_DIR/debian


echo "{\"version\": \"$GUPPYSCREEN_VERSION\", \"theme\": \"$GUPPY_THEME\", \"asset_name\": \"$ASSET_NAME.tar.gz\"}" > $RELEASES_DIR/.version
tar czf $ASSET_NAME.tar.gz -C releases .
