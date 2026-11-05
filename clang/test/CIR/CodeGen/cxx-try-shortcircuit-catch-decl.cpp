// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=gnu++20 -fcxx-exceptions -fexceptions -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=gnu++20 -fcxx-exceptions -fexceptions -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=gnu++20 -fcxx-exceptions -fexceptions -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

// Regression test: catch-parameter declarations must not be emitted twice when
// a try/catch appears in the else-arm reached through short-circuit if CFG.
void h();

int f(bool a, bool b) {
  if (a && b)
    return 0;
  else {
    try {
      h();
    } catch (int e) {
      return e;
    }
  }
  return 1;
}

// CIR-LABEL: cir.func{{.*}} @_Z1fbb
// CIR:       cir.alloca !s32i, !cir.ptr<!s32i>, ["e"]
// CIR-NOT:   cir.alloca !s32i, !cir.ptr<!s32i>, ["e"]
// CIR:       cir.return

// LLVM-LABEL: define{{.*}} @_Z1fbb
// LLVM:       landingpad

// OGCG-LABEL: define{{.*}} @_Z1fbb
// OGCG:       landingpad
