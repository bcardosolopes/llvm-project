// /
// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fno-clangir-direct-lowering -emit-mlir=core %s -o %t.mlir 2>&1 | FileCheck %s

void for_continue() {
  for (int i = 0; i < 100; i++)
    continue;
}

// The unbraced loop body now gets a cir.scope wrapper, which causes the
// through-MLIR lowering to fail because the CIRScopeOpLowering converts
// the scope to memref.alloca_scope before the loop lowering can handle
// the cir.continue.
// CHECK: error: 'cir.continue' op must be within a loop
