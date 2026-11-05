// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir \
// RUN:   -ffp-exception-behavior=strict %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -ffp-exception-behavior=strict %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -ffp-exception-behavior=strict %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// Verify constrained FP end-to-end: CIR attrs → LLVM constrained intrinsics.

float test_fadd(float a, float b) { return a + b; }
// CIR-LABEL: cir.func {{.*}}@test_fadd
// CIR: cir.binop(add, {{.*}}) : !cir.float {fpExceptionBehavior
// LLVM: call float @llvm.experimental.constrained.fadd.f32(
// LLVM-SAME: metadata !"fpexcept.strict"
// OGCG: call float @llvm.experimental.constrained.fadd.f32(
// OGCG-SAME: metadata !"fpexcept.strict"

float test_fsub(float a, float b) { return a - b; }
// CIR-LABEL: cir.func {{.*}}@test_fsub
// CIR: cir.binop(sub, {{.*}}) : !cir.float {fpExceptionBehavior
// LLVM: call float @llvm.experimental.constrained.fsub.f32(
// OGCG: call float @llvm.experimental.constrained.fsub.f32(

float test_fmul(float a, float b) { return a * b; }
// CIR-LABEL: cir.func {{.*}}@test_fmul
// CIR: cir.binop(mul, {{.*}}) : !cir.float {fpExceptionBehavior
// LLVM: call float @llvm.experimental.constrained.fmul.f32(
// OGCG: call float @llvm.experimental.constrained.fmul.f32(

float test_fdiv(float a, float b) { return a / b; }
// CIR-LABEL: cir.func {{.*}}@test_fdiv
// CIR: cir.binop(div, {{.*}}) : !cir.float {fpExceptionBehavior
// LLVM: call float @llvm.experimental.constrained.fdiv.f32(
// OGCG: call float @llvm.experimental.constrained.fdiv.f32(
