// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s

// Compile-time regression: applyReplacements mismatch-user scans should skip
// declaration-only functions. Ensure correctness remains unchanged.

extern int decl_only0(int);
extern int decl_only1(int);
extern int decl_only2(int);
extern int decl_only3(int);
extern int decl_only4(int);
extern int decl_only5(int);

int np_ar();
int use_np_ar0(int x) { return np_ar(x); }
int use_np_ar1(int y) { return np_ar(y); }
int np_ar(int v) { return v + 11; }

// CHECK-LABEL: cir.func{{.*}} @use_np_ar0
// CHECK: cir.call @np_ar(%{{.*}}) {{.*}} -> !s32i
// CHECK-LABEL: cir.func{{.*}} @use_np_ar1
// CHECK: cir.call @np_ar(%{{.*}}) {{.*}} -> !s32i
