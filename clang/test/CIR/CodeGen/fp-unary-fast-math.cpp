// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -menable-no-nans \
// RUN:   -menable-no-infs -fno-signed-zeros -freciprocal-math -fapprox-func \
// RUN:   -funsafe-math-optimizations -mreassociate -ffp-contract=fast \
// RUN:   -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -menable-no-nans \
// RUN:   -menable-no-infs -fno-signed-zeros -freciprocal-math -fapprox-func \
// RUN:   -funsafe-math-optimizations -mreassociate -ffp-contract=fast \
// RUN:   -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=OGCG %s

// Unary negation on float.
float neg_f(float a) { return -a; }
// LLVM-LABEL: @_Z5neg_ff
// LLVM: fneg fast float
// OGCG-LABEL: @_Z5neg_ff
// OGCG: fneg fast float

// Unary negation on double.
double neg_d(double a) { return -a; }
// LLVM-LABEL: @_Z5neg_dd
// LLVM: fneg fast double
// OGCG-LABEL: @_Z5neg_dd
// OGCG: fneg fast double

// Float pre-increment.
float inc_f(float a) { return ++a; }
// LLVM-LABEL: @_Z5inc_ff
// LLVM: fadd fast float
// OGCG-LABEL: @_Z5inc_ff
// OGCG: fadd fast float

// Float pre-decrement.
float dec_f(float a) { return --a; }
// LLVM-LABEL: @_Z5dec_ff
// LLVM: fadd fast float
// OGCG-LABEL: @_Z5dec_ff
// OGCG: fadd fast float

// Float post-increment.
float postinc_f(float a) { return a++; }
// LLVM-LABEL: @_Z9postinc_ff
// LLVM: fadd fast float
// OGCG-LABEL: @_Z9postinc_ff
// OGCG: fadd fast float

// Long double pre-increment (uses BinOp path).
long double inc_ld(long double a) { return ++a; }
// LLVM-LABEL: @_Z6inc_lde
// LLVM: fadd fast x86_fp80
// OGCG-LABEL: @_Z6inc_lde
// OGCG: fadd fast x86_fp80
