# Upstream's open issues and pull requests

Triage of everything still open on `ballaswag/guppyscreen` as of 2026-08-18,
against our tree. Upstream `main` is still exactly our fork point `07409cb`, and
its `dev` and `btt_pad7` branches are stale from December 2023 with nothing in
them that is not already in `main`'s history. So this list is the whole of what
is left there: 63 issues and 6 pull requests.

The point of this file is that the triage does not have to be done twice. Each
entry says what we found and where, so a later reader can disagree with the
verdict rather than start over.

Verdicts:

- **open** means the defect is present in our tree and we intend to fix it.
- **fixed** means our fork already closed it, naming the commit.
- **declined** means we read it and decided against, with the reason.
- **no depth** means a support question, a duplicate, or a one-line complaint
  with nothing actionable in it. Listed for completeness, not discussed.

Where a claim was checked against the development K1 Max rather than read out of
the source, it says so. Those checks were read-only queries to Moonraker.

---

## Measured on the printer

Several of these issues could not be settled from the source alone. These are
the facts that settle them, taken from `192.168.1.202` on 2026-08-18, and the
power loss rows on 2026-08-19.

| What | Value | Settles |
| --- | --- | --- |
| `output_pin fan0`, `fan1`, `fan2` | `pwm: true`, `scale: 255.0` | #33, #118 |
| `gcode_macro PRINTER_PARAM` | `fan0_min 25`, `fan1_min 50`, `fan2_min 180` | #33 |
| `gcode_macro M106` | maps `S` onto `min + (255 - min) * S / 255`, and `S=0` stays 0 | #33, #118 |
| `output_pin fan1` and `temperature_fan chamber_fan` | both on pin `PC0` | #118 |
| `output_pin LED` | `scale: 1.0` | #72 |
| `temperature_sensor chamber_temp` | present | PR #141 |
| `gcode_macro G28` | does not exist | #66 |
| `printer.objects.list` | contains `calibrate_shaper_config`, not `resonance_tester` or `adxl345` | corrects `AGENTS.md` |
| power loss recovery module | absent from `printer.objects.list` | #151, #100 |
| power loss recovery macro | none. `RESUME` exists but only resumes a paused print | #151, #100 |
| `save_variables` | holds `{"zoffset": {"z": 0}}` and nothing about an interrupted print | #151, #100 |

One more, which is about deployment rather than the printer: the installed
`_GUPPY_LOAD_MATERIAL` on that machine is

```
G1 E120 F300
```

It reads `EXTRUDE_LEN` into a variable and then ignores it. That is neither our
version nor upstream's, which both say `G1 E{extrude_len} F180`. See #132 below
and the `update.sh` entry for why.

---

## Pull requests

| PR | Title | Verdict |
| --- | --- | --- |
| #164 | Add CFS helper for hot filament overrides and UI enhancements | **declined** |
| #141 | Add chamber temp to print status | **open**, taking the idea not the patch |
| #137 | Prevent line breaks in env var list | **fixed**, `5658e14` rewrote `DEVELOPMENT.md` |
| #136 | Add more extruder temperature options | **fixed**, `ff6dfad` and `a937708` |
| #105 | SAVE_INPUT_SHAPER is not a valid command, SET_INPUT_SHAPER is | **declined** |
| #61 | Add configuration option to disable spoolman integration | **open**, taking it |

**#164, CFS.** 2358 lines across 13 files. It adds a panel, a pre-print slot
mapping dialog, an embedded Python module carried as a generated C header, and a
sync script to keep the two in step. The helper monkey-patches Creality's `box`
module at runtime to overlay manual assignments. There is no CFS on the
development printer, so none of it can be tested, and a runtime patch of a
closed vendor module is not something to ship untested. The pre-print filament
mapping step is the part worth rebuilding cleanly if a CFS ever turns up.

**#141, chamber temp.** The gap is real and the free grid cell is there, but the
patch hardcodes `temperature_sensor chamber_temp`. `src/config.cpp` already has
a `monitored_sensors` list which names that sensor on a K1 Max, so the tile
should come from config. `pellcorp/grumpyscreen` made the same mistake in
`ddb1974` (it used `temperature_fan chamber_fan`) and corrected it in `5c48c2d`.

**#105, SET_INPUT_SHAPER.** Correct for stock Klipper and wrong for us. We ship
`k1/k1_mods/calibrate_shaper_config.py`, which registers `SAVE_INPUT_SHAPER`,
and `ad40b2a` fixed what that command defaults to. `SET_INPUT_SHAPER` applies
values without writing them to `printer.cfg`, which is a different operation.
`c896979` added applying-before-saving as its own thing.

---

## Issues, by verdict

### Fixed in this round

All of these were present when the triage was written and are fixed now. The
commit is named so the reasoning can be read back.

| Issue | What | Fixed by |
| --- | --- | --- |
| #33, #118 | Fan sliders ignored the speed the fan starts at | `4679274` |
| #94, #103, #47 | Print status never dismissed, stuck at 99 percent | `6f355f8` |
| #95 | File list did not refresh when a file was uploaded | `150073c` |
| #104 | Belt calibration crashed Klipper, and its panel span forever | `745ab03` |
| #135 | Belt calibration warned twice on every run | `c443f0b` |
| #90 | Load filament leaked relative extrusion mode | `59993da` |
| #132 | Extrude length had no effect on load | `87f1f56` |
| #116 | Tiny Z offsets rendered in scientific notation | `775bf27` |
| #116, #91, #41 | Three digit temperatures wrapped onto two lines | `28e6c87` |
| #32 | Moonraker API key was never sent | `ca18c1d` |
| #156, #143 | No way to forget a saved wifi network, and no failure state | `575fb1c` |
| #146 | Four punctuation characters could not be typed | `568c43a` |
| #117 | No chamber temperature on the print status screen | `9d25e81` |
| #102 | Touch calibration's rotation compensation was wrong | `c48434a` |
| #80, #7 | Screen woke itself about every hour | `b1682b4` |
| #107, #115 | Spoolman could not be turned off and its errors vanished | `ec3b9e3` |
| #56 | No confirmation before pause | `b9e37de` |
| #28 | Z+ carried the gap-closing arrow | `c7c8cd5` |

Two of those deserve a footnote.

**#132 is a deployment fix, not a code fix.** The macro we ship has always
honoured `EXTRUDE_LEN`. `installer.sh` copies the Klipper configs into the
printer's own config tree and `update.sh` only replaced the binary, so whatever
was installed first stayed there forever. That also gated #90, #104 and #135,
since all three are changes under `k1/`.

**#102 is unverified on hardware.** A K1 Max builds with `EVDEV_CALIBRATE`
unset and `touch_calibrated` false, so none of that code runs here. The
rotation algebra was checked by composing it against LVGL's own
`indev_pointer_proc` for all four rotations, which is exact, but nobody has put
a finger on a rotated calibrated panel with this build. Worth asking a KE or
Nebula owner before believing it.

#### What each of those actually was

Written in the present tense, describing the code as it stood before the fix,
because the diagnosis is the part worth keeping. Only the ones where the reading
is not obvious from the issue text.

**#33 and #118, the fans.** Creality defines the K1's fans as `[output_pin]`
with `scale: 255`, and each has a minimum value below which it does not spin.
Their `M106` maps a user 0-255 onto `min..255` so the whole slider is useful.
We write raw pin values, so on the development machine the Side Fan does nothing
until the slider passes 71 percent, the Back Fan until 20, and the Toolhead Fan
until 10. The readback has the mirrored problem: a slicer asking for half speed
puts the pin at 217 of 255 and we display 85 percent.

#118's specific complaint, the Back Fan reading zero while running, has a second
cause on top of that: `output_pin fan1` and `temperature_fan chamber_fan` are
both on pin `PC0`. The closed loop chamber fan drives the hardware while
`output_pin fan1`'s reported `value` stays at whatever `SET_PIN` last wrote.

**#104 and #135, belt calibration.** The two warnings are structural rather than
intermittent, so they fire on every run on every machine.
`k1/scripts/graph_belts.py` compares the belt letters against uppercase `A` and
`B` while the macro passes `NAME=a` and `NAME=b`, and it tries to parse a
Klippain style `raw_data_<date>_<time>_<belt>.csv` out of filenames that never
have that shape. The crash is separate: both sweeps and the analysis are one
macro with only `M400` between them, which waits for moves and not for the
accelerometer writer, so two full datasets are held at once on a machine with
118 MB free.

**#80 and #7, the hourly wake.** `custom_tick_get` drives every LVGL tick from
`gettimeofday`, so an NTP step moves the display sleep timer, every animation
and the input shaper watchdog. It also computes `tv_sec * 1000000` in `time_t`,
which on a 32-bit `time_t` overflows with a period of 4294.967 seconds, or 71.6
minutes. That is the cadence in the logs users attached. Our Bootlin musl
toolchain has a 64-bit `time_t`, so the overflow is latent for us rather than
live, but the wall clock dependence is real on every target and
`CLOCK_MONOTONIC` removes both.

**#102, touch calibration and rotation.** Upstream `44d11fc` added a rotation
block to `lv_tc_transform_point`. `lvgl/src/core/lv_indev.c:347-355` rotates
pointer input too, and the affine is already fit from raw driver points to
logical post-rotation points, so what belongs there is the *inverse* of LVGL's
rotation in the driver's own resolutions. Upstream applied the rotation itself,
in logical resolutions. Composed against `indev_pointer_proc`, 270 happens to
agree, 180 is off by one pixel, and 90 puts the touch somewhere else entirely.

The half users actually notice is the preview. It passes the transformed point
to `lv_obj_set_pos`, which wants a screen coordinate, and gets the pre-rotation
one with nothing to un-rotate it. So the cursor appears mirrored while the
calibration underneath is correct, which reads as the calibration having been
ignored and sends people round the recalibration loop.

### Fixed already by this fork

| Issue | Fixed by |
| --- | --- |
| #52, cross compile fails | `9e6d564` capped sub-make parallelism, `5658e14` rewrote the toolchain docs. Audit B1 and B5 |
| #110, static linking fails for x86_64 | `4eabfc7`, only link statically when cross compiling. Audit B3 |
| #161, #117 temperature half, #6, #132 lengths | `ff6dfad` widened the ranges to 320C and 200mm and clamps them against the printer's own limits, `a937708` made them configurable |
| #51, layer counts not updating | Upstream `e21b163` and `9c52e8a` removed the monotonic guard. A small residual remains: `reset()` does not clear `current_file`, so the previous file's layer count shows until the metadata reply lands |
| #158, #119, Android | Android was removed from this fork in `b827111` |
| #114, #81, input shaper problems | Largely addressed by the input shaper round, `cf4f60b` through `ad40b2a`. Audit C13 to C17 |
| #155, #108, USB access | `installer.sh:152` symlinks `/tmp/udisk` into the gcodes root, so a stick appears as a folder in the file browser |

### Declined

| Issue | Why |
| --- | --- |
| #106, non-ASCII filenames render as squares | Real, and a font job rather than a code job. Montserrat has no Cyrillic. `assets/dejavusans_mono_14.c` does cover Cyrillic, Greek, Hebrew and Arabic but only at 14px and monospaced, so a proper fix means generating font assets at the sizes the UI uses. Worth its own round |
| #151, #100, power loss recovery | Measured above: this machine's Klipper has no recovery module, no recovery macro, and `save_variables` holds only a z offset, so there is nothing for a screen button to call. The feature is a Klipper extras module rather than a panel. It has to persist the file offset, Z, E, coordinate mode, heater targets and fan speeds to eMMC at every layer, from inside the gcode path, and then on boot re-home without dragging the toolhead through the part still on the bed. Guppyscreen observes Moonraker, is not in the gcode path, and is the first thing to die when the board browns out. Testing it means cutting power mid print, repeatedly, and the only machine here is the one that prints. Creality's stock firmware does have recovery, but it lives in their closed userspace rather than in the Klipper this printer runs. Where a machine does provide a recovery macro, wiring a confirmed button to it is small and worth doing then |
| #44, screen brightness | Nothing exists in the tree, and the K1 has no sysfs backlight, so it would need a jzfb ioctl. `consp`'s `601736d` on the FF5M is the reference for how that looks |
| #48, #41, progress indicator on long actions | A genuine gap, and the input shaper panel's status line is now the idiom to copy, but it touches every panel and deserves a round of its own |
| #31, HappyHare integration | No MMU hardware to test against |
| #113, Raspberry Pi 3 | `installer-deb.sh:105` accepts only `aarch64`, which is what we build. A 32-bit arm target is a new build variant, not an installer tweak |
| #126, gifts mode and time estimate accuracy | The filename half is worth doing later. A "secret" print mode and a better remaining-time model are both larger than they look |
| #69, custom background image | Would need image loading from disk and a file picker for it |
| #89, factory reset and delete file | Delete is reasonable and wants its own change. Factory reset is not a screen's job |
| #160, SSH changes root password | The installer replaces `/etc/init.d/S50dropbear`. Nothing in it changes a password, and the report has no detail to act on |

### Not reproducible here

| Issue | Finding |
| --- | --- |
| #72, LED will not turn off or dim on a K1 Max | `output_pin LED` has `scale: 1.0`, which is exactly what `src/led_panel.cpp:128-163` assumes. Measured. The panel and the hardware agree |
| #66, Home All homes twice | There is no `gcode_macro G28` override on a K1 Max, so `G28 X Y Z` is native homing. Reported on a CR10-SE. Worth noting that `src/homing_panel.cpp:171` sends `G28 X Y Z` while the bed mesh fix in `66cf61b` settled on a bare `G28` |
| #84, Y axis arrows opposite bed movement | Reported on an Ender 3 V3 KE. There is an Invert Z Arrows setting and no Y equivalent, so a Y toggle would answer it, but it cannot be verified here |

### No depth

Support questions, duplicates, or complaints with nothing actionable:
#154, #149, #143 (the thread, as opposed to the wifi defects it exposed), #119,
#115, #112, #111, #107 (the configuration question), #103, #96, #94 (the
one-line form, covered above), #89, #81, #64, #59, #58, #54, #41 (the general
complaint), #39, #37, #28, #23, #7 (duplicate of #80), #6 (the ideas list,
individual points covered above).

#28, Z arrows reversed, was triaged here as a genuine disagreement rather than
a bug, on the grounds that a K1 moves the bed and a KE moves the toolhead so
"up" means opposite things. That was half right. Our own issue #1 turned up the
other half: the icons draw an arrow above a plate, so the arrow is the nozzle
and the plate is the bed, and the pair therefore says whether the gap opens or
closes rather than which way any part travels. Read that way the old default
was wrong everywhere, because it put the gap-closing arrow on the button
labelled Z+, and you had to switch Invert Z Icon on to get a button that agreed
with itself.

The default is now the arrow that matches the gcode, and the setting is Invert
Z Arrows under a new config key, `invert_z_arrows`. Anyone who had turned the
old one on to get an up arrow on Z+ keeps one, since the old key is ignored and
the new default gives them the same picture. The toggle still exists for people
who would rather the arrow tracked the part they can see moving.
