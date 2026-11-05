// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -debug-info-kind=limited \
// RUN:   -emit-llvm %s -o - | FileCheck %s

// The store of an incoming argument into its stack slot is part of the prologue
// and must carry no !dbg location, so the backend places prologue_end on the
// first real statement (line 13 here) rather than on the function's opening
// brace. With a body location on these stores, a function breakpoint would stop
// before the arguments are spilled and show uninitialized argument values.

int add(int a, int b) {
  return a + b;
}

// CHECK-LABEL: define {{.*}}@add
// CHECK: store i32 %0, ptr %{{[0-9]+}}, align 4{{$}}
// CHECK: store i32 %1, ptr %{{[0-9]+}}, align 4{{$}}
// The first statement's instructions do carry a !dbg location.
// CHECK: load i32, ptr %{{[0-9]+}}, align 4, !dbg
