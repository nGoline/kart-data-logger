#!/usr/bin/env bash
#
# Find the dash on USB and flash it, after showing you what is about to be built.
#
# Exists because platformio.ini pins `upload_port = /dev/cu.usbmodem1101`, which is
# one particular machine's enumeration — elsewhere the same board comes up as
# /dev/cu.usbmodem101 and `pio run -t upload` fails on a port that isn't there. This
# discovers the port by USB VID:PID instead and passes it with --upload-port, which
# overrides the pinned value.
#
# Usage: tools/flash.sh [-e ENV] [-p PORT] [-y] [-m] [-- pio args...]
#
#   -e ENV    PlatformIO env (default: default_envs from platformio.ini)
#   -p PORT   skip discovery and use this port
#   -y        don't ask for confirmation
#   -m        open the serial monitor after a successful flash
#
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

ENV_NAME=""
PORT=""
ASSUME_YES="${FLASH_YES:-0}"
MONITOR=0

while getopts ":e:p:ymh" opt; do
    case "$opt" in
        e) ENV_NAME="$OPTARG" ;;
        p) PORT="$OPTARG" ;;
        y) ASSUME_YES=1 ;;
        m) MONITOR=1 ;;
        h) sed -n '3,17p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        \?) echo "flash: unknown option -$OPTARG (try -h)" >&2; exit 2 ;;
        :)  echo "flash: -$OPTARG needs an argument" >&2; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
[[ "${1:-}" == "--" ]] && shift

bold() { printf '\033[1m%s\033[0m' "$1"; }
warn() { printf '\033[33m%s\033[0m\n' "$1"; }
die()  { printf '\033[31mflash: %s\033[0m\n' "$1" >&2; exit 1; }

command -v pio >/dev/null 2>&1 || die "pio not on PATH — activate the PlatformIO venv first."

# --- env ------------------------------------------------------------------
if [[ -z "$ENV_NAME" ]]; then
    ENV_NAME="$(sed -n 's/^[[:space:]]*default_envs[[:space:]]*=[[:space:]]*\([^[:space:],]*\).*/\1/p' platformio.ini | head -1)"
    [[ -n "$ENV_NAME" ]] || die "no default_envs in platformio.ini — pass -e ENV."
fi
grep -q "^\[env:${ENV_NAME}\]" platformio.ini || die "no [env:${ENV_NAME}] in platformio.ini."

# --- discovery ------------------------------------------------------------
# 303A is Espressif's own VID (the S3's native USB-serial/JTAG, 303A:1001). The rest
# are the usual external UART bridges, in case this is ever run against a devkit that
# has one: 10C4 = CP210x, 1A86 = CH340/CH9102, 0403 = FTDI.
KNOWN_VIDS="303A 10C4 1A86 0403"

DESC=""
SERIAL=""
VIDPID=""

if [[ -z "$PORT" ]]; then
    # Only discovery needs an interpreter, so this is checked here rather than up top:
    # -p PORT stays usable on a box without python3. Without the guard a missing
    # interpreter reads as "no ESP32 found on USB", which sends you hunting a cable.
    command -v python3 >/dev/null 2>&1 || \
        die "python3 not on PATH — needed to parse the port list. Pass -p PORT to skip discovery."

    # `pio device list` reports hwid as "USB VID:PID=303A:1001 SER=... LOCATION=...",
    # which beats parsing ioreg and behaves the same on Linux.
    MATCHES="$(pio device list --json-output 2>/dev/null | VIDS="$KNOWN_VIDS" python3 -c '
import json, os, re, sys
vids = os.environ["VIDS"].split()
for d in json.load(sys.stdin):
    hwid = d.get("hwid") or ""
    m = re.search(r"VID:PID=([0-9A-Fa-f]{4}):([0-9A-Fa-f]{4})", hwid)
    if not m:
        continue
    vid, pid = m.group(1).upper(), m.group(2).upper()
    if vid not in vids:
        continue
    ser = re.search(r"SER=(\S+)", hwid)
    print("\t".join([d.get("port",""), f"{vid}:{pid}",
                     d.get("description") or "?", ser.group(1) if ser else ""]))
')" || die "could not enumerate USB serial ports."

    [[ -n "$MATCHES" ]] || die "no ESP32 found on USB. Check the cable, and that it is a data cable."

    # More than one candidate: narrowing to Espressif's native USB is almost always
    # the right answer, but if that is still ambiguous, make the human choose.
    if [[ "$(wc -l <<<"$MATCHES")" -gt 1 ]]; then
        NATIVE="$(grep -F $'\t'"303A:" <<<"$MATCHES" || true)"
        if [[ -n "$NATIVE" && "$(wc -l <<<"$NATIVE")" -eq 1 ]]; then
            MATCHES="$NATIVE"
        else
            echo "flash: more than one candidate — pick one with -p PORT:" >&2
            while IFS=$'\t' read -r p v d s; do
                printf '  %-24s %s  %s\n' "$p" "$v" "$d" >&2
            done <<<"$MATCHES"
            exit 1
        fi
    fi

    IFS=$'\t' read -r PORT VIDPID DESC SERIAL <<<"$MATCHES"
fi

[[ -e "$PORT" ]] || die "$PORT does not exist."

# --- what is about to be built -------------------------------------------
# FW_VERSION is `git describe --tags --always --dirty` (platformio.ini:40) and shows
# on the setup screen, so a dirty tree ships a -dirty build. Surface that here: it is
# easy to flash uncommitted edits you have forgotten about.
FW_VERSION="$(git describe --tags --always --dirty 2>/dev/null || echo unknown)"
DIRTY="$(git status --porcelain --untracked-files=no 2>/dev/null || true)"

echo
echo "$(bold 'Target')"
printf '  %-10s %s\n' "Port" "$PORT"
[[ -n "$DESC"   ]] && printf '  %-10s %s%s\n' "Device" "$DESC" "${VIDPID:+  ($VIDPID)}"
[[ -n "$SERIAL" ]] && printf '  %-10s %s\n' "Serial" "$SERIAL"
printf '  %-10s %s\n' "Env" "$ENV_NAME"
printf '  %-10s %s\n' "Version" "$FW_VERSION"
if [[ -n "$DIRTY" ]]; then
    echo
    warn "  ! Uncommitted changes — this flashes your working tree, not $(git rev-parse --short HEAD 2>/dev/null || echo HEAD):"
    sed 's/^/      /' <<<"$DIRTY"
fi
echo

# --- confirm --------------------------------------------------------------
if [[ "$ASSUME_YES" != "1" ]]; then
    # -r /dev/tty only checks the mode bits; opening it still fails under cron, a
    # pipe or a CI runner ("Device not configured"), so prove it opens first.
    if ! { : </dev/tty; } 2>/dev/null; then
        die "no terminal to confirm on — pass -y to flash unattended."
    fi
    reply=""
    read -r -p "Flash this device? [y/N] " reply </dev/tty || true
    case "$reply" in
        y|Y|yes|YES) ;;
        *) echo "Aborted."; exit 1 ;;
    esac
fi

# --- flash ----------------------------------------------------------------
echo
pio run -e "$ENV_NAME" -t upload --upload-port "$PORT" "$@"

if [[ "$MONITOR" == "1" ]]; then
    # The S3's native USB re-enumerates across the reset, so the port can be briefly
    # absent right after upload; wait for it rather than failing on a race.
    for _ in $(seq 1 25); do [[ -e "$PORT" ]] && break; sleep 0.2; done
    echo
    exec pio device monitor -e "$ENV_NAME" --port "$PORT"
fi
