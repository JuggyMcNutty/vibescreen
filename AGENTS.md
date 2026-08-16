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
`ballaswag/guppyscreen` stopped at commit `07409cb` on 2024-07-15 with 69 open
issues. We forked from that commit and are picking it back up.

Our target is a **Creality K1 Max**. Everything measured about it is in
`docs/k1max-facts.md`. Do not guess at hardware details, that file has the real
values.

## Read these first

| File | What it is for |
| --- | --- |
| `docs/k1max-facts.md` | Real hardware, firmware, framebuffer and input values from the printer |
| `docs/audit.md` | Known bugs in the inherited code, with severity and suggested order |
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

The simulator ignores `SIGTERM` and only exits on `SIGINT`, because SDL installs
its own handler and nothing consumes the resulting quit event. So `timeout 20
./guppyscreen` hangs, and you want `timeout -s INT 20 ./guppyscreen` or
`pkill -x guppyscreen`. This is SDL-specific: the framebuffer build has no such
handler and dies on `SIGTERM` normally, so the printer's init script is fine.

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
scripts/              ours, see below
tools/                ours, fake_moonraker.py for local testing
docs/                 ours
```

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
- Rollbacks live in `/usr/data/guppyscreen/`: `guppyscreen.orig-07409cb` is the
  upstream build that was there before we touched it, and
  `guppyscreen-backup-dev4f563d8.tar.gz` is a full directory snapshot.
- Credentials come from the environment, never from a committed file:
  `PRINTER_HOST=... PRINTER_PASS=... scripts/probe-printer.sh`
- Never commit the SSID, PSK, MAC addresses, printer serial or the Moonraker API
  key. `probe-printer.sh` strips the API key on the printer before it crosses
  the wire; the rest is on you to check.
- Its `curl` is a cut-down build that rejects `-s` and `--max-time`. Use
  `wget -q -T <sec> -O -` in anything that runs on the printer.
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

**Nothing checks whether Klipper accepted it.** `KWebSocketClient::gcode_script`
logs the payload and never looks at the reply. So any limit a panel enforces is
advisory, and a rejected command is silent in that panel. Validate before
sending rather than relying on the error coming back.

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

## Testing UI changes without touching the printer

The printer is read-only, and pressing Extrude against a live Moonraker really
does heat the hotend. Use `tools/fake_moonraker.py` for anything that would
otherwise command real hardware, and point the simulator at `127.0.0.1`:

```sh
pip install websockets
python3 tools/fake_moonraker.py 240 240            # reports 240C, accepts gcode
python3 tools/fake_moonraker.py 240 240 --reject   # rejects every command
PRINTER_HOST=127.0.0.1 scripts/build.sh sim
```

It records every `printer.gcode.script` it receives to `$GCODE_LOG`, which is
how you check what a button actually sends.

For screenshots, SDL picks Wayland when it can and the X root window is not
capturable under XWayland. Force X11 and grab the window by name:

```sh
SDL_VIDEODRIVER=x11 ./build/bin/guppyscreen &
xwd -silent -name "TFT Simulator" | xwdtopnm | pnmtopng > shot.png
```

`xdotool` can drive it, but LVGL polls its input device, so an instantaneous
click gets missed. Move, then `mousedown`, wait ~0.4s, then `mouseup`.

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
