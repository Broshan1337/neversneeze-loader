#!/bin/bash

DIR="$(cd "$(dirname "$0")" && pwd)"

BIN=""
for CAND in "$DIR/Loader_vmp" "$DIR/Loader"; do
    if [ -x "$CAND" ]; then
        BIN="$CAND"
        break
    fi
done
if [ -z "$BIN" ] && [ -f "$DIR/Loader_vmp" ]; then
    chmod +x "$DIR/Loader_vmp" 2>/dev/null && BIN="$DIR/Loader_vmp"
fi
if [ -z "$BIN" ] && [ -f "$DIR/Loader" ]; then
    chmod +x "$DIR/Loader" 2>/dev/null && BIN="$DIR/Loader"
fi
if [ -z "$BIN" ]; then
    echo "Neversnooze: loader binary not found next to $0" >&2
    exit 1
fi

if [ "$(id -u)" -eq 0 ]; then
    exec "$BIN"
fi


xhost +si:localuser:root >/dev/null 2>&1 || true

exec pkexec env \
    HOME="$HOME" USER="$USER" LOGNAME="$LOGNAME" \
    DISPLAY="${DISPLAY-}" XAUTHORITY="${XAUTHORITY-}" \
    QT_QPA_PLATFORM=xcb \
    "$BIN"
