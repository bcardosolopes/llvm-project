// RUN: %clang_cc1 -fclangir -emit-llvm -triple x86_64-unknown-linux-gnu %s -o - | FileCheck %s
// Regression: __builtin_elementwise_clzg on vector type is supported.

typedef short short8 __attribute__((ext_vector_type(8)));
void test(short8 vs1) {
    vs1 = __builtin_elementwise_clzg(vs1);
}

// CHECK: call <8 x i16> @llvm.ctlz.v8i16(<8 x i16> %{{.*}}, i1 true)
