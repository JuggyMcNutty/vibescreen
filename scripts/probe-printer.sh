#!/usr/bin/env bash
#
# Collect hardware and firmware facts from a target printer over SSH.
#
# Strictly read only. Every command below either reads a file, reads sysfs, or
# does a GET against the local Moonraker. Nothing is written, installed,
# started, or stopped on the printer.
#
# Usage:
#   PRINTER_HOST=192.168.1.202 PRINTER_PASS=... scripts/probe-printer.sh
#   PRINTER_HOST=192.168.1.202 scripts/probe-printer.sh       # uses ssh keys
#
# Credentials come from the environment on purpose. Do not hardcode the stock
# Creality password here, and do not commit the raw output without redacting
# the WiFi SSID, PSK, MAC addresses and printer serial first.

set -uo pipefail

PRINTER_HOST="${PRINTER_HOST:-}"
PRINTER_USER="${PRINTER_USER:-root}"
PRINTER_PASS="${PRINTER_PASS:-}"

if [ -z "$PRINTER_HOST" ]; then
    echo "ERROR: set PRINTER_HOST (for example PRINTER_HOST=192.168.1.202)" >&2
    exit 1
fi

SSH_OPTS=(-o StrictHostKeyChecking=accept-new -o ConnectTimeout=10 -o LogLevel=ERROR)

if [ -n "$PRINTER_PASS" ]; then
    if ! command -v sshpass >/dev/null; then
        echo "ERROR: PRINTER_PASS is set but sshpass is not installed" >&2
        exit 1
    fi
    run_remote() { sshpass -p "$PRINTER_PASS" ssh "${SSH_OPTS[@]}" "$PRINTER_USER@$PRINTER_HOST" "$1"; }
else
    run_remote() { ssh -o BatchMode=yes "${SSH_OPTS[@]}" "$PRINTER_USER@$PRINTER_HOST" "$1"; }
fi

section() {
    printf '\n## %s\n\n```\n' "$1"
    run_remote "$2" 2>&1
    printf '```\n'
}

printf '# Printer probe: %s\n\n' "$PRINTER_HOST"
printf 'Collected %s by scripts/probe-printer.sh. Read only, nothing was\n' "$(date -u '+%Y-%m-%d %H:%M UTC')"
printf 'modified on the printer.\n'

section "Kernel and system" \
    'uname -a; echo; cat /proc/version'

section "CPU" \
    'cat /proc/cpuinfo'

section "Memory and storage" \
    'free -m; echo; df -h; echo; head -5 /proc/meminfo'

section "Firmware version" \
    'for f in /etc/os-release /usr/share/version /etc/ota_info /etc/version /usr/data/creality/userdata/config/system_version.json; do
       [ -f "$f" ] && { echo "--- $f"; cat "$f"; echo; }
     done'

section "C library and dynamic loader" \
    'ls -la /lib/ld-* 2>/dev/null; echo;
     ls -la /lib/libc.so* 2>/dev/null; echo;
     (strings /lib/libc.so.6 2>/dev/null || strings /lib/libc.so.0 2>/dev/null) | grep -i "GNU C Library" | head -3'

section "Framebuffer" \
    'for f in /sys/class/graphics/fb0/virtual_size /sys/class/graphics/fb0/bits_per_pixel \
              /sys/class/graphics/fb0/stride /sys/class/graphics/fb0/rotate \
              /sys/class/graphics/fb0/name /sys/class/graphics/fb0/modes; do
       [ -r "$f" ] && echo "$f = $(cat $f)"
     done
     echo; command -v fbset >/dev/null && fbset -i 2>&1'

section "Input devices" \
    'cat /proc/bus/input/devices; echo; ls -la /dev/input/'

# The K1 ships a cut-down curl that rejects -s and --max-time, so use wget,
# which busybox always provides.
section "Moonraker server info" \
    'wget -q -T 10 -O - http://localhost:7125/server/info'

section "Klipper printer info" \
    'wget -q -T 10 -O - http://localhost:7125/printer/info'

section "Moonraker machine system info" \
    'wget -q -T 10 -O - http://localhost:7125/machine/system_info'

section "Existing install state" \
    'ls -la /usr/data/ 2>/dev/null; echo;
     echo "--- /etc/init.d"; ls /etc/init.d/; echo;
     echo "--- guppyscreen present?"; ls -la /usr/data/guppyscreen/ 2>/dev/null || echo "not installed";
     echo; echo "--- .version"; cat /usr/data/guppyscreen/.version 2>/dev/null || echo "none"'

# Drop the api key line on the printer so it never crosses the wire.
section "Installed guppyconfig.json (api key line stripped)" \
    'grep -v moonraker_api_key /usr/data/guppyscreen/guppyconfig.json 2>/dev/null || echo "not installed"'

section "wpa_supplicant sockets" \
    'ls -la /var/run/wpa_supplicant/ 2>/dev/null || echo "no /var/run/wpa_supplicant"'

section "Running processes" \
    'ps w 2>/dev/null || ps'

printf '\n'
