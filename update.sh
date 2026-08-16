#!/bin/sh
#
# Update guppyscreen from our own GitHub releases.
#
# Networking on the K1 is the awkward part. busybox wget cannot negotiate TLS
# with api.github.com or github.com, it fails with alert 80, and only reaches
# raw.githubusercontent.com. The stock /usr/bin/curl is not curl at all, it is a
# Creality utility with an unrelated command line. Upstream worked around this
# by downloading a curl binary from a third party repository over plain
# --no-check-certificate and running it as root.
#
# Python 3 is already installed because Klipper needs it, and it reaches both
# hosts fine, so we use that and drop the downloaded binary entirely.

set -u

GUPPY_DIR=$(cd "$(dirname "$0")" && pwd)
VERSION_FILE=$GUPPY_DIR/.version
CUSTOM_UPGRADE_SCRIPT=$GUPPY_DIR/custom_upgrade.sh
TARBALL=/tmp/guppyscreen-update.tar.gz

# Overridable so a fork or a test can point somewhere else.
REPO=${GUPPY_UPDATE_REPO:-JuggyMcNutty/vibescreen}

FORCE=false
[ "${1:-}" = "--force" ] && FORCE=true

PY=$(command -v python3 || command -v python || true)
if [ -z "$PY" ]; then
    echo "python3 is required to check for updates"
    exit 1
fi

CURRENT_VERSION=unknown
ASSET_NAME=guppyscreen.tar.gz
if [ -f "$VERSION_FILE" ]; then
    CURRENT_VERSION=$("$PY" -c "
import json,sys
d=json.load(open(sys.argv[1]))
print(d.get('version','unknown'))" "$VERSION_FILE" 2>/dev/null || echo unknown)
    ASSET_NAME=$("$PY" -c "
import json,sys
d=json.load(open(sys.argv[1]))
print(d.get('asset_name','guppyscreen.tar.gz'))" "$VERSION_FILE" 2>/dev/null || echo guppyscreen.tar.gz)
fi

echo "Installed version: $CURRENT_VERSION"
echo "Checking $REPO for a newer release"

# Prints "<tag>\t<download url>" for the newest release carrying our asset, or
# nothing at all if there is no usable release.
LATEST=$("$PY" -c "
import json, sys, urllib.request
repo, asset = sys.argv[1], sys.argv[2]
try:
    with urllib.request.urlopen(
            'https://api.github.com/repos/%s/releases' % repo, timeout=30) as r:
        releases = json.load(r)
except Exception as e:
    sys.stderr.write('could not reach github: %s\n' % e)
    sys.exit(1)
for rel in releases:
    if rel.get('draft'):
        continue
    for a in rel.get('assets', []):
        if a.get('name') == asset:
            print('%s\t%s' % (rel.get('tag_name', ''), a.get('browser_download_url', '')))
            sys.exit(0)
sys.exit(2)
" "$REPO" "$ASSET_NAME" 2>&1)
RC=$?

if [ $RC -eq 2 ]; then
    echo "No release in $REPO publishes $ASSET_NAME yet, nothing to update to."
    exit 0
elif [ $RC -ne 0 ]; then
    echo "Update check failed: $LATEST"
    exit 1
fi

LATEST_VERSION=$(printf '%s' "$LATEST" | cut -f1)
ASSET_URL=$(printf '%s' "$LATEST" | cut -f2)

if [ -z "$LATEST_VERSION" ] || [ -z "$ASSET_URL" ]; then
    echo "Could not work out the latest release, leaving the install alone."
    exit 1
fi

if [ "$CURRENT_VERSION" = "$LATEST_VERSION" ]; then
    echo "Already on $LATEST_VERSION."
    exit 0
fi

# Locally built binaries are versioned dev-<sha>. Replacing one with a release
# throws away whatever was being tested, which is almost never what is wanted
# from a button on the printer.
case "$CURRENT_VERSION" in
    dev-*)
        if [ "$FORCE" != true ]; then
            echo "This is a development build ($CURRENT_VERSION), refusing to replace it"
            echo "with release $LATEST_VERSION. Re-run with --force if that is what you want."
            exit 0
        fi
        echo "Development build, but --force given."
        ;;
esac

echo "Downloading $LATEST_VERSION from $ASSET_URL"
rm -f "$TARBALL"
if ! "$PY" -c "
import shutil, sys, urllib.request
with urllib.request.urlopen(sys.argv[1], timeout=180) as r, open(sys.argv[2], 'wb') as f:
    shutil.copyfileobj(r, f)
" "$ASSET_URL" "$TARBALL"; then
    echo "Download failed, leaving the install alone."
    rm -f "$TARBALL"
    exit 1
fi

# Check the archive before unpacking over a working install. Upstream ran tar
# unconditionally, outside the branch that did the download, so a failed fetch
# still reached the extract step.
if ! tar tzf "$TARBALL" > /dev/null 2>&1; then
    echo "Downloaded file is not a valid archive, leaving the install alone."
    rm -f "$TARBALL"
    exit 1
fi

if ! tar tzf "$TARBALL" 2>/dev/null | grep -q 'guppyscreen/guppyscreen$'; then
    echo "Archive does not contain guppyscreen/guppyscreen, leaving the install alone."
    rm -f "$TARBALL"
    exit 1
fi

echo "Installing $LATEST_VERSION"
if ! tar xzf "$TARBALL" -C "$GUPPY_DIR/.."; then
    echo "Extract failed. The install may be inconsistent, check $GUPPY_DIR."
    rm -f "$TARBALL"
    exit 1
fi
rm -f "$TARBALL"

if [ -f "$CUSTOM_UPGRADE_SCRIPT" ]; then
    echo "Running custom_upgrade.sh for release $LATEST_VERSION"
    "$CUSTOM_UPGRADE_SCRIPT"
fi

echo "Updated Guppy Screen to $LATEST_VERSION"

if grep -Fqs "ID=buildroot" /etc/os-release; then
    [ -f /etc/init.d/S99guppyscreen ] && /etc/init.d/S99guppyscreen stop > /dev/null 2>&1
    killall -q guppyscreen
    /etc/init.d/S99guppyscreen restart > /dev/null 2>&1
fi

exit 0
