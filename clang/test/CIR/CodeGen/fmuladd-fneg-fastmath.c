// RUN: %clang_cc1 -triple aarch64-none-linux-android -menable-no-infs -menable-no-nans -ffp-contract=on -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-android -menable-no-infs -menable-no-nans -ffp-contract=on -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// When `a*b - c` is contracted into fmuladd(a, b, -c), the fneg created for the
// negated addend must inherit the same fast-math flags as the rest of the
// expression. Previously CIRGen emitted the contraction fneg with no flags.

// CIR:   fneg nnan ninf double
// CIR:   call nnan ninf double @llvm.fmuladd.f64
// OGCG:  fneg nnan ninf double
// OGCG:  call nnan ninf double @llvm.fmuladd.f64
double f(double a, double b, double c) { return a * b - c; }
