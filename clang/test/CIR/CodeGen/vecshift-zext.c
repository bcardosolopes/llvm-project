// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-error=vec-elem-size -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-error=vec-elem-size -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s

// Vector shift amounts should be zero-extended (not sign-extended)
// regardless of source type signedness.

typedef __attribute__((vector_size(32))) unsigned int vector_uint8;
typedef __attribute__((vector_size(16))) short vector_short8;

vector_uint8 vui8;
vector_short8 vs8;

void test_shl() {
  vui8 = vui8 << vs8;
}

// CHECK: zext <8 x i16> %{{.*}} to <8 x i32>
// OGCG: zext <8 x i16> %{{.*}} to <8 x i32>
