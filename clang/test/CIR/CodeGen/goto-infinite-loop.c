// RUN: %clang_cc1 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll

// Regression test: cir.return crash with unreachable code
// after infinite goto loops. Previously crashed during CFG flattening
// with "'cir.return' op expects parent op to be one of 'cir.func'".

void f(int a) {
  L1: goto L1;
  L2: if (!a) goto L2;
}

// CHECK: define {{.*}} @f(
// CHECK: br label %[[LOOP:.*]]
// CHECK: [[LOOP]]:
// CHECK-NEXT: br label %[[LOOP]]
