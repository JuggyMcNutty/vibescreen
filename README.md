# vibescreen

A touch UI for Klipper printers. It talks to Moonraker over a websocket and
draws straight to the framebuffer, so there is no X or Wayland underneath and
nothing else to install alongside it.

This is a maintained fork of [guppyscreen](https://github.com/ballaswag/guppyscreen),
which stopped receiving commits in July 2024 with 69 issues open.

![Bed mesh drawn as a shaded surface](screenshots/bedmesh.png)

That is a real K1 Max bed, 6x6 probed, 1.988mm from its lowest point to its
highest.

## What this fork changes

**The bed mesh panel was rewritten.** Upstream drew the mesh as a table of
cells tinted by a ramp that saturated at 0.25mm, and only wrote the numbers in
when the mesh was smaller than 6x6. A K1 Max probes 6x6 by default, so the one
panel you look at to judge the bed gave you a block of undifferentiated red
squares with no numbers on it. It now draws onto a canvas, either as a surface
you drag to rotate or as a flat heatmap, on a diverging scale normalised about
zero, with a floor so a trammed bed reads as trammed instead of turning eight
microns of probe noise into a mountain range.

| Flat heatmap | The probed points |
| --- | --- |
| ![Bed mesh as a flat heatmap](screenshots/bedmesh_flat.png) | ![The 36 probed points](screenshots/bedmesh_probed.png) |

The same mesh interpolated, and as the 36 points the probe actually visited.

**Calibrate wipes the nozzle first.** A blob of filament on the tip gets
measured as bed and lands in the mesh as a bump that is not on the plate. When
the printer has a `WIPE_NOZZLE` macro, Calibrate runs it before probing,
wrapped in a gcode state save so it cannot leave a modal mode set behind it. On
a K1, K1C, K1SE or K1 Max that macro comes from
[ProWiper](https://www.printables.com/model/1023575-prowiper-for-creality-k1-series),
formerly Advanced Nozzle Wiper, and that mod's own toggle still governs: switch
wiping off there and the macro is a no-op, so calibration simply proceeds. The
macro is tested for rather than sent hopefully, because Klipper abandons the
rest of a script at the first command it does not recognise, which would leave
anyone without the mod with an error and no mesh at all.

**Calibrate homes only when something is unhomed**, rather than re-homing a
machine that already is, which is the normal state after a print.

**A refused command is no longer silent.** Every gcode reply is checked, and a
rejection is raised where you pressed the button instead of only in the
console.

| Extruder options | A rejected command |
| --- | --- |
| ![Extruder panel with configurable option lists](screenshots/extrude_retract.png) | ![Dialog reading printer rejected the command](screenshots/gcode_rejected.png) |

**The extruder panel's option lists are configurable** and clamped to the
printer's own limits read from Klipper, so the temperatures stop where your
hotend's configuration stops rather than at a hardcoded 240.

Underneath: exceptions are contained at the LVGL event callbacks, where one
escaping used to freeze the UI outright, the websocket client's shared state is
locked across the two threads that touch it, and parses that could abort the
process on bad configuration no longer can. [docs/audit.md](docs/audit.md)
lists all of it, fixed and still open.

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

## What it does

Print status with thumbnails, file browser, console and macro shell, bed mesh
viewer, input shaper with PSD graphs, belt calibration, TMC metrics and tuning,
temperature control, fan, LED and movement control, extrude and retract,
fine tuning for speed, flow, z-offset and pressure advance, velocity and
acceleration limits, Spoolman integration, and multi-printer support.

| Move | Tuning |
| --- | --- |
| ![Move panel](screenshots/move.png) | ![Tuning menu](screenshots/printer_tune.png) |

| Macros | Console |
| --- | --- |
| ![Macro list with parameters](screenshots/macros.png) | ![Console with the command palette](screenshots/console.png) |

The screenshots are the simulator build. The bed mesh, the macros and the
command list came from the development K1 Max, so they are that machine's own
data. The rejected command is `tools/fake_moonraker.py` refusing on purpose,
which is not a thing worth doing to a real printer.

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
