// RUN: not %clang_cc1 -triple x86_64-linux-gnu -frounding-math -ffp-exception-behavior=strict -fclangir -emit-llvm -O2 %s -o - 2>&1 | FileCheck %s

typedef float float4 __attribute__((ext_vector_type(4)));

float4 strict_elementwise_exp10(float4 a) {
  return __builtin_elementwise_exp10(a);
}

// CHECK: error:
// CHECK-SAME: __builtin_elementwise_exp10
