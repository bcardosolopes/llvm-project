// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fclangir -emit-llvm -O3 -o - %s | FileCheck %s

struct A {
  virtual int vf1() { return 1; }
  virtual int vf2() { return 2; }
};

int f(A *a, int (A::*fp)()) {
  return (a->*fp)();
}

// CHECK-LABEL: define noundef i32 @_Z1fP1AMS_FivE(
// CHECK: [[ADJ:%.*]] = getelementptr inbounds i8, ptr %{{.*}}, i64 %{{.*}}
// CHECK: [[VTABLE:%.*]] = load ptr, ptr [[ADJ]], align 8, !tbaa [[VTBAA:![0-9]+]]
// CHECK-DAG: [[ROOT:![0-9]+]] = !{!"Simple C++ TBAA"}
// CHECK-DAG: [[VNODE:![0-9]+]] = !{!"vtable pointer", [[ROOT]], i64 0}
// CHECK-DAG: [[VTBAA]] = !{[[VNODE]], [[VNODE]], i64 0}
