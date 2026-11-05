// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Previously expected to fail: VLA size assertion in cast expression.
// emitExplicitCastExprType was not passing the CIRGenFunction to register
// VLA sizes, so getVLASize failed when encountering VLA types in casts.

void foo(void) {
  char a[1];
  int t = 1;
  ((char (*)[t]) a)[0][0] = 0;
}

// CHECK: define{{.*}} void @foo()
// CHECK: load i32, ptr %{{.*}}
// CHECK: mul nsw i64 0, %{{.*}}
// CHECK: store i8 0

// OGCG: define{{.*}} void @foo()
// OGCG: load i32, ptr %{{.*}}
// OGCG: mul nsw i64 0, %{{.*}}
// OGCG: store i8 0
