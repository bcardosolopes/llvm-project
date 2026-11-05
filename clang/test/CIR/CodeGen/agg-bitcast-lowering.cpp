// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -fclangir -emit-llvm \
// RUN:   -fno-clangir-call-conv-lowering -std=gnu++20 -w -o - %s | FileCheck %s

// Regression test: aggregate-type bitcasts from ABI coercion must use
// alloca+store+load instead of llvm.bitcast (which can't handle aggregates).

typedef struct { int x, y; } Pair;

Pair make_pair(int a, int b) {
  Pair p;
  p.x = a;
  p.y = b;
  return p;
}

// CHECK-LABEL: @_Z9make_pairii
// CHECK-NOT: llvm.bitcast {{.*}}struct
// CHECK: ret
