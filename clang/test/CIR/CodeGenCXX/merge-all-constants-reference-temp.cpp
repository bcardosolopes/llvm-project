// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fmerge-all-constants -fclangir -emit-llvm %s -o - | FileCheck %s

struct A {
  int i1;
  int i2;
};

// CHECK-DAG: @ref.tmp{{[0-9]+}} = private constant %struct.A { i32 1, i32 2 }, align 4
// CHECK-DAG: @ref.tmp{{[0-9]+}} = private constant [3 x i32] [i32 1, i32 2, i32 3], align 4

void reference_to_aggregate(int i) {
  const A &ra{1, i};
  const int (&arrayRef)[] = {1, 2, i};
  const A &constra{1, 2};
  const int (&constarrayRef)[] = {1, 2, 3};

  // Non-constant temporaries are still stack-backed.
  // CHECK-LABEL: @_Z22reference_to_aggregatei
  // CHECK: store ptr %{{.*}}, ptr %{{.*}}, align 8
  // CHECK: store ptr %{{.*}}, ptr %{{.*}}, align 8

  // Constant temporaries are promoted to private globals under
  // -fmerge-all-constants.
  // CHECK: store ptr @ref.tmp{{[0-9]+}}, ptr %{{.*}}, align 8
  // CHECK: store ptr @ref.tmp{{[0-9]+}}, ptr %{{.*}}, align 8
}

