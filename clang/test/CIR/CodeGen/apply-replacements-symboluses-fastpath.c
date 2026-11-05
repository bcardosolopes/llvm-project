// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s

// Regression coverage for applyReplacements symbol-uses fast path with
// mismatched replacements: preserve call/get_global rewrite behavior.

int np_fast0();
int np_fast1();

int use_fast0(int x) { return np_fast0(x); }
int use_fast1(int x) { return np_fast1(x + 1); }
int use_fast0_again(int x) { return np_fast0(x + 2); }
int use_fast_mix(int x) { return np_fast0(x) + np_fast1(x); }
int use_fast_alternating(int x) {
  return np_fast0(x) + np_fast1(x + 1) + np_fast0(x + 2);
}

int np_fast0(int x) { return x + 10; }
int np_fast1(int x) { return x + 20; }

// CHECK-LABEL: cir.func{{.*}} @use_fast0
// CHECK: cir.call @np_fast0(%{{.*}}) {{.*}} -> !s32i
// CHECK-LABEL: cir.func{{.*}} @use_fast1
// CHECK: cir.call @np_fast1(%{{.*}}) {{.*}} -> !s32i
// CHECK-LABEL: cir.func{{.*}} @use_fast0_again
// CHECK: cir.call @np_fast0(%{{.*}}) {{.*}} -> !s32i
// CHECK-LABEL: cir.func{{.*}} @use_fast_mix
// CHECK: cir.call @np_fast0(%{{.*}}) {{.*}} -> !s32i
// CHECK: cir.call @np_fast1(%{{.*}}) {{.*}} -> !s32i
// CHECK-LABEL: cir.func{{.*}} @use_fast_alternating
// CHECK-COUNT-3: cir.call
