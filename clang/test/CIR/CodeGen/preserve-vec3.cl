// See also clang/test/CodeGenOpenCL/preserve_vec3.cl
// RUN: %clang_cc1 -fclangir -triple amdgcn-amd-amdhsa -emit-cir %s -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir --check-prefix=CIR
// RUN: %clang_cc1 -fclangir -triple amdgcn-amd-amdhsa -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll --check-prefix=LLVM

// Test that CIR correctly handles __builtin_astype between vec3 and other types.
// Vec3 types are stored as vec4 in memory (OpenCL spec), so astype conversions
// involving vec3 need shufflevector instead of plain bitcast.

typedef char char3 __attribute__((ext_vector_type(3)));
typedef short short3 __attribute__((ext_vector_type(3)));
typedef float float3 __attribute__((ext_vector_type(3)));
typedef float float4 __attribute__((ext_vector_type(4)));
typedef double double2 __attribute__((ext_vector_type(2)));

// vec3 -> vec4 (same element type): shuffle to expand
void float3_to_float4(global float3 *a, global float4 *b) {
  *b = __builtin_astype(*a, float4);
}
// CIR: cir.func {{.*}}@{{.*}}float3_to_float4
// CIR: cir.vec.shuffle({{.*}} : !cir.vector<3 x !cir.float>) [#cir.int<0> : !s32i, #cir.int<1> : !s32i, #cir.int<2> : !s32i, #cir.int<-1> : !s32i] : !cir.vector<4 x !cir.float>
// CIR-NOT: cir.cast bitcast {{.*}} : !cir.vector<3 x !cir.float> -> !cir.vector<4 x !cir.float>

// LLVM: define {{.*}}void @{{.*}}float3_to_float4
// LLVM: shufflevector <3 x float> %{{.*}}, <3 x float> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 poison>

// vec3 -> different-size type: shuffle to vec4, then bitcast
void float3_to_double2(global float3 *a, global double2 *b) {
  *b = __builtin_astype(*a, double2);
}
// CIR: cir.func {{.*}}@{{.*}}float3_to_double2
// CIR: cir.vec.shuffle({{.*}} : !cir.vector<3 x !cir.float>) [#cir.int<0> : !s32i, #cir.int<1> : !s32i, #cir.int<2> : !s32i, #cir.int<-1> : !s32i] : !cir.vector<4 x !cir.float>
// CIR: cir.cast bitcast {{.*}} : !cir.vector<4 x !cir.float> -> !cir.vector<2 x !cir.double>

// LLVM: define {{.*}}void @{{.*}}float3_to_double2
// LLVM: shufflevector <3 x float> %{{.*}}, <3 x float> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 poison>

// scalar -> vec3: bitcast to vec4, then shuffle to vec3
void scalar_to_char3(int a, global char3 *out) {
  *out = __builtin_astype(a, char3);
}
// CIR: cir.func {{.*}}@{{.*}}scalar_to_char3
// CIR: cir.cast bitcast {{.*}} : !s32i -> !cir.vector<4 x !s8i>
// CIR: cir.vec.shuffle({{.*}} : !cir.vector<4 x !s8i>) [#cir.int<0> : !s32i, #cir.int<1> : !s32i, #cir.int<2> : !s32i] : !cir.vector<3 x !s8i>

// LLVM: define {{.*}}void @{{.*}}scalar_to_char3
// LLVM: bitcast i32 %{{.*}} to <4 x i8>
// LLVM: shufflevector <4 x i8> %{{.*}}, <4 x i8> poison, <{{3|4}} x i32>

// vec4 -> vec3 (same element type): already vec4, just shuffle
void float4_to_float3(global float3 *a, global float4 *b) {
  *a = __builtin_astype(*b, float3);
}
// CIR: cir.func {{.*}}@{{.*}}float4_to_float3
// CIR: cir.vec.shuffle({{.*}} : !cir.vector<4 x !cir.float>) [#cir.int<0> : !s32i, #cir.int<1> : !s32i, #cir.int<2> : !s32i] : !cir.vector<3 x !cir.float>
// CIR-NOT: cir.cast bitcast {{.*}} : !cir.vector<4 x !cir.float> -> !cir.vector<3 x !cir.float>

// LLVM: define {{.*}}void @{{.*}}float4_to_float3
// LLVM-NOT: bitcast {{.*}} to <3 x float>
