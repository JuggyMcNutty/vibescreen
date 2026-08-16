## Development

This repository contains the Guppy Screen source code and all its external dependencies.

Dependencies:
 - [lvgl](https://github.com/lvgl/lvgl)
   An embedded graphics library
 - [libhv](https://github.com/ithewei/libhv)
   A network library
 - [spdlog](https://github.com/gabime/spdlog)
   A logging library
 - [wpa_supplicant](https://w1.fi/wpa_supplicant/)
   Handles wireless connections

## Toolchains

Guppy Screen uses C++17, so gcc/g++ 7.2 or newer is required.

> The toolchain instructions here used to point at the Ingenic
> `mips-gcc720-glibc229` toolchain. That was already wrong: it produces a
> dynamically linked binary that only runs on firmware shipping glibc 2.29,
> which is why the installer has to check for `/lib/ld-2.29.so` before it will
> install. Every nightly since `a42427cb` has been built by CI against a Bootlin
> musl toolchain and linked statically. See `AGENTS.md` for the full history.

### Environment variables

`CROSS_COMPILE` - toolchain prefix, `mipsel-linux-` for the K1 family
`GUPPY_THEME` - `material` (default) or `zbolt`
`GUPPY_ROTATE` - set for the K1/Max, whose panel is portrait
`GUPPY_SMALL_SCREEN` - set for panels under 800px on the long edge
`EVDEV_CALIBRATE` - set where the touch panel needs calibration
`GUPPYSCREEN_VERSION` - version string shown in the System panel
`NPROC` - cap the parallelism of the vendored library builds

The SDL simulator is selected by `CROSS_COMPILE` being unset, not by a variable
of its own.

### Build environment

Ubuntu and Debian:

    sudo apt-get install -y build-essential cmake libsdl2-dev

Arch and derivatives (`sdl2` is `sdl2-compat` on current Arch):

    sudo pacman -S base-devel cmake sdl2-compat

### Building

    git clone --recursive <your fork> && cd guppyscreen
    scripts/setup-toolchain.sh      # downloads the cross toolchain, once
    scripts/build.sh mips           # K1 / K1 Max
    scripts/build.sh sim            # x86_64 SDL build for this machine

`scripts/build.sh` applies the patches in `patches/`, builds the vendored
libraries if they are missing, rebuilds them when you switch target, and picks
the right flags per target. The executable is `./build/bin/guppyscreen`.

Variants:

    scripts/build.sh mips zbolt     # Z-Bolt icon set
    scripts/build.sh mips --clean   # rebuild the vendored libraries too

Driving `make` directly still works, but then applying `patches/` and setting
the flags is on you. `scripts/apply-patches.sh` is idempotent and safe to run
at any time.

### Simulation

`scripts/build.sh sim` writes a working `build/bin/guppyconfig.json` on the
first build if there is not one already. Point it at a printer with:

    PRINTER_HOST=<printer ip> scripts/build.sh sim

Log and thumbnail paths default to directories beside the binary in simulator
builds, so nothing needs `/usr/data` to exist.

To test against something other than a real printer, `tools/fake_moonraker.py`
speaks enough of the Moonraker protocol for the UI to start, and can be told to
reject gcode so error handling can be exercised. Use it for anything that would
otherwise command real hardware.
