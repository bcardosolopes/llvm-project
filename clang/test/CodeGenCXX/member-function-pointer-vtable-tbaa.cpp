// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -x c++ -std=c++11 -emit-llvm -O1 -disable-llvm-passes -o - %s | FileCheck %s --check-prefix=CHECK
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-apple-darwin10 -x c++ -std=c++11 -fclangir -emit-llvm -O1 -disable-llvm-passes -o - %s | FileCheck %s --check-prefix=CIR %}

struct A {
  virtual int g1();
  int g2();
};

int f(A *a, int (A::*fp)()) {
  return (a->*fp)();
}

int A::g1() { return 1; }
int A::g2() { return 2; }

// CHECK-LABEL: define noundef i32 @_Z1fP1AMS_FivE(
// CHECK: br i1 %{{.*}}, label %[[VIRT:.*]], label %[[NONVIRT:.*]]
// CHECK: [[VIRT]]:
// CHECK: %[[VTABLE:.*]] = load ptr, ptr %{{.*}}, align 8, !tbaa ![[VTBAA:[0-9]+]]
// CHECK-DAG: ![[ROOT:[0-9]+]] = !{!"Simple C++ TBAA"}
// CHECK-DAG: ![[VTDESC:[0-9]+]] = !{!"vtable pointer", ![[ROOT]], i64 0}
// CHECK-DAG: ![[VTBAA]] = !{![[VTDESC]], ![[VTDESC]], i64 0}

// CIR-LABEL: define noundef i32 @_Z1fP1AMS_FivE(
// CIR: br i1 %{{.*}}, label %[[VIRT:.*]], label %[[NONVIRT:.*]]
// CIR: [[VIRT]]:
// CIR: %[[VTABLE:.*]] = load ptr, ptr %{{.*}}, align 8, !tbaa ![[VTBAA:[0-9]+]]
// CIR-DAG: ![[ROOT:[0-9]+]] = !{!"Simple C++ TBAA"}
// CIR-DAG: ![[VTDESC:[0-9]+]] = !{!"vtable pointer", ![[ROOT]], i64 0}
// CIR-DAG: ![[VTBAA]] = !{![[VTDESC]], ![[VTDESC]], i64 0}
