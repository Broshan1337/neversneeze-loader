# Neversnooze Loader

Qt6 GUI loader for the Neversnooze modules (CS2 and TF2). It replaces the old
`inject.sh` flow: detects which game is running, does the Steam preload step,
injects the module, and shows a log panel with everything that used to be printed
to the console.

## How it works

- **Game detection** — finds the running game (CS2 / `tf_linux64`) and picks the
  matching module.
- **Steam step** — the loader first asks whether to inject the small Steam module
  into the running Steam process (gdb + dlopen round). This step can be skipped.
- **Module injection** — the game module is loaded with `memfd_create`: the
  library exists only in RAM (`/proc/<pid>/fd/<n>`), nothing is written to disk.
  There is a gdb-based fallback path if the direct write fails.
- **Log panel** — every step is timestamped in the UI.
- **Root** — needs root, same as the old script. `run.sh` re-execs through
  `pkexec` so you get a native password dialog; `Neversnooze.desktop` /
  `neversnooze-loader.desktop` can be copied to the desktop for double-click use.

## Payloads

`tools/payload_packer.cpp` packs the module builds into **encrypted + signed
payloads** that are embedded into the loader binary:

- On a dev machine with the source tree, a freshly built module always wins —
  the loader uses the newest local build.
- Embedded payloads are the fallback for machines without the tree, so friends
  only need the loader binary.
- Every module carries a **loader-stamped trailer** that is verified at module
  init. Verification fails closed: a module that is dumped and re-injected
  standalone does nothing. Loader death releases orphaned modules after a
  timeout.

## Ship pipeline

`./rebuild-ship.sh` builds the whole chain in one go: the dev GCC tree, the
obfuscated ship tree, the Steam module, TF2, and finally the loader with the
newest payloads embedded. `run-ship.sh` runs the shipped build.

## VMProtect build

The shipped artifact is a VMProtect-processed loader:

1. `cmake -B build-vmp -S .` produces the **stock-clang, unstripped** input
   binary (never obfuscated — VMP cannot parse obfuscator output, so the two
   are never combined in one binary).
2. The `GNU_PROPERTY` program header is dropped and `elf_shift.py` makes room
   for VMP's extra segment.
3. VMProtect Ultra is applied to a small set of crown-jewel functions only.
4. The VMP output is already symbol-free and becomes the shipped loader.

## Anti-reversing

- Optional Arkari (LLVM fork) obfuscation for the loader itself
  (`-DNS_LOADER_OBFUSCATE=ON`), with per-function policy via
  `src/ObfAnnotations.h` — hot paths are never annotated.
- `src/AntiDebug.h` — basic debugger/analysis checks.
- Identity strings and patterns are kept out of plaintext rodata.

## Building

```sh
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

`proton/` contains the Windows/Proton-side injector sources (cross-compiled
with mingw, built automatically as part of the ship pipeline).
