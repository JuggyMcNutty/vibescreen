# vibescreen

A touch UI for Klipper printers. It talks to Moonraker over a websocket and
draws straight to the framebuffer, so there is no X or Wayland underneath and
nothing else to install alongside it.

This is a maintained fork of [guppyscreen](https://github.com/ballaswag/guppyscreen),
which stopped receiving commits in July 2024 with 69 issues open.

![Material theme](screenshots/material/material_screenshot.png)

## Scope

Built and tested on a **Creality K1 Max**. That is the machine the work is
aimed at and the only one anything is verified on.

CI also builds two other variants, both inherited from upstream and neither
tested here:

- `guppyscreen-smallscreen.tar.gz` for the Ender 3 V3 KE and Nebula Pad
- `guppyscreen-arm.tar.gz` for aarch64 boards such as a Pi or BTT Pad

They compile on every push. Whether they run is unknown. Treat them as a
starting point rather than a supported target.

Android is not supported. Upstream shipped an APK built from a separate branch
and that has been removed.

## Installing

SSH into the printer and run:

```sh
sh -c "$(wget --no-check-certificate -qO - https://raw.githubusercontent.com/JuggyMcNutty/vibescreen/main/installer.sh)"
```

Add `-s zbolt` for the Z-Bolt icon set instead of Material Design.

The installer replaces Creality's display server, so the stock UI will be gone
afterwards. It backs up what it displaces to `/usr/data/guppyify-backup` and
offers to disable the rest of Creality's services while it is there.

This installs as `guppyscreen`, in `/usr/data/guppyscreen`, using the same
service name and config file as upstream. It is a drop-in replacement: an
existing guppyscreen install can be moved across without touching anything
else.

### Raspberry Pi and Debian

```sh
wget -O - https://raw.githubusercontent.com/JuggyMcNutty/vibescreen/main/installer-deb.sh | bash
```

Untested here. Have a way back to your current setup before running it.

## Updating

From the printer:

```sh
/usr/data/guppyscreen/update.sh
```

or press Update in the settings panel.

Releases are rolling. Every push to `main` that builds cleanly replaces the
`rolling` release, and there is no separate stable track. Builds are not tested
on hardware before they are published.

If you built the binary yourself, `.version` will say `dev-<sha>` and the
updater will refuse to overwrite it unless you pass `--force`.

## Uninstalling

```sh
/usr/data/guppyscreen/reinstall-creality.sh
```

That restores the Creality services and display server from the backup the
installer made.

## What it does

Print status with thumbnails, file browser, console and macro shell, bed mesh
viewer, input shaper with PSD graphs, belt calibration, TMC metrics and tuning,
temperature control, fan, LED and movement control, extrude and retract,
fine tuning for speed, flow, z-offset and pressure advance, velocity and
acceleration limits, Spoolman integration, and multi-printer support.

## Building

`scripts/setup-toolchain.sh` once, then `scripts/build.sh mips` for the printer
or `scripts/build.sh sim` for an SDL build that runs on your desktop against a
real or fake Moonraker.

[DEVELOPMENT.md](DEVELOPMENT.md) covers the toolchain and the build targets.
[AGENTS.md](AGENTS.md) is the working reference for anyone changing the code,
including the hardware details in [docs/k1max-facts.md](docs/k1max-facts.md)
and the known defects in [docs/audit.md](docs/audit.md).

## Credits

guppyscreen was written by [ballaswag](https://github.com/ballaswag). This fork
exists because that work was worth keeping, and all original code belongs to them.

Everything it talks to and borrows from:

- [Klipper](https://github.com/Klipper3d/klipper), the firmware this is a front
  end for
- [Moonraker](https://github.com/Arksine/moonraker), the API it speaks to
- [KlipperScreen](https://github.com/KlipperScreen/KlipperScreen), prior art
  that shaped what a Klipper touch UI should do
- [Fluidd](https://github.com/fluidd-core/fluidd), for interface ideas and the
  print status calculations
- [Klippain-shaketune](https://github.com/Frix-x/klippain-shaketune), behind the
  input shaper graphs and belt calibration
- [Material Design Icons](https://pictogrammers.com/library/mdi/) and
  [Z-Bolt](https://github.com/Z-Bolt/OctoScreen) for the two icon sets

Built on [LVGL](https://github.com/lvgl/lvgl),
[libhv](https://github.com/ithewei/libhv),
[spdlog](https://github.com/gabime/spdlog) and
[wpa_supplicant](https://w1.fi/wpa_supplicant/).

GPL-3.0, same as upstream.
