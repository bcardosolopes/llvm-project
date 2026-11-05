// RUN: %clang_cc1 -fclangir -emit-llvm -triple x86_64-unknown-linux-gnu %s -o /dev/null
// This test was previously XFAIL'd (atomic complex cast NYI).
// The feature is now implemented. Verify it compiles without errors.

void test(void) {
  _Atomic(_Complex float) x = 2.0f;
}
