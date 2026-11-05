// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++20 -fcxx-exceptions \
// RUN:   -fexceptions -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++20 -fcxx-exceptions \
// RUN:   -fexceptions -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Verify that captured statement calls use invoke (not call) when there
// are active RAII cleanups, matching OGCG's EmitCallOrInvoke.

struct Foo {
  ~Foo();
};

void test() {
  Foo f;
  #pragma clang __debug captured
  {
    (void)f;
  }
}

// Both should use invoke for the captured stmt call:
// CIR: invoke void @__captured_stmt
// CIR: landingpad

// OGCG: invoke void @__captured_stmt
// OGCG: landingpad
