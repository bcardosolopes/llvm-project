// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -ffast-math -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify that nofpclass(nan inf) is applied to complex parameters and return
// values when -ffast-math is enabled, matching OGCG behavior.

float _Complex add_float_rr(float a, float b) {
  return a + b;
}
// CHECK-LABEL: define dso_local nofpclass(nan inf) <2 x float> @add_float_rr(
// CHECK-SAME: float noundef nofpclass(nan inf) %{{.*}}, float noundef nofpclass(nan inf) %{{.*}})

float _Complex add_float_cc(float _Complex a, float _Complex b) {
  return a + b;
}
// CHECK-LABEL: define dso_local nofpclass(nan inf) <2 x float> @add_float_cc(
// CHECK-SAME: <2 x float> noundef nofpclass(nan inf) %{{.*}}, <2 x float> noundef nofpclass(nan inf) %{{.*}})

double _Complex mul_double_cc(double _Complex a, double _Complex b) {
  return a * b;
}
// CHECK-LABEL: define dso_local nofpclass(nan inf) { double, double } @mul_double_cc(
// CHECK-SAME: double noundef nofpclass(nan inf) %{{.*}}, double noundef nofpclass(nan inf) %{{.*}}, double noundef nofpclass(nan inf) %{{.*}}, double noundef nofpclass(nan inf) %{{.*}})
