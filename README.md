# vibescreen

A touch UI for Klipper printers.

This is a maintained fork of [guppyscreen](https://github.com/ballaswag/guppyscreen),
which stopped receiving commits in July 2024.

![Bed mesh drawn as a shaded surface](screenshots/bedmesh.png)


## What has changed sense guppyscreen?

**The bed mesh panel was rewritten.** 

| Flat heatmap | The probed points |
| --- | --- |
| ![Bed mesh as a flat heatmap](screenshots/bedmesh_flat.png) | ![The 36 probed points](screenshots/bedmesh_probed.png) |

**Rewrote the extruder panel and option are configurable and clamped to the
printer's own limits read from Klipper**

**A refused command is no longer silent.**

| Extruder options | A rejected command |
| --- | --- |
| ![Extruder panel with configurable option lists](screenshots/extrude_retract.png) | ![Dialog reading printer rejected the command](screenshots/gcode_rejected.png) |

**Bed mesh Calibrate wipes the nozzle first.** When the printer has a `WIPE_NOZZLE` macro, Calibrate runs it before probing,
On a K1, K1C, K1SE or K1 Max that macro comes from
[ProWiper](https://www.printables.com/model/1023575-prowiper-for-creality-k1-series),

**Bed mesh Calibrate homes only when something is unhomed**

**The input shaper panel was rebuilt.**

| Input shaper with graphs | Input shaper with numbers |
| --- | --- |
| ![Frequency response for one axis](screenshots/inputshaper.png) | ![Every shaper with its vibration, smoothing and max acceleration](screenshots/inputshaper_numbers.png) |

**Underneath: lots of bug fixes and exception handlers.**

The rest of the interface:

| Move | Tuning |
| --- | --- |
| ![Move panel](screenshots/move.png) | ![Tuning menu](screenshots/printer_tune.png) |

| Files | WiFi |
| --- | --- |
| ![File browser with a sliced thumbnail](screenshots/files.png) | ![Picking a network and entering its password](screenshots/wifi.png) |

| Fine tune | Limits |
| --- | --- |
| ![Live z-offset, pressure advance, speed and flow](screenshots/finetune.png) | ![Velocity and acceleration limits](screenshots/limits.png) |

| Macros | Console |
| --- | --- |
| ![Macro list with parameters](screenshots/macros.png) | ![Console with the command palette](screenshots/console.png) |

| Temperature | Fans |
| --- | --- |
| ![Entering a target on the numpad](screenshots/temp.png) | ![Fan speed control](screenshots/fan.png) |

| LED | Settings |
| --- | --- |
| ![LED brightness control](screenshots/led.png) | ![Settings menu](screenshots/settings.png) |

## Scope

Built and tested on my **Creality K1 Max**. That is the machine anything is verified against.

we also build for:

- `guppyscreen-smallscreen.tar.gz` for the Ender 3 V3 KE and Nebula Pad
- `guppyscreen-arm.tar.gz` for aarch64 boards such as a Pi or BTT Pad

Whether they run is unknown. Treat them as a
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

Releases are rolling and there is no separate stable track. Every push to
`main` that changes something other than documentation publishes its own
release, tagged by date and commit, so the
[release list](https://github.com/JuggyMcNutty/vibescreen/releases) doubles as a
build history. The updater always takes the newest.

Coming from the original guppyscreen, run the installer above instead. That
project's updater points at its own releases and will not see these.

## Uninstalling

```sh
/usr/data/guppyscreen/reinstall-creality.sh
```

That restores the Creality services and display server from the backup the
installer made.

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
exists because that work was worth keeping, and all original code belongs to
them.

Everything it talks to and borrows from:

- [Klipper](https://github.com/Klipper3d/klipper), the firmware all this is built around
- [Moonraker](https://github.com/Arksine/moonraker), the API it speaks to
- [KlipperScreen](https://github.com/KlipperScreen/KlipperScreen), prior art
  that shaped what a Klipper touch UI should do
- [Fluidd](https://github.com/fluidd-core/fluidd), for interface ideas and the
  print status calculations
- [Klippain-shaketune](https://github.com/Frix-x/klippain-shaketune), behind the
  belt calibration graphs
- [Material Design Icons](https://pictogrammers.com/library/mdi/) and
  [Z-Bolt](https://github.com/Z-Bolt/OctoScreen) for the two icon sets

Built on [LVGL](https://github.com/lvgl/lvgl),
[libhv](https://github.com/ithewei/libhv),
[spdlog](https://github.com/gabime/spdlog) and
[wpa_supplicant](https://w1.fi/wpa_supplicant/).

GPL-3.0, same as upstream.
