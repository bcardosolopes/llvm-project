// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s

// Compile-time regression: calls with too-few args are non-rewritable in the
// no-proto replacement pass and should skip builder insertion/guard work.

int fewer_nr();
int call_fewer_nr(void) { return fewer_nr(); }
int call_fewer_nr_again(void) { return fewer_nr(); }
int fewer_nr(int x) { return x + 3; }

// CHECK-LABEL: define dso_local i32 @call_fewer_nr(
// CHECK: call i32 (...) @fewer_nr()
// CHECK-LABEL: define dso_local i32 @call_fewer_nr_again(
// CHECK: call i32 (...) @fewer_nr()
