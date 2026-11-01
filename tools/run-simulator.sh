#!/usr/bin/env bash

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SIM="$ROOT/build/bin/guppyscreen"
CONFIG="$ROOT/build/bin/guppyconfig.json"
FAKE_LOG="$ROOT/build/bin/fake_moonraker.log"
GCODE_LOG="$ROOT/build/bin/gcode_received.log"

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    printf 'Usage: %s\n' "$0"
    printf 'Starts the simulator against a local fake Moonraker on 127.0.0.1:7125.\n'
    exit 0
fi

if [[ ! -x "$SIM" || ! -f "$CONFIG" ]]; then
    echo "Simulator or config missing. Run: scripts/build.sh sim" >&2
    exit 1
fi

python3 - "$CONFIG" <<'PYCONFIG'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as config_file:
    config = json.load(config_file)

printer = config["printers"][config["default_printer"]]
host = printer.get("moonraker_host")
port = int(printer.get("moonraker_port", 7125))
if host != "127.0.0.1" or port != 7125:
    print(
        "Refusing to start: simulator config must target 127.0.0.1:7125, "
        f"not {host}:{port}.",
        file=sys.stderr,
    )
    sys.exit(1)
PYCONFIG

if python3 - 127.0.0.1 7125 <<'PYPORT'
import socket
import sys

with socket.socket() as sock:
    sock.settimeout(0.3)
    sys.exit(0 if sock.connect_ex((sys.argv[1], int(sys.argv[2]))) == 0 else 1)
PYPORT
then
    echo "Port 7125 is already in use; refusing to connect to an unknown service." >&2
    exit 1
fi

FAKE_PYTHON=python3
if ! python3 -c 'import websockets' >/dev/null 2>&1; then
    VENV="${XDG_CACHE_HOME:-$HOME/.cache}/guppyscreen/fake-moonraker-venv"
    if [[ ! -x "$VENV/bin/python" ]]; then
        if ! python3 -m venv "$VENV"; then
            echo "Python venv support is missing. Install python3-venv first." >&2
            exit 1
        fi
    fi
    if ! "$VENV/bin/python" -c 'import websockets' >/dev/null 2>&1; then
        "$VENV/bin/python" -m pip install --disable-pip-version-check websockets
    fi
    FAKE_PYTHON="$VENV/bin/python"
fi

GCODE_LOG="$GCODE_LOG" "$FAKE_PYTHON" "$ROOT/tools/fake_moonraker.py" \
    25 0 --mesh bowl >"$FAKE_LOG" 2>&1 &
FAKE_PID=$!

cleanup() {
    if kill -0 "$FAKE_PID" 2>/dev/null; then
        kill "$FAKE_PID" 2>/dev/null || true
        wait "$FAKE_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

ready=false
for _ in {1..50}; do
    if ! kill -0 "$FAKE_PID" 2>/dev/null; then
        cat "$FAKE_LOG" >&2
        echo "Fake Moonraker exited during startup." >&2
        exit 1
    fi
    if python3 - 127.0.0.1 7125 <<'PYREADY'
import socket
import sys

with socket.socket() as sock:
    sock.settimeout(0.2)
    sys.exit(0 if sock.connect_ex((sys.argv[1], int(sys.argv[2]))) == 0 else 1)
PYREADY
    then
        ready=true
        break
    fi
    sleep 0.1
done

if [[ "$ready" != true ]]; then
    cat "$FAKE_LOG" >&2
    echo "Fake Moonraker did not open port 7125." >&2
    exit 1
fi

printf 'Fake Moonraker log: %s\n' "$FAKE_LOG"
printf 'Received G-code log: %s\n' "$GCODE_LOG"
printf 'Close the simulator window to stop the fake service.\n'
"$SIM"