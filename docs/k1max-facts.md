# Creality K1 Max target facts

Captured 2026-08-16 from the development printer with `scripts/probe-printer.sh`.
The probe itself only reads.

**This is a snapshot, not a live view.** The machine has moved on since: it now
runs release `2026.08.19-a5286cb8`, having gone through `c722ed54`, `4cdb21af`
and `c9abd7f0`, and on 2026-08-19 the Klipper config tree under
`/usr/data/printer_data/config/GuppyScreen/` was replaced for the first time
since it was installed, so its macros are ours rather than whatever the first
install left. Anything below that reads as a measurement is what was true on
the day it was taken. Re-probe before relying on a number.

Serial numbers, MAC addresses and the WiFi SSID are deliberately not recorded here.
Re-run the probe if you need fresh values.

## Summary for the build

| Property | Value | Why it matters |
| --- | --- | --- |
| SoC | Ingenic X2000E, XBurst2 dual core | MIPS32r2 little endian, `mips32el` toolchain |
| ISA | `mips1 mips2 mips32r2`, MSA ASE | Do not emit MIPS r6. MIPS32 or MIPS32r2 both run |
| Kernel | 4.4.94 SMP PREEMPT, built 2024-05-11 | Old. Bootlin headers are 5.4, musl handles the fallbacks |
| Firmware | `1.3.3.29`, board `CR4CU220812S11` | K1 Max |
| Rootfs | Buildroot 2020.02.1, overlayfs, `/rom` 100% full | Only `/usr/data` (6.5G, 45% used) has room |
| libc | glibc 2.29, loader `/lib/ld-linux-mipsn8.so.1` | We link static musl, so this does not constrain us |
| RAM | 209 MB total, 128 MB swap file. Free was 118 MB at probe time, which is a reading rather than a property | Tight. Watch allocation in long-running panels |
| Framebuffer | `/dev/fb0`, `jzfb`, mode `480x800p-50` | Portrait panel |
| fb virtual size | `480,1600` (double buffered), 32 bpp, stride 1920 | Matches `LV_COLOR_DEPTH 32` in `lv_conf.h:35` |
| Touch | `goodix-ts` on `/dev/input/event0` | Matches `EVDEV_NAME` in `lv_drv_conf.h:452`. `goodix-pen` on event1 is unused |
| wpa_supplicant | socket dir `/var/run/wpa_supplicant`, iface `wlan0` | Matches the `wpa_supplicant` config key |
| Display rotate | `display_rotate: 3` in the installed config | 270 degrees, matches CI building K1 with `GUPPY_ROTATE` |

## Build flags this implies

What `scripts/build.sh mips` sets, and the K1/Max row of our CI matrix:

```
CROSS_COMPILE=mipsel-linux-
GUPPY_THEME=material        # or zbolt
GUPPY_ROTATE=true
# GUPPY_SMALL_SCREEN unset  # panel is 800px on the long edge, not a small screen
# EVDEV_CALIBRATE unset     # goodix reports absolute coords, no calibration needed
```

## What was on it when we found it

Upstream guppyscreen, built from the exact commit we forked:

```
{"version": "nightly-07409cb031bbbfc57cd7817ba295e5385e3d5565", "theme": "material", "asset_name": "guppyscreen.tar.gz"}
```

`07409cb` is upstream `main` HEAD and our fork point, and that binary was
6011904 bytes, matching upstream's published `nightly` asset. It was a useful
reference while we were still building from an unmodified tree.

It runs our builds now, so that comparison no longer applies. The original is
kept on the printer as `/usr/data/guppyscreen/guppyscreen.orig-07409cb` if it is
ever needed again.

Either way it is launched by `/etc/init.d/S99guppyscreen` under OpenRC
`supervise-daemon`, which restarts it if it dies, and logs to
`/usr/data/printer_data/logs/guppyscreen.log`. A crashed process comes back; a
wedged one does not, which is why the main loop aborts rather than continuing,
see `docs/audit.md` C10.

Other mods present in `/usr/data`: `fluidd`, `helper-script`, `moonraker`,
`nginx`. Also accumulating there are the `guppyscreen-backup-*.tar.gz`
snapshots, one per deploy at about 4 MB each, five as of 2026-08-19. That is
the only thing on this partition that grows on its own, and this partition is
the only one with room, so prune them rather than letting them run. Creality's `S99start_app` is gone from `/etc/init.d`, so whoever
installed guppyscreen answered yes to the "disable all Creality services"
prompt. Some Creality services do still run: `cx_ai_middleware`, `webrtc`,
`cam_app`, `mjpg_streamer`.

## Getting on it

SSH as `root`, password `creality_2023`. That is Creality's factory default for
the K1 series, the same on every unmodified machine, which is why it is written
down here rather than treated as a secret. It is not a secret worth keeping, but
it is worth changing on a printer exposed to anything but a home LAN.

Only password auth is accepted: there is no `authorized_keys` on the machine,
so `sshpass` or an interactive prompt is the way in. `scripts/probe-printer.sh`
still takes `PRINTER_PASS` from the environment, so a printer with a changed
password needs nothing else.

## Klipper paths

Where Klipper actually lives on this machine, which `update.sh` needs and
hardcodes as its fallback when Moonraker does not answer:

| What | Path |
| --- | --- |
| `klipper_path` | `/usr/share/klipper` |
| `config_file` | `/usr/data/printer_data/config/printer.cfg` |
| Our macros | `/usr/data/printer_data/config/GuppyScreen/` |
| `save_variables` | `/usr/data/printer_data/config/Helper-Script/variables.cfg` |

Both come from `/printer/info`, which is why `refresh_klipper_files` asks
rather than guessing.

**The klippy extras are symlinks here, not copies.**
`klippy/extras/calibrate_shaper_config.py` points into
`/usr/data/guppyscreen/k1_mods/` and `klippy/extras/gcode_shell_command.py`
into `/usr/data/helper-script/files/gcode-shell-command/`. That is a
helper-script install talking, and it matters because `cp` follows a symlink:
refreshing the second one would write through it into helper-script's own tree.
`update.sh` skips symlinked destinations for exactly this reason.

## Moonraker

Configured as `host: 0.0.0.0`, `port: 7125`, started by `S56moonraker_service`.

During the initial probe nothing was listening on 7125 and no moonraker process
was running, because Moonraker was mid-update at the time. Re-checked the same
day and it is up and healthy:

```
{"result":{"klippy_connected":true,"klippy_state":"ready", ...}}
```

Components enabled include `authorization`, `webcam`, `update_manager`,
`timelapse` and `octoprint_compat`, so this is a helper-script style install
rather than stock.

This is the address to point the simulator at:

```sh
PRINTER_HOST=192.168.1.202 scripts/build.sh sim
```

## This machine never reports a layer number

Read 2026-08-20 from `printer/objects/query`, idle after a finished print.
`print_stats` carries the `info` key and nothing in it:

```json
"info": { "total_layer": null, "current_layer": null }
```

Klipper only fills those in when the gcode calls `SET_PRINT_STATS_INFO`, which
is the slicer's job. The files on this machine do not. Four of them were
sampled over the wire, OrcaSlicer 2.3.0 and 2.4.2, sliced between June 2025 and
August 2026: no occurrence in any start gcode, and none in 200 KB taken from
the middle of an 8 MB print, where a per-layer call would have to appear.

So `PrintStatusPanel`'s reported-layer path is dead on our target hardware and
the counter runs entirely on its estimate. That needs `layer_count`, or
`first_layer_height` with `layer_height` and `object_height`, out of the file
metadata, plus `gcode_move.gcode_position` for Z. Moonraker does extract them:
for the print above, `layer_count` was 123, heights 0.2 and 0.2, object height
24.6.

Two things follow from this.

The key being present with null members is not the same as the key being
absent, and both have to reach the estimate. `max_layer` and `current_layer`
test the inner values rather than the object, so they do.

And the simulator's fake reports real layer numbers by default, so out of the
box it exercises the one path this printer never takes. `--layer-info none` is
the printer's shape. Use it before believing anything about the layer counter.

While querying that print, `virtual_sdcard.progress` read exactly `1.0`, with
`file_position` and `file_size` both 8270417. Upstream #103, the progress bar
stuck at 99, comes from a printer where progress stops short of 1.0. This one
does not, so the `state == "complete"` branch that forces 100 is belt and
braces here rather than the thing that fixes it.

Creality's fork also adds `layer` and `layer_count` to `virtual_sdcard`. Both
read 0 after that 123 layer print, so nothing fills those either.

## Raw excerpts

### CPU

```
system type             : xburst2-based
machine                 : ingenic,x2000_module_base
cpu model               : Ingenic XBurst@II.V2 V0.0  FPU V0.0
BogoMIPS                : 2390.01
tlb_entries             : 288
isa                     : mips1 mips2 mips32r2
ASEs implemented        : msa
```

Two identical cores.

### Kernel

```
Linux 4.4.94 #362 SMP PREEMPT Sat May 11 16:15:06 CST 2024 mips GNU/Linux
gcc version 7.2.0 (Ingenic Linux-Release5.1.0-Default(xburst2(fp64)+glibc2.29) 2021.12-22)
```

### Framebuffer

```
/sys/class/graphics/fb0/virtual_size   = 480,1600
/sys/class/graphics/fb0/bits_per_pixel = 32
/sys/class/graphics/fb0/stride         = 1920
/sys/class/graphics/fb0/rotate         = 0
/sys/class/graphics/fb0/name           = jzfb
/sys/class/graphics/fb0/modes          = U:480x800p-50
```

`stride` 1920 is 480 * 4 bytes, confirming 32 bpp. `virtual_size` y of 1600 is
two 800px frames, so the driver is double buffering and page flipping.

### Input

```
N: Name="goodix-ts"    Handlers=kbd event0   PROP=2  EV=b  ABS=6e50000 0
N: Name="goodix-pen"   Handlers=event1       PROP=0  EV=b  ABS=6610000 0
```

`PROP=2` on the touchscreen is `INPUT_PROP_DIRECT`, a real touch panel reporting
absolute coordinates.

### Storage

```
/dev/root          121.4M  121.4M       0 100% /rom
/dev/mmcblk0p9      96.8M   96.8M       0 100% /overlay
overlayfs:/overlay  96.8M   96.8M       0 100% /
/dev/mmcblk0p10      6.5G    2.8G    3.4G  45% /usr/data
```

The overlay is completely full. Anything we install has to live in `/usr/data`.

## Gotchas found while probing

- Networking from the printer is awkward and cost real time to work out. The
  stock `/usr/bin/curl` is not curl, it is a Creality utility with an unrelated
  command line (`curl [hVvX:H:r:d:F:n:] [METHOD] url ...`, and no `--version`).
  Re-checked 2026-08-17: `-s` warns `invalid option` and then fetches anyway
  with exit 0, so testing the exit code will tell you curl is fine. It is
  `--max-time` that actually breaks it, exiting 234. busybox `wget` handles
  plain HTTP fine, so use `wget -q -T <sec> -O -` for Moonraker checks, but
  it cannot negotiate TLS with `github.com` or `api.github.com`, failing with
  alert 80, and reaches only `raw.githubusercontent.com`. That is why upstream
  downloaded a curl binary from a third party repo and ran it as root. Python 3
  is already installed for Klipper (3.8.2) and reaches everything, which is what
  our `update.sh` and `installer.sh` use instead. All re-verified 2026-08-17.
  See the networking section of `AGENTS.md`.
- busybox `tar` unlinks before it overwrites, so unpacking a release over a
  running binary or over the executing `update.sh` is safe: open readers keep
  the old inode.
- `/etc/os-release` reports Buildroot, not the Creality firmware version. The
  firmware version lives in `/etc/ota_info` and
  `/usr/data/creality/userdata/config/system_version.json`.
