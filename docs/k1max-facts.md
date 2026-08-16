# Creality K1 Max target facts

Captured 2026-08-16 from the development printer with `scripts/probe-printer.sh`.
Read only, nothing on the printer was modified.

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
| RAM | 209 MB total, 118 MB free, 128 MB swap file | Tight. Watch allocation in long-running panels |
| Framebuffer | `/dev/fb0`, `jzfb`, mode `480x800p-50` | Portrait panel |
| fb virtual size | `480,1600` (double buffered), 32 bpp, stride 1920 | Matches `LV_COLOR_DEPTH 32` in `lv_conf.h:35` |
| Touch | `goodix-ts` on `/dev/input/event0` | Matches `EVDEV_NAME` in `lv_drv_conf.h:452`. `goodix-pen` on event1 is unused |
| wpa_supplicant | socket dir `/var/run/wpa_supplicant`, iface `wlan0` | Matches the `wpa_supplicant` config key |
| Display rotate | `display_rotate: 3` in the installed config | 270 degrees, matches CI building K1 with `GUPPY_ROTATE` |

## Build flags this implies

Matches the K1/Max row of upstream's `.github/workflows/build.yml`:

```
CROSS_COMPILE=mipsel-linux-
GUPPY_THEME=material        # or zbolt
GUPPY_ROTATE=true
# GUPPY_SMALL_SCREEN unset  # panel is 800px on the long edge, not a small screen
# EVDEV_CALIBRATE unset     # goodix reports absolute coords, no calibration needed
```

## Already installed

The printer is running guppyscreen built from the exact commit we forked:

```
/usr/data/guppyscreen/.version
{"version": "nightly-07409cb031bbbfc57cd7817ba295e5385e3d5565", "theme": "material", "asset_name": "guppyscreen.tar.gz"}
```

`07409cb` is upstream `main` HEAD and our fork point. The installed binary is
6011904 bytes, byte-for-byte the size of upstream's published `nightly` asset.
That gives us a known-good reference: a binary we build from an unmodified tree
should land very close to it.

Launched by `/etc/init.d/S99guppyscreen` under OpenRC `supervise-daemon`, which
restarts it if it dies. Logs to `/usr/data/printer_data/logs/guppyscreen.log`.

Other mods present in `/usr/data`: `fluidd`, `helper-script`, `moonraker`,
`nginx`. Creality's `S99start_app` is gone from `/etc/init.d`, so whoever
installed guppyscreen answered yes to the "disable all Creality services"
prompt. Some Creality services do still run: `cx_ai_middleware`, `webrtc`,
`cam_app`, `mjpg_streamer`.

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

- The K1's `curl` is a cut down build that rejects both `-s` and `--max-time`.
  Use `wget -q -T <sec> -O -` for HTTP checks in any script that runs on the
  printer. Upstream's `installer.sh` hits this too, which is why it downloads a
  working `curl` from `k1-discovery` before fetching the release.
- `/etc/os-release` reports Buildroot, not the Creality firmware version. The
  firmware version lives in `/etc/ota_info` and
  `/usr/data/creality/userdata/config/system_version.json`.
