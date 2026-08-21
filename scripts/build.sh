#!/usr/bin/env bash
#
# Build guppyscreen for a target.
#
#   scripts/build.sh mips            K1 / K1 Max, material theme (default)
#   scripts/build.sh mips zbolt      K1 / K1 Max, Z-Bolt icon set
#   scripts/build.sh mips --small    Ender 3 V3 KE / Nebula Pad sized panel
#   scripts/build.sh arm             aarch64, Raspberry Pi and BTT Pad
#   scripts/build.sh sim             x86_64 SDL simulator for this machine
#
#   scripts/build.sh mips --clean    wipe build/ and the vendored libs first
#
# The mips flags match the panel we measured in docs/k1max-facts.md: 480x800
# portrait, 32 bpp, goodix touch on event0, so rotate on and no touch
# calibration. The other targets match the rows upstream's CI used to build.
#
# CI calls this script rather than repeating the flags, so the two cannot drift.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

MIPS_TOOLCHAIN="$REPO_ROOT/toolchains/mips32el--musl--stable-2025.08-1"
ARM_TOOLCHAIN="$REPO_ROOT/toolchains/arm-gnu-toolchain-14.3.rel1-x86_64-aarch64-none-linux-gnu"

target="${1:-mips}"
shift || true

theme="material"
do_clean=false
small=false
for arg in "$@"; do
    case "$arg" in
        zbolt|material) theme="$arg" ;;
        --clean)        do_clean=true ;;
        --small)        small=true ;;
        *) echo "unknown argument: $arg" >&2; exit 1 ;;
    esac
done

export GUPPY_THEME="$theme"
export GUPPYSCREEN_VERSION="${GUPPYSCREEN_VERSION:-dev-$(git rev-parse --short HEAD)}"

# Smaller panels use the reduced icon set and need touch calibration, and they
# are landscape already so they must not be rotated.
if [ "$small" = true ]; then
    export GUPPY_SMALL_SCREEN=true
    export EVDEV_CALIBRATE=true
fi

# Key for the build stamp below. It has to cover everything that changes the
# compiler flags, not just the architecture: the theme adds -D ZBOLT and
# --small adds -D GUPPY_SMALL_SCREEN, and both affect every object, so
# switching either without a clean would link a mix of the two.
stamp_target="$target-$theme"

case "$target" in
    mips)
        if [ ! -x "$MIPS_TOOLCHAIN/bin/mipsel-linux-gcc" ]; then
            echo "Toolchain missing. Run scripts/setup-toolchain.sh mips first." >&2
            exit 1
        fi
        export PATH="$MIPS_TOOLCHAIN/bin:$PATH"
        export CROSS_COMPILE=mipsel-linux-
        [ "$small" = true ] || export GUPPY_ROTATE=true
        ;;
    arm)
        # Arm's own toolchain, downloaded and pinned like the mips one. This
        # used to be whatever aarch64-linux-gnu-gcc the host had installed,
        # which made the compiler that built a release depend on the machine
        # that built it. Note the triple: aarch64-none-linux-gnu, not the
        # aarch64-linux-gnu a distribution package gives you.
        if [ ! -x "$ARM_TOOLCHAIN/bin/aarch64-none-linux-gnu-gcc" ]; then
            echo "Toolchain missing. Run scripts/setup-toolchain.sh arm first." >&2
            exit 1
        fi
        export PATH="$ARM_TOOLCHAIN/bin:$PATH"
        export CROSS_COMPILE=aarch64-none-linux-gnu-
        export EVDEV_CALIBRATE=true
        ;;
    sim)
        # The Makefile switches on CROSS_COMPILE being unset to enable SDL.
        unset CROSS_COMPILE || true
        ;;
    *)
        echo "usage: $0 {mips|arm|sim} [material|zbolt] [--small] [--clean]" >&2
        exit 1
        ;;
esac

if [ "$small" = true ]; then
    stamp_target="$stamp_target-small"
fi

# The patches are not committed into the submodules, so re-assert them on every
# build. Cheap, and skips anything already applied.
./scripts/apply-patches.sh

# Two separate stamps, because the two halves of the build go stale for
# different reasons.
#
# libhv, spdlog and libwpa_client are built in place inside their own source
# trees with no notion of which architecture they hold, so they are stale when
# the architecture changes. They do not care about the theme.
#
# Our objects additionally carry -D ZBOLT and -D GUPPY_SMALL_SCREEN, so they are
# stale when any of those change, which is much more often.
ARCH_STAMP="$REPO_ROOT/.vendor-target"
FLAG_STAMP="$REPO_ROOT/.build-flags"
prev_arch="$(cat "$ARCH_STAMP" 2>/dev/null || echo none)"
prev_flags="$(cat "$FLAG_STAMP" 2>/dev/null || echo none)"

if [ "$prev_arch" != "$target" ] || [ "$do_clean" = true ]; then
    if [ "$prev_arch" != "$target" ] && [ "$prev_arch" != none ]; then
        echo "Vendored libs were built for '$prev_arch', rebuilding for '$target'"
    fi
    make wpaclean
    make spdlogclean
    # libhv's own clean drops include/hv but leaves lib/, which would fool the
    # freshness checks below into reusing a wrong-architecture archive.
    make libhvclean
    rm -rf libhv/lib
fi

if [ "$prev_flags" != "$stamp_target" ] || [ "$do_clean" = true ]; then
    if [ "$prev_flags" != "$stamp_target" ] && [ "$prev_flags" != none ]; then
        echo "Objects were built as '$prev_flags', rebuilding as '$stamp_target'"
    fi
    make clean
fi

# libhv only populates include/hv as part of building the library, so treat a
# missing header dir as "not built" too.
[ -f wpa_supplicant/wpa_supplicant/libwpa_client.a ]              || make wpaclient
{ [ -f libhv/lib/libhv.a ] && [ -d libhv/include/hv ]; }          || make libhv.a
[ -f spdlog/build/libspdlog.a ]                                   || make libspdlog.a

echo "$target" > "$ARCH_STAMP"
echo "$stamp_target" > "$FLAG_STAMP"

make -j"$(nproc)"

# Ships in the debian/ directory of every asset and is what
# disable_blinking_cursor.service runs. It used to be built for mips only,
# which is the one target that has no use for it, so the arm asset carried a
# unit file pointing at a binary that was never in the tarball. Built for both
# now, so each asset's copy matches its own architecture.
if [ "$target" != "sim" ]; then
    make kd_graphic_mode
fi

# The simulator reads guppyconfig.json from beside the binary and will not
# start without one. Seed a usable default, but never overwrite an existing
# file, since that is where the developer's printer address lives.
if [ "$target" = "sim" ] && [ ! -f build/bin/guppyconfig.json ]; then
    sim_host="${PRINTER_HOST:-127.0.0.1}"
    mkdir -p "$REPO_ROOT/build/bin/thumbnails"
    # Starts from the Debian template but fills in the fans and sensors a K1 Max
    # reports, because those two lists are what decide whether the fan, LED and
    # home panels draw anything at all. Empty lists gave a simulator where
    # several panels could not be exercised, which is the opposite of what it is
    # for. The template itself stays empty: it ships to real Debian machines
    # whose hardware we do not know.
    python3 - "$REPO_ROOT" "$sim_host" <<'PYCONF'
import json, sys
root, host = sys.argv[1], sys.argv[2]
c = json.load(open("debian/guppyconfig.json"))
c["log_path"] = root + "/build/bin/guppyscreen.log"
c["thumbnail_path"] = root + "/build/bin/thumbnails"
p = c["printers"][c["default_printer"]]
p["moonraker_host"] = host
p["fans"] = [
    {"id": "output_pin fan0", "display_name": "Toolhead Fan"},
    {"id": "output_pin fan1", "display_name": "Back Fan"},
    {"id": "output_pin fan2", "display_name": "Side Fan"},
]
p["monitored_sensors"] = [
    {"id": "extruder", "display_name": "Extruder", "controllable": True, "color": "red"},
    {"id": "heater_bed", "display_name": "Bed", "controllable": True, "color": "purple"},
    {"id": "temperature_sensor chamber_temp", "display_name": "Chamber",
     "controllable": False, "color": "blue"},
]
json.dump(c, open("build/bin/guppyconfig.json", "w"), indent=2, sort_keys=True)
PYCONF
    echo "Wrote build/bin/guppyconfig.json pointing at moonraker on $sim_host"
    echo "Override with PRINTER_HOST=<ip>, or just edit the file."
fi

echo
echo "Built: $(ls -l build/bin/guppyscreen | awk '{print $5}') bytes -> build/bin/guppyscreen"
