// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s

// Compile-time regression coverage: mismatched replacement symbols may have no
// typed call/get_global users in the TU. applyReplacements should avoid extra
// fixup work while preserving output correctness.

int np_no_user();
int np_with_user();
int np_with_user2();

int use_np_with_user(int x) { return np_with_user(x); }
int use_np_with_user2(int x) { return np_with_user2(x + 1); }

int np_no_user(int x) { return x + 3; }
int np_with_user(int x) { return x + 7; }
int np_with_user2(int x) { return x + 11; }

// CHECK-LABEL: cir.func{{.*}} @use_np_with_user
// CHECK: cir.call @np_with_user(%{{.*}}) {{.*}} -> !s32i
// CHECK-LABEL: cir.func{{.*}} @use_np_with_user2
// CHECK: cir.call @np_with_user2(%{{.*}}) {{.*}} -> !s32i
