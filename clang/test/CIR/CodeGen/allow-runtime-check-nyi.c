// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o /dev/null

// Regression test: __builtin_allow_runtime_check must compile in CIR mode
// without crashing.
_Bool test_allow_runtime_check(void) {
  return __builtin_allow_runtime_check("mycheck");
}
