// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -fclangir -emit-llvm %s -o /dev/null

// __builtin_arm_clrex — clears exclusive monitor (fixed)

void test(void) {
  __builtin_arm_clrex();
}
