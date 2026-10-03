#!/bin/bash
# Neversnooze Loader launcher: elevates to root with a GUI password prompt (pkexec).
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

# NOTE: deliberately do NOT pass XDG_RUNTIME_DIR / DBUS_SESSION_BUS_ADDRESS into the
# root session - a root process sharing the user's runtime dir can spawn root-owned
# portal/fuse services over /run/user/1000/doc and wedge every flatpak on the system
# (happened 2026-09-04). The GUI only needs the X11 connect vars.
exec pkexec env \
    HOME="$HOME" USER="$USER" LOGNAME="$LOGNAME" \
    DISPLAY="${DISPLAY-}" XAUTHORITY="${XAUTHORITY-}" \
    QT_QPA_PLATFORM=xcb \
    "$BIN"
