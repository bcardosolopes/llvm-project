// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -fclangir -emit-llvm %s -o /dev/null

// __builtin_flt_rounds — maps to llvm.get.rounding intrinsic (fixed)
int get_rounding_mode(void) {
  return __builtin_flt_rounds();
}
