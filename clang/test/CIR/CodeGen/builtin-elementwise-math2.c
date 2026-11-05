// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// Test elementwise canonicalize, fshl, fshr, maximumnum, minimumnum builtins,
// and scalar canonicalize.

typedef float float4 __attribute__((ext_vector_type(4)));
typedef int int4 __attribute__((ext_vector_type(4)));

float test_canonicalize_scalar(float x) {
  return __builtin_canonicalizef(x);
}

// CIR-LABEL: @test_canonicalize_scalar
// CIR: cir.canonicalize {{%.*}} : !cir.float

// LLVM-LABEL: @test_canonicalize_scalar
// LLVM: call float @llvm.canonicalize.f32(float

// OGCG-LABEL: @test_canonicalize_scalar
// OGCG: call float @llvm.canonicalize.f32(float

double test_canonicalize_double(double x) {
  return __builtin_canonicalize(x);
}

// CIR-LABEL: @test_canonicalize_double
// CIR: cir.canonicalize {{%.*}} : !cir.double

// LLVM-LABEL: @test_canonicalize_double
// LLVM: call double @llvm.canonicalize.f64(double

// OGCG-LABEL: @test_canonicalize_double
// OGCG: call double @llvm.canonicalize.f64(double

float4 test_elementwise_canonicalize(float4 a) {
  return __builtin_elementwise_canonicalize(a);
}

// CIR-LABEL: @test_elementwise_canonicalize
// CIR: cir.canonicalize {{%.*}} : !cir.vector<4 x !cir.float>

// LLVM-LABEL: @test_elementwise_canonicalize
// LLVM: call <4 x float> @llvm.canonicalize.v4f32(<4 x float>

// OGCG-LABEL: @test_elementwise_canonicalize
// OGCG: call <4 x float> @llvm.canonicalize.v4f32(<4 x float>

int4 test_elementwise_fshl(int4 a, int4 b, int4 c) {
  return __builtin_elementwise_fshl(a, b, c);
}

// CIR-LABEL: @test_elementwise_fshl
// CIR: cir.fshl {{%.*}}, {{%.*}}, {{%.*}} : !cir.vector<4 x !s32i>

// LLVM-LABEL: @test_elementwise_fshl
// LLVM: call <4 x i32> @llvm.fshl.v4i32(<4 x i32>

// OGCG-LABEL: @test_elementwise_fshl
// OGCG: call <4 x i32> @llvm.fshl.v4i32(<4 x i32>

int4 test_elementwise_fshr(int4 a, int4 b, int4 c) {
  return __builtin_elementwise_fshr(a, b, c);
}

// CIR-LABEL: @test_elementwise_fshr
// CIR: cir.fshr {{%.*}}, {{%.*}}, {{%.*}} : !cir.vector<4 x !s32i>

// LLVM-LABEL: @test_elementwise_fshr
// LLVM: call <4 x i32> @llvm.fshr.v4i32(<4 x i32>

// OGCG-LABEL: @test_elementwise_fshr
// OGCG: call <4 x i32> @llvm.fshr.v4i32(<4 x i32>

float4 test_elementwise_maximumnum(float4 a, float4 b) {
  return __builtin_elementwise_maximumnum(a, b);
}

// CIR-LABEL: @test_elementwise_maximumnum
// CIR: cir.maximumnum {{%.*}}, {{%.*}} : !cir.vector<4 x !cir.float>

// LLVM-LABEL: @test_elementwise_maximumnum
// LLVM: call <4 x float> @llvm.maximumnum.v4f32(<4 x float>

// OGCG-LABEL: @test_elementwise_maximumnum
// OGCG: call <4 x float> @llvm.maximumnum.v4f32(<4 x float>

float4 test_elementwise_minimumnum(float4 a, float4 b) {
  return __builtin_elementwise_minimumnum(a, b);
}

// CIR-LABEL: @test_elementwise_minimumnum
// CIR: cir.minimumnum {{%.*}}, {{%.*}} : !cir.vector<4 x !cir.float>

// LLVM-LABEL: @test_elementwise_minimumnum
// LLVM: call <4 x float> @llvm.minimumnum.v4f32(<4 x float>

// OGCG-LABEL: @test_elementwise_minimumnum
// OGCG: call <4 x float> @llvm.minimumnum.v4f32(<4 x float>
