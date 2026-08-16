#!/usr/bin/env python3
"""Minimal fake Moonraker for local testing.

Speaks enough of the protocol for guppyscreen to connect, report a temperature
and list objects. Records every printer.gcode.script it receives, and can
reject them so error handling can be exercised.

Exists so UI and gcode changes can be verified without commanding a real
printer. Needs the websockets package: pip install websockets

    python3 tools/fake_moonraker.py                    # 240C, accepts gcode
    python3 tools/fake_moonraker.py 25 0               # cold
    python3 tools/fake_moonraker.py 240 240 --reject   # reject every command
    python3 tools/fake_moonraker.py 240 240 --reject --burst

Received gcode is appended to $GCODE_LOG, default /tmp/guppy_gcode_received.txt.
Point the simulator at it with moonraker_host 127.0.0.1 in guppyconfig.json.
"""
import asyncio, json, os, sys, time
import websockets

TEMP = float(sys.argv[1]) if len(sys.argv) > 1 else 240.0
TARGET = float(sys.argv[2]) if len(sys.argv) > 2 else 240.0
# Reject every gcode command, optionally several times per request so the
# coalescing in the UI can be exercised.
REJECT_GCODE = "--reject" in sys.argv
REJECT_BURST = 10 if "--burst" in sys.argv else 1
GCODE_LOG = os.environ.get("GCODE_LOG", "/tmp/guppy_gcode_received.txt")

EXTRUDER_SETTINGS = {
    "min_temp": 0.0, "max_temp": 350.0, "min_extrude_temp": 170.0,
    "max_extrude_only_distance": 1000.0, "max_extrude_only_velocity": 212.86,
    "max_extrude_only_accel": 5321.6, "max_extrude_cross_section": 80.0,
    "nozzle_diameter": 0.4, "filament_diameter": 1.75, "pressure_advance": 0.02,
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
        return {"objects": list(STATUS.keys())}
    if method == "server.database.get_item":
        return {"namespace": params.get("namespace", ""), "value": {}}
    if method == "printer.gcode.help":
        return {}
    if method == "server.files.list":
        return []
    if method == "machine.system_info":
        return {"system_info": {"cpu_info": {"cpu_count": 2}}}
    return {}


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

            if "id" in msg:
                if method == "printer.gcode.script" and REJECT_GCODE:
                    # Reply the way Moonraker does when Klipper refuses a command.
                    for n in range(REJECT_BURST):
                        await ws.send(json.dumps({
                            "jsonrpc": "2.0", "id": msg["id"],
                            "error": {"code": -32602,
                                      "message": f"Extrude below minimum temp (burst {n + 1})"}}))
                    continue
                await ws.send(json.dumps({"jsonrpc": "2.0", "id": msg["id"],
                                          "result": result_for(method, params)}))
    finally:
        pusher.cancel()


async def main():
    print(f"fake moonraker on 7125, extruder {TEMP}/{TARGET}", flush=True)
    async with websockets.serve(handler, "127.0.0.1", 7125):
        await asyncio.Future()

asyncio.run(main())
