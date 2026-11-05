// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
//
// Regression test: VLA pointer arithmetic
// should have the inbounds flag on getelementptr.

// CHECK-LABEL: define {{.*}}@vla_ptr_arith(
void vla_ptr_arith(int n) {
    int vla[n];
    int *p = vla;
    // Pointer arithmetic on VLA elements should use inbounds GEP.
    int *q = p + 1;
    *q = 42;
}

// CHECK: getelementptr inbounds i32, ptr
// CHECK: store i32 42
