// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o /dev/null

// FlattenCFG used raw block erasure when pruning dead switch-case
// blocks. If the dead block still contained structured control flow, greedy
// rewriting could later pop a stale op and crash.

int dead_case_if(int s, int y) {
  for (;;) {
    switch (s) {
    case 0:
      break;
      if (y)
        return 1;
    case 1:
      s = 0;
      break;
    default:
      return 3;
    }
    return 4;
  }
}

// CIR-LABEL: cir.func{{.*}} @dead_case_if(
// CIR:         cir.for : cond {
// CIR:           cir.scope {
// CIR:             cir.scope {
// CIR:               cir.switch
// CIR:               ^bb1:  // no predecessors
// CIR:                 cir.if
