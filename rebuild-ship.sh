#!/bin/bash
# One-command ship rebuild: builds all three modules, then relinks the loader -
# the payload pack step re-runs automatically whenever a built .so changed (mtime
# DEPENDS), so the embedded payloads always match the freshest builds.
#
# Usage:  ./Loader/rebuild-ship.sh            (all four steps)
#         ./Loader/rebuild-ship.sh cs2        (only the cs2 module + loader)
#         ./Loader/rebuild-ship.sh tf2 steam  (subsets + loader)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

want_all=false
[ $# -eq 0 ] && want_all=true

build() {
    echo "==> $*"
    cmake --build "$@"
}

if $want_all || [[ " $* " == *" cs2 "* ]]; then
    build "$ROOT/cs2/build" --target Neversnooze
    # ship artifact = the Arkari-obfuscated build (NEVERSNOOZE_OBFUSCATE+SHIP, preconfigured)
    build "$ROOT/cs2/build-obf-test" --target Neversnooze
fi
if $want_all || [[ " $* " == *" steam "* ]]; then
    build "$ROOT/cs2/build-steam" --target SteamModule32
fi
if $want_all || [[ " $* " == *" tf2 "* ]]; then
    build "$ROOT/tf2/build" --target NeversnoozeTF2
fi

# Loader LAST: it embeds whatever the module trees just produced.
build "$ROOT/Loader/build"

echo
echo "Ship artifact: $ROOT/Loader/build/Loader"
echo "Payload freshness (pack timestamps):"
ls -l --time-style=+"%Y-%m-%d %H:%M" "$ROOT/Loader/build/payloads/"p?bin | awk '{print "  " $6, $7, $8}'
