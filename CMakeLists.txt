cmake_minimum_required(VERSION 3.21)
project(NeversnoozeLoader LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
find_package(OpenSSL REQUIRED)

# ---- embedded payloads -----------------------------------------------------------------
#
# The newest game modules are packed (encrypted + Ed25519-signed) into the loader binary,
# so the loader is fully self-contained: the friend's machine needs ONLY the loader.
#  - packer:     tools/payload_packer.cpp (host tool, not shipped)
#  - keydir:     ~/.config/neversnooze-keys (private key NEVER leaves this machine;
#                created on first configure via `payload_packer keygen`)
#  - blobs:      p{0..3}bin -> ld -r -b binary objects linked into the loader
#  - keys:       the Ed25519 PUBLIC key + the XOR-masked symmetric key are emitted as a
#                generated header (payload_keys.h) included by src/Payloads.cpp
#
# Blob sources (the newest builds at loader-build time):
# CS2 ships the ARKARI-OBFUSCATED artifact (build-obf-test: NEVERSNOOZE_OBFUSCATE+SHIP);
# the plain GCC build in cs2/build stays the daily dev tree and wins locally on dev machines.
# Override for a debug round: cmake -B build -S . -DNS_CS2_MODULE=<abs path to the .so>
set(NS_CS2_MODULE "${CMAKE_CURRENT_SOURCE_DIR}/../cs2/build-obf-test/Source/libMangoHud.so" CACHE PATH "CS2 module packed into the loader payload")
set(_tf2_so     "${CMAKE_CURRENT_SOURCE_DIR}/../tf2/build/Source/libMangoHud.so" CACHE PATH "TF2 module packed into the loader payload")
set(_steam_so   ${CMAKE_CURRENT_SOURCE_DIR}/../cs2/build-steam/Source/libSteamModule.so)
set(_injector   ${CMAKE_CURRENT_SOURCE_DIR}/../cs2/inject_memfd)
set(_keydir     "$ENV{HOME}/.config/neversnooze-keys" CACHE PATH "Ed25519 payload-signing key dir")
set(_gen_dir    ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(_blob_dir   ${CMAKE_CURRENT_BINARY_DIR}/payloads)
file(MAKE_DIRECTORY ${_gen_dir} ${_blob_dir})

# host packer
add_executable(payload_packer tools/payload_packer.cpp)
target_link_libraries(payload_packer PRIVATE OpenSSL::Crypto)

# key material: generated on first build (build-time, not configure - the packer binary
# must exist first); ns_payload_keys stamps it once
add_custom_command(
  OUTPUT ${_blob_dir}/.key_ready
  COMMAND ${CMAKE_COMMAND} -E make_directory ${_keydir}
  COMMAND payload_packer keygen ${_keydir}
  COMMAND ${CMAKE_COMMAND} -E touch ${_blob_dir}/.key_ready
  DEPENDS payload_packer
  COMMENT "Ensuring payload signing keys in ${_keydir}")
add_custom_command(
  OUTPUT ${_gen_dir}/payload_keys.h
  COMMAND payload_packer headers ${_keydir} ${_gen_dir}/payload_keys.h
  DEPENDS payload_packer ${_blob_dir}/.key_ready
  COMMENT "Emitting payload key header")
add_custom_target(ns_payload_keys ALL DEPENDS ${_gen_dir}/payload_keys.h)

function(ns_pack_blob index name source)
  set(_blob ${_blob_dir}/p${index}bin)
  set(_obj ${_blob_dir}/p${index}.o)
  add_custom_command(
    OUTPUT ${_blob}
    COMMAND payload_packer pack ${source} ${_blob} ${_keydir} ${name}
    DEPENDS payload_packer ${source} ${_blob_dir}/.key_ready
    COMMENT "Packing payload p${index} (${name})")
  add_custom_command(
    OUTPUT ${_obj}
    # chdir so the blob's FILENAME (not its absolute path) drives the symbol names:
    # p0bin -> _binary_p0bin_start/_end (what Payloads.cpp declares)
    # GNU ld explicitly: with a clang/lld toolchain CMAKE_LINKER is lld, and lld's
    # `-r -b binary` needs an explicit emulation; bfd handles it out of the box.
    COMMAND ${CMAKE_COMMAND} -E chdir ${_blob_dir} ld.bfd -r -b binary -o ${_obj} p${index}bin
    DEPENDS ${_blob}
    COMMENT "Linking payload p${index} into an object")
  set(NS_BLOB_OBJS ${NS_BLOB_OBJS} ${_obj} PARENT_SCOPE)
endfunction()

set(NS_BLOB_OBJS "")
if(EXISTS ${NS_CS2_MODULE})
  ns_pack_blob(0 cs2 ${NS_CS2_MODULE})
else()
  message(WARNING "CS2 module not found at ${NS_CS2_MODULE} - loader will be built WITHOUT the embedded CS2 payload")
endif()
if(EXISTS ${_tf2_so})
  ns_pack_blob(1 tf2 ${_tf2_so})
else()
  message(WARNING "TF2 module not found at ${_tf2_so} - loader will be built WITHOUT the embedded TF2 payload")
endif()
if(EXISTS ${_steam_so})
  ns_pack_blob(2 steam ${_steam_so})
else()
  message(WARNING "Steam module not found at ${_steam_so} - loader will be built WITHOUT the embedded Steam payload")
endif()
if(EXISTS ${_injector})
  ns_pack_blob(3 injector ${_injector})
else()
  message(WARNING "inject_memfd not found at ${_injector} - loader will be built WITHOUT the embedded injector")
endif()

qt_add_executable(Loader
    src/main.cpp
    src/Icons.cpp
    src/StatusCard.cpp
    src/Injector.cpp
    src/Payloads.cpp
    src/AdvancedWindow.cpp
    src/MainWindow.cpp
)

# plain CMake target_sources: qt_add_executable re-classifies sources and drops raw
# object files, so the payload objects are attached here instead
target_sources(Loader PRIVATE ${NS_BLOB_OBJS})

add_dependencies(Loader ns_payload_keys)
target_include_directories(Loader PRIVATE ${_gen_dir} src)
# libcrypto deliberately NOT linked: Payloads.cpp resolves the EVP functions via
# dlopen/dlsym at runtime, keeping the crypto imports out of the loader's import table.
target_link_libraries(Loader PRIVATE Qt6::Widgets Qt6::Network)

# Exploit mitigations FIRST (this is a ROOT binary - a memory-corruption bug here is
# instant root for whoever triggers it, no reversing required):
#  - PIE (position-independent executable) + full RELRO + BIND_NOW: kills GOT-overwrite
#    and fixed-address ROP against the loader.
#  - stack canaries + _FORTIFY_SOURCE: standard runtime hardening.
#  - -Wl,-z,nodump: the loader's PT_DUMP... no such flag on ELF; nodump is a phdr flag -
#    skipped. The payload ciphertext in .data is what matters and it is already encrypted.
target_compile_options(Loader PRIVATE -fPIE -fstack-protector-strong -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2)
target_link_options(Loader PRIVATE -pie -Wl,-z,relro,-z,now)

qt_add_resources(Loader "assets"
    PREFIX "/assets"
    BASE "assets"
    FILES assets/steam.png assets/cs2.png assets/tf2logo.png assets/chevron.png assets/nslogo.png
)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Arkari OLLVM (loader side): annotate the payload verify/decrypt/stream logic. Configure with
#   -DNS_LOADER_OBFUSCATE=ON -DCMAKE_C(CXX)_COMPILER=~/obfuscator/Arkari/llvm/build/bin/clang++
# VMP then virtualizes the few hottest functions on top (build-vmp tree = unstripped input).
option(NS_LOADER_OBFUSCATE "Arkari OLLVM: loader-side per-function obfuscation" OFF)
set(NS_LOADER_ARKARI_CFG "$ENV{HOME}/obfuscator/arkari-cfg/cs2.cfg" CACHE FILEPATH "Arkari config (seed + cse)")
if(NS_LOADER_OBFUSCATE)
  add_compile_definitions(NS_LOADER_OBFUSCATE)
  add_compile_options("-mllvm" "-arkari-cfg=${NS_LOADER_ARKARI_CFG}")
endif()

# clang depth flags (required by the Arkari clang; harmless for stock clang)
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  add_compile_options("-fconstexpr-depth=4096" "-fbracket-depth=1024" "-ftemplate-depth=2048")
endif()

# Ship hardening: NS_LOADER_SHIP=ON fully strips the distributed loader binary and keeps a
# local .debug copy (same scheme as the game modules - see cs2/Source/CMakeLists.txt).
option(NS_LOADER_SHIP "Ship hardening: fully strip the loader, keep local .debug copy" OFF)
if(NS_LOADER_SHIP AND CMAKE_OBJCOPY)
  set(_loader_debug_dir ${CMAKE_CURRENT_BINARY_DIR}/ship-debug)
  add_custom_command(TARGET Loader POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${_loader_debug_dir}
    COMMAND ${CMAKE_OBJCOPY} --only-keep-debug "$<TARGET_FILE:Loader>"
            "${_loader_debug_dir}/$<TARGET_FILE_NAME:Loader>.debug"
    COMMAND ${CMAKE_STRIP} --strip-all "$<TARGET_FILE:Loader>"
    COMMAND ${CMAKE_OBJCOPY}
            --add-gnu-debuglink="${_loader_debug_dir}/$<TARGET_FILE_NAME:Loader>.debug"
            "$<TARGET_FILE:Loader>")
endif()

# ns_inject.exe - the Windows-side half of the Proton DLL injector (cross-compiled when MinGW
# is available; the Loader runs it through the game's own Proton wine inside the game's prefix).
find_program(MINGW_GXX x86_64-w64-mingw32-g++)
set(NS_INJECT_EXE ${CMAKE_BINARY_DIR}/ns_inject.exe)
if(MINGW_GXX)
    add_custom_command(
        OUTPUT ${NS_INJECT_EXE}
        COMMAND ${MINGW_GXX} -O2 -static -municode -o ${NS_INJECT_EXE}
                ${CMAKE_CURRENT_SOURCE_DIR}/proton/ns_inject.cpp -lkernel32 -luser32 -lshlwapi
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/proton/ns_inject.cpp
        COMMENT "Cross-compiling ns_inject.exe (Proton DLL injector)")
    add_custom_target(ns_inject ALL DEPENDS ${NS_INJECT_EXE})
else()
    message(WARNING "x86_64-w64-mingw32-g++ not found - ns_inject.exe (Proton DLL injector) will NOT be built")
endif()
