// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s

// Exercise the replaceUsesOfNonProtoTypeWithRealFunction fast path where
// argument types already match the eventual prototype and no coercion is
// required.

int np_match();

int call_np_match(int x) { return np_match(x); }
int call_np_match_again(int y) { return np_match(y); }

int np_match(int z) { return z + 1; }

// CHECK-LABEL: define dso_local i32 @call_np_match(
// CHECK: call i32 @np_match(i32 noundef
// CHECK-LABEL: define dso_local i32 @call_np_match_again(
// CHECK: call i32 @np_match(i32 noundef
