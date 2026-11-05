// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -ffast-math -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -ffast-math -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s -check-prefix=OGCG
//
// Verify that FastMathFlags are propagated through CIR lowering for
// unary floating-point builtin operations.

// acos
float test_acosf(float x) { return __builtin_acosf(x); }
// CIR-LABEL: define {{.*}} @test_acosf
// CIR: call reassoc nnan ninf nsz arcp afn float @llvm.acos.f32(float
// OGCG-LABEL: define {{.*}} @test_acosf
// OGCG: call reassoc nnan ninf nsz arcp afn float @llvm.acos.f32(float

// exp10
float test_exp10f(float x) { return __builtin_exp10f(x); }
// CIR-LABEL: define {{.*}} @test_exp10f
// CIR: call reassoc nnan ninf nsz arcp afn float @llvm.exp10.f32(float
// OGCG-LABEL: define {{.*}} @test_exp10f
// OGCG: call reassoc nnan ninf nsz arcp afn float @llvm.exp10.f32(float

// cosh
float test_coshf(float x) { return __builtin_coshf(x); }
// CIR-LABEL: define {{.*}} @test_coshf
// CIR: call reassoc nnan ninf nsz arcp afn float @llvm.cosh.f32(float
// OGCG-LABEL: define {{.*}} @test_coshf
// OGCG: call reassoc nnan ninf nsz arcp afn float @llvm.cosh.f32(float

// sinh
float test_sinhf(float x) { return __builtin_sinhf(x); }
// CIR-LABEL: define {{.*}} @test_sinhf
// CIR: call reassoc nnan ninf nsz arcp afn float @llvm.sinh.f32(float
// OGCG-LABEL: define {{.*}} @test_sinhf
// OGCG: call reassoc nnan ninf nsz arcp afn float @llvm.sinh.f32(float

// tanh
float test_tanhf(float x) { return __builtin_tanhf(x); }
// CIR-LABEL: define {{.*}} @test_tanhf
// CIR: call reassoc nnan ninf nsz arcp afn float @llvm.tanh.f32(float
// OGCG-LABEL: define {{.*}} @test_tanhf
// OGCG: call reassoc nnan ninf nsz arcp afn float @llvm.tanh.f32(float

// floor
float test_floorf(float x) { return __builtin_floorf(x); }
// CIR-LABEL: define {{.*}} @test_floorf
// CIR: call reassoc nnan ninf nsz arcp afn float @llvm.floor.f32(float
// OGCG-LABEL: define {{.*}} @test_floorf
// OGCG: call reassoc nnan ninf nsz arcp afn float @llvm.floor.f32(float

// fabs
float test_fabsf(float x) { return __builtin_fabsf(x); }
// CIR-LABEL: define {{.*}} @test_fabsf
// CIR: call reassoc nnan ninf nsz arcp afn float @llvm.fabs.f32(float
// OGCG-LABEL: define {{.*}} @test_fabsf
// OGCG: call reassoc nnan ninf nsz arcp afn float @llvm.fabs.f32(float

// round
float test_roundf(float x) { return __builtin_roundf(x); }
// CIR-LABEL: define {{.*}} @test_roundf
// CIR: call reassoc nnan ninf nsz arcp afn float @llvm.round.f32(float
// OGCG-LABEL: define {{.*}} @test_roundf
// OGCG: call reassoc nnan ninf nsz arcp afn float @llvm.round.f32(float

// roundeven
float test_roundevenf(float x) { return __builtin_roundevenf(x); }
// CIR-LABEL: define {{.*}} @test_roundevenf
// CIR: call reassoc nnan ninf nsz arcp afn float @llvm.roundeven.f32(float
// OGCG-LABEL: define {{.*}} @test_roundevenf
// OGCG: call reassoc nnan ninf nsz arcp afn float @llvm.roundeven.f32(float

// trunc
float test_truncf(float x) { return __builtin_truncf(x); }
// CIR-LABEL: define {{.*}} @test_truncf
// CIR: call reassoc nnan ninf nsz arcp afn float @llvm.trunc.f32(float
// OGCG-LABEL: define {{.*}} @test_truncf
// OGCG: call reassoc nnan ninf nsz arcp afn float @llvm.trunc.f32(float

// nearbyint
float test_nearbyintf(float x) { return __builtin_nearbyintf(x); }
// CIR-LABEL: define {{.*}} @test_nearbyintf
// CIR: call reassoc nnan ninf nsz arcp afn float @llvm.nearbyint.f32(float
// OGCG-LABEL: define {{.*}} @test_nearbyintf
// OGCG: call reassoc nnan ninf nsz arcp afn float @llvm.nearbyint.f32(float
