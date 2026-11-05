// Regression test: the two UBSan runtime helpers reached via -fsanitize=unreachable
// (__builtin_unreachable / missing return -> __ubsan_handle_builtin_unreachable) and
// -fsanitize=builtin (__builtin_assume/ctz/clz -> __ubsan_handle_invalid_builtin) must
// be materialized with DISTINCT stub symbols. They previously shared the symbol
// __cir_ubsan_builtin_unreachable with conflicting FuncTypes (()->void vs (iN)->void);
// in a TU exercising both, the second cir.call disagreed with the callee arity and the
// strongly-typed cir.call verifier rejected the module:
//   'cir.call' op incorrect number of operands for callee
//   fatal error: CIR module verification error before running CIR-to-CIR passes
// Both real handlers must still be emitted.

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fsanitize=unreachable,builtin \
// RUN:   -fclangir -emit-llvm %s -o - | FileCheck %s

int with_unreachable(int x) {
  if (x)
    return 1;
  __builtin_unreachable();
}

void with_assume(bool c) {
  __builtin_assume(c);
}

// CHECK-DAG: call void @__ubsan_handle_builtin_unreachable
// CHECK-DAG: call void @__ubsan_handle_invalid_builtin
