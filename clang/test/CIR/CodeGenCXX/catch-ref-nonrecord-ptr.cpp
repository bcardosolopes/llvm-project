// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fcxx-exceptions -fexceptions %s -o /dev/null 2>&1
//
// Previously expected to fail: initCatchParam for reference to non-record pointer
// was NYI. Now handled by storing the by-value pointer from __cxa_begin_catch
// into a temporary alloca and binding the reference to that temporary.

void test_catch_ref_ptr() {
  try {
    throw;
  } catch (int **&p) {
  }
}
