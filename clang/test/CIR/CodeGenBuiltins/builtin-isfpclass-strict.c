// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir \
// RUN:   -ffp-exception-behavior=strict %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -ffp-exception-behavior=strict %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

// Verify that isnan/isinf/isfinite work correctly under strict FP exception
// behavior. These builtins use is_fpclass which has no constrained variant,
// so they produce identical output regardless of FP exception mode.

int test_isnan(float x) {
  return __builtin_isnan(x);
}

// CIR-LABEL: cir.func {{.*}}@test_isnan
// CIR: cir.is_fp_class %{{.*}}, 3 : (!cir.float) -> !cir.bool
// LLVM-LABEL: @test_isnan
// LLVM: call i1 @llvm.is.fpclass.f32(float %{{.*}}, i32 3)

int test_isinf(float x) {
  return __builtin_isinf(x);
}

// CIR-LABEL: cir.func {{.*}}@test_isinf
// CIR: cir.is_fp_class %{{.*}}, 516 : (!cir.float) -> !cir.bool
// LLVM-LABEL: @test_isinf
// LLVM: call i1 @llvm.is.fpclass.f32(float %{{.*}}, i32 516)

int test_isfinite(float x) {
  return __builtin_isfinite(x);
}

// CIR-LABEL: cir.func {{.*}}@test_isfinite
// CIR: cir.is_fp_class %{{.*}}, 504 : (!cir.float) -> !cir.bool
// LLVM-LABEL: @test_isfinite
// LLVM: call i1 @llvm.is.fpclass.f32(float %{{.*}}, i32 504)
