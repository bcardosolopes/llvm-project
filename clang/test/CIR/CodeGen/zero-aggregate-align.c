// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

// Keep parity with OGCG for small aggregate zero-init lowering:
// use llvm.memset on the stack object, not memcpy from a synthetic
// zeroinitializer global.
struct A {
  short s;
  short t;
  int i;
};

void q(void) {
  struct A a = {0};
}

// CIR-LABEL: define{{.*}} void @q()
// CIR: %[[A:.*]] = alloca %struct.A, i64 1, align 4
// CIR: call void @llvm.memset.p0.i64(ptr align 4 %[[A]], i8 0, i64 8, i1 false)
// CIR-NOT: memcpy
// CIR-NOT: zeroinitializer

// OGCG-LABEL: define{{.*}} void @q()
// OGCG: %[[A:.*]] = alloca %struct.A, align 4
// OGCG: call void @llvm.memset.p0.i64(ptr align 4 %[[A]], i8 0, i64 8, i1 false)
// OGCG-NOT: memcpy
// OGCG-NOT: zeroinitializer
