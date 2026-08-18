#!/usr/bin/env python3
"""Minimal fake Moonraker for local testing.

Speaks enough of the protocol for guppyscreen to connect, report a temperature,
serve a bed mesh and list objects. Records every printer.gcode.script it
receives, and can reject them so error handling can be exercised.

Exists so UI and gcode changes can be verified without commanding a real
printer. Needs the websockets package: pip install websockets

    python3 tools/fake_moonraker.py                    # 240C, accepts gcode
    python3 tools/fake_moonraker.py 25 0               # cold
    python3 tools/fake_moonraker.py 240 240 --reject   # reject every command
    python3 tools/fake_moonraker.py 240 240 --reject --burst
    python3 tools/fake_moonraker.py --mesh bowl        # pick a bed mesh shape
    python3 tools/fake_moonraker.py --wiper            # pretend WIPE_NOZZLE exists
    python3 tools/fake_moonraker.py --shaper           # run resonance tests
    python3 tools/fake_moonraker.py --shaper fail      # and have them fail

Mesh shapes: adaptive, full, bowl, tilt, flat. BED_MESH_CLEAR and
BED_MESH_CALIBRATE are acted on rather than just logged, so the panel's clear
and recalibrate paths can be driven from here.

--shaper acts on TEST_RESONANCES and RUN_SHELL_COMMAND CMD=guppy_input_shaper,
emitting the notify_gcode_response sequence Klipper and gcode_shell_command.py
really produce, so the input shaper panel's whole reply path is reachable
without shaking a printer for five minutes per attempt. Modes: normal, slow,
fail, timeout. --shaper-type sets the configured X shaper, which is how a type
the panel does not list gets in front of it, and --no-shaper-config withholds
the config sections the panel checks before offering to calibrate.

Received gcode is appended to $GCODE_LOG, default /tmp/guppy_gcode_received.txt.

--print [complete|cancelled|error] walks a print from standby to that ending,
over --print-seconds seconds, default 30.

--drop-file <seconds> announces a new gcode file that long after connect, the
way Moonraker does after an upload.

--belts [normal|slow|fail|timeout|empty] answers the belts panel's half-axis
sweeps and its analysis, ending the way the mode says.
Point the simulator at it with moonraker_host 127.0.0.1 in guppyconfig.json.
"""
import asyncio, json, math, os, re, shlex, struct, sys, time, zlib
import websockets

# The two temperatures are positional and optional, so they have to be read from
# the leading bare arguments rather than from argv by index. Taking argv[1]
# meant every flag-only invocation this file documents, --mesh and --wiper
# included, died on float("--mesh") before it could serve anything.
_temps = []
for _arg in sys.argv[1:]:
    if _arg.startswith("-"):
        break
    _temps.append(float(_arg))

TEMP = _temps[0] if _temps else 240.0
TARGET = _temps[1] if len(_temps) > 1 else 240.0
# Reject every gcode command, optionally several times per request so the
# coalescing in the UI can be exercised.
REJECT_GCODE = "--reject" in sys.argv
REJECT_BURST = 10 if "--burst" in sys.argv else 1
GCODE_LOG = os.environ.get("GCODE_LOG", "/tmp/guppy_gcode_received.txt")

# Pretend the ProWiper mod is installed, so the bed mesh panel's check for it
# can be exercised both ways round.
EXTRA_OBJECTS = ["gcode_macro WIPE_NOZZLE"] if "--wiper" in sys.argv else []

# Walk a print from standby through to a finish, so the print status panel can
# be watched doing the thing it gets wrong: reaching the end and staying there.
# The value is how the run ends, and how many seconds it takes to get there.
PRINT_RUN = None
PRINT_SECONDS = 30.0


def _arg_value(name, default):
    if name in sys.argv:
        i = sys.argv.index(name)
        if i + 1 < len(sys.argv):
            return sys.argv[i + 1]
    return default


def _mode_value(name, default, choices):
    """A flag that works both bare and with a mode after it.

    --shaper on its own is the common case and should not need a word after it,
    but the next argv entry might be another flag rather than this one's value.
    """
    if name not in sys.argv:
        return None
    value = _arg_value(name, default)
    if value not in choices:
        print(f"unknown {name} {value}, using {default}. "
              f"choices: {', '.join(choices)}", flush=True)
        return default
    return value


EXTRUDER_SETTINGS = {
    "min_temp": 0.0, "max_temp": 350.0, "min_extrude_temp": 170.0,
    "max_extrude_only_distance": 1000.0, "max_extrude_only_velocity": 212.86,
    "max_extrude_only_accel": 5321.6, "max_extrude_cross_section": 80.0,
    "nozzle_diameter": 0.4, "filament_diameter": 1.75, "pressure_advance": 0.02,
}

# Bed mesh shapes. The bed mesh panel is the one panel whose whole job is to
# draw a surface, so it needs more than one surface to be worth testing against.
# Each entry is (probe_count, mesh_min, mesh_max, height function over u,v in
# [0,1]). Values are in mm and chosen to sit in the range a real K1 Max
# produces.
MESH_SHAPES = {
    # The real 3x3 adaptive mesh read off the development printer, so at least
    # one case is measured rather than invented. Covers only part of the bed.
    "adaptive": (3, (190.19, 100.28), (280.29, 215.46), None),
    # A 6x6 full bed mesh, which is what probe_count in the K1 Max config asks
    # for. Gently saddled, the shape a real bed usually has.
    "full": (6, (5.0, 5.0), (295.0, 295.0),
             lambda u, v: 0.18 * math.sin(math.pi * u) - 0.12 * math.sin(math.pi * v) + 0.04),
    # Dished centre. The obvious visual check that the surface is not inverted.
    "bowl": (6, (5.0, 5.0), (295.0, 295.0),
             lambda u, v: 0.45 * ((2 * u - 1) ** 2 + (2 * v - 1) ** 2) / 2 - 0.22),
    # A plane tilted corner to corner, straddling zero so both halves of the
    # diverging colour scale get used.
    "tilt": (6, (5.0, 5.0), (295.0, 295.0), lambda u, v: 0.3 * (u + v - 1)),
    # Near flat, well inside the colour scale's floor, so it should render as a
    # calm pale plate rather than 8 microns of noise blown up into a mountain
    # range. Also the case whose folded quads found the polygon hang.
    "flat": (6, (5.0, 5.0), (295.0, 295.0),
             lambda u, v: 0.004 * math.sin(9 * u) * math.cos(7 * v)),
}

ADAPTIVE_POINTS = [
    [0.393098, 0.404111, 0.458436],
    [0.147894, 0.179828, 0.241844],
    [0.035494, 0.028320, 0.018827],
]

MESH_SHAPE = _arg_value("--mesh", "full")
if MESH_SHAPE not in MESH_SHAPES:
    print(f"unknown --mesh {MESH_SHAPE}, using full. "
          f"choices: {', '.join(MESH_SHAPES)}", flush=True)
    MESH_SHAPE = "full"


def build_probed(shape):
    count, _, _, fn = MESH_SHAPES[shape]
    if fn is None:
        return [row[:] for row in ADAPTIVE_POINTS]
    d = count - 1
    return [[round(fn(c / d, r / d), 6) for c in range(count)] for r in range(count)]


def interpolate(probed, pps):
    """Bilinear fill between probe points, the way mesh_pps densifies a mesh.

    Klipper uses lagrange or bicubic here. Bilinear is close enough for a fake
    and keeps this file readable; what the panel needs is a denser matrix of
    the right shape, not Klipper's exact numbers.
    """
    rows, cols = len(probed), len(probed[0])
    out_rows = (rows - 1) * (pps + 1) + 1
    out_cols = (cols - 1) * (pps + 1) + 1
    out = []
    for r in range(out_rows):
        fr = r * (rows - 1) / (out_rows - 1)
        r0 = min(int(fr), rows - 2)
        tr = fr - r0
        line = []
        for c in range(out_cols):
            fc = c * (cols - 1) / (out_cols - 1)
            c0 = min(int(fc), cols - 2)
            tc = fc - c0
            top = probed[r0][c0] * (1 - tc) + probed[r0][c0 + 1] * tc
            bot = probed[r0 + 1][c0] * (1 - tc) + probed[r0 + 1][c0 + 1] * tc
            line.append(round(top * (1 - tr) + bot * tr, 6))
        out.append(line)
    return out


def build_bed_mesh(shape):
    count, mesh_min, mesh_max, _ = MESH_SHAPES[shape]
    probed = build_probed(shape)
    pps = 2
    params = {
        "min_x": mesh_min[0], "max_x": mesh_max[0],
        "min_y": mesh_min[1], "max_y": mesh_max[1],
        "x_count": count, "y_count": count,
        "mesh_x_pps": pps, "mesh_y_pps": pps,
        "algo": "lagrange" if count < 4 else "bicubic",
        "tension": 0.2,
    }
    return {
        "profile_name": "default",
        "mesh_min": list(mesh_min),
        "mesh_max": list(mesh_max),
        "probed_matrix": probed,
        "mesh_matrix": interpolate(probed, pps),
        "profiles": {
            "default": {"points": probed, "mesh_params": params},
            "adaptive": {"points": probed, "mesh_params": params},
        },
    }

SHAPER_MODES = ("normal", "slow", "fail", "timeout")
# Announce a new gcode file this many seconds after connect, so the file panel
# can be watched noticing it without anyone pressing Reload.
FILE_DROP_SECONDS = float(_arg_value("--drop-file", "0")) if "--drop-file" in sys.argv else 0

PRINT_MODES = ("complete", "cancelled", "error")
PRINT_RUN = _mode_value("--print", "complete", PRINT_MODES)
PRINT_SECONDS = float(_arg_value("--print-seconds", "30"))

BELTS_MODES = ("normal", "slow", "fail", "timeout", "empty")
BELTS = _mode_value("--belts", "normal", BELTS_MODES)

SHAPER = _mode_value("--shaper", "normal", SHAPER_MODES)
SHAPER_LOCK = None
# The configured shaper type to report for X. Worth being able to set, because
# the panel has to show back whatever the printer is really configured for, and
# that is not limited to what a calibration proposes: zvd is legal in
# [input_shaper] but k1/scripts/shaper_calibrate.py leaves it out of
# AUTOTUNE_SHAPERS, so it only arrives via Klipper's own SHAPER_CALIBRATE or by
# hand. Any other value stands in for a type the panel has never heard of.
SHAPER_TYPE_X = _arg_value("--shaper-type", "ei")
SHAPER_CONFIG = "--no-shaper-config" not in sys.argv

# Values measured off the development K1 Max on 2026-08-17. The panel clamps its
# frequency control against min_freq and max_freq, so inventing them would test
# the clamp against numbers no printer has.
RESONANCE_TESTER = {
    "move_speed": 50.0, "min_freq": 5.0, "max_freq": 133.33333333333334,
    "accel_per_hz": 75.0, "hz_per_sec": 1.0, "probe_points": [[150.0, 150.0, 10.0]],
    "low_mem": True, "accel_chip": "adxl345",
}

INPUT_SHAPER = {
    "shaper_type": "mzv", "shaper_type_x": SHAPER_TYPE_X,
    "damping_ratio_x": 0.1, "shaper_freq_x": 40.3,
    "shaper_type_y": "zv", "damping_ratio_y": 0.1, "shaper_freq_y": 46.9,
}

# What calibrate_shaper.py prints for each axis. Deliberately includes a three
# digit frequency and a two digit vibration figure, because those are the values
# that expose a table whose columns are padded on the header row only.
SHAPER_RESULTS = {
    "x": {
        "shapers": {
            "zv": {"freq": 34.8, "vib": 12.45, "smooth": 0.084, "max_acel": 4500.0},
            "mzv": {"freq": 40.3, "vib": 2.11, "smooth": 0.115, "max_acel": 3300.0},
            "ei": {"freq": 47.4, "vib": 1.03, "smooth": 0.128, "max_acel": 3000.0},
            "2hump_ei": {"freq": 66.4, "vib": 0.12, "smooth": 0.132, "max_acel": 2900.0},
            "3hump_ei": {"freq": 103.6, "vib": 0.0, "smooth": 0.118, "max_acel": 3200.0},
        },
        "best": "mzv",
    },
    "y": {
        "shapers": {
            "zv": {"freq": 39.6, "vib": 9.87, "smooth": 0.065, "max_acel": 5800.0},
            "mzv": {"freq": 46.9, "vib": 1.74, "smooth": 0.085, "max_acel": 4400.0},
            "ei": {"freq": 55.2, "vib": 0.94, "smooth": 0.094, "max_acel": 4000.0},
            "2hump_ei": {"freq": 77.4, "vib": 0.08, "smooth": 0.097, "max_acel": 3900.0},
            "3hump_ei": {"freq": 120.8, "vib": 0.0, "smooth": 0.087, "max_acel": 4300.0},
        },
        "best": "ei",
    },
}


def _png_chunk(tag, payload):
    return (struct.pack(">I", len(payload)) + tag + payload
            + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))


def write_png(path, width, height, axis):
    """Write a plot shaped enough to tell a fresh image from a stale one.

    The printer draws the real thing with matplotlib, which is far too heavy to
    require here and would make this file's dependency list a lie. The panel
    only needs a PNG it can decode, so this draws the one property worth seeing
    at a glance: a resonance peak where that axis's peak belongs.
    """
    peak = SHAPER_RESULTS[axis]["shapers"]["zv"]["freq"]
    lo, hi = RESONANCE_TESTER["min_freq"], RESONANCE_TESTER["max_freq"]

    def power(freq):
        # A main resonance with a weaker harmonic above it, on a noise floor.
        # A single narrow spike would sit on the bottom axis everywhere else and
        # read as an empty image.
        main = math.exp(-(((freq - peak) / (peak / 4.0)) ** 2))
        harmonic = 0.32 * math.exp(-(((freq - 2 * peak) / (peak / 2.5)) ** 2))
        return min(1.0, 0.06 + main + harmonic)

    curve = []
    for x in range(width):
        freq = lo + (hi - lo) * x / max(width - 1, 1)
        curve.append(height - 3 - int(power(freq) * (height - 14)))

    trace = b"\x1f\x77\xb4" if axis == "x" else b"\xd6\x5f\x3a"
    rows = bytearray()
    for y in range(height):
        rows.append(0)  # filter type none
        for x in range(width):
            # Fill between neighbours so a steep section stays a continuous
            # line instead of breaking into dashes.
            near = curve[max(x - 1, 0):x + 2]
            if min(near) - 1 <= y <= max(near) + 1:
                rows += trace
            elif x <= 1 or y >= height - 2:
                rows += b"\x40\x40\x40"
            else:
                rows += b"\xff\xff\xff"
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n"
                + _png_chunk(b"IHDR", header)
                + _png_chunk(b"IDAT", zlib.compress(bytes(rows), 6))
                + _png_chunk(b"IEND", b""))


STATUS = {
    "extruder": {"temperature": TEMP, "target": TARGET, "power": 0.0},
    "heater_bed": {"temperature": 25.0, "target": 0.0, "power": 0.0},
    "configfile": {
        "settings": {"extruder": EXTRUDER_SETTINGS,
                     "printer": {"max_velocity": 800.0, "max_accel": 20000.0,
                                 "kinematics": "corexy"},
                     "input_shaper": INPUT_SHAPER},
        # config holds the raw file text, so every value here is a string even
        # where settings has it typed. Panels reading the wrong one of the two
        # is a recurring mistake, so both are served with their real shapes.
        "config": {"extruder": {"filament_diameter": "1.75"},
                   "input_shaper": {k: str(v) for k, v in INPUT_SHAPER.items()}},
    },
    "toolhead": {"max_velocity": 800.0, "max_accel": 20000.0,
                 "homed_axes": "xyz", "position": [0, 0, 0, 0]},
    "print_stats": {"state": "standby", "filename": "", "print_duration": 0.0},
    "virtual_sdcard": {"is_active": False, "progress": 0.0},
    "gcode_move": {"speed_factor": 1.0, "extrude_factor": 1.0,
                   "homing_origin": [0, 0, 0, 0], "gcode_position": [0, 0, 0, 0]},
    "idle_timeout": {"state": "Idle"},
    "display_status": {"progress": 0.0, "message": None},
    "bed_mesh": build_bed_mesh(MESH_SHAPE),

    # The K1's three fans, as Creality actually defines them: [output_pin] with
    # scale 255 rather than [fan], which is why the panel drives them with
    # SET_PIN and not M106. Klipper divides by scale before reporting, so value
    # here is the normalised 0 to 1 and never the raw number SET_PIN took.
    "output_pin fan0": {"value": 0.0},
    "output_pin fan1": {"value": 0.0},
    "output_pin fan2": {"value": 0.0},
    "temperature_sensor chamber_temp": {"temperature": 21.4, "measured_min_temp": 19.8,
                                        "measured_max_temp": 46.2},
    # Two more with long display names, because a K1 Max reports these and they
    # are what made the home screen's rows overlap: the name had no width bound
    # and ran underneath the next row's value. See audit M5.
    "temperature_fan chamber_fan": {"temperature": 21.4, "target": 75.0, "speed": 0.0},
    "temperature_fan soc_fan": {"temperature": 33.3, "target": 45.0, "speed": 0.0},

    # Creality keeps the value below which each fan does not turn at all in
    # this macro's variables. The panel needs them to map its slider onto the
    # part of the range that does something, so it is the one gcode_macro the
    # UI subscribes to. Values measured on a K1 Max.
    "gcode_macro PRINTER_PARAM": {"fans": 3, "fan0_min": 25, "fan1_min": 50,
                                  "fan2_min": 180, "max_x_position": 300.0,
                                  "max_y_position": 300.0, "max_z_position": 300.0},
}

STATUS["configfile"]["settings"].update({
    "output_pin fan0": {"pwm": True, "scale": 255.0, "value": 0.0},
    "output_pin fan1": {"pwm": True, "scale": 255.0, "value": 0.0},
    "output_pin fan2": {"pwm": True, "scale": 255.0, "value": 0.0},
    "temperature_sensor chamber_temp": {"sensor_type": "EPCOS 100K B57560G104F"},
    "temperature_fan chamber_fan": {"sensor_type": "EPCOS 100K B57560G104F",
                                    "target_temp": 75.0},
    "temperature_fan soc_fan": {"sensor_type": "temperature_mcu", "target_temp": 45.0},
})

# The sections the panel checks before offering to calibrate or save. Neither
# resonance_tester nor adxl345 appears in printer.objects.list on a real
# printer, measured, because they have no get_status. calibrate_shaper_config
# does appear but reports {}, so configfile is the only place a panel can find
# any of their values.
# --no-shaper-config withholds them to drive the disabled half of that check.
if SHAPER_CONFIG:
    STATUS["configfile"]["settings"].update({
        "gcode_shell_command guppy_belts_calibration": {
            "command": "/usr/data/printer_data/config/GuppyScreen/scripts/graph_belts.py",
            "timeout": 600.0, "verbose": True},
        "resonance_tester": RESONANCE_TESTER,
        "adxl345": {"axes_map": ["x", "-z", "y"], "rate": 3200,
                    "cs_pin": "nozzle_mcu:PA4", "spi_speed": 5000000},
        "calibrate_shaper_config": {"shaper_type": "mzv", "shaper_type_x": "mzv",
                                    "shaper_freq_x": 0.0, "shaper_type_y": "mzv",
                                    "shaper_freq_y": 0.0},
        "gcode_shell_command guppy_input_shaper": {
            "command": "/usr/data/printer_data/config/GuppyScreen/scripts/calibrate_shaper.py",
            "timeout": 600.0, "verbose": True},
    })
    STATUS["configfile"]["config"].update({
        "resonance_tester": {"accel_chip": "adxl345", "accel_per_hz": "75",
                             "probe_points": "\n150,150,10"},
        "adxl345": {"cs_pin": "nozzle_mcu:PA4", "spi_speed": "5000000"},
        "calibrate_shaper_config": {},
        "gcode_shell_command guppy_input_shaper": {
            "command": "/usr/data/printer_data/config/GuppyScreen/scripts/calibrate_shaper.py",
            "timeout": "600.0", "verbose": "True"},
    })


# Enough of a gcodes root to populate the file browser and to answer the
# metadata request the print status panel fires when a print starts. No
# thumbnails: those are fetched over HTTP, which this does not serve.
FILES = [
    {"path": "fake_benchy.gcode", "modified": time.time() - 60, "size": 2118400},
    {"path": "calibration/first_layer.gcode", "modified": time.time() - 86400,
     "size": 184320},
]


def result_for(method, params):
    if method == "server.info":
        return {"klippy_connected": True, "klippy_state": "ready",
                "components": [], "failed_components": [], "warnings": [],
                "moonraker_version": "fake"}
    if method == "printer.info":
        return {"state": "ready", "state_message": "ready",
                "klipper_path": "/usr/share/klipper",
                "config_file": "/usr/data/printer_data/config/printer.cfg"}
    if method == "printer.objects.subscribe" or method == "printer.objects.query":
        return {"eventtime": time.time(), "status": STATUS}
    if method == "printer.objects.list":
        # Macros are listed but not subscribed to, and their case is whatever
        # the config used, which is why WIPE_NOZZLE is upper here and lower in
        # configfile.settings on a real printer.
        return {"objects": list(STATUS.keys()) + EXTRA_OBJECTS}
    if method == "server.database.get_item":
        return {"namespace": params.get("namespace", ""), "value": {}}
    if method == "printer.gcode.help":
        return {}
    if method == "server.files.list":
        return FILES
    if method == "server.files.metadata":
        for f in FILES:
            if f["path"] == params.get("filename"):
                return dict(f, estimated_time=int(PRINT_SECONDS),
                            layer_count=120, first_layer_height=0.2,
                            layer_height=0.2, object_height=24.0,
                            filament_total=4210.0, thumbnails=[])
        return {}
    if method == "machine.system_info":
        return {"system_info": {"cpu_info": {"cpu_count": 2}}}
    return {}


SET_PIN_RE = re.compile(r"SET_PIN\s+PIN=(\S+)\s+VALUE=(\S+)", re.I)


def set_pin_effect_of(script):
    """Status deltas a SET_PIN would produce, applied to STATUS too.

    Acted on rather than only logged, because the fan panel's real bug was that
    what it wrote and what it read back were on different scales. Only running
    the round trip shows that: Klipper stores VALUE / scale, so a slider at 50
    percent on the side fan has to come back as 50 percent and not as the 85 it
    used to.
    """
    m = SET_PIN_RE.search(script)
    if not m:
        return []
    obj = "output_pin %s" % m.group(1)
    if obj not in STATUS:
        return []
    scale = STATUS["configfile"]["settings"].get(obj, {}).get("scale", 1.0)
    try:
        raw = float(m.group(2))
    except ValueError:
        return []
    STATUS[obj]["value"] = max(0.0, min(1.0, raw / scale))
    return [{obj: {"value": STATUS[obj]["value"]}}]


def bed_mesh_effect_of(script):
    """Status deltas a bed mesh command would produce, applied to STATUS too.

    Moonraker only sends the keys that changed, and BED_MESH_CALIBRATE is the
    command that most often lands a bed_mesh delta carrying no profile_name at
    all. Reproducing that shape here is the point: a panel that assumes every
    delta is complete will misbehave against it exactly as it does live.
    """
    bm = STATUS["bed_mesh"]
    if "BED_MESH_CLEAR" in script:
        bm["profile_name"] = ""
        bm["probed_matrix"] = []
        bm["mesh_matrix"] = []
        return [{"bed_mesh": {"profile_name": "", "probed_matrix": [],
                              "mesh_matrix": []}}]
    if "BED_MESH_CALIBRATE" in script:
        was = bm["profile_name"]
        fresh = build_bed_mesh(MESH_SHAPE)
        bm.update(fresh)
        delta = {"probed_matrix": fresh["probed_matrix"],
                 "mesh_matrix": fresh["mesh_matrix"]}
        # Only a genuine change carries the key, which is what makes the common
        # case a matrices-only delta.
        if fresh["profile_name"] != was:
            delta["profile_name"] = fresh["profile_name"]
        return [{"bed_mesh": delta}]
    if "BED_MESH_PROFILE LOAD" in script:
        return [{"bed_mesh": {"profile_name": bm["profile_name"],
                              "probed_matrix": bm["probed_matrix"],
                              "mesh_matrix": bm["mesh_matrix"]}}]
    return []


async def respond(ws, text):
    await ws.send(json.dumps({"jsonrpc": "2.0", "method": "notify_gcode_response",
                              "params": [text]}))


def _shell_params(line):
    """The PARAMS="..." of a RUN_SHELL_COMMAND, split the way Klipper splits it.

    Two stages, and both are needed. Klipper shlex.splits an extended command's
    arguments before assigning them, which is what removes the quotes the panel
    puts on with fmt's debug format, and gcode_shell_command.py then shlex.splits
    the PARAMS value itself into argv. Doing it once leaves the whole argument
    list as a single token and every flag in it invisible.
    """
    for token in shlex.split(line)[1:]:
        key, _, value = token.partition("=")
        if key.upper() == "PARAMS":
            return shlex.split(value)
    return []


async def run_resonance_test(ws, axis, name):
    csv = f"/tmp/resonances_{axis}_{name}.csv"
    step = 0.7 if SHAPER == "slow" else 0.12
    await respond(ws, f"// Testing axis {axis}")

    lo, hi = RESONANCE_TESTER["min_freq"], RESONANCE_TESTER["max_freq"]
    steps = 8
    for i in range(steps):
        await asyncio.sleep(step)
        freq = lo + (hi - lo) * i / (steps - 1)
        if SHAPER == "fail" and i == 2:
            # An accelerometer that stops answering mid run. Klipper broadcasts
            # this as an error rather than a reply, and the run never reaches
            # the message the panel is waiting for.
            await respond(ws, "!! adxl345: No data received from sensor")
            return
        await respond(ws, f"// Testing frequency {freq:.0f} Hz")

    await asyncio.sleep(step)
    await respond(ws, f"// Resonances data written to {csv} file")


async def run_belts_test(ws, axis, name):
    """A half-axis sweep, named the way Klipper names its output.

    The filename is the whole point: it is what the panel waits for to know the
    capture has been flushed, and what graph_belts.py reads the belt letter out
    of. Klipper builds it from the axis and NAME, preserving NAME's case.
    """
    csv = f"/tmp/raw_data_axis={axis} file".replace(" file", f"_{name}.csv")
    step = 0.7 if BELTS == "slow" else 0.12
    await respond(ws, f"// Testing axis {axis}")

    for i in range(6):
        await asyncio.sleep(step)
        if BELTS == "fail" and i == 2:
            await respond(ws, "!! adxl345: No data received from sensor")
            return
        await respond(ws, f"// Testing frequency {5 + i * 25} Hz")

    await asyncio.sleep(step)
    await respond(ws, f"// Resonances data written to {csv} file")


async def run_belts_analysis(ws, params):
    await respond(ws, "// Running Command {guppy_belts_calibration}...:")
    await asyncio.sleep(1.0 if BELTS != "slow" else 4.0)

    if BELTS == "timeout":
        await respond(ws, "// Command {guppy_belts_calibration} timed out")
        return
    if BELTS == "empty":
        # The process exits without producing a plot. gcode_shell_command still
        # says "finished", which is why "finished" alone cannot mean success.
        await respond(ws, "// Command {guppy_belts_calibration} finished\n")
        return

    await respond(ws, "// Belts estimated similarity: 95.0%")
    await respond(ws, "// Command {guppy_belts_calibration} finished\n")


async def run_shaper_analysis(ws, params):
    axis = "y" if "_y_" in (params[0] if params else "") else "x"
    png = width = height = None
    for i, arg in enumerate(params):
        if arg == "-o" and i + 1 < len(params):
            png = params[i + 1]
        elif arg == "-w" and i + 1 < len(params):
            width = float(params[i + 1])
        elif arg == "-l" and i + 1 < len(params):
            height = float(params[i + 1])

    await respond(ws, "// Running Command {guppy_input_shaper}...:")
    await asyncio.sleep(1.4 if SHAPER == "slow" else 0.3)

    if SHAPER == "timeout":
        # The shell command's own timeout. Nothing was printed, so a panel
        # waiting for the JSON payload waits forever unless it reads this.
        await respond(ws, "// Command {guppy_input_shaper} timed out")
        return

    payload = dict(SHAPER_RESULTS[axis])
    payload["logfile"] = params[0] if params else f"/tmp/resonances_{axis}_{axis}.csv"
    if png:
        # matplotlib's savefig defaults to 100 dpi, and the panel asks for the
        # size in inches, so this is the pixel size the printer really produces.
        write_png(png, int((width or 8.0) * 100), int((height or 4.8) * 100), axis)
        payload["png"] = png

    await respond(ws, "// " + json.dumps(payload))
    await respond(ws, "// Command {guppy_input_shaper} finished")


async def shaper_effect_of(ws, script):
    """Act on the resonance commands in a script, one after another.

    Klipper runs one command at a time, and the panel's sequencing bugs only
    show up against that, so these are serialized rather than run concurrently.
    """
    async with SHAPER_LOCK:
        for line in script.splitlines():
            line = line.strip()
            upper = line.upper()
            if upper.startswith("TEST_RESONANCES"):
                fields = dict(f.split("=", 1) for f in line.split()[1:] if "=" in f)
                axis = fields.get("AXIS", "").lower()
                # The belts panel drives the same command with AXIS=1,1 and
                # AXIS=1,-1, which is a different flow and not ours to answer.
                if axis in ("x", "y"):
                    await run_resonance_test(ws, axis, fields.get("NAME", axis))
                elif BELTS and "," in axis:
                    # 1,1 and 1,-1 are the belts panel's half axes. Klipper
                    # formats them into the filename as floats.
                    a, b = (float(v) for v in axis.split(","))
                    await run_belts_test(ws, f"{a:.3f},{b:.3f}",
                                         fields.get("NAME", "A"))
            elif "RUN_SHELL_COMMAND" in upper and "guppy_input_shaper" in line:
                await run_shaper_analysis(ws, _shell_params(line))
            elif "RUN_SHELL_COMMAND" in upper and "guppy_belts_calibration" in line:
                await run_belts_analysis(ws, _shell_params(line))


async def drop_a_file(ws):
    """Announce a new gcode file the way Moonraker does after an upload.

    The panel had no way to hear about this at all: it consumed only
    notify_status_update, so the list changed when someone pressed Reload and
    not before.
    """
    await asyncio.sleep(FILE_DROP_SECONDS)
    item = {"path": "dropped_by_the_fake.gcode", "root": "gcodes",
            "modified": time.time(), "size": 991232}
    FILES.append({k: item[k] for k in ("path", "modified", "size")})
    await ws.send(json.dumps({
        "jsonrpc": "2.0", "method": "notify_filelist_changed",
        "params": [{"action": "create_file", "item": item}]}))
    print("fake announced a new file", flush=True)


async def run_print(ws):
    """Drive print_stats and virtual_sdcard from standby to a finish.

    The end is the part worth reproducing. virtual_sdcard.progress is file
    position over file size, so it approaches 1.0 without necessarily reaching
    it, and print_stats.state goes to complete while it is still short. A panel
    that only trusts progress therefore stops at 99 and never dismisses, which
    is upstream #94 and #103.
    """
    async def push(delta):
        await ws.send(json.dumps({"jsonrpc": "2.0", "method": "notify_status_update",
                                  "params": [delta, time.time()]}))

    await asyncio.sleep(3.0)
    name = "fake_benchy.gcode"
    STATUS["print_stats"].update({"state": "printing", "filename": name,
                                  "print_duration": 0.0,
                                  "info": {"total_layer": 120, "current_layer": 0}})
    STATUS["virtual_sdcard"].update({"is_active": True, "progress": 0.0})
    await push({"print_stats": STATUS["print_stats"],
                "virtual_sdcard": STATUS["virtual_sdcard"]})
    print(f"fake print started, {PRINT_RUN} in {PRINT_SECONDS:.0f}s", flush=True)

    steps = 40
    for i in range(1, steps + 1):
        await asyncio.sleep(PRINT_SECONDS / steps)
        # Deliberately stops just short of 1.0. Klipper does the same, because
        # the end gcode runs after the last position the slicer wrote.
        progress = 0.994 * i / steps
        STATUS["virtual_sdcard"]["progress"] = progress
        STATUS["print_stats"]["print_duration"] = PRINT_SECONDS * i / steps
        STATUS["print_stats"]["info"]["current_layer"] = int(120 * i / steps)
        await push({"virtual_sdcard": {"progress": progress},
                    "print_stats": {
                        "print_duration": STATUS["print_stats"]["print_duration"],
                        "info": STATUS["print_stats"]["info"]}})

    STATUS["print_stats"]["state"] = PRINT_RUN
    STATUS["virtual_sdcard"]["is_active"] = False
    delta = {"print_stats": {"state": PRINT_RUN},
             "virtual_sdcard": {"is_active": False}}
    if PRINT_RUN != "complete":
        # Klipper clears the filename when a run is abandoned. That empty string
        # is what used to pull a blank status panel to the front.
        STATUS["print_stats"]["filename"] = ""
        delta["print_stats"]["filename"] = ""
    await push(delta)
    print(f"fake print ended: {PRINT_RUN}", flush=True)


async def handler(ws):
    print("guppyscreen connected", flush=True)

    async def push_status():
        # Keep re-announcing so the panel's cached values stay fresh.
        while True:
            await asyncio.sleep(1.0)
            try:
                await ws.send(json.dumps({
                    "jsonrpc": "2.0", "method": "notify_status_update",
                    "params": [{"extruder": {"temperature": TEMP, "target": TARGET}},
                               time.time()]}))
            except Exception:
                return

    pusher = asyncio.create_task(push_status())
    printer = asyncio.create_task(run_print(ws)) if PRINT_RUN else None
    dropper = asyncio.create_task(drop_a_file(ws)) if FILE_DROP_SECONDS else None
    # Held only so the tasks are not garbage collected while they run, which
    # asyncio does not otherwise prevent for a task nobody awaits.
    tasks = set()
    try:
        async for raw in ws:
            msg = json.loads(raw)
            method = msg.get("method", "")
            params = msg.get("params", {}) or {}

            if method == "printer.gcode.script":
                script = params.get("script", "")
                print("=== GCODE RECEIVED ===\n" + script, flush=True)
                with open(GCODE_LOG, "a") as f:
                    f.write("--- press ---\n" + script + "\n")
                for delta in bed_mesh_effect_of(script) + set_pin_effect_of(script):
                    await ws.send(json.dumps({
                        "jsonrpc": "2.0", "method": "notify_status_update",
                        "params": [delta, time.time()]}))
                if SHAPER or BELTS:
                    # Detached, because a resonance run outlasts the request
                    # that starts it and Moonraker keeps answering meanwhile.
                    task = asyncio.create_task(shaper_effect_of(ws, script))
                    tasks.add(task)
                    task.add_done_callback(tasks.discard)

            if "id" in msg:
                if method == "printer.gcode.script" and REJECT_GCODE:
                    # Reply the way Moonraker does when Klipper refuses a command.
                    # The burst counter is only worth showing when bursting, so a
                    # plain rejection reads exactly as Klipper's own would.
                    for n in range(REJECT_BURST):
                        detail = f" (burst {n + 1})" if REJECT_BURST > 1 else ""
                        await ws.send(json.dumps({
                            "jsonrpc": "2.0", "id": msg["id"],
                            "error": {"code": -32602,
                                      "message": f"Extrude below minimum temp{detail}"}}))
                    continue
                await ws.send(json.dumps({"jsonrpc": "2.0", "id": msg["id"],
                                          "result": result_for(method, params)}))
    finally:
        pusher.cancel()
        if printer is not None:
            printer.cancel()
        if dropper is not None:
            dropper.cancel()
        for task in tasks:
            task.cancel()


async def main():
    global SHAPER_LOCK
    # Built here rather than at import, because before Python 3.10 an
    # asyncio.Lock binds to whatever loop is current when it is constructed.
    SHAPER_LOCK = asyncio.Lock()
    shaper = f", shaper {SHAPER}" if SHAPER else ""
    print(f"fake moonraker on 7125, extruder {TEMP}/{TARGET}, "
          f"mesh {MESH_SHAPE}{shaper}", flush=True)
    async with websockets.serve(handler, "127.0.0.1", 7125):
        await asyncio.Future()

asyncio.run(main())
