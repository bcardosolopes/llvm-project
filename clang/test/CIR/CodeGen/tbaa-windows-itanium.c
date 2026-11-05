// RUN: %clang_cc1 -triple thumbv7--windows-itanium -O1 -emit-llvm -fclangir %s -o - | FileCheck %s
// REQUIRES: arm-registered-target

void *x;

void f(void *p) {
  x = p;
}

// CHECK-LABEL: define dso_local arm_aapcs_vfpcc void @f(ptr %{{.*}})
// CHECK: store ptr %{{.*}}, ptr @x, align 4, !tbaa ![[ANYPTR:[0-9]+]]
// CHECK-DAG: ![[ROOT:[0-9]+]] = !{!"Simple C/C++ TBAA"}
// CHECK-DAG: ![[CHAR:[0-9]+]] = !{!"omnipotent char", ![[ROOT]], i64 0}
// CHECK-DAG: ![[ANYPTRP:[0-9]+]] = !{!"any pointer", ![[CHAR]], i64 0}
// CHECK-DAG: ![[ANYPTR]] = !{![[ANYPTRP]], ![[ANYPTRP]], i64 0}
