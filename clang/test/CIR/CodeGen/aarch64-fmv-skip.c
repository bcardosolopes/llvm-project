// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -fclangir -emit-cir -o /dev/null %s
//
// Previously crashed with assertion:
//   Assertion `getTarget().getTriple().isX86() && "Unsupported target"' failed.
// in emitMultiVersionFunctions() when AArch64 target_version functions
// were processed through the x86-only FMV code path.

__attribute__((target_version("default"))) int fmv(void) { return 0; }
__attribute__((target_version("sha2"))) int fmv(void) { return 1; }
