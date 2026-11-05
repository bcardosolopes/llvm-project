// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s

// Ensure no-proto replacement keeps calls with too-few args as variadic calls,
// while still resolving to the final symbol.

int fewer();
int call_fewer(void) { return fewer(); }
int call_fewer2(void) { return fewer(); }
int call_mixed(void) { return fewer() + fewer(7); }
int fewer(int x) { return x + 2; }

// CHECK-LABEL: define dso_local i32 @call_fewer(
// CHECK: call i32 (...) @fewer()
// CHECK-LABEL: define dso_local i32 @call_fewer2(
// CHECK: call i32 (...) @fewer()
// CHECK-LABEL: define dso_local i32 @call_mixed(
// CHECK: call i32 (...) @fewer()
// CHECK: call i32 @fewer(i32 noundef 7)
