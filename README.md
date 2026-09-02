# Neversneeze Loader

Qt6 GUI front-end for the injector flow that used to live in `inject.sh`.

## What it does

- **Steam card** - asks "Inject module into Steam?" (yes/no), then runs the same
  gdb + dlopen round as `inject.sh` against the running Steam process.
- **CS2 card** - X while the Steam step is pending, `?` once it is decided,
  checkmark once `libOsiris.so` is mapped. Release/Debug picker, re-injection
  guard, staleness warning, memfd injection with gdb fallback - same as the script.
- **Log panel** - everything the old script printed, timestamped.

The GUI performs no interactive console prompts; dialogs replace them.

## Build

```sh
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Run (needs root, like inject.sh)

```sh
./run.sh
```

`run.sh` grants root access to the X server for this session and re-execs the
binary through `pkexec`, so you get a native password dialog when double-clicked
from the desktop. `neversneeze-loader.desktop` can be copied to `~/Desktop`.
