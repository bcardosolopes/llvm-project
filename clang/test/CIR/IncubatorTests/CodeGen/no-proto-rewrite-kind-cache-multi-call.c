// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s

// Compile-time regression scaffold for no-proto replacement kind-classifier
// caching: rewrite two independent calls against the same final declaration.

struct S;
int np_mix();

int call_mix_a(int x, void *p) { return np_mix(x, p); }
int call_mix_b(int x, void *p) { return np_mix(x, p); }

struct S {
  int v;
};

int np_mix(int x, struct S *p) { return x + p->v; }

// CHECK-LABEL: cir.func {{.*}} @call_mix_a(
// CHECK: cir.call @np_mix

// CHECK-LABEL: cir.func {{.*}} @call_mix_b(
// CHECK: cir.call @np_mix
