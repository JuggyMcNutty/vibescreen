#!/usr/bin/env bash
#
# Build guppyscreen for a target.
#
#   scripts/build.sh mips            K1 / K1 Max, material theme (default)
#   scripts/build.sh mips zbolt      K1 / K1 Max, Z-Bolt icon set
#   scripts/build.sh sim             x86_64 SDL simulator for this machine
#
#   scripts/build.sh mips --clean    wipe build/ and the vendored libs first
#
# The mips flags match the K1/Max row of upstream's .github/workflows/build.yml
# and the panel we measured in docs/k1max-facts.md: 480x800 portrait, 32 bpp,
# goodix touch on event0, so rotate on and no touch calibration.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

TOOLCHAIN="$REPO_ROOT/toolchains/mips32el--musl--stable-2025.08-1"

target="${1:-mips}"
shift || true

theme="material"
do_clean=false
for arg in "$@"; do
    case "$arg" in
        zbolt|material) theme="$arg" ;;
        --clean)        do_clean=true ;;
        *) echo "unknown argument: $arg" >&2; exit 1 ;;
    esac
done

export GUPPY_THEME="$theme"
export GUPPYSCREEN_VERSION="${GUPPYSCREEN_VERSION:-dev-$(git rev-parse --short HEAD)}"

case "$target" in
    mips)
        if [ ! -x "$TOOLCHAIN/bin/mipsel-linux-gcc" ]; then
            echo "Toolchain missing. Run scripts/setup-toolchain.sh first." >&2
            exit 1
        fi
        export PATH="$TOOLCHAIN/bin:$PATH"
        export CROSS_COMPILE=mipsel-linux-
        export GUPPY_ROTATE=true
        ;;
    sim)
        # The Makefile switches on CROSS_COMPILE being unset to enable SDL.
        unset CROSS_COMPILE || true
        ;;
    *)
        echo "usage: $0 {mips|sim} [material|zbolt] [--clean]" >&2
        exit 1
        ;;
esac

# The patches are not committed into the submodules, so re-assert them on every
# build. Cheap, and skips anything already applied.
./scripts/apply-patches.sh

# libhv, spdlog and libwpa_client are built in place inside their source trees,
# with no notion of which architecture they hold. Switching between mips and
# sim therefore silently links yesterday's objects for the wrong target, so
# remember what the vendored libs were last built for and redo them on a
# switch.
STAMP="$REPO_ROOT/.vendor-target"
previous="$(cat "$STAMP" 2>/dev/null || echo none)"

if [ "$previous" != "$target" ] || [ "$do_clean" = true ]; then
    if [ "$previous" != "$target" ] && [ "$previous" != none ]; then
        echo "Vendored libs were built for '$previous', rebuilding for '$target'"
    fi
    make clean
    make wpaclean
    make spdlogclean
    # libhv's own clean drops include/hv but leaves lib/, which would fool the
    # freshness checks below into reusing a wrong-architecture archive.
    make libhvclean
    rm -rf libhv/lib
fi

# libhv only populates include/hv as part of building the library, so treat a
# missing header dir as "not built" too.
[ -f wpa_supplicant/wpa_supplicant/libwpa_client.a ]              || make wpaclient
{ [ -f libhv/lib/libhv.a ] && [ -d libhv/include/hv ]; }          || make libhv.a
[ -f spdlog/build/libspdlog.a ]                                   || make libspdlog.a

echo "$target" > "$STAMP"

make -j"$(nproc)"

if [ "$target" = "mips" ]; then
    make kd_graphic_mode
fi

# The simulator reads guppyconfig.json from beside the binary and will not
# start without one. Seed a usable default, but never overwrite an existing
# file, since that is where the developer's printer address lives.
if [ "$target" = "sim" ] && [ ! -f build/bin/guppyconfig.json ]; then
    sim_host="${PRINTER_HOST:-127.0.0.1}"
    mkdir -p "$REPO_ROOT/build/bin/thumbnails"
    sed -e "s|<PRINTER_DATA_DIR>/logs|$REPO_ROOT/build/bin|" \
        -e "s|<GUPPY_DIR>/thumbnails|$REPO_ROOT/build/bin/thumbnails|" \
        -e "s|\"moonraker_host\": \"127.0.0.1\"|\"moonraker_host\": \"$sim_host\"|" \
        debian/guppyconfig.json > build/bin/guppyconfig.json
    echo "Wrote build/bin/guppyconfig.json pointing at moonraker on $sim_host"
    echo "Override with PRINTER_HOST=<ip>, or just edit the file."
fi

echo
echo "Built: $(ls -l build/bin/guppyscreen | awk '{print $5}') bytes -> build/bin/guppyscreen"
