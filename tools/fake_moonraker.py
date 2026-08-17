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

Mesh shapes: adaptive, full, bowl, tilt, flat. BED_MESH_CLEAR and
BED_MESH_CALIBRATE are acted on rather than just logged, so the panel's clear
and recalibrate paths can be driven from here.

Received gcode is appended to $GCODE_LOG, default /tmp/guppy_gcode_received.txt.
Point the simulator at it with moonraker_host 127.0.0.1 in guppyconfig.json.
"""
import asyncio, json, math, os, sys, time
import websockets

TEMP = float(sys.argv[1]) if len(sys.argv) > 1 else 240.0
TARGET = float(sys.argv[2]) if len(sys.argv) > 2 else 240.0
# Reject every gcode command, optionally several times per request so the
# coalescing in the UI can be exercised.
REJECT_GCODE = "--reject" in sys.argv
REJECT_BURST = 10 if "--burst" in sys.argv else 1
GCODE_LOG = os.environ.get("GCODE_LOG", "/tmp/guppy_gcode_received.txt")

# Pretend the ProWiper mod is installed, so the bed mesh panel's check for it
# can be exercised both ways round.
EXTRA_OBJECTS = ["gcode_macro WIPE_NOZZLE"] if "--wiper" in sys.argv else []


def _arg_value(name, default):
    if name in sys.argv:
        i = sys.argv.index(name)
        if i + 1 < len(sys.argv):
            return sys.argv[i + 1]
    return default


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

STATUS = {
    "extruder": {"temperature": TEMP, "target": TARGET, "power": 0.0},
    "heater_bed": {"temperature": 25.0, "target": 0.0, "power": 0.0},
    "configfile": {
        "settings": {"extruder": EXTRUDER_SETTINGS,
                     "printer": {"max_velocity": 800.0, "max_accel": 20000.0,
                                 "kinematics": "corexy"}},
        "config": {"extruder": {"filament_diameter": "1.75"}},
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
}


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
        return []
    if method == "machine.system_info":
        return {"system_info": {"cpu_info": {"cpu_count": 2}}}
    return {}


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
                for delta in bed_mesh_effect_of(script):
                    await ws.send(json.dumps({
                        "jsonrpc": "2.0", "method": "notify_status_update",
                        "params": [delta, time.time()]}))

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


async def main():
    print(f"fake moonraker on 7125, extruder {TEMP}/{TARGET}, "
          f"mesh {MESH_SHAPE}", flush=True)
    async with websockets.serve(handler, "127.0.0.1", 7125):
        await asyncio.Future()

asyncio.run(main())
