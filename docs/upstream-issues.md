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
the facts that settle them, taken from `192.168.1.202` on 2026-08-18.

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

### Open, and being fixed in this round

| Issue | What | Where in our tree |
| --- | --- | --- |
| #33, #118 | K1 fan sliders are not scaled to the speed the fan starts at | `src/fan_panel.cpp:122-155`, `:41-57`, `:90-110` |
| #94, #103, #47 | Print status never dismisses, sticks at 99 percent | `src/print_status_panel.cpp:312-322`, `:250-257`, `:393-400` |
| #95 | File list does not refresh when a file is uploaded | `src/print_panel.cpp:142`, `:177-195` |
| #104 | Belt calibration crashes Klipper, and its panel spins forever | `k1/scripts/guppy_cmd.cfg:48-56`, `src/belts_calibration_panel.cpp:182-204` |
| #135 | Belt calibration warns twice on every run | `k1/scripts/graph_belts.py:297-307`, `:521-528` |
| #90 | Load filament leaks relative extrusion mode | `k1/scripts/guppy_cmd.cfg:91-99` |
| #132 | Extrude length has no effect on load | `update.sh`, see below |
| #116 | Tiny Z offsets render in scientific notation | `src/finetune_panel.cpp:106,112,149,154` |
| #116, #91, #41 | Three digit temperatures wrap onto two lines | `src/sensor_container.cpp:54,59,65` |
| #32 | Moonraker API key is never sent | `src/websocket_client.cpp:132-133`, `src/utils.cpp:145,175` |
| #156, #143 | No way to forget a saved wifi network | `src/wifi_panel.cpp:286-293` |
| #143, #156 | A rejected wifi password never fails visibly | `src/wifi_panel.cpp:165-243` |
| #146 | Four punctuation characters cannot be typed | `src/wifi_panel.cpp:39` |
| #117 | No chamber temperature on the print status screen | `src/print_status_panel.cpp:76-102` |
| #102 | Touch calibration ignores display rotation | `lv_touch_calibration/lv_tc.c:167-177` |
| #80, #7 | Screen wakes itself about every hour | `src/guppyscreen.cpp:341-356` |
| #107, #115, #59, #58 | Spoolman cannot be turned off and its errors vanish | `src/init_panel.cpp:74-84`, `src/spoolman_panel.cpp:93,115` |
| #56 | No confirmation before pause | `src/print_status_panel.cpp:439-441` |

Notes on the ones where the reading is not obvious.

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

**#132, extrude length.** The macro we ship does honour `EXTRUDE_LEN`. The one
installed on the printer does not, because `installer.sh` **copies**
`scripts/*.cfg` into the Klipper config tree and `update.sh` only replaces the
binary. Every `k1/` fix is therefore invisible to an existing install until
`update.sh` re-copies them. That makes this the entry that gates #90, #104 and
#135.

**#80 and #7, the hourly wake.** `custom_tick_get` drives every LVGL tick from
`gettimeofday`, so an NTP step moves the display sleep timer, every animation
and the input shaper watchdog. It also computes `tv_sec * 1000000` in `time_t`,
which on a 32-bit `time_t` overflows with a period of 4294.967 seconds, or 71.6
minutes. That is the cadence in the logs users attached. Our Bootlin musl
toolchain has a 64-bit `time_t`, so the overflow is latent for us rather than
live, but the wall clock dependence is real on every target and
`CLOCK_MONOTONIC` removes both.

**#102, touch calibration and rotation.** Upstream `44d11fc` added a rotation
block to `lv_tc_transform_point`, but `lvgl/src/core/lv_indev.c:347-355` already
rotates pointer input, and the affine is fit from raw driver points to logical
post-rotation points, so it needs no rotation of its own. At 180 degrees the two
cancel to within a pixel, which is why it looks like touch was not rotated at
all. At 90 and 270 they do not cancel. The calibration preview goes through
`lv_tc_transform_point` but never through `indev_pointer_proc`, so nothing
cancels the surplus there and the cursor lands mirrored, which reads as the
calibration having been ignored.

Not reachable on our own hardware: a K1 Max builds with `EVDEV_CALIBRATE` unset
and `touch_calibrated` false, so `src/main.cpp:106` installs the bare
`evdev_read` and this code is bypassed. Affected users are on calibrated builds.

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
| #151, #100, power loss recovery | Belongs to Creality's firmware. Integration work well beyond a screen |
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
| #84, Y axis arrows opposite bed movement | Reported on an Ender 3 V3 KE. There is an Invert Z Icon setting and no Y equivalent, so a Y toggle would answer it, but it cannot be verified here |

### No depth

Support questions, duplicates, or complaints with nothing actionable:
#154, #149, #143 (the thread, as opposed to the wifi defects it exposed), #119,
#115, #112, #111, #107 (the configuration question), #103, #96, #94 (the
one-line form, covered above), #89, #81, #64, #59, #58, #54, #41 (the general
complaint), #39, #37, #28, #23, #7 (duplicate of #80), #6 (the ideas list,
individual points covered above).

#28, Z arrows reversed, deserves one word: it is a genuine disagreement rather
than a bug. On a K1 the bed moves and on a KE the toolhead does, so "up" means
opposite things. Upstream added the Invert Z Icon setting for exactly this, and
it is in `src/sysinfo_panel.cpp:180`.
