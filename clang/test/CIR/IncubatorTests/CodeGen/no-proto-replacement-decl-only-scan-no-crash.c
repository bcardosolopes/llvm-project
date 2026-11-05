// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s

// Compile-time regression: replaceUsesOfNonProtoTypeWithRealFunction performs
// a module function scan for symbol users; declaration-only functions should be
// skipped without changing correctness.

int np_target();
extern int scan_decl_0(int);
extern int scan_decl_1(int);
extern int scan_decl_2(int);
extern int scan_decl_3(int);
extern int scan_decl_4(int);
extern int scan_decl_5(int);

int np_user0(int x) { return np_target(x); }
int np_user1(int y) { return np_target(y); }

int np_target(int v) { return v + 3; }

// CHECK-LABEL: define dso_local i32 @np_user0(
// CHECK: call i32 @np_target(i32 noundef
// CHECK-LABEL: define dso_local i32 @np_user1(
// CHECK: call i32 @np_target(i32 noundef
