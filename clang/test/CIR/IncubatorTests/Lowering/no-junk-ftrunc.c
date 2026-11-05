// RUN: %clang_cc1 -fno-strict-float-cast-overflow -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -fno-strict-float-cast-overflow -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -fno-strict-float-cast-overflow -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Verify that -fno-strict-float-cast-overflow uses saturating intrinsics.

// Also verify that the default (strict) mode uses plain fptosi/fptoui.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.strict.ll
// RUN: FileCheck --input-file=%t.strict.ll %s --check-prefix=STRICT

int main(void) {
  double d = 1e20;
  return (int)d != 1e20 && (unsigned)d != 1e20;
}

// CIR: cir.cast float_to_int_sat
// CIR: cir.cast float_to_int_sat

// LLVM: call i32 @llvm.fptosi.sat.i32.f64
// LLVM: call i32 @llvm.fptoui.sat.i32.f64

// OGCG: call i32 @llvm.fptosi.sat.i32.f64
// OGCG: call i32 @llvm.fptoui.sat.i32.f64

// STRICT: = fptosi
// STRICT: = fptoui
// STRICT-NOT: sat
