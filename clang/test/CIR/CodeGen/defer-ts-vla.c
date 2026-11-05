// RUN: %clang_cc1 -triple x86_64-unknown-linux -std=c23 -fdefer-ts -fclangir -emit-llvm %s -o /dev/null -O1 -disable-llvm-passes
// RUN: %clang_cc1 -triple x86_64-unknown-linux -std=c23 -fdefer-ts -fclangir -emit-llvm %s -o - -O1 -disable-llvm-passes | FileCheck %s

#define defer _Defer

// Direct-CIR emits deferred cleanups inline along each exit edge. Re-emitting
// the same cleanup must recompute VLA sizes when a previously cached SSA value
// does not dominate the new cleanup path.
//
// CHECK-LABEL: define {{.*}} i32 @defer_vla(
// CHECK: store i32 10, ptr %[[RETVAL:.*]], align 4
// After cir-flatten-cfg's unifyRetvalMultiReturns, both return paths converge
// on a single ret that loads the shared retval slot.
// CHECK: store i32 7, ptr %[[RETVAL]], align 4
// CHECK: load i32, ptr %[[RETVAL]]
// CHECK: ret i32 %{{.*}}
int defer_vla(int x, int *p) {
  defer {
    int a[x];
    a[2] = 3;
    *p = a[2];
  }
  if (x < 5)
    return 10;
  defer {
    int b[x];
    b[2] = 4;
    *p = b[2];
  }
  return 7;
}
