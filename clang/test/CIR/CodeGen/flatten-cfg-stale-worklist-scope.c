// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o /dev/null

// FlattenCFG used raw block erasure while rewriting nested scopes,
// which could leave stale ops on the greedy worklist and crash direct CIR
// lowering.

int nested_scope_loop(int *p, int n) {
  {
    for (int i = 0; i < n; ++i) {
      {
        int x = i + 1;
        p[i] = x;
      }
    }
  }
  return 0;
}

// CIR-LABEL: cir.func{{.*}} @nested_scope_loop(
// CIR:         cir.scope {
// CIR:           cir.scope {
// CIR:             cir.for : cond {
// CIR:               cir.scope {
// CIR:                 cir.scope {
// CIR:         cir.return
