// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fenable-matrix -fclangir -emit-llvm -disable-llvm-passes -std=c++11 %s -o - | FileCheck %s

template <typename EltTy, unsigned Rows, unsigned Columns>
struct MyMatrix {
  using matrix_t = EltTy __attribute__((matrix_type(Rows, Columns)));
  matrix_t value;
};

struct IntWrapper {
  char x;
  operator int() { return x; }
};

void scalar_mul_matrix(MyMatrix<double, 10, 9> &m, IntWrapper &w) {
  // CHECK-LABEL: define{{.*}} @_Z17scalar_mul_matrixR8MyMatrixIdLj10ELj9EER10IntWrapper(
  // CHECK: [[SCALAR:%.*]] = call noundef i32 @_ZN10IntWrappercviEv(ptr {{.*}})
  // CHECK: [[SCALAR_FP:%.*]] = sitofp i32 [[SCALAR]] to double
  // CHECK: [[MAT:%.*]] = load <90 x double>, ptr {{.*}}, align 8
  // CHECK: [[EMBED:%.*]] = insertelement <90 x double> poison, double [[SCALAR_FP]], i64 0
  // CHECK: [[SPLAT:%.*]] = shufflevector <90 x double> [[EMBED]], <90 x double> poison, <90 x i32> zeroinitializer
  // CHECK: [[RES:%.*]] = fmul <90 x double> [[SPLAT]], [[MAT]]
  // CHECK: store <90 x double> [[RES]], ptr {{.*}}, align 8
  m.value = w * m.value;
}
