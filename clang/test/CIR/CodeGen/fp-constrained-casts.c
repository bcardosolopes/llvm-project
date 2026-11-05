// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir \
// RUN:   -ffp-exception-behavior=strict %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -ffp-exception-behavior=strict %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -ffp-exception-behavior=strict %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

int fptosi(float x) { return (int)x; }
// CIR-LABEL: cir.func {{.*}}@fptosi
// CIR: cir.cast float_to_int {{.*}} {cir.fp_exception
// LLVM: call i32 @llvm.experimental.constrained.fptosi.i32.f32(
// LLVM-SAME: metadata !"fpexcept.strict"
// OGCG: call i32 @llvm.experimental.constrained.fptosi.i32.f32(
// OGCG-SAME: metadata !"fpexcept.strict"

unsigned fptoui(float x) { return (unsigned)x; }
// CIR-LABEL: cir.func {{.*}}@fptoui
// CIR: cir.cast float_to_int {{.*}} {cir.fp_exception
// LLVM: call i32 @llvm.experimental.constrained.fptoui.i32.f32(
// LLVM-SAME: metadata !"fpexcept.strict"
// OGCG: call i32 @llvm.experimental.constrained.fptoui.i32.f32(
// OGCG-SAME: metadata !"fpexcept.strict"

float sitofp(int x) { return (float)x; }
// CIR-LABEL: cir.func {{.*}}@sitofp
// CIR: cir.cast int_to_float {{.*}} {cir.fp_exception
// LLVM: call float @llvm.experimental.constrained.sitofp.f32.i32(
// LLVM-SAME: metadata !"round.tonearest"
// LLVM-SAME: metadata !"fpexcept.strict"
// OGCG: call float @llvm.experimental.constrained.sitofp.f32.i32(
// OGCG-SAME: metadata !"round.tonearest"
// OGCG-SAME: metadata !"fpexcept.strict"

float uitofp(unsigned x) { return (float)x; }
// CIR-LABEL: cir.func {{.*}}@uitofp
// CIR: cir.cast int_to_float {{.*}} {cir.fp_exception
// LLVM: call float @llvm.experimental.constrained.uitofp.f32.i32(
// LLVM-SAME: metadata !"round.tonearest"
// LLVM-SAME: metadata !"fpexcept.strict"
// OGCG: call float @llvm.experimental.constrained.uitofp.f32.i32(
// OGCG-SAME: metadata !"round.tonearest"
// OGCG-SAME: metadata !"fpexcept.strict"

double fpext(float x) { return (double)x; }
// CIR-LABEL: cir.func {{.*}}@fpext
// CIR: cir.cast floating {{.*}} {cir.fp_exception
// LLVM: call double @llvm.experimental.constrained.fpext.f64.f32(
// LLVM-SAME: metadata !"fpexcept.strict"
// OGCG: call double @llvm.experimental.constrained.fpext.f64.f32(
// OGCG-SAME: metadata !"fpexcept.strict"

float fptrunc(double x) { return (float)x; }
// CIR-LABEL: cir.func {{.*}}@fptrunc
// CIR: cir.cast floating {{.*}} {cir.fp_exception
// LLVM: call float @llvm.experimental.constrained.fptrunc.f32.f64(
// LLVM-SAME: metadata !"round.tonearest"
// LLVM-SAME: metadata !"fpexcept.strict"
// OGCG: call float @llvm.experimental.constrained.fptrunc.f32.f64(
// OGCG-SAME: metadata !"round.tonearest"
// OGCG-SAME: metadata !"fpexcept.strict"
