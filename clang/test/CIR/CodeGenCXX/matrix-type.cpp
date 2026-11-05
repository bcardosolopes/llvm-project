// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi -fenable-matrix %s -o - | FileCheck %s

typedef float m4x4 __attribute__((matrix_type(4, 4)));

// CHECK-LABEL: cir.func{{.*}} @_Z4testu11matrix_typeILm4ELm4EfE(
// CHECK-SAME: %arg0: !cir.vector<16 x !cir.float>
// CHECK: attributes {cir.matrix_args = array<i1: true>, cir.ret.matrix}
// CHECK: %[[LOAD:.*]] = cir.load{{.*}} !cir.vector<16 x !cir.float>
// CHECK: cir.return %[[LOAD]]
m4x4 test(m4x4 a) {
  return a;
}
