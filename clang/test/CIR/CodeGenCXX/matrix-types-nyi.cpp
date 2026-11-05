// RUN: %clang_cc1 -triple x86_64-linux-gnu -fenable-matrix -fclangir -emit-cir %s -o - | FileCheck %s

typedef float m4x4 __attribute__((matrix_type(4, 4)));

// CHECK-LABEL: cir.func{{.*}} @_Z4testu11matrix_typeILm4ELm4EfE(
// CHECK-SAME: %arg0: !cir.vector<16 x !cir.float>
// CHECK: attributes {cir.matrix_args = array<i1: true>}
void test(m4x4 m) {
}
