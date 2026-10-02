#!/usr/bin/env sh
# Compile, and optionally flash, the firmware with arduino-cli.
#
#   sh build.sh                      # compile only
#   sh build.sh COM3                 # compile + flash over USB
#   sh build.sh ota [host]           # compile + flash over WiFi
#
# OTA needs the admin password: set PRINTR_PASSWORD, or you'll be asked.
# Host defaults to $PRINTR_HOST, then printr.local.
#
# Environment: ARDUINO_CLI (path to arduino-cli), FQBN, LIBS (extra library dir).
# Requires ESP8266 core 3.1.x and the libraries listed in README.md.
set -e
cd "$(dirname "$0")"

CLI="${ARDUINO_CLI:-arduino-cli}"
if ! command -v "$CLI" >/dev/null 2>&1; then
    # Arduino IDE 2 bundles arduino-cli; use it if there's none on PATH.
    for c in "$LOCALAPPDATA/Programs/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe" \
             "/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"; do
        [ -x "$c" ] && CLI="$c" && break
    done
fi
# NodeMCU / D1 mini with the 4 MB flash layout (2 MB LittleFS, ~1 MB for OTA).
FQBN="${FQBN:-esp8266:esp8266:nodemcuv2:eesz=4M2M}"
if [ -z "$LIBS" ] && [ -d "$HOME/Documents/Arduino/libraries" ]; then
    LIBS="$HOME/Documents/Arduino/libraries"
fi

"$CLI" compile --fqbn "$FQBN" --warnings all ${LIBS:+--libraries "$LIBS"} --output-dir build printr

case "$1" in
    "")
        ;;
    ota)
        HOST="${2:-${PRINTR_HOST:-printr.local}}"
        if [ -z "$PRINTR_PASSWORD" ]; then
            printf "Admin password for %s: " "$HOST"
            stty -echo 2>/dev/null || true
            read -r PRINTR_PASSWORD
            stty echo 2>/dev/null || true
            echo
        fi
        echo "Uploading build/printr.ino.bin to http://$HOST/update ..."
        code=$(curl -s -o /tmp/printr-ota.txt -w "%{http_code}" -m 180 \
            -u "admin:$PRINTR_PASSWORD" -F "firmware=@build/printr.ino.bin" "http://$HOST/update") || true
        if [ "$code" = "200" ]; then
            echo "Done - the printer is rebooting into the new firmware."
        else
            echo "Upload failed (HTTP $code): $(cat /tmp/printr-ota.txt 2>/dev/null)" >&2
            exit 1
        fi
        ;;
    *)
        "$CLI" upload --fqbn "$FQBN" -p "$1" --input-dir build printr
        ;;
esac
