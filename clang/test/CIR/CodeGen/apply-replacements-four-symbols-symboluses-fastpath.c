// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s

// Compile-time regression for applyReplacements generic fallback path (>3
// mismatches): preserve call/get_global rewrites while allowing symbol-use
// collection to satisfy most symbols before any fallback module scan.

int np4_0();
int np4_1();
int np4_2();
int np4_3();

int use_np4_0(int x) { return np4_0(x); }
int use_np4_1(int x) { return np4_1(x + 1); }
int use_np4_2(int x) { return np4_2(x + 2); }
int use_np4_3(int x) { return np4_3(x + 3); }

int np4_0(int x) { return x + 10; }
int np4_1(int x) { return x + 20; }
int np4_2(int x) { return x + 30; }
int np4_3(int x) { return x + 40; }

// CHECK-LABEL: cir.func{{.*}} @use_np4_0
// CHECK: cir.call @np4_0(%{{.*}}) {{.*}} -> !s32i
// CHECK-LABEL: cir.func{{.*}} @use_np4_1
// CHECK: cir.call @np4_1(%{{.*}}) {{.*}} -> !s32i
// CHECK-LABEL: cir.func{{.*}} @use_np4_2
// CHECK: cir.call @np4_2(%{{.*}}) {{.*}} -> !s32i
// CHECK-LABEL: cir.func{{.*}} @use_np4_3
// CHECK: cir.call @np4_3(%{{.*}}) {{.*}} -> !s32i
