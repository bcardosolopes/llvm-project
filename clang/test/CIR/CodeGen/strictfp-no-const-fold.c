// Under strict FP (-frounding-math / -ffp-exception-behavior=strict), CIRGen
// lowering must NOT constant-fold FP arithmetic, int<->float conversions, or FP
// comparisons on constant operands: it must defer to the constrained-FP
// intrinsics so dynamic rounding and strict exception semantics are preserved.
// Without the strict flags the same expressions are still folded (the CONTROL
// run below), matching default codegen.

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O0 -fclangir \
// RUN:   -frounding-math -ffp-exception-behavior=strict -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=STRICT
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O0 -fclangir \
// RUN:   -emit-llvm %s -o - | FileCheck %s --check-prefix=DEFAULT

// STRICT-LABEL: define{{.*}} double @fdiv_
// STRICT: call double @llvm.experimental.constrained.fdiv.f64(double 1.000000e+00, double 3.000000e+00, metadata !"round.dynamic", metadata !"fpexcept.strict")
// DEFAULT-LABEL: define{{.*}} double @fdiv_
// DEFAULT: ret double 0x3FD5555555555555
double fdiv_(void) { return 1.0 / 3.0; }

// STRICT-LABEL: define{{.*}} double @fadd_
// STRICT: call double @llvm.experimental.constrained.fadd.f64(double 1.100000e+00, double 3.300000e+00, metadata !"round.dynamic", metadata !"fpexcept.strict")
// DEFAULT-LABEL: define{{.*}} double @fadd_
// DEFAULT: ret double 4.400000e+00
double fadd_(void) { return 1.1 + 3.3; }

// STRICT-LABEL: define{{.*}} double @fmul_
// STRICT: call double @llvm.experimental.constrained.fmul.f64(double 1.100000e+00, double 3.300000e+00, metadata !"round.dynamic", metadata !"fpexcept.strict")
// DEFAULT-LABEL: define{{.*}} double @fmul_
// DEFAULT: ret double 3.630000e+00
double fmul_(void) { return 1.1 * 3.3; }

// A signed int -> double conversion that is inexact (2^53 + 1 is not exactly
// representable) must not be folded under strict FP.
// STRICT-LABEL: define{{.*}} double @i2f_
// STRICT: call double @llvm.experimental.constrained.sitofp.f64.i64(i64 9007199254740993, metadata !"round.dynamic", metadata !"fpexcept.strict")
// DEFAULT-LABEL: define{{.*}} double @i2f_
// DEFAULT: ret double 0x4340000000000000
double i2f_(void) { return (double)9007199254740993LL; }

// double -> int conversion must defer to constrained.fptosi.
// STRICT-LABEL: define{{.*}} i32 @f2i_
// STRICT: call i32 @llvm.experimental.constrained.fptosi.i32.f64(double 2.500000e+00, metadata !"fpexcept.strict")
// DEFAULT-LABEL: define{{.*}} i32 @f2i_
// DEFAULT: ret i32 2
int f2i_(void) { return (int)2.5; }

// FP comparison on (constant) operands must defer to constrained.fcmp / fdiv.
// STRICT-LABEL: define{{.*}} i32 @fcmp_
// STRICT: call double @llvm.experimental.constrained.fdiv.f64
// STRICT: call i1 @llvm.experimental.constrained.fcmps.f64
// DEFAULT-LABEL: define{{.*}} i32 @fcmp_
// DEFAULT: ret i32 1
int fcmp_(void) { return (1.0 / 3.0) < 0.3334; }
