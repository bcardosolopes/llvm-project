// RUN: %clang_cc1 -std=c++20 -triple aarch64-unknown-linux-gnu -fexceptions \
// RUN:   -fcxx-exceptions -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Regression test: when CC lowering coerces a try_call's return type (e.g.
// struct{int,int} -> i64 on AArch64), the return value coercion ops must be
// placed in the normal destination block, not after the TryCallOp terminator
// in the same block. Placing ops after a terminator produces blocks without
// terminators after CIR-to-LLVM lowering, crashing in attachLoopAccessGroups
// or causing "Broken module" LLVM verifier errors.

struct Small {
  int a, b;
};

struct Guard { ~Guard(); };
Guard::~Guard() {}

// On AArch64, Small is returned in registers as i64.
Small make_small();

// CHECK-LABEL: define{{.*}} i32 @_Z4testv
// CHECK:         invoke i64 @_Z10make_smallv()
// CHECK-NEXT:            to label %[[NORMAL:[0-9]+]] unwind
// CHECK:       [[NORMAL]]:
// CHECK-NEXT:    store i64
int test() {
  Guard g;
  try {
    Small s = make_small();
    return s.a + s.b;
  } catch (...) {
    return -1;
  }
}
