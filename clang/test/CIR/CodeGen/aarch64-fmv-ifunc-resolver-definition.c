// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -fclangir -emit-llvm -o /dev/null %s

// Regression test: this used to hit LLVM verifier failure:
//   IFunc resolver must be a definition
// when lowering AArch64 target_version multi-versioning with -fclangir.

__attribute__((target_version("aes"))) int fmv(void) { return 1; }
__attribute__((target_version("default"))) int fmv(void) { return 0; }

int call_fmv(void) { return fmv(); }
