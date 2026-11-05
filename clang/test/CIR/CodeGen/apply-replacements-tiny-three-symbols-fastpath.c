// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s

// Compile-time regression: keep exact 3-symbol mismatched replacement shape
// covered so tiny-set applyReplacements fast path (including third-symbol
// dispatch) preserves get_global/call rewrites.

int np_t0();
int np_t1();
int np_t2();

int use_np_t0(int x) { return np_t0(x); }
int use_np_t1(int x) { return np_t1(x + 1); }
int use_np_t2(int x) { return np_t2(x + 2); }

int np_t0(int x) { return x + 10; }
int np_t1(int x) { return x + 20; }
int np_t2(int x) { return x + 30; }

// CHECK-LABEL: cir.func{{.*}} @use_np_t0
// CHECK: cir.call @np_t0(%{{.*}}) {{.*}} -> !s32i
// CHECK-LABEL: cir.func{{.*}} @use_np_t1
// CHECK: cir.call @np_t1(%{{.*}}) {{.*}} -> !s32i
// CHECK-LABEL: cir.func{{.*}} @use_np_t2
// CHECK: cir.call @np_t2(%{{.*}}) {{.*}} -> !s32i
