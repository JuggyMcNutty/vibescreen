# Inherited codebase audit

First pass over the code we adopted at upstream `07409cb` (2024-07-15), kept up
to date as findings are fixed. Scope is `src/` plus the build system. Vendored
trees (`lvgl`, `libhv`, `spdlog`, `wpa_supplicant`, `src/subprocess.hpp`) are
excluded except where our build touches them.

Nothing here is speculative. Every finding was read in the source and the
claims about reachability were checked rather than assumed.

Bugs that upstream's own issue tracker reported are triaged separately in
`docs/upstream-issues.md`, which says for each one whether it survives in our
tree and where. This file is the code audit; that one is the inbox.

Status key: **open** means we have not fixed it yet. Fixed items name the commit.

---

## What the previous developer knew

There are 13 TODO/XXX markers in our own `src/`, and they are worth reading as a
handover note rather than as noise. They cluster in three places. No line
numbers here on purpose: the marker text is exact, so `grep` finds them, and a
line number is one commit away from pointing at something else.

| File | Marker |
| --- | --- |
| `src/websocket_client.cpp` | `// XXX: get rid of consumers and use function ptrs for callback` |
| `src/websocket_client.cpp` | `// XXX: check success, remove callback if send is unsuccessfull` |
| `src/wpa_event.cpp` | `// XXX: replace callback?`, `// TODO: retries` |
| `src/print_status_panel.cpp` | `// XXX: check config`, `// XXX: better estimate` |
| `src/print_panel.cpp` | `// XXX: maybe use the directory instead of file endpoint in moonraker` |
| `src/inputshaper_panel.cpp` | subscribe only to the macros the panel cares about, then unregister |
| `src/printertune_panel.cpp` | `// TODO: handle remote guppy instance` |
| `src/spoolman_panel.cpp` | `// TODO: calculate color distance` |
| `src/tree.h` | `// XXX: fix my index` |

It was 15 when this was written and 11 now. `fc12faa` retired one of the two
"check success" markers by actually checking the reply, and the survivor now
says "callback" where it used to say "consumer". The `setting_panel.cpp`
threadpool marker went with the update dialog, which runs `update.sh` on its own
thread.

The two "this is a race condition" notes were the interesting ones. They were
not two local bugs but two sightings of one structural problem, and both went
with C1: dispatch runs on the LVGL thread now, so there is no second thread for
either of them to race with.

---

## Correctness

### C1. `State` returns references out from under its own mutex (fixed)

`src/state.cpp:66-69`

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
`WebSocketClient::onmessage` (`src/websocket_client.cpp:41,73`), which runs on
the libhv event loop thread, not the LVGL thread.

`merge_patch` can rehash and reallocate the underlying object, so a reader
holding a `json&` into it is not merely reading a torn value, it can read freed
memory. That makes this a use-after-free class of bug, not a benign data race.

Two further wrinkles:

- `data[ptr]` on a `json` object **default-inserts a null** when the pointer is
  absent, so a nominal read mutates the map. Two concurrent reads of missing
  keys both write.
- There are 51 `get_data(...)` call sites. The mutex is doing nothing useful for
  any of the reference-returning ones. The value-returning accessors
  (`get_extruders`, `get_heaters`, and friends) are genuinely safe.

This is the root cause the previous developer marked twice and never fixed.

Fixed by moving dispatch rather than by changing the accessor. With `consume()`
and every `get_data` caller on the LVGL thread, the reference cannot be
invalidated under a reader, and none of the three options this entry proposed
was needed. Both `// TODO: this is a race condition` markers are gone with it.

The default-insert was a separate bug and is fixed on its own terms:
`get_data` returns `const json&` now and answers a missing pointer with a
shared empty json instead of `data[ptr]`. Making it const is what proves no
caller was writing through it, and the compiler found the five that bound a
mutable reference. All five turned out to be reads.

It found two real ones on the way. `MacrosPanel` read a macro's hidden flag
with `operator[]`, so listing macros wrote a null into `State` for every macro
nobody had hidden. And `KUtils::parse_macros` did the same with `/gcode`,
inserting into whatever it was handed. Both use `contains` and `at` now.

Worth knowing for anything similar: `operator[]` with a `json_pointer` on a
**const** json does not insert, it asserts, and with `NDEBUG` that assert is
gone and it dereferences `end()`. So const does not make `operator[]` safe, it
makes it worse. `contains` then `at` is the only correct pair.

`State`'s mutex stays. It guards nothing against a second thread any more, but
it costs an uncontended lock and the alternative is a public accessor with no
protection at all if something is ever dispatched from elsewhere again.

### C2. Empty Moonraker port field terminates the process (fixed, `bdbfa03`)

`src/printer_select_panel.cpp`, as it was before the fix:

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
"0123456789")` and `set_max_length(5)` at `src/printer_select_panel.cpp:185-186`
mean empty is the only bad input that can reach the parse. Cheap to fix.

### C3. Malformed theme colour aborts at startup (fixed, `0886341`)

`src/guppyscreen.cpp`, in two places, as it was before the fix:

```cpp
auto primary_color = theme_conf->get_json("/primary_color").empty()
        ? lv_color_hex(0x2196F3)
        : lv_color_hex(std::stoul(theme_conf->get<std::string>("/primary_color"), nullptr, 16));
```

The guard tests for absent, not for parseable. A theme file with
`"primary_color": "blue"` throws out of `std::stoul` before the UI ever comes
up, and per C2 that aborts. Users are invited to edit `themes/*.json`, so this
is reachable by ordinary configuration.

### C6. Manual moves leaked gcode modes into a running print (fixed, `ff6dfad` and `8719a33`)

Found during the extruder round, and the worst thing in this file after C1.

`ExtruderPanel` sent `M109 S<t>` then a bare `M83` then the move. `HomingPanel`
sent a bare `G91` then the move. Neither ever set the mode back, and nothing
anywhere in the tree emitted `M82` or `G90` or used `SAVE_GCODE_STATE`.

Both change gcode state **globally**, not just for the next command. Pause a
print, purge or jog from the panel, resume, and the rest of that print runs in
relative extrusion or relative positioning. Slicers emit `M82`/`M83` and
`G90`/`G91` once in the start gcode and never again, so nothing corrects it. For
a job sliced with absolute E, every subsequent `G1 E<absolute>` is then read as a
relative extrusion, which ruins the remainder of the print.

Both now wrap the move in `SAVE_GCODE_STATE` and `RESTORE_GCODE_STATE`, which
defaults to `MOVE=0` and so restores the mode without moving the toolhead.

Worth a general note: any future panel that emits a modal gcode needs the same
treatment. `M104`, `M106`, `SET_VELOCITY_LIMIT` and friends are all sticky.

### C7. `M109` can block the queue until `verify_heater` faults (open)

`ExtruderPanel` waits for temperature with `M109`, which blocks the gcode queue
until the target is reached. Now that the range reaches 320C this matters more:
selecting a target the hotend cannot physically achieve means `M109` never
returns, and `verify_heater` eventually shuts the printer down. On our K1 Max
that is `max_error: 120`, `heating_gain: 2.0`, `check_gain_time: 20`.

Clamping to `max_temp` does not solve this. `max_temp` is what the config
permits, not what the hardware can reach, and those are different numbers on a
stock hotend with a 350C `max_temp`.

`ff6dfad` reduced the exposure by skipping `M109` when already at the selected
target, so repeat presses no longer re-wait. The real fix is non-blocking
`M104` plus a heating state in the UI, which is a behaviour change worth doing
deliberately rather than as a side effect.

### C12. Bed mesh Calibrate homed the wrong way round (fixed)

`BedMeshPanel::handle_callback` skipped `G28` only when `homed_axes` was
exactly the string `"xy"`, which got it wrong in both directions.

Klipper reports `"xyz"` on a fully homed machine, the normal state after any
print, so the common case fell through to `G28 X Y Z\nBED_MESH_CALIBRATE` and
re-homed all three axes for nothing. And in the one state the test did match,
X and Y homed but not Z, it skipped homing and went straight to a probe that
cannot run: the probe lifts to `horizontal_move_z` first, and Klipper refuses a
Z move on an unhomed Z.

Now homes unless all three of x, y and z are present, and sends a bare `G28`
rather than `G28 X Y Z` when it does. Both branches were driven against
`tools/fake_moonraker.py` with `homed_axes` set each way.

### C13. Input shaper wrote a shaper type nobody chose (fixed)

`find_shaper_index` returned `std::distance` to `cend()` for a name not in its
list, which is one past the end, and `lv_dropdown_set_selected` clamps an out of
range index to the last option rather than rejecting it
(`lvgl/src/widgets/lv_dropdown.c:285`). An unrecognised shaper therefore
displayed as `3hump_ei`, and Save wrote `3hump_ei` into `printer.cfg` at the
original shaper's frequency, silently.

`zvd` was hitting that on every machine set to it. It is legal in
`[input_shaper]` and it is in `shaper_defs.py`, but the panel's list left it out
because `k1/scripts/shaper_calibrate.py:16` leaves it out of `AUTOTUNE_SHAPERS`,
so a guppy calibration never proposes it. Klipper's own `SHAPER_CALIBRATE` does,
and the K1's stock `INPUT_SHAPER_CALIBRATION` macro runs exactly that.

The lookup now fails rather than clamps, and an unknown name disables Save.

### C14. A failed resonance run span forever (fixed)

The spinner was only ever cleared by success. A rejected `TEST_RESONANCES`, an
accelerometer that stopped answering, an analysis that died before printing its
result, a shell command that hit its timeout, Klipper disconnecting: every one
of them left the panel waiting on something that was never coming, with no way
out but Back, and it came back in that state.

Every ending is handled now, from the `!!` prefix Klipper broadcasts errors with
and the terminal lines in `k1/k1_mods/gcode_shell_command.py`, plus a watchdog
for when nothing arrives at all.

`BeltsCalibrationPanel` had the same shape and has since been given the same
treatment, along with the preconditions of C16 and the one-at-a-time sequencing
of C15. It also drives its two sweeps itself rather than through
`GUPPY_BELTS_SHAPER_CALIBRATION`, which sent both back to back with only `M400`
between them. `M400` waits for moves and not for the accelerometer writer, so
two full datasets were held at once, which is what upstream #104 reports as an
MCU timeout part way through the second belt.

Worth recording one thing found while doing it: `gcode_shell_command.py` prints
"Command {x} finished" whenever the process exits, crash included, so
"finished" on its own never meant success. The panel keys on the similarity
line `graph_belts.py` prints on its way to writing the plot, the same shape as
the input shaper keying on its `{"shapers":...}` payload.

### C15. Input shaper queued both axes at once (fixed)

Both `TEST_RESONANCES` went out together. Klipper runs one command at a time, so
the analysis sent when X's data landed queued behind the Y test that was already
waiting, and X's result could not appear until Y had finished shaking: about
five minutes on the development printer at its configured `hz_per_sec` of 1.0.
Each axis is now sent when the one before it has produced a result.

### C16. Nothing checked the printer could run a resonance test (fixed)

Calibrate and Save went out unconditionally, against the rule in AGENTS.md that
Klipper abandons the rest of a script at the first command it will not run.

Finding the preconditions is the part worth remembering.
`printer.objects.list` only reports objects implementing `get_status`, and
measured on the development K1 Max neither `resonance_tester` nor `adxl345`
appears in it despite both being configured, so `KUtils::has_gcode_macro`'s
source cannot answer this. `configfile` carries a section either way, which is
what `KUtils::has_config_section` reads.

Re-measured 2026-08-18: `calibrate_shaper_config` **does** appear in the object
list, which this entry originally said it did not. Our module defines a
`get_status` and it returns `{}`, so the list confirms the object exists and
carries no values. That does not change the conclusion, since the panel needs
the configured shaper values and only `configfile` has them.

### C17. `SAVE_INPUT_SHAPER` silently disabled the axis it was not given (fixed)

`k1/k1_mods/calibrate_shaper_config.py` defaulted a missing parameter to its own
`[calibrate_shaper_config]` section, which nobody fills in. Measured, it reports
`mzv` at 0.0 Hz while the machine runs `ei` at 40.3 and `zv` at 46.9, and a
shaper frequency of zero turns that axis's shaping off. So a single axis save
disabled the other one, and a bare `SAVE_INPUT_SHAPER` disabled both.

The panel always sent all four parameters, so it never triggered this. It is a
trap for anything else calling the command, and this is a module we ship.

Defaults now come from `[input_shaper]` read through `configfile`. The
`input_shaper` object cannot answer for itself: it reports an empty status on
the Klipper the K1 ships.

### C11. Panel destructors double-delete their own widgets (fixed)

Every panel destructor calls `lv_obj_del` on its root container, and its widget
members' destructors then call `lv_obj_del` on objects that root already
deleted as children. `~BedMeshPanel` deletes `cont`, which takes
`controls_cont` and every `Selector` under it with it, and then `~Selector`
deletes `selector_cont` again (`src/selector.cpp:66-71`). `ImageLabel` and
the panel-owned `lv_obj_t*` members have the same shape: 36 destructors
still `lv_obj_del` a container their parent may already have taken.

`~ButtonContainer` used to be the example here and no longer is: it is empty
now (`src/button_container.cpp:72-73`). The pattern is unchanged, only the
one instance that happened to be named.

Nothing hits it today because the panels are members of `MainPanel` and
`PrinterTunePanel` and live for the whole process, so no panel is ever
destroyed. It becomes real the moment anything is torn down, which is exactly
what the multi-printer switch in `PrinterSelectPanel` would want to do.

`MeshView` avoided it by nulling its handle from an `LV_EVENT_DELETE` callback
(`src/mesh_view.h`), which works whichever order the two deletes happen in.
That is now `KWidget::null_on_delete` in `src/widget_handle.h`, called from the
constructor of all seventeen classes that delete a container they did not
create at screen level.

One thing the shared version has to do that `MeshView`'s did not: check that
the event target is the container itself. `LV_EVENT_DELETE` from a child
carrying `LV_OBJ_FLAG_EVENT_BUBBLE`, which `ButtonContainer` sets on its
button, arrives at the parent's callback too. Nulling on that would leave the
container alive with nothing left to delete it, turning a double free into a
leak.

Still latent in the sense that nothing tears a panel down, so the fix cannot be
demonstrated failing first. What is live is the destructor path itself:
`FanPanel` and `LedPanel` clear and rebuild their `SliderContainer`s, and
`MacrosPanel` its `MacroItem`s, on every refresh. Both were driven in the
simulator and both still draw.

### C18. Startup crashes about one time in six, in the connect callback (open)

`InitPanel::connected` runs on the libhv websocket thread and builds widgets
from there. Measured 2026-08-17 against `tools/fake_moonraker.py`: one startup
in six segfaults, always with the same shape.

```
#0  obj_valid_child          <- the object tree is already inconsistent
#1  lv_obj_is_valid
#2  lv_obj_get_screen
#3  lv_obj_mark_layout_as_dirty
#4  lv_obj_class_init_obj
#5  lv_obj_create
#6  SensorContainer::SensorContainer
#7  MainPanel::create_sensors
#8  InitPanel::connected(...)::{lambda}
#9  KWebSocketClient::connect(...)::{lambda}     <- websocket thread
#10 on_frame_end(websocket_parser*)
```

A second shape appears when something is deleted afterwards: `lv_obj_del` ->
`lv_obj_destructor` -> `_lv_event_mark_deleted` walking `event_head`, LVGL's
global chain of in-flight events, into a pointer that is plainly not one. Both
say the same thing, that LVGL's own globals have been written by two threads.

The odd part is that `MainPanel::create_sensors`, `MainPanel::init`,
`PowerPanel::create_devices` and every `consume` do take `lv_lock`, and it is
the same mutex the main loop holds around `lv_timer_handler`. So the hole is
somewhere the lock is not taken rather than the lock being wrong.
`SettingPanel::enable_spoolman` was one confirmed hole, calling
`ButtonContainer::enable` with no lock, though that path needs a printer
running spoolman and was not what crashed here. `MainPanel::enable_spoolman`
takes the lock now, which closes that one; the crash above is a different path
and is still open.

Reproduce with `scripts/build.sh sim` against the fake, opening a panel a few
seconds after start, six runs. It is not specific to any panel or to anything
the user does: the run above only navigated between panels and opened no
dialog. An earlier crash from before the update dialog was written fits the
same picture but left a stack too corrupt to read, so it proves nothing.

This is the same disease as C1 and C9 and wants the same cure: dispatch onto a
queue drained by the LVGL loop, so that no websocket callback ever touches a
widget. Until then, every new websocket-thread path needs `lv_lock` and an
audit of the ones already there.

### C19. `WpaEvent` shares one control socket across two threads (fixed)

Found while fixing the wifi panel, and left alone deliberately.

`WpaEvent::send_command` (`src/wpa_event.cpp`) writes to a single
`struct wpa_ctrl *conn` with nothing guarding it. Two threads reach it. The
LVGL thread does, from `WifiPanel::foreground`, from selecting a row, and now
from the Forget button. The libhv event loop thread does too, from
`handle_wpa_event` calling `SCAN_RESULTS` and `LIST_NETWORKS` while handling an
unsolicited event.

`wpa_ctrl_request` writes a command and then reads the reply off the same
datagram socket, so two overlapping calls can each take the other's answer. The
window is small, which is presumably why nobody has noticed: a scan result
arriving in the same moment as a button press.

Same cure as C1 and C18, and it got it. `handle_wpa_events` queues the event
string and `WpaEvent::drain` runs the callbacks, called from an `lv_timer`
owned by `WifiPanel`. `send_command` then has one caller thread and the
overlapping requests cannot happen.

The `callbacks` map went with it: it was written from the LVGL thread at
registration and read from the wpa thread on every event, which was a second
unguarded container nobody had noticed.

The part worth remembering is what draining from an `lv_timer` requires.
`lv_timer_handler` is called with `lv_lock` already held by the main loop, so
the three `lv_lock` acquisitions inside `WifiPanel::handle_wpa_event` had to
go: taking a non-recursive mutex the caller already holds is a deadlock, and
the panel would have hung on its first scan result rather than failed loudly.
`WifiPanel` no longer takes a `std::mutex&` at all. This is the same reason
`UpdateDialog`'s worker does not take `lv_lock`, noted there since it was
written.

Driven against `tools/fake_wpa_supplicant.py`: the network list populates from
a scan result, which only reaches the table through the queue, and selecting a
network still sends `LIST_NETWORKS` and brings up the password prompt.

### C20. Reading a missing config key writes it back as null (fixed)

`Config::get_json` is one line:

```cpp
return data[json::json_pointer(json_path)];
```

`nlohmann::json::operator[]` inserts a default-constructed null for a pointer
that is not there, so **reading an absent option mutates the config**, and the
next `Config::save()` writes that null to disk. Any setting saved afterwards
carries the nulls with it.

Found on 2026-08-19 while adding the update check: the config came back with
`"update_check_interval_hours": null` after a run in which nothing had set it.

There are 37 `get_json` call sites outside `config.cpp`, and every one that
reads an optional key can do this: `/invert_z_arrows`, `/prompt_emergency_stop`,
`/touch_calibrated`, `/touch_calibration_coeff`, `/display_rotate`, `/theme`,
`/primary_color`, `/secondary_color`, and the per-printer keys reached through
`conf->df()`. It is not new. It has been quietly seeding nulls into people's
config files for as long as those keys have been optional.

Harmless so far, because every caller treats null as "not set" and falls back to
a default, which is also why it went unnoticed. It stops being harmless the day
a caller distinguishes null from absent, or a config is read by something
stricter than we are.

`9bbe3ec` mitigated it for two keys by seeding them in `Config::init`.

The fix turned out to be much smaller than this entry assumed. It proposed
either a new `get_json_or` with 37 call sites to move, or seeding every
documented default at load. Neither was needed. Of the 39 `get_json` sites,
only seven bind the returned reference, and all seven read it. So `get_json`
now returns `const json&`, and returns a shared empty json rather than
`data[ptr]` when the key is absent. Changing the return type to const is what
makes that safe to assert rather than hope: the compiler rejects any caller
that would write through it, and none did.

The `get<T>` template had the same defect and is fixed the same way, falling
back to a null json so behaviour is unchanged for every caller: `get<json>` on
a missing key still yields null, everything else still throws on it.

`Config::init`'s seeding block stays. It is doing a different job, giving new
installs sensible values rather than protecting reads.

Driven in the simulator on 2026-08-20: with `invert_z_arrows` absent, the
system panel rendered its toggle from that key and then a settings change wrote
the config. The key was not inserted and the file came back with no null values
anywhere in it.

### C8. Rejected gcode is invisible to the user (fixed, `fc12faa`)

`KWebSocketClient::gcode_script` used to fire and forget: it logged the
outgoing payload and never inspected the reply. This was the previous
developer's own `// XXX: check success` in `src/websocket_client.cpp`, one of
the two markers that carried it.

The consequence was that every limit the UI enforced, including the clamping
added in `ff6dfad`, was advisory. If Klipper rejected a command for a reason we
had not modelled, the panel showed nothing at all. The console panel saw the
error, the panel that sent it did not.

It now checks the reply and routes a rejection to the error handler
(`src/websocket_client.cpp:228`). **That is a backstop, not a licence to send
speculatively.** Klipper abandons the rest of a script at the first command it
refuses, so anything after the bad line silently never runs. Validate first;
see the gcode section of `AGENTS.md`.

### C9. Websocket client's shared state was unguarded across threads

The container races are **fixed** in `c042ae6`. The handler lifetime hazard
below them is **still open**. The two used to share a heading that said only
"fixed", directly above a paragraph beginning "Still open".

`callbacks` and `consumers` were plain `std::map` and `notify_consumers` a plain
`std::vector`, all mutated from two threads with nothing between them. Panels
insert from the LVGL thread on button presses, libhv's `onmessage` erases from
its own event loop thread, and panel constructors and destructors register and
unregister while `onmessage` iterates. Concurrent mutation of a red-black tree
corrupts it, so the symptom was a crash or hang inside `std::map` with a stack
pointing nowhere useful.

Two related bugs went with it. The request id was read separately from the map
insert, so a concurrent send could consume the id a handler had just been filed
under and the reply would go to the wrong caller. And
`unregister_notify_update` called `erase(remove_if(...))` with no end iterator,
which is undefined when the consumer is not in the list.

**The lifetime hazard is closed too**, by the same queue as C1 and C18.
Handlers are still invoked after the lock is released, but the thread that
copies them out and the thread that could destroy a panel are now the same one,
so there is no window between the copy and the call for a panel to disappear
in.

There is a reproducible instance of it on the simulator's exit path, found
2026-08-20. Closing the window or sending `SIGTERM` prints

```
pure virtual method called
terminate called without an active exception
```

We install no signal handler, so the default action should kill the process
outright and run nothing. SDL installs one, and `lv_drivers/sdl/sdl.c:296`
calls `exit(0)` from its quit filter, which runs static destructors while the
libhv event loop thread and `WpaEvent`'s own thread are both still live. A
consumer whose vtable has already been torn down then takes a `consume()` call,
which is exactly the lifetime hazard above, and a `std::thread` still joinable
at its own destruction is the second line.

Simulator only: the fbdev and evdev path has no quit filter, and on the printer
`supervise-daemon` kills the process rather than asking it to leave. Recorded
because it is a way to watch the hazard happen on demand rather than waiting
for it, and because a clean shutdown wants the websocket thread stopped before
anything it dispatches into is destroyed.

### C10. Exceptions could not be contained outside LVGL (fixed, `5afb5b8`)

Worth recording in full because two reasonable-looking fixes were built and
measured before the third one worked.

Catching in the main loop around `lv_timer_handler` does not work.
`lvgl/src/misc/lv_timer.c` sets a re-entrancy guard on entry and clears it on
exit, and an exception unwinding past the clear leaves it set. Every later call
then returns at the guard. The process survives with a **frozen UI**, which is
worse than crashing, since the init script restarts a dead process but never a
wedged one.

Patching LVGL to release the guard from the catch does release it, confirmed by
the handler running again, but recovery still fails. The input state machine is
also interrupted mid gesture and re-dispatches the same event forever. One
injected throw produced **215 exceptions** and no further input was accepted.

So the exception must never leave our code. `KGuard::event`
(`src/event_guard.h`) wraps the body of each of the 61 static trampolines LVGL
calls into. The same injected throw then produces exactly one log line, nothing
reaches the main loop, and the next tap works.

Also relevant: `-funwind-tables` is needed on the C sources. mips does not emit
unwind tables for C by default, so without it an exception cannot unwind through
LVGL at all and calls `std::terminate`. x86-64 does emit them by default, so
anything relying on unwinding would test green in the simulator and do nothing
on the printer. Costs 64KB.

**The residual is closed.** Every `add_event_cb` callback in the tree now
either carries `KGuard::event` or is a `cb` handed in by the caller, and both
of those callers pass a guarded `&Class::_handler`.

Two things this file got wrong, found while doing it.

`slider_cb` was listed as an unguarded named callback. It is a parameter of
`SliderContainer`'s constructor, not a function, and the only two callers pass
`&FanPanel::_handle_fan_update_*` and `&LedPanel::_handle_led_update_generic`,
which are guarded. Nothing to fix there.

The two callbacks in `lv_touch_calibration/lv_tc_screen.c` were dismissed as
calling nothing that throws. The accept button reaches `lv_tc_save_coeff`,
which calls our registered `GuppyScreen::save_calibration_coeff`, which opens a
file and serialises json. That can throw, and it would unwind through two C
frames into LVGL. `KGuard::event` is a C++ template so it cannot go in that C
file; the guard is on `save_calibration_coeff` instead, which is the boundary
the exception would have to cross. The two C callbacks carry a comment saying
so, since an unguarded LVGL entry point otherwise reads as an oversight.

To recount if it ever needs recounting: every `add_event_cb` whose callback is
neither `&Class::_name` nor a lambda opening with `KGuard::event`. The one
deliberate exception is `ButtonContainer`'s prompt confirm, which puts the
guard inside the lambda so `lv_msgbox_close` still runs on a throw.

### C4. `interface_ip` ignores every error it can hit (fixed, `4027abe`)

`src/utils.cpp`, as it was before the fix:

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
   (`src/utils.cpp:382-393`), which returns a filename from the configurable
   `wpa_supplicant` directory, so it is config-influenced rather than fixed.
3. `ioctl` result unchecked. If `SIOCGIFADDR` fails, `ifr.ifr_addr` is the
   zero-initialised value from `ifreq ifr{}` rather than a real address, and the
   function confidently returns "0.0.0.0".
4. The second `strcpy` is safe in practice, `inet_ntoa` returns at most 15
   characters plus a terminator into a 16 byte buffer, but it is one refactor
   away from not being.

Not fixed in `pellcorp/grumpyscreen` either; they moved the identical code to
`src/net_utils.cpp:37-45`.

### C5. Unguarded numeric parsing elsewhere (partly fixed, lower severity)

14 `std::sto*` call sites, none guarded. Beyond C2 and C3 the reachable ones are:

- `src/spoolman_panel.cpp:437` parses a spool colour from the Spoolman API
  response. A non-hex colour from a third-party service aborts the UI.
  **Fixed in `0886341`.**
- `src/wifi_panel.cpp:300` parses the signal level out of a wpa_supplicant
  `SCAN_RESULTS` line. Correctly guarded by `wifi_parts.size() == 5` first, and
  the field is always numeric in practice, so low risk.
- `src/numpad.cpp:86` `std::stod` on textarea contents. Worth noting that this
  looked alarming and is not: the numpad installs a custom keymap of digits,
  backspace and OK only (`src/numpad.cpp:31`), so the only way to throw is to
  enter 300-plus digits and overflow a double. The `// input validation, e.g.
  range` comment directly above it is still an unkept promise.

The helpers now exist: `KUtils::parse_int`, `parse_double` and `parse_hex`
(`src/utils.cpp`), which log and return a fallback, and additionally reject
trailing garbage that plain `std::stoi` accepts. Converted so far are C2, C3,
the Spoolman swatch, the extruder speed and the input shaper. The remaining
sites are the low risk ones listed above plus the TMC tune, finetune and print
status parses, which all read numbers Klipper itself produced, and four that
read numbers something else produced: `src/spoolman_panel.cpp:371` and `:429`
take a spool id straight from Spoolman, and `src/utils.cpp:259` and `:264`
take thumbnail widths straight from Moonraker's metadata.

Still wanted: a `try`/`catch` at the top level so an unexpected throw anywhere
logs rather than vanishing.

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

### B5. `DEVELOPMENT.md` documents the wrong toolchain (fixed, `5658e14`)

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

### B7. The arm release shipped a systemd unit pointing at a missing binary (fixed)

Found on 2026-08-20 while building the arm target for the first time on this
machine.

`debian/disable_blinking_cursor.service` runs
`/home/biqu/guppyscreen/debian/kd_graphic_mode`, and `release.sh` copies that
binary into the `debian/` directory of every asset. But `scripts/build.sh` only
builds it for mips:

```sh
if [ "$target" = "mips" ]; then
    make kd_graphic_mode
fi
```

So the arm build never produces it, `release.sh`'s `cp` prints
`cannot stat './build/bin/kd_graphic_mode'`, and nothing checks that copy, so
the asset is published anyway. Every `guppyscreen-arm.tar.gz` we have released
carries a unit file that cannot start.

It is a five line program doing one `KDSETMODE` ioctl, nothing mips specific
about it. It is built for every cross target now, so each asset's copy matches
its own architecture, and `release.sh` runs under `set -euo pipefail` so a
missing file fails the packaging instead of printing and publishing anyway.

Two more things fell out of fixing it, and the unit would still not have worked
without either.

**The unit hardcoded one user's home.** `ExecStart` was
`/home/biqu/guppyscreen/debian/kd_graphic_mode`, `biqu` being the BTT Pad's
default account, while the `guppyscreen.service` beside it uses `<USER>` and
gets templated by `install_services`. So even with the binary present the unit
pointed at nothing on any machine with a different account. It takes `<USER>`
now and `installer-deb.sh` fills it in.

**It was linked with no flags at all**, so unlike everything else we ship it
came out dynamic. Built against Arm's toolchain that gives a binary demanding
`GLIBC_2.34`, which does not exist on a Debian 11 arm64 image, so it would have
failed to start on exactly the older machines most likely to be running this.
The rule passes `$(LINK_MODE)` now, and not `$(LDFLAGS)`, since this program
needs libhv and spdlog about as much as it needs a websocket.

Existing Debian installs keep the broken unit until `installer-deb.sh` is run
again. `update.sh` deliberately no-ops on a Debian install, so it will not
repair this on its own.

---

### B8. libhv's mbedTLS backend cannot verify a certificate (upstream, patched)

Found on 2026-08-21 while compiling mbedTLS in. Carried as
`patches/0004-libhv-mbedtls-ca.patch`, so it is ours to re-check on every libhv
bump.

`libhv/ssl/mbedtls.c` takes an `hssl_ctx_opt_t`, which has `ca_file` and
`ca_path` fields, and **never reads either of them**. It reaches
`mbedtls_ssl_conf_ca_chain` only inside `if (check)`, which is set when a
`key_file` was supplied, i.e. when configuring a server's own certificate. A
client therefore has no trust anchors at all, and both of the settings it can
end up with are wrong:

- `verify_peer = 0`, which is what `hssl_ctx_new(NULL)` produces, sets
  `MBEDTLS_SSL_VERIFY_NONE`. The connection is encrypted and completely
  unauthenticated. That is worse than plain `ws://`, because it looks secure.
- `verify_peer = 1` sets `MBEDTLS_SSL_VERIFY_REQUIRED` against an empty chain,
  so every handshake fails.

The openssl backend sitting next to it in the same directory handles both
fields, and falls back to `SSL_CTX_set_default_verify_paths`. The mbedTLS one
was simply never finished. Still true at v1.3.4 and on `master`.

The patch adds a second `mbedtls_x509_crt` to the context for trust anchors,
parses `ca_file` and `ca_path` into it, and installs it with
`mbedtls_ssl_conf_ca_chain`. Two details in it are deliberate:

- It tests the parse result for `< 0`, not `!= 0`. `mbedtls_x509_crt_parse_file`
  returns the **number of certificates it could not parse**, so a bundle with
  one certificate this build dislikes would otherwise be rejected whole.
- It leaves `mbedtls_ssl_conf_ca_chain(&ctx->conf, ctx->cert.next, NULL)` in the
  server branch alone. That looks like an off-by-one but is mbedTLS's own
  server-example idiom: `crt_file` holds the leaf followed by its issuers, so
  the anchors really are everything after the leaf.

Failing closed then comes for free. `src/tls.cpp` always sets `verify_peer`,
including when it found no trust store, because leaving `g_ssl_ctx` unset would
send libhv down its `hssl_ctx_new(NULL)` fallback and back to verifying nothing.

---

## Maintainability

### M1. Commented-out code left behind (fixed)

Was of the order of 90 lines across a couple of dozen files, concentrated in
`src/numpad.cpp`, `src/extruder_panel.cpp`, `src/image_label.cpp` and
`src/macro_item.cpp`. Gone in two commits, the four worst files and then the
rest.

Explanatory comments stayed, including the ones that read like code. The test
applied was whether the line documents the code around it or is a copy of code
that used to run. `// input validation, e.g. range` in `src/numpad.cpp` is
still there and still an unkept promise, and the `// XXX:` markers listed at
the top of this file are untouched.

Two things fell out of it. `style_imgbtn_default` in `GuppyScreen` was declared
and defined but only ever initialised and applied from commented-out lines, so
it went with them, and `main.cpp`'s `mouse_indev` had no reader once the
commented cursor block was removed.

Worth recording the near miss: deleting by line number took two live lines with
it, `data[key].merge_patch(patch)` in `State::set_data` and
`lv_indev_set_group(enc_indev, g)` in `hal_init`. Both were caught by reading
the diff rather than by the build, which compiled clean without them. A sweep
like this wants the diff checked for lines that are not comments, not just a
green build.

### M2. Four leaked singletons (open, cosmetic)

`Config`, `GuppyScreen`, `ThemeConfig` and `State` are each `instance = new ...`
with no matching `delete` (`src/config.cpp:17`, `src/guppyscreen.cpp:41`,
`src/theme.cpp:17`, `src/state.cpp:40`). They live for the whole process, so
nothing leaks in practice. Only worth touching if we ever want a clean shutdown
path or to run the UI under a leak checker.

### M3. Logging before the logger exists (fixed, `5cdef7e`)

`src/main.cpp` called `spdlog::debug` before `Config::init` and before
`GuppyScreen::init` install the sinks and set the level, so that line goes to
the default logger and is normally dropped. Either move it after init or delete
it.

### M4. K1 paths hardcoded into non-K1 builds (fixed, `5cdef7e`)

`src/main.cpp` passed `/usr/data/printer_data/thumbnails` as the thumbnail
root unconditionally, and `src/config.cpp` defaulted `log_path` to
`/usr/data/printer_data/logs/guppyscreen.log`. Both are wrong for the simulator
and for the Debian target. Worked around today by generating a config in
`scripts/build.sh`, but the defaults should be platform-conditional.

### M5. Home panel sensor rows overlap and clip (fixed)

`screenshots/home.png` is kept as the evidence, captured against the
development K1 Max on 2026-08-16, before the fix. **It is deliberately not
retaken**, and it is the one screenshot in that directory the README does not
use. That machine reported two extra temperature sensors beyond the extruder
and bed, and the third row drew as `Temperature Fan 20Chamber Fan`: one
sensor's value sat on top of the next sensor's name. A fourth row was started
below it and clipped by the temperature chart, so only its colour bar showed.

Two things combine. `SensorContainer` gives the name label no width bound and
aligns it out-right of the icon (`src/sensor_container.cpp:51`), while the
value is aligned to a fixed offset from the right edge (`:55`), so a long
display name runs straight under the value. And nothing bounds how many
sensors `MainPanel::create_sensors` will lay out, so a printer with more of
them than fit overruns the space the chart occupies.

Cosmetic rather than dangerous, but it is on the first screen you see, and it
is why the home panel is not in the README's gallery.

`SensorContainer` is a flex row now, so the name takes what is left over and
ellipsises rather than growing under the value, and the numbers size to their
content so a three digit temperature pushes its neighbours along instead of
wrapping onto a second line. That second half is upstream #116, #91 and #41,
which are the same code seen from the other side: fixed label widths scaled
with the horizontal resolution while `src/main.cpp` picks the font from the
vertical one, so which displays wrapped did not follow from either number
alone.

`MainPanel::create_sensors` now scrolls the list when it does not fit, the same
way `FanPanel` and `LedPanel` already did, rather than running rows off the
bottom. Driven in the simulator with the five sensors the development K1 Max
reports.

### M6. LVGL's tick came from the wall clock (fixed, `b1682b4`)

`custom_tick_get` used `gettimeofday`, so every LVGL timer, the display sleep
timer and the input shaper watchdog moved whenever NTP stepped the clock. It
also computed `tv_sec * 1000000` in `time_t`, which overflows on any target
with a 32 bit one and produces a 71.6 minute sawtooth. That is the exact period
in the "wakes up about every hour" reports upstream, #80 and #7.

Latent rather than live for us: the Bootlin musl toolchain has a 64 bit
`time_t`, so the overflow needs 292 000 years rather than 71 minutes. The clock
stepping half was real on any target. Now `clock_gettime(CLOCK_MONOTONIC)`
(`src/guppyscreen.cpp:366-376`), which removes both.

### M7. Small measurements rendered in scientific notation (fixed, `775bf27`)

`fmt`'s `{:.5}` with no presentation type is `%g`, which switches to an
exponent below 1e-4. A `homing_origin[2]` of 5.55e-17, which is ordinary
residue from two opposite `Z_ADJUST` calls and means zero, printed as
`5.5511e-17 mm` on the fine tune and print status panels.

`KUtils::short_measure` (`src/utils.cpp:117`) fixes the precision and snaps
anything under half a micron to zero. Six call sites. grumpyscreen's `1d7e0d3`
treats the symptom by string matching `e-` in the formatted output; this does
not.

### M8. The Moonraker API key was never sent (fixed, `ca18c1d`)

`moonraker_api_key` was written by the config, by the printer select panel and
by the Debian default config, and read nowhere. The websocket opened with an
empty header set, so against a Moonraker with `authorization` configured the
UI simply never connected, with nothing on screen to say why. Upstream #32.

Now `src/websocket_client.cpp:141` sets `X-Api-Key` from
`KUtils::moonraker_api_key` (`src/utils.cpp:81`), and the two HTTP fetches go
through `KUtils::fetch_to_file`, which carries the same header. Spoolman rides
the websocket via `server.spoolman.proxy`, so it is covered by the same change.
`tools/fake_moonraker.py --api-key` refuses an anonymous handshake, which is
how this is driven.

---

## On mining `pellcorp/grumpyscreen`

Worth recalibrating the expectation here. The fork is 233 commits ahead and 0
behind, which sounds like a large pool of fixes to cherry-pick. A good part of
that lead is deletion. It carries roughly 70 files in `src/` against our 90
odd, having dropped:

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

## Where this ended up

C1, C18, C19 and the C9 lifetime hazard were one problem wearing four hats:
dispatch happened on a thread that did not own the widgets, so shared state
needed locking that could not be made both correct and deadlock-free while
handlers ran under it.

`onmessage` pushes onto a queue now and the LVGL loop drains it, and
`WpaEvent` does the same through an `lv_timer`. Every handler runs on one
thread, `State` returns references safely, and a panel cannot be destroyed
mid-dispatch. None of the local fixes those entries proposed were needed, which
is the argument for having waited: copying json out of `State` at each of its
51 call sites would all have had to come back out.

Two things the queues have to keep doing, which are easy to lose in a later
refactor. Connection events go through the queue, not just messages, because
`onopen` runs on the libhv thread and that is the path that built widgets from
it. And each drain takes only what is queued at entry, because handlers send
requests whose replies land back on the same queue.

The mutexes stayed. `cb_lock` and `State`'s lock guard nothing against a second
thread any more, but they cost an uncontended acquisition and the alternative
is a public accessor with no protection at all the day something is dispatched
from elsewhere again.

## Suggested order

Done: everything except C7 and M2. C1 to C6, C8 to C20, B1 to B6, M1, M3 to
M8, and the `KUtils` parse helpers.

Remaining:

1. C7, the non-blocking heat change. Behavioural, wants its own discussion, and
   the only correctness finding left open.

M2, the four leaked singletons, is harmless and stays open.
