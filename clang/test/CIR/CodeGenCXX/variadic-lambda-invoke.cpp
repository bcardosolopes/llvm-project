// RUN: not %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o /dev/null 2>&1 | FileCheck %s

// Test: variadic lambda static invoke body is NYI.
// Converting a C-variadic lambda to function pointer requires
// forwarding the va_list, which CIR does not yet support.
auto test() {
  return +[](int first, ...) -> int {
    return first;
  };
}

// CHECK: ClangIR code gen Not Yet Implemented: emitLambdaStaticInvokeBody: variadic
