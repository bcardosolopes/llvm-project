// RUN: %clang_cc1 -triple x86_64-none-linux-android21 -std=gnu++20 -fclangir -emit-llvm %s -o - | FileCheck %s

// Regression test: a coerced by-value record parameter (ABIArgInfo::Direct)
// followed by an empty-record parameter (ABIArgInfo::Ignore) drove a
// heap-use-after-free in CallConvLowering's buildFunctionProlog: the Direct
// case erased the original argument's init-store chain (freeing the op the
// saved insertion point referred to), and the next op creation (the Ignore
// case's cir.const) inserted against that freed insertion point (SIGSEGV, seen
// compiling a generated serialization file for Android). The
// fix re-seats the insertion point to a live op after the erase instead of
// restoring the dangling one. This exercises the Direct-then-Ignore prologue
// path; the ASAN-instrumented check-clang-cir clang flags a regression if the
// use-after-free returns.

struct Empty {};
struct Coerce { double x; double y; };
void f(Coerce c, Empty e) {}

// CHECK-LABEL: define{{.*}} @_Z1f6Coerce5Empty(double %0, double %1)
// CHECK: alloca %struct.Coerce
// CHECK: store double %0
// CHECK: store double %1
// CHECK: ret void
