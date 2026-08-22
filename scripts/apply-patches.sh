#!/usr/bin/env bash
#
# Apply the local patches in patches/ to their submodules.
#
# These patches are required to build. They are not committed into the
# submodules, so a fresh clone, a submodule update, or a "git -C <sub> checkout
# ." silently reverts them and the build then fails in confusing ways. Upstream
# expects you to run three git apply commands by hand and gives no way to tell
# whether they already ran.
#
# This script is idempotent. Anything already applied is reported and skipped.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

# submodule:patch
PATCHES=(
    "lv_drivers:patches/0001-lv_driver_fb_ioctls.patch"
    "spdlog:patches/0002-spdlog_fmt_initializer_list.patch"
    "lvgl:patches/0003-lvgl-dpi-text-scale.patch"
    "libhv:patches/0004-libhv-mbedtls-ca.patch"
)

failed=0

for entry in "${PATCHES[@]}"; do
    submodule="${entry%%:*}"
    patch="${entry#*:}"
    label="$(basename "$patch") -> $submodule/"

    if [ ! -d "$submodule/.git" ] && [ ! -f "$submodule/.git" ]; then
        echo "MISSING  $label (submodule not checked out, run: git submodule update --init --recursive)"
        failed=1
        continue
    fi

    # --reverse --check succeeds only if the patch is already in the tree.
    if git -C "$submodule" apply --reverse --check "../$patch" 2>/dev/null; then
        echo "SKIP     $label (already applied)"
        continue
    fi

    if ! git -C "$submodule" apply --check "../$patch" 2>/dev/null; then
        echo "FAILED   $label (does not apply cleanly and is not already applied)"
        failed=1
        continue
    fi

    git -C "$submodule" apply "../$patch"
    echo "APPLIED  $label"
done

if [ "$failed" -ne 0 ]; then
    echo
    echo "One or more patches could not be applied. Fix the above before building." >&2
    exit 1
fi
