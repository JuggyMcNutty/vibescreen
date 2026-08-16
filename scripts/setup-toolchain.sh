#!/usr/bin/env bash
#
# Download and unpack the MIPS cross-compile toolchain for the Creality K1/K1 Max.
#
# The K1 family runs an Ingenic X2000E (XBurst2, MIPS32r2, little endian) on a
# 4.4 kernel, and the glibc version moves around between Creality firmware
# releases. We therefore build fully static against musl so the binary does not
# care which firmware the printer is running.
#
# Safe to run repeatedly. An already-extracted toolchain is left alone.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOLCHAIN_DIR="$REPO_ROOT/toolchains"

# Bootlin mips32el musl stable 2025.08-1: gcc 14.3.0, binutils 2.43.1,
# musl 1.2.5, Linux headers 5.4.296.
#
# The known-good fallback is mips32el--musl--stable-2024.02-1.tar.bz2 (gcc
# 12.3.0). That is what upstream CI and pellcorp/grumpyscreen both use, so
# switch to it if gcc 14 strictness ever becomes more trouble than it is worth.
TOOLCHAIN_NAME="mips32el--musl--stable-2025.08-1"
TOOLCHAIN_ARCHIVE="$TOOLCHAIN_NAME.tar.xz"
TOOLCHAIN_URL="https://toolchains.bootlin.com/downloads/releases/toolchains/mips32el/tarballs/$TOOLCHAIN_ARCHIVE"

# Where to look for an already-downloaded archive before hitting the network.
CACHE_DIR="${TOOLCHAIN_CACHE_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/guppyscreen}"

target="$TOOLCHAIN_DIR/$TOOLCHAIN_NAME"

if [ -x "$target/bin/mipsel-linux-gcc" ]; then
    echo "Toolchain already present: $target"
else
    mkdir -p "$TOOLCHAIN_DIR" "$CACHE_DIR"
    archive="$CACHE_DIR/$TOOLCHAIN_ARCHIVE"

    if [ -s "$archive" ]; then
        echo "Using cached archive: $archive"
    else
        echo "Downloading $TOOLCHAIN_URL"
        # Download to a temp name so an interrupted run does not leave a
        # truncated archive that later runs would happily reuse.
        wget --progress=dot:giga -O "$archive.partial" "$TOOLCHAIN_URL"
        mv "$archive.partial" "$archive"
    fi

    echo "Extracting to $TOOLCHAIN_DIR"
    tar -xf "$archive" -C "$TOOLCHAIN_DIR"
fi

if [ ! -x "$target/bin/mipsel-linux-gcc" ]; then
    echo "ERROR: expected $target/bin/mipsel-linux-gcc after extraction" >&2
    exit 1
fi

echo
echo "gcc: $("$target/bin/mipsel-linux-gcc" --version | head -1)"
echo
echo "To use it in this shell:"
echo
echo "  export PATH=\"$target/bin:\$PATH\""
echo "  export CROSS_COMPILE=mipsel-linux-"
echo
echo "Or just run scripts/build.sh mips, which sets both for you."
