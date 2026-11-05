// RUN: %clang_cc1 -O1 -disable-llvm-passes -triple x86_64-linux-gnu -fclangir -emit-llvm %s -o %t.ll -std=c++20
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=O1

// RUN: %clang_cc1 -O0 -disable-llvm-passes -triple x86_64-linux-gnu -fclangir -emit-llvm %s -o %t.O0.ll -std=c++20
// RUN: FileCheck --input-file=%t.O0.ll %s --check-prefix=O0

// RUN: %clang_cc1 -O0 -disable-llvm-passes -triple x86_64-linux-gnu -emit-llvm %s -o %t.ogcg.O0.ll -std=c++20
// RUN: FileCheck --input-file=%t.ogcg.O0.ll %s --check-prefix=OGCG-O0

// Verify [[likely]] on switch cases emits branch weight metadata at -O1
// but not at -O0. The OptimizationLevel > 0 guard matches OGCG's condition
// for collecting SwitchLikelihood.

extern volatile int i;

void OneCaseL() {
  switch (i) {
  [[likely]] case 1: i = 1; break;
  default: break;
  }
}

// At -O1, branch weights are emitted.
// O1: !{!"branch_weights", i32 {{[0-9]+}}, i32 {{[0-9]+}}}

// At -O0, no branch weights (matches OGCG behavior).
// O0-NOT: branch_weights
// OGCG-O0-NOT: branch_weights
