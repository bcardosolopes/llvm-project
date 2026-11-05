// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// __builtin_elementwise_fma

float test_fma(float a, float b, float c) {
  return __builtin_elementwise_fma(a, b, c);
}

// CIR-LABEL: @test_fma
// CIR: cir.fma {{%.*}}, {{%.*}}, {{%.*}} : !cir.float

// LLVM-LABEL: @test_fma
// LLVM: call float @llvm.fma.f32(float {{%.*}}, float {{%.*}}, float {{%.*}})

// OGCG-LABEL: @test_fma
// OGCG: call float @llvm.fma.f32(float {{%.*}}, float {{%.*}}, float {{%.*}})

double test_fma_double(double a, double b, double c) {
  return __builtin_elementwise_fma(a, b, c);
}

// CIR-LABEL: @test_fma_double
// CIR: cir.fma {{%.*}}, {{%.*}}, {{%.*}} : !cir.double

// LLVM-LABEL: @test_fma_double
// LLVM: call double @llvm.fma.f64(double {{%.*}}, double {{%.*}}, double {{%.*}})

// OGCG-LABEL: @test_fma_double
// OGCG: call double @llvm.fma.f64(double {{%.*}}, double {{%.*}}, double {{%.*}})

// __builtin_elementwise_max / min (integer, signed)

int test_max_signed(int a, int b) {
  return __builtin_elementwise_max(a, b);
}

// CIR-LABEL: @test_max_signed
// CIR: cir.smax {{%.*}}, {{%.*}} : !s32i

// LLVM-LABEL: @test_max_signed
// LLVM: call i32 @llvm.smax.i32(i32 {{%.*}}, i32 {{%.*}})

// OGCG-LABEL: @test_max_signed
// OGCG: call i32 @llvm.smax.i32(i32 {{%.*}}, i32 {{%.*}})

int test_min_signed(int a, int b) {
  return __builtin_elementwise_min(a, b);
}

// CIR-LABEL: @test_min_signed
// CIR: cir.smin {{%.*}}, {{%.*}} : !s32i

// LLVM-LABEL: @test_min_signed
// LLVM: call i32 @llvm.smin.i32(i32 {{%.*}}, i32 {{%.*}})

// OGCG-LABEL: @test_min_signed
// OGCG: call i32 @llvm.smin.i32(i32 {{%.*}}, i32 {{%.*}})

// __builtin_elementwise_max / min (integer, unsigned)

unsigned test_max_unsigned(unsigned a, unsigned b) {
  return __builtin_elementwise_max(a, b);
}

// CIR-LABEL: @test_max_unsigned
// CIR: cir.umax {{%.*}}, {{%.*}} : !u32i

// LLVM-LABEL: @test_max_unsigned
// LLVM: call i32 @llvm.umax.i32(i32 {{%.*}}, i32 {{%.*}})

// OGCG-LABEL: @test_max_unsigned
// OGCG: call i32 @llvm.umax.i32(i32 {{%.*}}, i32 {{%.*}})

unsigned test_min_unsigned(unsigned a, unsigned b) {
  return __builtin_elementwise_min(a, b);
}

// CIR-LABEL: @test_min_unsigned
// CIR: cir.umin {{%.*}}, {{%.*}} : !u32i

// LLVM-LABEL: @test_min_unsigned
// LLVM: call i32 @llvm.umin.i32(i32 {{%.*}}, i32 {{%.*}})

// OGCG-LABEL: @test_min_unsigned
// OGCG: call i32 @llvm.umin.i32(i32 {{%.*}}, i32 {{%.*}})

// __builtin_elementwise_max / min (float)

float test_max_float(float a, float b) {
  return __builtin_elementwise_max(a, b);
}

// CIR-LABEL: @test_max_float
// CIR: cir.fmaxnum {{%.*}}, {{%.*}} : !cir.float

// LLVM-LABEL: @test_max_float
// LLVM: call float @llvm.maxnum.f32(float {{%.*}}, float {{%.*}})

// OGCG-LABEL: @test_max_float
// OGCG: call float @llvm.maxnum.f32(float {{%.*}}, float {{%.*}})

float test_min_float(float a, float b) {
  return __builtin_elementwise_min(a, b);
}

// CIR-LABEL: @test_min_float
// CIR: cir.fminnum {{%.*}}, {{%.*}} : !cir.float

// LLVM-LABEL: @test_min_float
// LLVM: call float @llvm.minnum.f32(float {{%.*}}, float {{%.*}})

// OGCG-LABEL: @test_min_float
// OGCG: call float @llvm.minnum.f32(float {{%.*}}, float {{%.*}})

// __builtin_elementwise_add_sat / sub_sat

int test_add_sat_signed(int a, int b) {
  return __builtin_elementwise_add_sat(a, b);
}

// CIR-LABEL: @test_add_sat_signed
// CIR: cir.sadd.sat {{%.*}}, {{%.*}} : !s32i

// LLVM-LABEL: @test_add_sat_signed
// LLVM: call i32 @llvm.sadd.sat.i32(i32 {{%.*}}, i32 {{%.*}})

// OGCG-LABEL: @test_add_sat_signed
// OGCG: call i32 @llvm.sadd.sat.i32(i32 {{%.*}}, i32 {{%.*}})

unsigned test_add_sat_unsigned(unsigned a, unsigned b) {
  return __builtin_elementwise_add_sat(a, b);
}

// CIR-LABEL: @test_add_sat_unsigned
// CIR: cir.uadd.sat {{%.*}}, {{%.*}} : !u32i

// LLVM-LABEL: @test_add_sat_unsigned
// LLVM: call i32 @llvm.uadd.sat.i32(i32 {{%.*}}, i32 {{%.*}})

// OGCG-LABEL: @test_add_sat_unsigned
// OGCG: call i32 @llvm.uadd.sat.i32(i32 {{%.*}}, i32 {{%.*}})

int test_sub_sat_signed(int a, int b) {
  return __builtin_elementwise_sub_sat(a, b);
}

// CIR-LABEL: @test_sub_sat_signed
// CIR: cir.ssub.sat {{%.*}}, {{%.*}} : !s32i

// LLVM-LABEL: @test_sub_sat_signed
// LLVM: call i32 @llvm.ssub.sat.i32(i32 {{%.*}}, i32 {{%.*}})

// OGCG-LABEL: @test_sub_sat_signed
// OGCG: call i32 @llvm.ssub.sat.i32(i32 {{%.*}}, i32 {{%.*}})

unsigned test_sub_sat_unsigned(unsigned a, unsigned b) {
  return __builtin_elementwise_sub_sat(a, b);
}

// CIR-LABEL: @test_sub_sat_unsigned
// CIR: cir.usub.sat {{%.*}}, {{%.*}} : !u32i

// LLVM-LABEL: @test_sub_sat_unsigned
// LLVM: call i32 @llvm.usub.sat.i32(i32 {{%.*}}, i32 {{%.*}})

// OGCG-LABEL: @test_sub_sat_unsigned
// OGCG: call i32 @llvm.usub.sat.i32(i32 {{%.*}}, i32 {{%.*}})
