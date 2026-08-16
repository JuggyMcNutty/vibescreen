# Inherited codebase audit

First pass over the code we adopted at upstream `07409cb` (2024-07-15). Scope is
`src/` (92 files, 457 KB) plus the build system. Vendored trees (`lvgl`,
`libhv`, `spdlog`, `wpa_supplicant`, `src/subprocess.hpp`) are excluded except
where our build touches them.

Nothing here is speculative. Every finding was read in the source and the
claims about reachability were checked rather than assumed.

Status key: **open** means we have not fixed it yet. Fixed items name the commit.

---

## What the previous developer knew

There are 15 TODO/XXX markers in `src/`, and they are worth reading as a
handover note rather than as noise. They cluster in three places:

| Location | Marker |
| --- | --- |
| `src/console_panel.cpp:190` | `// TODO: this is a race condition` |
| `src/macros_panel.cpp:53` | `// TODO: this is a race condition` |
| `src/websocket_client.cpp:48` | `// XXX: get rid of consumers and use function ptrs for callback` |
| `src/websocket_client.cpp:115,130` | `// XXX: check success, remove consumer if send is unsuccessfull` |
| `src/wpa_event.cpp:44,49` | `// XXX: replace callback?`, `// TODO: retries` |
| `src/print_status_panel.cpp:68,458` | `// XXX: check config`, `// XXX: better estimate` |
| `src/print_panel.cpp:295` | `// XXX: maybe use the directory instead of file endpoint in moonraker` |
| `src/inputshaper_panel.cpp:239` | subscribe only to the macros the panel cares about, then unregister |
| `src/printertune_panel.cpp:101` | `// TODO: handle remote guppy instance` |
| `src/setting_panel.cpp:117` | `// TODO: throw this inside the global threadpool to make it async` |
| `src/spoolman_panel.cpp:317` | `// TODO: calculate color distance` |
| `src/tree.h:123` | `// XXX: fix my index` |

The two "this is a race condition" notes are the interesting ones. They are not
two local bugs, they are two sightings of one structural problem. See C1.

---

## Correctness

### C1. `State` returns references out from under its own mutex (open)

`src/state.cpp:50-53`

```cpp
json &State::get_data(const json::json_pointer& ptr) {
  std::lock_guard<std::mutex> guard(lock);
  return data[ptr];
}
```

The guard is destroyed when the function returns, so the caller receives a raw
reference into the shared `data` object and then reads it with no lock held.
Meanwhile `State::set_data` runs `data[key].merge_patch(patch)` under the same
lock, from a different thread: `consume()` is driven by libhv's
`WebSocketClient::onmessage` (`src/websocket_client.cpp:40,66`), which runs on
the libhv event loop thread, not the LVGL thread.

`merge_patch` can rehash and reallocate the underlying object, so a reader
holding a `json&` into it is not merely reading a torn value, it can read freed
memory. That makes this a use-after-free class of bug, not a benign data race.

Two further wrinkles:

- `data[ptr]` on a `json` object **default-inserts a null** when the pointer is
  absent, so a nominal read mutates the map. Two concurrent reads of missing
  keys both write.
- There are 42 `get_data(...)` call sites. The mutex is doing nothing useful for
  any of the reference-returning ones. The value-returning accessors
  (`get_extruders`, `get_heaters`, and friends) are genuinely safe.

This is the root cause the previous developer marked twice and never fixed. Any
fix has to change the accessor contract, either returning a copy, or taking a
callback invoked under the lock, or handing back a lock-owning wrapper. It is
the largest single piece of work in this audit and should be its own change.

### C2. Empty Moonraker port field terminates the process (open)

`src/printer_select_panel.cpp:206-207`

```cpp
const char *mp = lv_textarea_get_text(p->moonraker_port);
auto port = mp != NULL ? std::stoi(mp) : 7125;
```

`lv_textarea_get_text` returns `lv_label_get_text(ta->label)`
(`lvgl/src/widgets/lv_textarea.c`), which is never null for a non-password
textarea. The null check is dead code and the 7125 fallback can never run. Clear
the port field and press save and `std::stoi("")` throws `std::invalid_argument`.

There is not a single `try` or `catch` anywhere in `src/`, and no
`std::set_terminate`, so the throw goes straight to `std::terminate` and the
process aborts. On the printer `supervise-daemon` restarts it, so the symptom is
the UI vanishing and coming back, with nothing obvious in the log.

The field is otherwise well guarded: `lv_textarea_set_accepted_chars(...,
"0123456789")` and `set_max_length(5)` at `src/printer_select_panel.cpp:184-185`
mean empty is the only bad input that can reach the parse. Cheap to fix.

### C3. Malformed theme colour aborts at startup (open)

`src/guppyscreen.cpp:70-76`, repeated at `272-276`

```cpp
auto primary_color = theme_conf->get_json("/primary_color").empty()
        ? lv_color_hex(0x2196F3)
        : lv_color_hex(std::stoul(theme_conf->get<std::string>("/primary_color"), nullptr, 16));
```

The guard tests for absent, not for parseable. A theme file with
`"primary_color": "blue"` throws out of `std::stoul` before the UI ever comes
up, and per C2 that aborts. Users are invited to edit `themes/*.json`, so this
is reachable by ordinary configuration.

### C4. `interface_ip` ignores every error it can hit (open)

`src/utils.cpp:163-174`

```cpp
std::string interface_ip(const std::string &interface) {
  int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);

  struct ifreq ifr{};
  strcpy(ifr.ifr_name, interface.c_str());
  ioctl(fd, SIOCGIFADDR, &ifr);
  close(fd);

  char ip[INET_ADDRSTRLEN];
  strcpy(ip, inet_ntoa(((sockaddr_in *) &ifr.ifr_addr)->sin_addr));
  return ip;
}
```

Four problems in eleven lines:

1. `socket()` result unchecked, then used and `close()`d. On failure that is
   `ioctl(-1, ...)` and `close(-1)`.
2. `strcpy` into `ifr.ifr_name`, which is `IFNAMSIZ` (16) bytes, with no length
   check. The name arrives from `get_wifi_interface()`
   (`src/utils.cpp:176-187`), which returns a filename from the configurable
   `wpa_supplicant` directory, so it is config-influenced rather than fixed.
3. `ioctl` result unchecked. If `SIOCGIFADDR` fails, `ifr.ifr_addr` is the
   zero-initialised value from `ifreq ifr{}` rather than a real address, and the
   function confidently returns "0.0.0.0".
4. The second `strcpy` is safe in practice, `inet_ntoa` returns at most 15
   characters plus a terminator into a 16 byte buffer, but it is one refactor
   away from not being.

Not fixed in `pellcorp/grumpyscreen` either; they moved the identical code to
`src/net_utils.cpp:37-45`.

### C5. Unguarded numeric parsing elsewhere (open, lower severity)

22 `std::sto*` call sites, none guarded. Beyond C2 and C3 the reachable ones are:

- `src/spoolman_panel.cpp:419` parses a spool colour from the Spoolman API
  response. A non-hex colour from a third-party service aborts the UI.
- `src/wifi_panel.cpp:186` parses the signal level out of a wpa_supplicant
  `SCAN_RESULTS` line. Correctly guarded by `wifi_parts.size() == 5` first, and
  the field is always numeric in practice, so low risk.
- `src/numpad.cpp:86` `std::stod` on textarea contents. Worth noting that this
  looked alarming and is not: the numpad installs a custom keymap of digits,
  backspace and OK only (`src/numpad.cpp:31`), so the only way to throw is to
  enter 300-plus digits and overflow a double. The `// input validation, e.g.
  range` comment directly above it is still an unkept promise.

The systematic fix is a small `parse_int`/`parse_double` helper in `KUtils` that
returns a default, plus a `try`/`catch` around the top level so an unexpected
throw logs instead of vanishing.

---

## Build system

### B1. Sub-makes forked one compiler per file (fixed, `9e6d564`)

All four sub-makes passed `-j$(nproc)`. Make expanded `$(nproc)` itself, found no
variable of that name, substituted nothing, and left a bare `-j`. Plausible
cause of upstream issue #52, "Cross Compile Fails" (17 comments).

### B2. C++ sources compiled with the C driver (fixed, `f6465a5`)

`COMPILE_CXX` invoked `$(CC)`.

### B3. `-static` applied to the simulator too (fixed, `4eabfc7`)

Broke the SDL build, since distributions ship no static SDL2.

### B4. Submodule patches were manual and not idempotent (fixed, `fe2b9dc`)

Three patches applied by hand with `git apply`, silently reverted by any
re-clone or submodule update, with no way to tell whether they had run.

### B5. `DEVELOPMENT.md` documents the wrong toolchain (open)

It says to use the Ingenic `mips-gcc720-glibc229` toolchain. That produced the
last tagged release, `0.0.26-beta`, which is dynamically linked against
`/lib/ld-linux-mipsn8.so.1`. Every nightly since `a42427cb` is built by CI with
a Bootlin musl toolchain and is fully static. Following the doc gets you a
binary that only runs on firmware shipping glibc 2.29. Needs rewriting to match
`.github/workflows/build.yml`.

### B6. `lv_tc_screen.c` pointer types (fixed, `810a1dc`)

Two `lv_tc_screen_t*` values passed to `lv_obj_t*` parameters, working only
because the object is the first struct member. Hard error under gcc 14 and the
only source change needed to build the tree with it.

---

## Maintainability

### M1. 98 lines of commented-out code across 28 files (open)

Worst: `src/numpad.cpp` and `src/extruder_panel.cpp` (13 each),
`src/websocket_client.cpp`, `src/macro_item.cpp`, `src/image_label.cpp` (7 each).
Git remembers; these should go. Low risk, do it in one sweep per file so the
diffs stay reviewable.

### M2. Four leaked singletons (open, cosmetic)

`Config`, `GuppyScreen`, `ThemeConfig` and `State` are each `instance = new ...`
with no matching `delete` (`src/config.cpp:18`, `src/guppyscreen.cpp:45`,
`src/theme.cpp:18`, `src/state.cpp:40`). They live for the whole process, so
nothing leaks in practice. Only worth touching if we ever want a clean shutdown
path or to run the UI under a leak checker.

### M3. Logging before the logger exists (open, cosmetic)

`src/main.cpp:40` calls `spdlog::debug` before `Config::init` and before
`GuppyScreen::init` install the sinks and set the level, so that line goes to
the default logger and is normally dropped. Either move it after init or delete
it.

### M4. K1 paths hardcoded into non-K1 builds (open)

`src/main.cpp:44` passes `/usr/data/printer_data/thumbnails` as the thumbnail
root unconditionally, and `src/config.cpp:72` defaults `log_path` to
`/usr/data/printer_data/logs/guppyscreen.log`. Both are wrong for the simulator
and for the Debian target. Worked around today by generating a config in
`scripts/build.sh`, but the defaults should be platform-conditional.

---

## On mining `pellcorp/grumpyscreen`

Worth recalibrating the expectation here. The fork is 233 commits ahead and 0
behind, which sounds like a large pool of fixes to cherry-pick. A good part of
that lead is deletion. It carries 72 files in `src/` against our 92, having
dropped:

`bedmesh_panel`, `belts_calibration_panel`, `inputshaper_panel`, `limits_panel`,
`macros_panel`, `macro_item`, `power_panel`, `printer_select_panel`,
`printertune_panel`, `theme`, `tmc_status_container`, `tmc_status_panel`,
`tmc_tune_panel`, `config.cpp`

Those are exactly the features that matter on a K1 Max: bed mesh, input shaper,
belt calibration, TMC tuning, multi-printer and theming. It has been narrowed to
one printer and one firmware.

So it stays a useful reference for the parts that overlap, especially touch
calibration, wifi and the lv_drivers work, and it is not a source we can bulk
merge from. Of the correctness findings above, it fixes none: C4 is present
there verbatim, and C1, C2 and C3 live in files it deleted.

Two concrete things it has that we could want later: patches folded directly
into forked `lvgl` and `lv_drivers` submodules instead of a `patches/`
directory, and a Python Klipper simulator for local testing.

---

## Suggested order

1. C2 and C3, small and self-contained, remove two ways to abort the UI.
2. A top-level `try`/`catch` plus `KUtils` parse helpers, which caps the blast
   radius of C5 generally.
3. C4, contained to one function.
4. B5, so the next person is not misled by the docs.
5. M1 and M3, mechanical.
6. C1 last. It is a real design change and deserves its own branch and careful
   review.
