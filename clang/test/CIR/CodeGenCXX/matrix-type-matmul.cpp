// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fenable-matrix -fclangir -emit-llvm -disable-llvm-passes -std=c++11 %s -o - | FileCheck %s

template <typename EltTy, unsigned Rows, unsigned Columns>
struct MyMatrix {
  using matrix_t = EltTy __attribute__((matrix_type(Rows, Columns)));
  matrix_t value;
};

template <typename EltTy0, unsigned R0, unsigned C0, unsigned C1>
typename MyMatrix<EltTy0, R0, C1>::matrix_t multiply(MyMatrix<EltTy0, R0, C0> &a,
                                                     MyMatrix<EltTy0, C0, C1> &b) {
  return a.value * b.value;
}

MyMatrix<float, 2, 2> test_multiply_template(MyMatrix<float, 2, 5> a,
                                             MyMatrix<float, 5, 2> b) {
// CHECK-LABEL: define{{.*}} @_Z22test_multiply_template8MyMatrixIfLj2ELj5EES_IfLj5ELj2EE(
// CHECK: [[RES:%.*]] = call noundef <4 x float> @_Z8multiplyIfLj2ELj5ELj2EEN8MyMatrixIT_XT0_EXT2_EE8matrix_tERS0_IS1_XT0_EXT1_EERS0_IS1_XT1_EXT2_EE(
// CHECK: store <4 x float> [[RES]], ptr {{.*}}, align 4
  MyMatrix<float, 2, 2> res;
  res.value = multiply(a, b);
  return res;
}

// CHECK-LABEL: define linkonce_odr noundef <4 x float> @_Z8multiplyIfLj2ELj5ELj2EEN8MyMatrixIT_XT0_EXT2_EE8matrix_tERS0_IS1_XT0_EXT1_EERS0_IS1_XT1_EXT2_EE(
// CHECK: [[LHS:%.*]] = load <10 x float>, ptr {{.*}}, align 4
// CHECK: [[RHS:%.*]] = load <10 x float>, ptr {{.*}}, align 4
// CHECK: [[MATMUL:%.*]] = call <4 x float> @llvm.matrix.multiply.v4f32.v10f32.v10f32(<10 x float> [[LHS]], <10 x float> [[RHS]], i32 2, i32 5, i32 2)
// CHECK: ret <4 x float> [[MATMUL]]
