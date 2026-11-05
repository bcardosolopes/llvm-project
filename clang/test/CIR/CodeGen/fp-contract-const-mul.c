// Verify that ClangIR does not form an fmuladd for a multiply of two
// floating-point constants under the default -ffp-contract=on. Classic clang
// constant-folds fmul(constFP, constFP) before FMA formation, so `u + (C1*C2)`
// becomes a plain fadd of `u` with the folded constant. ClangIR must match:
// fusing the constant product would apply a single (fused) rounding that
// diverges from classic clang and gcc on targets with a real fma (aarch64).
// See task #295.

// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -ffp-contract=on \
// RUN:   -fclangir -emit-llvm %s -o - | FileCheck %s

double const_mul_add(double u) { return u + (6.3335 * -4.6294); }
// CHECK-LABEL: @const_mul_add
// CHECK-NOT:   fmuladd
// CHECK:       fadd
// CHECK-NOT:   fmuladd

// Also a constant SUBEXPRESSION multiply: (C1/C2)*C3 is wholly constant, so it
// must not fuse either (classic pre-folds it). ClangIR has no FP-binop folder,
// so the div/mul survive as live ops; the guard must recognize the whole
// product is constant and refuse to contract.
double const_expr_mul_add(double u) { return u + ((8.4129 / 6.3729) * -3.8629); }
// CHECK-LABEL: @const_expr_mul_add
// CHECK-NOT:   fmuladd
// CHECK:       fadd
// CHECK-NOT:   fmuladd

// A multiply with a non-constant operand must still contract to fmuladd
// (matches classic clang) — the guard only excludes wholly-constant products.
double var_mul_add(double a, double b, double c) { return a * b + c; }
// CHECK-LABEL: @var_mul_add
// CHECK:       fmuladd
