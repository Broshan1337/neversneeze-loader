#pragma once

// Per-function Arkari (goron-derived OLLVM) obfuscation opt-in for the LOADER.
// Same scheme as the game module's Utils/ObfAnnotations.h: stock toolchains expand
// these to nothing; the Arkari clang turns them into function metadata consumed by
// the passes (cfg: ~/obfuscator/arkari-cfg/cs2.cfg carries the seed + global cse).
//
// Loader targets: payload verify/decrypt/stream, session trailer stamp, anti-debug.
// Qt UI code is deliberately NOT annotated.
#if defined(NS_LOADER_OBFUSCATE)
#define NS_OBF_FLATTEN __attribute__((annotate("+fla")))
#define NS_OBF_ICALL __attribute__((annotate("+icall")))
#define NS_OBF_INDGV __attribute__((annotate("+indgv")))
#else
#define NS_OBF_FLATTEN
#define NS_OBF_ICALL
#define NS_OBF_INDGV
#endif
