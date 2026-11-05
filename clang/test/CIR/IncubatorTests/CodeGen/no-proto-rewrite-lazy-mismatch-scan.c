// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s

// Regression test for no-proto replacement fast path scanning: first argument
// already matches and a later argument needs pointer coercion. This ensures we
// still rewrite the call correctly when mismatch detection and coercion happen
// in one pass.

struct S;
int np_mix();
int call_mix(int x, void *p) { return np_mix(x, p); }

struct S {
  int v;
};
int np_mix(int x, struct S *p) { return x + p->v; }

// CHECK-LABEL: cir.func {{.*}} @call_mix(
// CHECK: cir.call @np_mix(%{{.*}}, %{{.*}}){{.*}} : (!s32i, !cir.ptr<!rec_S>) -> !s32i
