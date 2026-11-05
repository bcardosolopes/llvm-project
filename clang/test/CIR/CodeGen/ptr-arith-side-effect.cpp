// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Test that pointer arithmetic in emitPointerWithAlignment does not
// cause double evaluation of side-effecting sub-expressions.

int g;
int* side_effect();

// CHECK-LABEL: @_Z14test_deref_addi
// CHECK: call {{.*}} @_Z11side_effectv
// CHECK-NOT: call {{.*}} @_Z11side_effectv
// CHECK: ret i32

// OGCG-LABEL: @_Z14test_deref_addi
// OGCG: call {{.*}} @_Z11side_effectv
// OGCG-NOT: call {{.*}} @_Z11side_effectv
// OGCG: ret i32
int test_deref_add(int n) {
  return *(side_effect() + n);
}

// Test the swapped operand case (int + ptr).
// CHECK-LABEL: @_Z18test_deref_swappedi
// CHECK: call {{.*}} @_Z11side_effectv
// CHECK-NOT: call {{.*}} @_Z11side_effectv
// CHECK: ret i32

// OGCG-LABEL: @_Z18test_deref_swappedi
// OGCG: call {{.*}} @_Z11side_effectv
// OGCG-NOT: call {{.*}} @_Z11side_effectv
// OGCG: ret i32
int test_deref_swapped(int n) {
  return *(n + side_effect());
}
