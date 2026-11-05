// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fenable-matrix -fclangir -emit-llvm -disable-llvm-passes -std=c++11 %s -o - | FileCheck %s

template <typename EltTy, unsigned Rows, unsigned Columns>
struct MyMatrix {
  using matrix_t = EltTy __attribute__((matrix_type(Rows, Columns)));
  matrix_t value;
};

struct DoubleWrapper {
  int x;
  operator double() { return x; }
};

void matrix_sub_matrix(MyMatrix<float, 2, 5> &a, MyMatrix<float, 2, 5> &b) {
  // CHECK-LABEL: define{{.*}} @_Z17matrix_sub_matrixR8MyMatrixIfLj2ELj5EES1_(
  // CHECK: [[A:%.*]] = load <10 x float>, ptr {{.*}}, align 4
  // CHECK: [[B:%.*]] = load <10 x float>, ptr {{.*}}, align 4
  // CHECK: [[RES:%.*]] = fsub <10 x float> [[A]], [[B]]
  // CHECK: store <10 x float> [[RES]], ptr {{.*}}, align 4
  a.value = a.value - b.value;
}

void matrix_sub_scalar(MyMatrix<double, 10, 9> &m) {
  // CHECK-LABEL: define{{.*}} @_Z17matrix_sub_scalarR8MyMatrixIdLj10ELj9EE(
  // CHECK: [[MAT:%.*]] = load <90 x double>, ptr {{.*}}, align 8
  // CHECK: [[SCALAR:%.*]] = call noundef double @_ZN13DoubleWrappercvdEv(ptr {{.*}})
  // CHECK: [[EMBED:%.*]] = insertelement <90 x double> poison, double [[SCALAR]], i64 0
  // CHECK: [[SPLAT:%.*]] = shufflevector <90 x double> [[EMBED]], <90 x double> poison, <90 x i32> zeroinitializer
  // CHECK: [[RES:%.*]] = fsub <90 x double> [[MAT]], [[SPLAT]]
  // CHECK: store <90 x double> [[RES]], ptr {{.*}}, align 8
  DoubleWrapper w;
  w.x = 10;
  m.value = m.value - w;
}

void scalar_sub_matrix(MyMatrix<double, 10, 9> &m) {
  // CHECK-LABEL: define{{.*}} @_Z17scalar_sub_matrixR8MyMatrixIdLj10ELj9EE(
  // CHECK: [[SCALAR:%.*]] = call noundef double @_ZN13DoubleWrappercvdEv(ptr {{.*}})
  // CHECK: [[MAT:%.*]] = load <90 x double>, ptr {{.*}}, align 8
  // CHECK: [[EMBED:%.*]] = insertelement <90 x double> poison, double [[SCALAR]], i64 0
  // CHECK: [[SPLAT:%.*]] = shufflevector <90 x double> [[EMBED]], <90 x double> poison, <90 x i32> zeroinitializer
  // CHECK: [[RES:%.*]] = fsub <90 x double> [[SPLAT]], [[MAT]]
  // CHECK: store <90 x double> [[RES]], ptr {{.*}}, align 8
  DoubleWrapper w;
  w.x = 10;
  m.value = w - m.value;
}
