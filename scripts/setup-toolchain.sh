#!/usr/bin/env bash
#
# Download and unpack the cross-compile toolchains.
#
#   scripts/setup-toolchain.sh          both
#   scripts/setup-toolchain.sh mips     Creality K1 / K1 Max
#   scripts/setup-toolchain.sh arm      Raspberry Pi, BTT Pad, aarch64 Debian
#
# The K1 family runs an Ingenic X2000E (XBurst2, MIPS32r2, little endian) on a
# 4.4 kernel, and the glibc version moves around between Creality firmware
# releases. We therefore build fully static against musl so the binary does not
# care which firmware the printer is running.
#
# Both toolchains are downloaded and pinned rather than taken from the host's
# packages. Distributions do package an aarch64 cross compiler, and using it
# would mean the compiler that built a release depended on which machine or
# which runner image happened to build it. Pinning also keeps the two targets on
# the same gcc, so a warning or an error found on one applies to the other.
#
# Safe to run repeatedly. An already-extracted toolchain is left alone.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOLCHAIN_DIR="$REPO_ROOT/toolchains"

# Where to look for an already-downloaded archive before hitting the network.
CACHE_DIR="${TOOLCHAIN_CACHE_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/guppyscreen}"

# Bootlin mips32el musl stable 2025.08-1: gcc 14.3.0, binutils 2.43.1,
# musl 1.2.5, Linux headers 5.4.296.
#
# The known-good fallback is mips32el--musl--stable-2024.02-1.tar.xz (gcc
# 12.3.0). That is what upstream CI and pellcorp/grumpyscreen both use, so
# switch to it if gcc 14 strictness ever becomes more trouble than it is worth.
#
# No checksum: Bootlin publishes none for this file.
MIPS_NAME="mips32el--musl--stable-2025.08-1"
MIPS_URL="https://toolchains.bootlin.com/downloads/releases/toolchains/mips32el/tarballs/$MIPS_NAME.tar.xz"
MIPS_PROBE="bin/mipsel-linux-gcc"
MIPS_SHA256=""

# Arm's own GNU toolchain, AArch64 GNU/Linux target. gcc 14.3.1, which is the
# same compiler generation as the mips one above, deliberately.
#
# 15.2.rel1 exists and is skipped for now on that ground alone. The triple is
# aarch64-none-linux-gnu, not the aarch64-linux-gnu that Debian and Arch ship,
# so CROSS_COMPILE differs from what a distribution package would give you.
#
# The hash is the one Arm publishes beside the archive, pinned here rather than
# fetched: fetching it from the host it verifies only catches corruption.
ARM_VERSION="14.3.rel1"
ARM_NAME="arm-gnu-toolchain-$ARM_VERSION-x86_64-aarch64-none-linux-gnu"
ARM_URL="https://developer.arm.com/-/media/Files/downloads/gnu/$ARM_VERSION/binrel/$ARM_NAME.tar.xz"
ARM_PROBE="bin/aarch64-none-linux-gnu-gcc"
ARM_SHA256="ddeaff1ea60d4135acba271b0143d9f5add02b68ab9e9be39672d1965c12e82f"

install_toolchain() {
    local name="$1" url="$2" probe="$3" want_sha="$4"
    local target="$TOOLCHAIN_DIR/$name"
    local archive="$CACHE_DIR/$name.tar.xz"

    if [ -x "$target/$probe" ]; then
        echo "Toolchain already present: $target"
        return
    fi

    mkdir -p "$TOOLCHAIN_DIR" "$CACHE_DIR"

    if [ -s "$archive" ]; then
        echo "Using cached archive: $archive"
    else
        echo "Downloading $url"
        # Download to a temp name so an interrupted run does not leave a
        # truncated archive that later runs would happily reuse. Arm's URL
        # redirects to blob storage, so follow redirects.
        wget --progress=dot:giga --max-redirect=10 -O "$archive.partial" "$url"
        mv "$archive.partial" "$archive"
    fi

    if [ -n "$want_sha" ]; then
        echo "Verifying $name.tar.xz"
        local got_sha
        got_sha="$(sha256sum "$archive" | cut -d' ' -f1)"
        if [ "$got_sha" != "$want_sha" ]; then
            echo "ERROR: checksum mismatch for $archive" >&2
            echo "  expected $want_sha" >&2
            echo "  got      $got_sha" >&2
            echo "Delete it and re-run if the download was interrupted." >&2
            exit 1
        fi
    fi

    echo "Extracting to $TOOLCHAIN_DIR"
    tar -xf "$archive" -C "$TOOLCHAIN_DIR"

    if [ ! -x "$target/$probe" ]; then
        echo "ERROR: expected $target/$probe after extraction" >&2
        exit 1
    fi

    echo "gcc: $("$target/$probe" --version | head -1)"
}

what="${1:-both}"
case "$what" in
    mips)
        install_toolchain "$MIPS_NAME" "$MIPS_URL" "$MIPS_PROBE" "$MIPS_SHA256"
        ;;
    arm)
        install_toolchain "$ARM_NAME" "$ARM_URL" "$ARM_PROBE" "$ARM_SHA256"
        ;;
    both)
        install_toolchain "$MIPS_NAME" "$MIPS_URL" "$MIPS_PROBE" "$MIPS_SHA256"
        install_toolchain "$ARM_NAME" "$ARM_URL" "$ARM_PROBE" "$ARM_SHA256"
        ;;
    *)
        echo "usage: $0 [mips|arm|both]" >&2
        exit 1
        ;;
esac

echo
echo "scripts/build.sh mips and scripts/build.sh arm set PATH and CROSS_COMPILE"
echo "for you. To use one by hand:"
echo
echo "  export PATH=\"$TOOLCHAIN_DIR/$MIPS_NAME/bin:\$PATH\" CROSS_COMPILE=mipsel-linux-"
echo "  export PATH=\"$TOOLCHAIN_DIR/$ARM_NAME/bin:\$PATH\" CROSS_COMPILE=aarch64-none-linux-gnu-"
