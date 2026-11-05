// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir \
// RUN:     -ffast-math -O1 %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir \
// RUN:     -fmath-errno %s -o %t-errno.cir
// RUN: FileCheck --input-file=%t-errno.cir %s -check-prefix=CIR-ERRNO

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:     -ffast-math -O1 %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:     -ffast-math -O1 %s -o %t-ogcg.ll
// RUN: FileCheck --input-file=%t-ogcg.ll %s -check-prefix=OGCG

extern "C" {

long test_lroundf(float x) {
  return __builtin_lroundf(x);
}
// CIR: cir.func {{.*}} @test_lroundf
// CIR:   cir.lround %{{.*}} : !cir.float -> !s64i
// CIR-ERRNO: cir.func {{.*}} @test_lroundf
// CIR-ERRNO:   cir.call @lroundf
// LLVM: define {{.*}} i64 @test_lroundf
// LLVM:   call i64 @llvm.lround.i64.f32(float %{{.*}})
// OGCG: define {{.*}} i64 @test_lroundf
// OGCG:   call i64 @llvm.lround.i64.f32(float %{{.*}})

long test_lround(double x) {
  return __builtin_lround(x);
}
// CIR: cir.func {{.*}} @test_lround
// CIR:   cir.lround %{{.*}} : !cir.double -> !s64i
// CIR-ERRNO: cir.func {{.*}} @test_lround
// CIR-ERRNO:   cir.call @lround
// LLVM: define {{.*}} i64 @test_lround
// LLVM:   call i64 @llvm.lround.i64.f64(double %{{.*}})
// OGCG: define {{.*}} i64 @test_lround
// OGCG:   call i64 @llvm.lround.i64.f64(double %{{.*}})

long long test_llroundf(float x) {
  return __builtin_llroundf(x);
}
// CIR: cir.func {{.*}} @test_llroundf
// CIR:   cir.llround %{{.*}} : !cir.float -> !s64i
// CIR-ERRNO: cir.func {{.*}} @test_llroundf
// CIR-ERRNO:   cir.call @llroundf
// LLVM: define {{.*}} i64 @test_llroundf
// LLVM:   call i64 @llvm.llround.i64.f32(float %{{.*}})
// OGCG: define {{.*}} i64 @test_llroundf
// OGCG:   call i64 @llvm.llround.i64.f32(float %{{.*}})

long long test_llround(double x) {
  return __builtin_llround(x);
}
// CIR: cir.func {{.*}} @test_llround
// CIR:   cir.llround %{{.*}} : !cir.double -> !s64i
// CIR-ERRNO: cir.func {{.*}} @test_llround
// CIR-ERRNO:   cir.call @llround
// LLVM: define {{.*}} i64 @test_llround
// LLVM:   call i64 @llvm.llround.i64.f64(double %{{.*}})
// OGCG: define {{.*}} i64 @test_llround
// OGCG:   call i64 @llvm.llround.i64.f64(double %{{.*}})

long test_lrintf(float x) {
  return __builtin_lrintf(x);
}
// CIR: cir.func {{.*}} @test_lrintf
// CIR:   cir.lrint %{{.*}} : !cir.float -> !s64i
// CIR-ERRNO: cir.func {{.*}} @test_lrintf
// CIR-ERRNO:   cir.call @lrintf
// LLVM: define {{.*}} i64 @test_lrintf
// LLVM:   call i64 @llvm.lrint.i64.f32(float %{{.*}})
// OGCG: define {{.*}} i64 @test_lrintf
// OGCG:   call i64 @llvm.lrint.i64.f32(float %{{.*}})

long test_lrint(double x) {
  return __builtin_lrint(x);
}
// CIR: cir.func {{.*}} @test_lrint
// CIR:   cir.lrint %{{.*}} : !cir.double -> !s64i
// CIR-ERRNO: cir.func {{.*}} @test_lrint
// CIR-ERRNO:   cir.call @lrint
// LLVM: define {{.*}} i64 @test_lrint
// LLVM:   call i64 @llvm.lrint.i64.f64(double %{{.*}})
// OGCG: define {{.*}} i64 @test_lrint
// OGCG:   call i64 @llvm.lrint.i64.f64(double %{{.*}})

long long test_llrintf(float x) {
  return __builtin_llrintf(x);
}
// CIR: cir.func {{.*}} @test_llrintf
// CIR:   cir.llrint %{{.*}} : !cir.float -> !s64i
// CIR-ERRNO: cir.func {{.*}} @test_llrintf
// CIR-ERRNO:   cir.call @llrintf
// LLVM: define {{.*}} i64 @test_llrintf
// LLVM:   call i64 @llvm.llrint.i64.f32(float %{{.*}})
// OGCG: define {{.*}} i64 @test_llrintf
// OGCG:   call i64 @llvm.llrint.i64.f32(float %{{.*}})

long long test_llrint(double x) {
  return __builtin_llrint(x);
}
// CIR: cir.func {{.*}} @test_llrint
// CIR:   cir.llrint %{{.*}} : !cir.double -> !s64i
// CIR-ERRNO: cir.func {{.*}} @test_llrint
// CIR-ERRNO:   cir.call @llrint
// LLVM: define {{.*}} i64 @test_llrint
// LLVM:   call i64 @llvm.llrint.i64.f64(double %{{.*}})
// OGCG: define {{.*}} i64 @test_llrint
// OGCG:   call i64 @llvm.llrint.i64.f64(double %{{.*}})

} // extern "C"
