// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++20 -fcxx-exceptions \
// RUN:   -fexceptions -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++20 -fcxx-exceptions \
// RUN:   -fexceptions -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Verify that __cxa_end_catch in a catch handler uses invoke (not call)
// when there's an outer RAII cleanup (Guard). If __cxa_end_catch throws
// (e.g., exception object destructor), Guard needs cleanup.

struct Guard { Guard(); ~Guard(); };
void may_throw();

int test() {
  Guard g;
  try { may_throw(); }
  catch (...) { return -1; }
  return 0;
}

// Both should use invoke for __cxa_end_catch:
// CIR: invoke void @__cxa_end_catch()
// OGCG: invoke void @__cxa_end_catch()
