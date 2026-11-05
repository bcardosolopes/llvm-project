// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Test that oversized vectors (256-bit without AVX) are passed via byval
// (indirect) instead of directly. Without AVX, the largest
// vector register is 128-bit (SSE2), so 256-bit vectors must go through
// the stack.

typedef unsigned long long uint64_t4 __attribute__((ext_vector_type(4)));
uint64_t4 test_oversized_vec(uint64_t4 Param) { return Param; }

// CIR: define {{.*}} <4 x i64> @{{.*}}test_oversized_vec{{.*}}(ptr noundef byval(<4 x i64>) align 32
// OGCG: define {{.*}} <4 x i64> @{{.*}}test_oversized_vec{{.*}}(ptr noundef byval(<4 x i64>) align 32

typedef int int8 __attribute__((ext_vector_type(8)));
int8 test_oversized_int_vec(int8 Param) { return Param; }

// CIR: define {{.*}} <8 x i32> @{{.*}}test_oversized_int_vec{{.*}}(ptr noundef byval(<8 x i32>) align 32
// OGCG: define {{.*}} <8 x i32> @{{.*}}test_oversized_int_vec{{.*}}(ptr noundef byval(<8 x i32>) align 32

// 128-bit vectors should still be passed directly (SSE).
typedef float float4 __attribute__((ext_vector_type(4)));
float4 test_sse_vec(float4 Param) { return Param; }

// CIR: define {{.*}} <4 x float> @{{.*}}test_sse_vec{{.*}}(<4 x float> noundef
// OGCG: define {{.*}} <4 x float> @{{.*}}test_sse_vec{{.*}}(<4 x float> noundef
