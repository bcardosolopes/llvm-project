// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// Test __builtin_reduce_* family of builtins.

typedef int int4 __attribute__((ext_vector_type(4)));
typedef unsigned uint4 __attribute__((ext_vector_type(4)));
typedef float float4 __attribute__((ext_vector_type(4)));

int test_reduce_add(int4 v) {
  return __builtin_reduce_add(v);
}

// CIR-LABEL: @test_reduce_add
// CIR: cir.vec.reduce.add {{%.*}} : <4 x !s32i> -> !s32i

// LLVM-LABEL: @test_reduce_add
// LLVM: call i32 @llvm.vector.reduce.add.v4i32(<4 x i32>

// OGCG-LABEL: @test_reduce_add
// OGCG: call i32 @llvm.vector.reduce.add.v4i32(<4 x i32>

int test_reduce_mul(int4 v) {
  return __builtin_reduce_mul(v);
}

// CIR-LABEL: @test_reduce_mul
// CIR: cir.vec.reduce.mul {{%.*}} : <4 x !s32i> -> !s32i

// LLVM-LABEL: @test_reduce_mul
// LLVM: call i32 @llvm.vector.reduce.mul.v4i32(<4 x i32>

// OGCG-LABEL: @test_reduce_mul
// OGCG: call i32 @llvm.vector.reduce.mul.v4i32(<4 x i32>

int test_reduce_and(int4 v) {
  return __builtin_reduce_and(v);
}

// CIR-LABEL: @test_reduce_and
// CIR: cir.vec.reduce.and {{%.*}} : <4 x !s32i> -> !s32i

// LLVM-LABEL: @test_reduce_and
// LLVM: call i32 @llvm.vector.reduce.and.v4i32(<4 x i32>

// OGCG-LABEL: @test_reduce_and
// OGCG: call i32 @llvm.vector.reduce.and.v4i32(<4 x i32>

int test_reduce_or(int4 v) {
  return __builtin_reduce_or(v);
}

// CIR-LABEL: @test_reduce_or
// CIR: cir.vec.reduce.or {{%.*}} : <4 x !s32i> -> !s32i

// LLVM-LABEL: @test_reduce_or
// LLVM: call i32 @llvm.vector.reduce.or.v4i32(<4 x i32>

// OGCG-LABEL: @test_reduce_or
// OGCG: call i32 @llvm.vector.reduce.or.v4i32(<4 x i32>

int test_reduce_xor(int4 v) {
  return __builtin_reduce_xor(v);
}

// CIR-LABEL: @test_reduce_xor
// CIR: cir.vec.reduce.xor {{%.*}} : <4 x !s32i> -> !s32i

// LLVM-LABEL: @test_reduce_xor
// LLVM: call i32 @llvm.vector.reduce.xor.v4i32(<4 x i32>

// OGCG-LABEL: @test_reduce_xor
// OGCG: call i32 @llvm.vector.reduce.xor.v4i32(<4 x i32>

int test_reduce_max_signed(int4 v) {
  return __builtin_reduce_max(v);
}

// CIR-LABEL: @test_reduce_max_signed
// CIR: cir.vec.reduce.smax {{%.*}} : <4 x !s32i> -> !s32i

// LLVM-LABEL: @test_reduce_max_signed
// LLVM: call i32 @llvm.vector.reduce.smax.v4i32(<4 x i32>

// OGCG-LABEL: @test_reduce_max_signed
// OGCG: call i32 @llvm.vector.reduce.smax.v4i32(<4 x i32>

unsigned test_reduce_max_unsigned(uint4 v) {
  return __builtin_reduce_max(v);
}

// CIR-LABEL: @test_reduce_max_unsigned
// CIR: cir.vec.reduce.umax {{%.*}} : <4 x !u32i> -> !u32i

// LLVM-LABEL: @test_reduce_max_unsigned
// LLVM: call i32 @llvm.vector.reduce.umax.v4i32(<4 x i32>

// OGCG-LABEL: @test_reduce_max_unsigned
// OGCG: call i32 @llvm.vector.reduce.umax.v4i32(<4 x i32>

float test_reduce_max_float(float4 v) {
  return __builtin_reduce_max(v);
}

// CIR-LABEL: @test_reduce_max_float
// CIR: cir.vec.reduce.fmax {{%.*}} : <4 x !cir.float> -> !cir.float

// LLVM-LABEL: @test_reduce_max_float
// LLVM: call float @llvm.vector.reduce.fmax.v4f32(<4 x float>

// OGCG-LABEL: @test_reduce_max_float
// OGCG: call float @llvm.vector.reduce.fmax.v4f32(<4 x float>

int test_reduce_min_signed(int4 v) {
  return __builtin_reduce_min(v);
}

// CIR-LABEL: @test_reduce_min_signed
// CIR: cir.vec.reduce.smin {{%.*}} : <4 x !s32i> -> !s32i

// LLVM-LABEL: @test_reduce_min_signed
// LLVM: call i32 @llvm.vector.reduce.smin.v4i32(<4 x i32>

// OGCG-LABEL: @test_reduce_min_signed
// OGCG: call i32 @llvm.vector.reduce.smin.v4i32(<4 x i32>

float test_reduce_maximum(float4 v) {
  return __builtin_reduce_maximum(v);
}

// CIR-LABEL: @test_reduce_maximum
// CIR: cir.vec.reduce.fmaximum {{%.*}} : <4 x !cir.float> -> !cir.float

// LLVM-LABEL: @test_reduce_maximum
// LLVM: call float @llvm.vector.reduce.fmaximum.v4f32(<4 x float>

// OGCG-LABEL: @test_reduce_maximum
// OGCG: call float @llvm.vector.reduce.fmaximum.v4f32(<4 x float>

float test_reduce_minimum(float4 v) {
  return __builtin_reduce_minimum(v);
}

// CIR-LABEL: @test_reduce_minimum
// CIR: cir.vec.reduce.fminimum {{%.*}} : <4 x !cir.float> -> !cir.float

// LLVM-LABEL: @test_reduce_minimum
// LLVM: call float @llvm.vector.reduce.fminimum.v4f32(<4 x float>

// OGCG-LABEL: @test_reduce_minimum
// OGCG: call float @llvm.vector.reduce.fminimum.v4f32(<4 x float>

// __builtin_convertvector to bool vector + reduce
typedef _Bool bool4 __attribute__((ext_vector_type(4)));

_Bool test_reduce_and_bool(int4 v) {
  return __builtin_reduce_and(__builtin_convertvector(v, bool4));
}

// CIR-LABEL: @test_reduce_and_bool
// CIR: cir.cast int_to_bool {{%.*}} : !cir.vector<4 x !s32i> -> !cir.vector<4 x !cir.bool>
// CIR: cir.vec.reduce.and {{%.*}} : <4 x !cir.bool> -> !cir.bool

// LLVM-LABEL: @test_reduce_and_bool
// LLVM: icmp ne <4 x i32> {{%.*}}, zeroinitializer
// LLVM: call i1 @llvm.vector.reduce.and.v4i1(<4 x i1>

// OGCG-LABEL: @test_reduce_and_bool
// OGCG: icmp ne <4 x i32> {{%.*}}, zeroinitializer
// OGCG: call i1 @llvm.vector.reduce.and.v4i1(<4 x i1>

_Bool test_reduce_and_float_bool(float4 v) {
  return __builtin_reduce_and(__builtin_convertvector(v, bool4));
}

// CIR-LABEL: @test_reduce_and_float_bool
// CIR: cir.cast float_to_bool {{%.*}} : !cir.vector<4 x !cir.float> -> !cir.vector<4 x !cir.bool>
// CIR: cir.vec.reduce.and {{%.*}} : <4 x !cir.bool> -> !cir.bool

// LLVM-LABEL: @test_reduce_and_float_bool
// LLVM: fcmp une <4 x float> {{%.*}}, zeroinitializer
// LLVM: call i1 @llvm.vector.reduce.and.v4i1(<4 x i1>

// OGCG-LABEL: @test_reduce_and_float_bool
// OGCG: fcmp une <4 x float> {{%.*}}, zeroinitializer
// OGCG: call i1 @llvm.vector.reduce.and.v4i1(<4 x i1>
