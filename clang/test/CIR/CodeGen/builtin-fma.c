// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

float test_fmaf(float a, float b, float c) {
  return __builtin_fmaf(a, b, c);
}

// CIR-LABEL: @test_fmaf
// CIR: cir.fma {{%.*}}, {{%.*}}, {{%.*}} : !cir.float

// LLVM-LABEL: @test_fmaf
// LLVM: call float @llvm.fma.f32(float {{%.*}}, float {{%.*}}, float {{%.*}})

// OGCG-LABEL: @test_fmaf
// OGCG: call float @llvm.fma.f32(float {{%.*}}, float {{%.*}}, float {{%.*}})

double test_fma(double a, double b, double c) {
  return __builtin_fma(a, b, c);
}

// CIR-LABEL: @test_fma
// CIR: cir.fma {{%.*}}, {{%.*}}, {{%.*}} : !cir.double

// LLVM-LABEL: @test_fma
// LLVM: call double @llvm.fma.f64(double {{%.*}}, double {{%.*}}, double {{%.*}})

// OGCG-LABEL: @test_fma
// OGCG: call double @llvm.fma.f64(double {{%.*}}, double {{%.*}}, double {{%.*}})
