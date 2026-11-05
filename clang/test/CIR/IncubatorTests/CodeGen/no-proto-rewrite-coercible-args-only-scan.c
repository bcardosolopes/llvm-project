// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s
// Compile-time regression: no-proto rewrite should only inspect coercible
// (pointer/method) parameter positions when deciding whether to materialize
// per-call coercion vectors. Non-coercible scalar positions should not force
// extra rewrite work.

struct S;
int np_coercible_scan();

int call_np_coercible_scan(int a, int b, int c, int d, int e, int f,
                           int g, int h, int i, int j, int k, int l,
                           void *p) {
  return np_coercible_scan(a, b, c, d, e, f, g, h, i, j, k, l, p);
}

struct S {
  int v;
};

int np_coercible_scan(int a, int b, int c, int d, int e, int f, int g, int h,
                      int i, int j, int k, int l, struct S *p) {
  return a + b + c + d + e + f + g + h + i + j + k + l + p->v;
}

// CHECK-LABEL: cir.func {{.*}} @call_np_coercible_scan(
// CHECK: cir.call @np_coercible_scan(%{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}){{.*}} : (!s32i, !s32i, !s32i, !s32i, !s32i, !s32i, !s32i, !s32i, !s32i, !s32i, !s32i, !s32i, !cir.ptr<!rec_S>) -> !s32i
