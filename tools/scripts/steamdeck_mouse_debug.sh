#!/usr/bin/env bash
# Place this beside pokeplatinum.AppImage and add this script to Steam as the
# non-Steam game's target to capture logs from *Gaming Mode* (not Desktop).
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
APPIMAGE="$HERE/pokeplatinum.AppImage"
LOGDIR="${XDG_STATE_HOME:-$HOME/.local/state}/pokeplatinum"
mkdir -p "$LOGDIR"
LOG="$LOGDIR/steamdeck-mouse.log"
if [[ ! -x "$APPIMAGE" ]]; then
    printf 'AppImage is missing or not executable: %s\n' "$APPIMAGE" >&2
    exit 1
fi
export POKEPLATINUM_MOUSE_DEBUG=1
printf '[mouse-test] Starting %s\n' "$APPIMAGE" > "$LOG"
exec "$APPIMAGE" "$@" >> "$LOG" 2>&1
