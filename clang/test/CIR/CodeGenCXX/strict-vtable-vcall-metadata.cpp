// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -O1 -disable-llvm-passes \
// RUN:   -fstrict-vtable-pointers -fclangir -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefix=CHECK
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -O1 -disable-llvm-passes \
// RUN:   -fstrict-vtable-pointers -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefix=CHECK
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -O0 \
// RUN:   -fstrict-vtable-pointers -fclangir -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefix=O0

struct A {
  virtual int f();
  int x;
};

int call(A *a) {
  return a->f();
}

// CHECK-LABEL: define{{.*}} @_Z4callP1A(
// CHECK-NOT: @llvm.launder.invariant.group
// CHECK: [[VTABLE:%.*]] = load ptr, ptr %{{.*}}, align {{[0-9]+}}{{.*}}!invariant.group ![[EMPTY:[0-9]+]]
// CHECK: [[SLOT:%.*]] = getelementptr inbounds ptr, ptr [[VTABLE]], i64 0
// CHECK: [[VFNPTR:%.*]] = load ptr, ptr [[SLOT]], align {{[0-9]+}}{{.*}}!invariant.load ![[EMPTY]]
// CHECK: call noundef i32 [[VFNPTR]](ptr

// CHECK-DAG: ![[SVTP:[0-9]+]] = !{i32 1, !"StrictVTablePointers", i32 1}
// CHECK-DAG: ![[REQ:[0-9]+]] = !{i32 3, !"StrictVTablePointersRequirement", ![[PAIR:[0-9]+]]}
// CHECK-DAG: ![[PAIR]] = !{!"StrictVTablePointers", i32 1}
// CHECK: ![[EMPTY]] = !{}

// O0-NOT: !"StrictVTablePointers"
// O0-NOT: !"StrictVTablePointersRequirement"
