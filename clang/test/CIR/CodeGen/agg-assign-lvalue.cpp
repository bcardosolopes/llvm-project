// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Test that using an aggregate assignment as an lvalue works correctly.
// This exercises the TEK_Aggregate case in emitBinaryOperatorLValue.

struct S {
  int x;
  int y;
};

int test_agg_assign_lvalue(S &a, S &b) {
  return (a = b).x;
}

// CIR: cir.func {{.*}} @_Z22test_agg_assign_lvalue
// CIR:   cir.copy
// CIR:   cir.get_member
// CIR:   cir.return

// LLVM: define {{.*}} @_Z22test_agg_assign_lvalue
// LLVM:   call void @llvm.memcpy
// LLVM:   getelementptr
// LLVM:   ret i32
