# Neversnooze loader: Qt6 GUI that packs + injects the game modules.
#
# The CMake tree normally reads the payload sources from the sibling game trees
# (../cs2/build-obf-test/..., ../tf2/build/..., ../cs2/inject_memfd). Inside the
# build sandbox those paths are synthesized in preConfigure from the gamesense
# flake's packages, so the loader embeds fully Nix-built, encrypted + signed
# payloads and stays self-contained.
#
# Two deviations from the manual ship pipeline, both noted inline:
#  - the CS2 payload is the plain GCC build (the Arkari-obfuscated artifact needs
#    the external OLLVM fork and VMProtect, which cannot run in a Nix sandbox)
#  - the signing keydir comes from the shared key-material derivation instead of
#    ~/.config/neversnooze-keys, so loader + modules always agree on keys
{
  lib,
  stdenv,
  cmake,
  ninja,
  qt6,
  openssl,
  binutils,
  gdb,
  mingwGcc,
  mcfgthreads,
  cs2Module,
  steamModule,
  tf2Module,
  injector,
  keyMaterial,
}:

stdenv.mkDerivation {
  pname = "neversnooze-loader";
  version = "unstable";

  # Same rationale as the gamesense packages: a stale <src>/build/CMakeCache.txt
  # (present whenever this flake is consumed as a path input) would poison configure.
  src = lib.cleanSourceWith {
    src = ./..;
    filter = path: type: !(type == "directory" && lib.hasPrefix "build" (baseNameOf path));
  };

  nativeBuildInputs = [
    cmake
    ninja
    qt6.wrapQtAppsHook
    binutils # ld.bfd -r -b binary for the payload blob objects
  ];
  buildInputs = [
    qt6.qtbase
    openssl # payload_packer host tool (libcrypto is NOT linked into the loader)
  ];

  preConfigure = ''
    # Stage the payloads exactly where the loader's CMakeLists looks for them. The
    # tree references its siblings as ../cs2 / ../tf2 - i.e. OUTSIDE the loader
    # source root, which unpacks to $NIX_BUILD_TOP/source - so the staging dirs are
    # created one level up.
    # p0 cs2 <- cs2 module (Nix build = plain GCC; see header note)
    # p1 tf2 <- tf2 module
    # p2 steam <- 32-bit Steam module
    # p3 injector <- inject_memfd (statically linked)
    mkdir -p ''${NIX_BUILD_TOP}/cs2/build-obf-test/Source \
             ''${NIX_BUILD_TOP}/cs2/build-steam/Source \
             ''${NIX_BUILD_TOP}/tf2/build/Source
    cp ${cs2Module}/lib/libMangoHud.so ''${NIX_BUILD_TOP}/cs2/build-obf-test/Source/libMangoHud.so
    cp ${tf2Module}/lib/libMangoHud.so ''${NIX_BUILD_TOP}/tf2/build/Source/libMangoHud.so
    cp ${steamModule}/lib/libSteamModule.so ''${NIX_BUILD_TOP}/cs2/build-steam/Source/libSteamModule.so
    cp ${injector}/bin/inject_memfd ''${NIX_BUILD_TOP}/cs2/inject_memfd

    # ns_inject.exe is linked by the cross gcc invoked directly from a custom command,
    # outside any cross-stdenv context - so the mcfgthread -L path its driver spec
    # needs is not set up automatically. Feed it to the wrapper explicitly (both the
    # plain and target-suffixed forms, whichever wrapper generation reads which).
    export NIX_LDFLAGS="-L${mcfgthreads}/lib $NIX_LDFLAGS"
    export NIX_LDFLAGS_x86_64_w64_mingw32="-L${mcfgthreads}/lib"
  '';

  cmakeFlags = [
    # signing keys live in the shared key-material derivation (world-readable store
    # path, see gamesense#key-material); `payload_packer keygen` is a no-op on an
    # existing keydir, so the build's .key_ready stamp still fires
    "-D_keydir=${keyMaterial}/keys"
    # ns_inject.exe - the Windows-side half of the Proton DLL injector
    "-DMINGW_GXX=${mingwGcc}/bin/x86_64-w64-mingw32-g++"
  ];

  # gdb is exec'd at runtime for the Steam-module injection round
  qtWrapperArgs = [
    "--prefix PATH : ${lib.makeBinPath [gdb]}"
  ];

  installPhase = ''
    runHook preInstall
    install -Dm555 Loader $out/bin/Loader
    install -Dm555 ns_inject.exe $out/bin/ns_inject.exe
    runHook postInstall
  '';

  meta = {
    description = "Neversnooze loader - Qt6 GUI injector with embedded encrypted game-module payloads";
    license = lib.licenses.unfree;
    platforms = ["x86_64-linux"];
    mainProgram = "Loader";
  };
}
