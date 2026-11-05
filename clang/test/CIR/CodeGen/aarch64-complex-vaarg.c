// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s

// Regression test: a _Complex va_arg on aarch64 must be expanded through the
// AAPCS64 HFA (FP/SIMD register save-area) sequence, exactly like classic
// CodeGen. Previously ClangIR left a raw aggregate-typed llvm `va_arg`
// instruction ({ double, double }) that no AArch64 isel path can select,
// crashing the backend (GlobalISel assert at -O0, "Unknown type!" at -O2/-O3).

double f(int n, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, n);
  _Complex double z = __builtin_va_arg(ap, _Complex double);
  __builtin_va_end(ap);
  return __real__ z + __imag__ z;
}

// The AAPCS64 expansion replaces the raw `va_arg`; no `va_arg` instruction
// (which is what crashed the backend for the aggregate _Complex type) remains.
// CHECK-LABEL: @f
// CHECK-NOT: va_arg
