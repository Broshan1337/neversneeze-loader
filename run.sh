#!/bin/bash
# Neversneeze Loader launcher: elevates to root with a GUI password prompt (pkexec).
DIR="$(cd "$(dirname "$0")" && pwd)"
BIN="$DIR/build/Loader"

if [ ! -x "$BIN" ]; then
    echo "Loader is not built. Run:  cmake -B build -S . && cmake --build build"
    exit 1
fi

if [ "$(id -u)" -eq 0 ]; then
    exec "$BIN"
fi

xhost +si:localuser:root >/dev/null 2>&1 || true

exec pkexec env \
    HOME="$HOME" USER="$USER" LOGNAME="$LOGNAME" \
    DISPLAY="${DISPLAY-}" WAYLAND_DISPLAY="${WAYLAND_DISPLAY-}" \
    XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR-}" XAUTHORITY="${XAUTHORITY-}" \
    QT_QPA_PLATFORM=xcb \
    "$BIN"
