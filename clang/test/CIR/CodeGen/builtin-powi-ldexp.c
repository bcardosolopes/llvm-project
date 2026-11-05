// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// __builtin_powi

float test_powif(float x, int n) {
  return __builtin_powif(x, n);
}

// CIR-LABEL: @test_powif
// CIR: cir.powi {{%.*}}, {{%.*}} : (!cir.float, !s32i) -> !cir.float

// LLVM-LABEL: @test_powif
// LLVM: call float @llvm.powi.f32.i32(float {{%.*}}, i32 {{%.*}})

// OGCG-LABEL: @test_powif
// OGCG: call float @llvm.powi.f32.i32(float {{%.*}}, i32 {{%.*}})

double test_powi(double x, int n) {
  return __builtin_powi(x, n);
}

// CIR-LABEL: @test_powi
// CIR: cir.powi {{%.*}}, {{%.*}} : (!cir.double, !s32i) -> !cir.double

// LLVM-LABEL: @test_powi
// LLVM: call double @llvm.powi.f64.i32(double {{%.*}}, i32 {{%.*}})

// OGCG-LABEL: @test_powi
// OGCG: call double @llvm.powi.f64.i32(double {{%.*}}, i32 {{%.*}})

// __builtin_ldexp / __builtin_elementwise_ldexp

float test_ldexpf(float x, int n) {
  return __builtin_ldexpf(x, n);
}

// CIR-LABEL: @test_ldexpf
// CIR: cir.ldexp {{%.*}}, {{%.*}} : (!cir.float, !s32i) -> !cir.float

// LLVM-LABEL: @test_ldexpf
// LLVM: call float @llvm.ldexp.f32.i32(float {{%.*}}, i32 {{%.*}})

// OGCG-LABEL: @test_ldexpf
// OGCG: call float @llvm.ldexp.f32.i32(float {{%.*}}, i32 {{%.*}})

double test_ldexp(double x, int n) {
  return __builtin_ldexp(x, n);
}

// CIR-LABEL: @test_ldexp
// CIR: cir.ldexp {{%.*}}, {{%.*}} : (!cir.double, !s32i) -> !cir.double

// LLVM-LABEL: @test_ldexp
// LLVM: call double @llvm.ldexp.f64.i32(double {{%.*}}, i32 {{%.*}})

// OGCG-LABEL: @test_ldexp
// OGCG: call double @llvm.ldexp.f64.i32(double {{%.*}}, i32 {{%.*}})

float test_elementwise_ldexp(float x, int n) {
  return __builtin_elementwise_ldexp(x, n);
}

// CIR-LABEL: @test_elementwise_ldexp
// CIR: cir.ldexp {{%.*}}, {{%.*}} : (!cir.float, !s32i) -> !cir.float

// LLVM-LABEL: @test_elementwise_ldexp
// LLVM: call float @llvm.ldexp.f32.i32(float {{%.*}}, i32 {{%.*}})

// OGCG-LABEL: @test_elementwise_ldexp
// OGCG: call float @llvm.ldexp.f32.i32(float {{%.*}}, i32 {{%.*}})
