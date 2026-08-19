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
# --check answers "is there a newer release" and installs nothing. The UI polls
# it, because the binary has no TLS: libhv is built with WITH_OPENSSL=no, and
# giving it a TLS stack and a trust store to fetch one version string is a poor
# trade when the check already lives here and python3 is already required.
CHECK_ONLY=false
for arg in "$@"; do
    case "$arg" in
        --force) FORCE=true ;;
        --check) CHECK_ONLY=true ;;
        *) echo "usage: $(basename "$0") [--force] [--check]"; exit 1 ;;
    esac
done

# --check is parsed by a machine, so it prints key=value on stdout and nothing
# else. Every path through it prints a status, including the failures, so the
# caller never has to tell "no update" apart from "never got an answer".
check_result() {
    [ "$CHECK_ONLY" = true ] || return 0
    echo "status=$1"
    echo "current=$CURRENT_VERSION"
    echo "latest=$2"
    exit 0
}


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

[ "$CHECK_ONLY" = true ] || echo "Installed version: $CURRENT_VERSION"
[ "$CHECK_ONLY" = true ] || echo "Checking $REPO for a newer build"

# Prints "<tag>\t<download url>" for the newest release, or exits 2 if it has no
# asset for us.
#
# Every push to main publishes its own release tagged <date>-<sha>, so the tag
# is unique per build and is the identity: no need to look inside the release
# to work out which build it holds.
LATEST=$("$PY" -c "
import json, sys, urllib.error, urllib.request
repo, asset = sys.argv[1], sys.argv[2]
try:
    with urllib.request.urlopen(
            'https://api.github.com/repos/%s/releases/latest' % repo,
            timeout=30) as r:
        rel = json.load(r)
except urllib.error.HTTPError as e:
    if e.code == 404:
        sys.exit(2)
    sys.stderr.write('could not reach github: %s\n' % e)
    sys.exit(1)
except Exception as e:
    sys.stderr.write('could not reach github: %s\n' % e)
    sys.exit(1)
for a in rel.get('assets', []):
    if a.get('name') == asset:
        print('%s\t%s' % (rel.get('tag_name', ''), a.get('browser_download_url', '')))
        sys.exit(0)
sys.exit(2)
" "$REPO" "$ASSET_NAME" 2>&1)
RC=$?

if [ $RC -eq 2 ]; then
    check_result noasset ""
    echo "No release in $REPO publishes $ASSET_NAME, nothing to update to."
    exit 0
elif [ $RC -ne 0 ]; then
    check_result unreachable ""
    echo "Update check failed: $LATEST"
    exit 1
fi

LATEST_VERSION=$(printf '%s' "$LATEST" | cut -f1)
ASSET_URL=$(printf '%s' "$LATEST" | cut -f2)

if [ -z "$LATEST_VERSION" ] || [ -z "$ASSET_URL" ]; then
    check_result unreachable ""
    echo "Could not work out the latest release, leaving the install alone."
    exit 1
fi

# The tarball carries the Klipper side of the install as well as the binary:
# scripts/*.cfg are the guppy macros and k1_mods/*.py the klippy extras. Klipper
# does not read them from here, though. installer.sh copies them into the
# printer's own config tree, so unpacking alone leaves Klipper running whatever
# was installed the first time.
#
# That was not theoretical. Measured on the development K1 Max on 2026-08-18,
# its _GUPPY_LOAD_MATERIAL extruded a hardcoded 120mm and ignored the EXTRUDE_LEN
# the panel sends, which is neither our version of the macro nor upstream's. So
# the Extrude Length selector did nothing, and every fix we make to the macros
# would have stayed invisible. Copy them across too.
refresh_klipper_files() {
    [ -d "$GUPPY_DIR/scripts" ] || return 0

    # Ask Moonraker where Klipper keeps its config and its extras rather than
    # guessing. The printer's /usr/bin/curl is a Creality utility with an
    # unrelated command line, so use busybox wget, which is fine over plain
    # HTTP. Falls back to the stock K1 paths when Moonraker is not answering.
    info=$(wget -q -T 10 -O - "http://127.0.0.1:7125/printer/info" 2>/dev/null || true)
    paths=$(printf '%s' "$info" | "$PY" -c "
import json, os, sys
klipper, config = '/usr/share/klipper', '/usr/data/printer_data/config'
try:
    r = json.load(sys.stdin)['result']
    klipper = r.get('klipper_path') or klipper
    cfgfile = r.get('config_file')
    if cfgfile:
        config = os.path.dirname(cfgfile)
except Exception:
    pass
print(klipper)
print(config)" 2>/dev/null)

    klipper_path=$(printf '%s' "$paths" | sed -n 1p)
    config_dir=$(printf '%s' "$paths" | sed -n 2p)
    [ -n "$klipper_path" ] || klipper_path=/usr/share/klipper
    [ -n "$config_dir" ] || config_dir=/usr/data/printer_data/config

    # Only touch an install that installer.sh has already set up. On anything
    # else, including the Debian packaging, these files are not ours to place.
    [ -d "$config_dir/GuppyScreen" ] || return 0

    changed=0

    # Keeps one backup of whatever was there before we replaced it. Anyone who
    # hand-edited a macro loses the edit on update, which is why the supported
    # way to change load and unload is the default_macros mapping in
    # guppyconfig.json rather than editing these files.
    install_file() {
        src=$1
        dst=$2
        [ -f "$src" ] || return 0
        if [ -f "$dst" ] && cmp -s "$src" "$dst"; then
            return 0
        fi
        [ -f "$dst" ] && cp "$dst" "$dst.bak"
        mkdir -p "$(dirname "$dst")"
        cp "$src" "$dst" || return 1
        echo "  updated $dst"
        changed=$((changed + 1))
    }

    for f in "$GUPPY_DIR"/scripts/*.cfg; do
        [ -f "$f" ] && install_file "$f" "$config_dir/GuppyScreen/$(basename "$f")"
    done
    for f in "$GUPPY_DIR"/scripts/*.py; do
        [ -f "$f" ] && install_file "$f" "$config_dir/GuppyScreen/scripts/$(basename "$f")"
    done

    # installer.sh copies these two into klippy/extras, so on a stock install
    # they are real files and want refreshing like everything else.
    #
    # On the development K1 Max they are both symlinks instead, which is what a
    # helper-script install leaves behind: calibrate_shaper_config.py points
    # back into our own k1_mods, and gcode_shell_command.py points into
    # /usr/data/helper-script/. cp follows a symlink and writes through it, so
    # refreshing the second one would replace helper-script's own file with
    # ours, in their directory, while the .bak of their original lands over in
    # klippy/extras where they would never look for it. Measured 2026-08-19:
    # the two are byte-identical today, so nothing has been damaged, but that
    # is luck rather than design.
    #
    # A symlink is someone else's file. Leave it alone and say so.
    for f in gcode_shell_command.py calibrate_shaper_config.py; do
        dst="$klipper_path/klippy/extras/$f"
        if [ -L "$dst" ]; then
            continue
        fi
        [ -f "$dst" ] && install_file "$GUPPY_DIR/k1_mods/$f" "$dst"
    done

    if [ "$changed" -gt 0 ]; then
        echo
        echo "$changed Klipper file(s) changed. Klipper has to be restarted before"
        echo "they take effect, and that ends any print in progress, so it is left"
        echo "for you to do when the printer is idle:"
        echo
        echo "    FIRMWARE_RESTART"
        echo
    fi
}

if [ "$CURRENT_VERSION" = "$LATEST_VERSION" ]; then
    check_result uptodate "$LATEST_VERSION"
    echo "Already on $LATEST_VERSION."
    # Still refresh the Klipper files. The script that runs an update is the one
    # already on disk, so the release that first shipped refresh_klipper_files
    # could not run it, and by the next run the version matches and we are here.
    # Without this the refresh is unreachable on exactly the upgrade it exists
    # for, and the configs stay stale until some later release happens to move
    # the version on. Measured on the development printer, 2026-08-19.
    refresh_klipper_files
    exit 0
fi

# Two version shapes exist and only one of them is protected:
#
#   dev-<sha>        built locally by scripts/build.sh, published nowhere
#   <date>-<sha>     published by CI from main, a real artifact
#
# Replacing a local build throws away whatever was being tested, which is
# almost never what someone wants from a button on the printer. Rolling builds
# are published artifacts and update normally.
case "$CURRENT_VERSION" in
    dev-*)
        if [ "$FORCE" != true ]; then
            check_result devbuild "$LATEST_VERSION"
            echo "This is a development build ($CURRENT_VERSION), refusing to replace it"
            echo "with release $LATEST_VERSION. Re-run with --force if that is what you want."
            exit 0
        fi
        echo "Development build, but --force given."
        ;;
esac

# Past every refusal, so this is a release that would really be installed.
check_result available "$LATEST_VERSION"

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


echo "Refreshing the Klipper macros and modules"
refresh_klipper_files

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
