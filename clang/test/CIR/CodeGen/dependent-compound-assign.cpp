// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -fclangir -emit-llvm \
// RUN:   -std=c++20 -w -o - %s | FileCheck %s

// Regression test: dependent computation types in CompoundAssignOperator
// should fall back to the LHS type, not crash with BuiltinType::Dependent.
//

template <unsigned KS, const int (&kernel)[KS]>
float weighted_sum(const float *data) {
  float sum = 0;
  for (unsigned k = 0; k < KS; ++k)
    sum += data[k] * kernel[k];
  return sum;
}

constexpr int weights[] = {1, 2, 3};

float test(const float *p) {
  return weighted_sum<3, weights>(p);
}

// CHECK-LABEL: @_Z12weighted_sum
// CHECK: fmul
// CHECK: fadd
// CHECK: ret float
