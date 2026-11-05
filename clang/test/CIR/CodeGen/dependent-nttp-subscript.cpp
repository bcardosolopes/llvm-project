// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -fclangir -emit-llvm \
// RUN:   -std=c++20 -w -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -emit-llvm \
// RUN:   -std=c++20 -w -o - %s | FileCheck %s

// Regression test: NTTP reference parameter whose type depends on a prior
// template parameter can leave SubstNonTypeTemplateParmExpr with unsubstituted
// type, causing dependent types to leak into codegen. Both CIR and OGCG must
// handle this gracefully. (SoftPyramidHelper pattern)

template <unsigned N>
struct Array {
  int data[N];
  int &operator[](unsigned i) { return data[i]; }
  const int &operator[](unsigned i) const { return data[i]; }
};

template <unsigned KS, const Array<KS> &kernel>
float convolve(const float *input) {
  float sum = 0;
  for (unsigned k = 0; k < KS; ++k)
    sum += input[k] * kernel[k];
  return sum;
}

constexpr Array<3> weights = {1, 2, 3};

float test(const float *p) {
  return convolve<3, weights>(p);
}

// CHECK-LABEL: @_Z4testPKf
// CHECK: call
// CHECK: ret float
