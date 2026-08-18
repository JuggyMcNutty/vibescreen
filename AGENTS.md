# AGENTS.md

Onboarding for anyone new to this repo, human or AI. `README.md` is for people
installing it, `DEVELOPMENT.md` covers the toolchain and build. This file is the
working reference: what is known about the hardware, what is known to be broken,
and the rules that are not obvious from reading the code.

## What this is

Guppyscreen is a touch UI for Klipper printers, talking to Moonraker over a
websocket, built on LVGL as a standalone binary with no X or Wayland underneath.
It draws straight to `/dev/fb0` and reads touch from `/dev/input/event0`.

This repo is a **takeover of an abandoned project**. Upstream
`ballaswag/guppyscreen` stopped at commit `07409cb` on 2024-07-15. We forked
from that commit and are picking it back up.

It is still stopped, re-checked 2026-08-18: `main` is our fork point exactly,
and the `dev` and `btt_pad7` branches are stale from December 2023 with nothing
in them that is not already in `main`. What is left there is 63 open issues and
6 open pull requests, all triaged in `docs/upstream-issues.md`. Read that before
acting on anything anyone reports upstream, because about half of it we have
already fixed.

Our target is a **Creality K1 Max**. Everything measured about it is in
`docs/k1max-facts.md`. Do not guess at hardware details, that file has the real
values.

## Read these first

| File | What it is for |
| --- | --- |
| `docs/k1max-facts.md` | Real hardware, firmware, framebuffer and input values from the printer |
| `docs/audit.md` | Known bugs in the inherited code, with severity and suggested order |
| `docs/upstream-issues.md` | Upstream's 63 open issues and 6 open PRs, triaged against our tree |
| `DEVELOPMENT.md` | Toolchain, build targets and running the simulator |

## Build

Three targets, all through one script.

```sh
scripts/setup-toolchain.sh          # once, downloads the cross toolchain
scripts/build.sh mips               # K1 / K1 Max binary
scripts/build.sh arm                # aarch64, Raspberry Pi and BTT Pad
scripts/build.sh sim                # x86_64 SDL build that runs on your desktop
```

Useful variants:

```sh
scripts/build.sh mips zbolt         # Z-Bolt icon set instead of Material
scripts/build.sh mips --small       # Ender 3 V3 KE / Nebula Pad sized panel
scripts/build.sh mips --clean       # rebuild the vendored libs too
PRINTER_HOST=192.168.1.202 scripts/build.sh sim   # point the sim at a printer
```

Output is `build/bin/guppyscreen` for every target. Two stamp files track what
was last built so nothing stale gets linked: `.vendor-target` for the
architecture, which is all libhv, spdlog and libwpa_client care about, and
`.build-flags` for architecture plus theme plus small screen, since those change
`-D` defines that affect every object of ours.

## CI

One workflow, `.github/workflows/build.yml`. It builds the same four variants
CI has always built, plus the simulator, and it **calls `scripts/build.sh`**
rather than repeating the flags. If you add a build option, put it in the script
and reference it from the matrix, so the two cannot drift.

It runs on push to `main` and on pull requests. There is no tag trigger:
publishing happens per push, so tagging is not how releases are made here.

Two version shapes, and `update.sh` keys off them:

| Trigger | Version | Published as |
| --- | --- | --- |
| local `scripts/build.sh` | `dev-<sha>` | nothing, never published |
| pull request | `dev-<sha>` | nothing, never published |
| push to `main` | `<date>-<sha>` | its own release, tagged the same |

Every push that changes source and builds cleanly gets its own release, so the
release list is a build history. Do not go back to a single fixed tag that
overwrites itself: that was the old arrangement and it left nothing to roll
back to.

**Documentation-only pushes still build but do not publish.** The `version` job
diffs against `github.event.before` and skips the release when every changed
path matches `*.md`, `docs/`, `screenshots/` or `LICENSE`. The filter lists what
to ignore rather than what to build, so an unrecognised path errs towards
publishing. Remember that `installer.sh`, `update.sh`, `k1/` and `themes/` ship
inside the tarball and count as source even though nothing compiles them.

If it ever skips a release you wanted, run the workflow manually: a
`workflow_dispatch` always publishes.

The version is worked out once in the `version` job and passed to the build and
release jobs. Computing it in both races across UTC midnight and would tag a
release differently from the string compiled into the binary. The tag, the
release name and `.version` are all the same string.

Releases are normal releases, not prereleases, so `releases/latest` resolves to
the newest build. `update.sh` and both installers follow that rather than any
fixed tag.

The release is published from its own job, after the whole matrix **and** the
simulator build pass. Do not move it back into the matrix: four parallel jobs
race to create the same tag, and a broken variant would produce a half
populated release.

Release notes are generated in the workflow. Keep them practical: which asset
suits which printer, and how to install. Anyone arriving from the original
guppyscreen needs the installer rather than `update.sh`, because that project's
updater points at upstream and will not see our builds.

CI asserts the mips binary is statically linked, which is the property that
decides whether it runs at all, since the glibc version moves between Creality
firmware releases.

**Never call `apt-get` directly in the workflow.** Use
`scripts/ci-apt-install.sh`, and only for something the runner image genuinely
lacks. The image already ships cmake, gcc, g++ and make, so only the arm build
(`gcc-aarch64-linux-gnu`) and the simulator (`libsdl2-dev`) need apt at all.

The runner's `/etc/apt/apt-mirrors.txt` lists `azure.archive.ubuntu.com` first
and only fails over to the canonical archives on a hard error, never on a slow
one. When that mirror degraded in August 2026 the package downloads dropped
from 11 MB/s to 57 kB/s and then stalled outright. apt's
`Acquire::http::Timeout` is an inactivity timeout, so a server dribbling a few
bytes never trips it, and with no job timeout nine jobs sat wedged for hours
rather than failing. The helper bounds each attempt with `timeout` on wall
clock time and drops the Azure mirror before retrying. Every job also carries a
`timeout-minutes`, so a stall now fails the run in minutes instead of sitting
until the 360 minute default. Keep both: the helper alone cannot catch a hang
somewhere else, and a timeout alone just fails without trying the good mirror.

**Android is gone.** Upstream built an APK from a separate `android` branch.
The workflow went with the CI rework and the `OS_ANDROID` guards and
`platform.h` went with it. Do not add conditional code for a platform that
cannot be built or tested here.

Two workflows were deleted with the fork's cleanup: `guppydroid.yml`, the
Android build described above, and `pull_request.yml`, which duplicated the
build matrix.

Do not reintroduce the `ballaswag/guppydev` container. It is unpinned, sits on
the abandoned upstream's account, and carries a different toolchain from the one
we develop against, so a green run in it says nothing about a local build.

This file used to say the simulator ignores `SIGTERM` and that `timeout 20
./guppyscreen` hangs. **It does not, measured 2026-08-17.** Plain `timeout`,
`timeout -s INT` and `pkill -x guppyscreen` all end it promptly, under the
`x11` and `wayland` SDL drivers and with the driver left to autoselect. Use
whichever you like; there is no workaround to remember.

If you do see it outlive a `SIGTERM`, that is new behaviour and worth writing
down rather than assuming this note is right.

Host packages needed: `base-devel`, `cmake`, `sdl2` (or `sdl2-compat`),
`sshpass` for the printer probe.

### The toolchain, and the trap it replaced

Upstream's `DEVELOPMENT.md` told you to use the Ingenic
`mips-gcc720-glibc229` toolchain. That is what built their last tagged release,
`0.0.26-beta`, which is dynamically linked against `/lib/ld-linux-mipsn8.so.1`
and only runs on firmware carrying glibc 2.29. It is why their `installer.sh`
refuses to install unless it finds `/lib/ld-2.29.so`. Meanwhile their own CI had
moved to a Bootlin musl toolchain and static linking back at `a42427cb`, so the
documented route produced a binary that would not run for most people.

Our `DEVELOPMENT.md` has been rewritten and no longer says that, but the history
is worth knowing before trusting anything else inherited from upstream.

We use Bootlin `mips32el--musl--stable-2025.08-1`: gcc 14.3.0, binutils 2.43.1,
musl 1.2.5. Static, so the K1's glibc version is irrelevant to us.

Known-good fallback if gcc 14 ever becomes more trouble than it is worth:
`mips32el--musl--stable-2024.02-1` (gcc 12.3.0), which is what upstream CI and
`pellcorp/grumpyscreen` both use. Change the two constants at the top of
`scripts/setup-toolchain.sh`.

### Verifying a mips build without a printer

Check the ELF. These are the properties that decide whether it runs at all, and
CI asserts the static one on every build:

```sh
toolchains/mips32el--musl--stable-2025.08-1/bin/mipsel-linux-readelf -h build/bin/guppyscreen
```

Expect `ELF32`, little endian, `EXEC`, `MIPS`, and flags **`0x50001007`**
(`noreorder, pic, cpic, o32, mips32`). There must be no `INTERP` or `DYNAMIC`
program header, because the K1's glibc version moves between firmware releases
and only a static binary is immune to that. Stripped it comes to about 6.4 MB.

To compare against a known good binary, pull one of our own releases rather
than anything of upstream's; ours are built by the same script with the same
toolchain.

XBurst2 is MIPS32r2, so never emit MIPS r6 instructions.

## Repo layout

```
src/                  the application, all ours to maintain
lvgl/                 submodule, v8.3.11
lv_drivers/           submodule, v8.3.0
libhv/                submodule, websocket and http client
spdlog/               submodule, logging
wpa_supplicant/       vendored hostap copy, built only for libwpa_client.a
lv_touch_calibration/ in-tree, not a submodule, touch calibration screens
patches/              three patches applied to submodules, see below
k1/                   payload installed onto the printer, init scripts and klippy modules
assets/               generated LVGL C arrays for icons and fonts
debian/               Raspberry Pi and Debian packaging, plus the config template
themes/               primary and secondary colour json, installed to the printer
scripts/              ours, see below
tools/                ours, fake_moonraker.py for local testing
docs/                 ours
screenshots/          referenced from README.md, ours plus some still upstream's
```

Not in git: `build/` and `toolchains/`, both generated.

### `lv_conf.h` is two configs, not one

`lv_conf.h:14` opens `#ifndef SIMULATOR`, `:715` is the `#else`, `:1353` the
`#endif`. The device build compiles lines 15 to 713 and the simulator 717 to
1351, and the two are not kept in step.

The difference that bites is fonts. The printer has Montserrat 8, 10, 12, 14,
16, 20 and 40 only; the simulator half enables every size from 8 to 48. So
`&lv_font_montserrat_18` builds and looks right in the simulator and fails to
link for mips. Check which half you are reading before believing anything in
that file.

Both halves do agree on the things that matter for drawing: `LV_COLOR_DEPTH 32`,
`LV_DRAW_COMPLEX 1`, `LV_USE_CANVAS 1`, and `LV_MEM_CUSTOM 1`, which means
`lv_mem_alloc` is plain `malloc` and the `LV_MEM_SIZE` in there is dead config
sitting inside an `#if LV_MEM_CUSTOM == 0`.

`scripts/` is all new in this fork:

| Script | Purpose |
| --- | --- |
| `setup-toolchain.sh` | Downloads and unpacks the cross toolchain. Idempotent |
| `apply-patches.sh` | Applies `patches/` to submodules. Idempotent |
| `build.sh` | Builds any target: `mips`, `arm` or `sim` |
| `probe-printer.sh` | Read-only fact gathering from a printer over SSH |

### The patches

Three patches in `patches/` must be applied to submodules before building. They
are not committed into the submodules, so a fresh clone or a `git submodule
update` silently reverts them and the build then fails confusingly.
`scripts/apply-patches.sh` handles this and `scripts/build.sh` calls it every
time, so you should not need to think about it. Never `git add` a submodule
pointer change; `git status` showing `m lvgl` and friends is expected and
correct.

## Git remotes

```
origin        JuggyMcNutty/vibescreen   ours, this is the one you push to
upstream      ballaswag/guppyscreen     the abandoned original, fetch only
grumpyscreen  pellcorp/grumpyscreen     a live fork, reference only
```

`upstream` and `grumpyscreen` both have their push URL set to a bogus string so
a stray `git push` to either fails loudly instead of trying.

`grumpyscreen` is configured with `tagOpt = --no-tags`. Do not undo that. One of
their releases is tagged literally `main`, and fetching it creates a
`refs/tags/main` that collides with our branch, after which every `git push
origin main` dies with "src refspec main matches more than one".

Our repo is named **vibescreen**, but nothing inside the tree has been renamed:
the binary, the install paths, the config file and the init script are all still
`guppyscreen`. That keeps us drop-in compatible with an existing install and
with upstream's `installer.sh`. Renaming is a separate decision, not an
oversight.

We work directly on `main`, stacking our commits on upstream history.

**On grumpyscreen:** it is 233 commits ahead and still active, but it is a
narrowing fork for pellcorp's Simple AF firmware and much of that lead is
deletion. It dropped bedmesh, input shaper, belt calibration, TMC tuning,
multi-printer and theming, which is most of what we care about on a K1 Max. Good
for reference on touch calibration, wifi and lv_drivers. Not something we can
bulk merge. Detail in `docs/audit.md`.

## The printer

Development printer is a K1 Max at `192.168.1.202`. It is a **production
machine**, the one its owner actually prints with, and it runs our published
builds.

- **Deploying is allowed, but ask first and back up first.** A bad write can
  leave the display path broken on a machine someone needs.
- The normal route is `/usr/data/guppyscreen/update.sh` on the printer, which
  pulls the newest release. Prefer that over copying a binary by hand.
- Hand-installing a local build is for testing something not yet pushed. Back
  up, stop the service, smoke test the new binary from the install directory,
  then install. A locally built binary is versioned `dev-<sha>` and `update.sh`
  will refuse to replace it without `--force`.
- Rollbacks live in two places. Inside `/usr/data/guppyscreen/`,
  `guppyscreen.orig-07409cb` is the upstream build that was there before we
  touched it, alongside `guppyconfig.json.orig` and `update.sh.orig`. One level
  up in `/usr/data/` are the full directory snapshots,
  `guppyscreen-backup-*.tar.gz`, one per deploy. Take a fresh one before
  installing anything:

  ```sh
  cd /usr/data && tar czf guppyscreen-backup-<installed-version>.tar.gz guppyscreen/
  ```

  Outside the directory being archived, or tar recurses into its own output.
- Credentials come from the environment, never from a committed file:
  `PRINTER_HOST=... PRINTER_PASS=... scripts/probe-printer.sh`
- Never commit the SSID, PSK, MAC addresses, printer serial or the Moonraker API
  key. `probe-printer.sh` strips the API key on the printer before it crosses
  the wire; the rest is on you to check.
- Its `curl` is not curl, and the two flags fail differently. `-s` prints
  `invalid option -- 's'` and then **fetches anyway, exiting 0**, so a script
  that only checks the exit code will conclude curl works. `--max-time` is
  fatal: it is unrecognised, the parse then slides and it tries to connect to
  the timeout value as a host, exiting 234. Use `wget -q -T <sec> -O -` in
  anything that runs on the printer.
- Moonraker is at `192.168.1.202:7125` and healthy. Point the sim at it with
  `PRINTER_HOST=192.168.1.202 scripts/build.sh sim`.

## Sending gcode

Two rules, both learned the hard way. See `docs/audit.md` C6 and C8.

**Wrap modal gcode.** `M83`, `M82`, `G90`, `G91` and friends change state
globally, not just for the next command. A panel that sets one and does not put
it back will corrupt a print that is merely paused, not stopped. Always:

```
SAVE_GCODE_STATE NAME=guppy_<what>
<the modal command and the move>
RESTORE_GCODE_STATE NAME=guppy_<what>
```

`RESTORE_GCODE_STATE` defaults to `MOVE=0`, so it restores the mode without
moving the toolhead.

**Validate before sending anyway.** `KWebSocketClient::gcode_script` does now
check the reply and surface a rejection, which it did not before `fc12faa`, so
a refused command is no longer silent. That is a backstop, not a substitute for
checking first, because **Klipper abandons the rest of a script at the first
command it does not like**. Send a command the printer lacks and everything
after it in the same script never runs, which is a worse failure than not
offering the button.

So test for what you are about to use. `KUtils::has_gcode_macro` covers macros
that only exist on modded machines, and `KUtils::is_homed` covers the other
common precondition. `BedMeshPanel`'s Calibrate is the worked example of both.

For anything that is not a macro, use `KUtils::has_config_section` and not the
object list. **`printer.objects.list` only reports objects that implement
`get_status`**, so `resonance_tester` and `adxl345` are both missing from it on
a K1 Max that plainly has them, measured. `calibrate_shaper_config` does appear,
because our module defines a `get_status`, but it returns an empty object, so
the object list can tell you it exists and nothing else. Only `configfile` shows
a section either way, with its settings, which is why `has_config_section` is
the one to reach for. `InputShaperPanel::update_available` is
the worked example, and it disables the button and names the missing section
rather than hiding the control.

While you are there: `SAVE_CONFIG` restarts Klipper and ends any print. Put it
behind `ButtonContainer`'s prompt with `prompt_optional` false, so the
confirmation is not governed by the emergency stop setting, which is about
something else.

Where a panel offers preset values, clamp them against the printer's own limits
read from `/printer_state/configfile/settings/...` in `State`. `LimitsPanel::init`
and `ExtruderPanel::init` are the two worked examples, both dispatched from
`MainPanel::init`.

## Networking from the printer

Only `raw.githubusercontent.com` is reachable with stock tooling. busybox
`wget` fails TLS against `api.github.com` and `github.com` with alert 80, and
`/usr/bin/curl` is not curl, it is a Creality utility with an unrelated command
line that does not understand `-s` or `-o`.

Upstream worked around this by downloading a curl binary from a third party
repo over `--no-check-certificate` and running it as root. Do not reintroduce
that. **Python 3 is already installed for Klipper and reaches everything**, so
`update.sh` and `installer.sh` use it for all fetching.

For plain HTTP to Moonraker on the printer, `wget -q -T <sec> -O -` is fine.

`update.sh` pulls releases from `JuggyMcNutty/vibescreen`, overridable with
`GUPPY_UPDATE_REPO`. It refuses to replace a locally built `dev-<sha>` binary
unless given `--force`, so the Settings panel's "Update Guppy" button cannot
silently discard a build you are testing on the printer.

## Never let an exception reach LVGL

Every static callback LVGL calls into must contain its own exceptions, using
`KGuard::event` from `src/event_guard.h`. If you add a new one, guard it.

This is not defensive habit, it is load bearing. LVGL cannot be recovered once
an exception has unwound through it: `lv_timer_handler` leaves its re-entrancy
guard set so every later call returns immediately, and the input state machine
is interrupted mid gesture and re-dispatches the same event forever. Catching in
the main loop gives a frozen UI, and patching LVGL to release the guard gives an
infinite exception loop. Both were built and measured, see `docs/audit.md` C10.

The main loop still has a catch, but it logs and aborts on purpose, because
anything reaching it means LVGL is already unrecoverable and a restart is the
better outcome.

## Never hand LVGL a non-convex polygon

`lv_canvas_draw_polygon` documents "only convex polygons are supported", and
the failure mode is not a bad drawing. `lv_draw_sw_polygon` walks a left and a
right chain from the lowest vertex; on a non-convex polygon neither chain
advances, `mask_cnt` never reaches `point_cnt`, and it spins forever inside the
UI lock. The process stays alive, so `supervise-daemon` does not restart it.
A wedged screen that nothing recovers is the worst outcome available here.

This is not theoretical. `MeshView` hit it drawing a bed mesh that was flat to
within probe noise: normalising the height blew 8 microns up to full relief,
the quads folded, and the simulator hung on startup with the mesh panel's
render still on the stack. It splits a failing quad into triangles, which are
convex by construction, after testing the rounded integer points rather than
the floats, since rounding alone can tip a marginal quad over.

If you add anything that fills a computed polygon, prove it is convex or
triangulate it. `lv_canvas_draw_line` has no such problem.

## Testing UI changes without touching the printer

The printer is read-only, and pressing Extrude against a live Moonraker really
does heat the hotend. Use `tools/fake_moonraker.py` for anything that would
otherwise command real hardware, and point the simulator at `127.0.0.1`:

```sh
pip install websockets
python3 tools/fake_moonraker.py 240 240            # reports 240C, accepts gcode
python3 tools/fake_moonraker.py 240 240 --reject   # rejects every command
python3 tools/fake_moonraker.py --mesh bowl        # pick a bed mesh shape
PRINTER_HOST=127.0.0.1 scripts/build.sh sim
```

It records every `printer.gcode.script` it receives to `$GCODE_LOG`, which is
how you check what a button actually sends.

It serves a `bed_mesh` object too. `--mesh` picks the shape: `adaptive` is the
real 3x3 read off the development printer, `full` a 6x6 saddle, then `bowl`,
`tilt` and `flat`. `BED_MESH_CLEAR` and `BED_MESH_CALIBRATE` are acted on
rather than logged, and calibrate deliberately replies with a matrices-only
delta carrying no `profile_name`, which is the shape Moonraker really sends and
the one panels get wrong.

For screenshots, SDL picks Wayland when it can and the X root window is not
capturable under XWayland. Force X11:

```sh
SDL_VIDEODRIVER=x11 ./build/bin/guppyscreen &
xwd -silent -name "TFT Simulator" | xwdtopnm | pnmtopng > shot.png
```

**Check the colours in anything that pipeline produces.** On a 32 bpp TrueColor
visual `xwdtopnm` emits maxval 65535 and maps the channels wrongly, pinning
blue to `0xff`, which turns the whole UI a flat blue. It is not obviously
broken, it just quietly lies, so a colour bug and a capture bug look the same.
Decoding the XWD header's `red_mask`, `green_mask` and `blue_mask` yourself is
exact and needs nothing installed.

`xdotool` can drive it, but LVGL polls its input device, so an instantaneous
click gets missed. Move, then `mousedown`, wait ~0.4s, then `mouseup`.

**The window's backing store lags an interaction by much more than a frame.**
Under XWayland a grab taken a second after a click can still show the state
before it, which reads exactly like the click having been dropped. Retrying
then double-presses the button: measured 2026-08-16, that is how an extra tap
landed on the panel underneath a dialog that had already closed. Do not sleep
and hope. Grab until two consecutive grabs are identical, then keep that one:

```sh
prev=""; while :; do
  cur=$(xwd -silent -name "TFT Simulator" | md5sum)
  [ "$cur" = "$prev" ] && break
  prev=$cur; sleep 0.4
done
```

If input really does stop, the usual cause is a `mouseup` that never arrived,
leaving the button stuck down so no later press is a new press. `xdotool
mouseup 1` clears it, and issuing one before every click makes the whole thing
idempotent.

## Threading model

Two threads, and the boundary is where the bugs are.

- **LVGL thread**: the main loop, all widget calls.
- **libhv event loop thread**: `WebSocketClient::onmessage` in
  `src/websocket_client.cpp`, which drives every `NotifyConsumer::consume`, which
  is what updates `State`.

LVGL is not thread safe. Panels take `lv_lock` around widget work.

`KWebSocketClient` has a `cb_lock` covering its handler maps. **Never invoke a
handler while holding it.** `InputShaperPanel` calls back into `gcode_script`
from inside a reply handler, which self deadlocks, and every `consume()` takes
`lv_lock`, which inverts the lock order against the LVGL thread. Copy the
handler out, unlock, then call. Every dispatch site in `onmessage` does this.

`State` has its own separate mutex, and per `docs/audit.md` C1 that mutex does
not actually protect the reference-returning accessors. Read C1 before touching
anything in `src/state.cpp` or adding a new `get_data` caller.

Both C1 and the remaining lifetime hazard in C9 dissolve if dispatch moves onto
a queue drained by the LVGL loop. That is the intended end state, so do not
solve C1 in a way that would have to be undone to get there.

## House style

The point is that a human or an AI picking this up in a year can read it.

- Small, focused commits. One concern each. Explain **why** in the body, not
  what the diff already shows.
- No em dashes. No emoji. No "comprehensive", "robust", "seamlessly", or other
  filler.
- Comments explain why, not what. A comment restating the code is worse than no
  comment.
- Match the surrounding code. `src/` is LVGL-flavoured C++17, two space indent,
  `snake_case` methods, `Panel` suffix on panel classes.
- When you find something broken but out of scope, add it to `docs/audit.md`
  rather than fixing it inline.
- Verify claims before writing them down. Several things in this file looked
  obvious and turned out to be wrong when actually checked.

## Keep this file current

Any change that alters a build command, a script, the repo layout or the remote
setup updates `AGENTS.md` **in the same commit**. A stale onboarding doc is worse
than none, which is exactly the problem `DEVELOPMENT.md` has.
