// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s

// Compile-time regression: no-proto replacement scans module functions for
// symbol users; declaration-only functions should be skipped without changing
// correctness.

int target();
extern int unrelated_decl_0(int);
extern int unrelated_decl_1(int);
extern int unrelated_decl_2(int);
extern int unrelated_decl_3(int);
extern int unrelated_decl_4(int);
extern int unrelated_decl_5(int);
extern int unrelated_decl_6(int);
extern int unrelated_decl_7(int);

int use_target(int x) { return target(x); }
int use_target_again(int y) { return target(y); }

int target(int v) { return v + 9; }

// CHECK-LABEL: define dso_local i32 @use_target(
// CHECK: call i32 @target(i32 noundef
// CHECK-LABEL: define dso_local i32 @use_target_again(
// CHECK: call i32 @target(i32 noundef
