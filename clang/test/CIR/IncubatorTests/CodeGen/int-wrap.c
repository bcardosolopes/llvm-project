// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fwrapv -fclangir -emit-cir %s -o - 2>&1 | FileCheck %s --check-prefix=WRAP
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - 2>&1 | FileCheck %s --check-prefix=NOWRAP
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fwrapv-pointer -fclangir -emit-cir %s -o - 2>&1 | FileCheck %s --check-prefix=WRAPPTR

#define N 42

typedef struct {
  const char* ptr;
} A;

// WRAP:   cir.binop(sub, {{.*}}, {{.*}}) : !s32i
// NOWRAP: cir.binop(sub, {{.*}}, {{.*}}) nsw : !s32i
void foo(int* ar, int len) {
  int x = ar[len - N];
}

// check that the ptr_stride is generated in both cases (i.e. no NYI fails)

// WRAP:    cir.ptr_stride
// NOWRAP:  cir.ptr_stride
void bar(A* a, unsigned n) {
  a->ptr = a->ptr + n;
}

// WRAP    cir.ptr_stride
// NOWRAP: cir.ptr_stride
void baz(A* a) {
  a->ptr--;
}

// Unary minus: nsw should NOT be set under -fwrapv
// WRAP:   cir.unary(minus, {{.*}}) : !s32i, !s32i
// NOWRAP: cir.unary(minus, {{.*}}) nsw : !s32i, !s32i
int negate(int x) {
  return -x;
}

// Pointer arithmetic: inbounds should NOT be set under -fwrapv-pointer
// WRAPPTR: cir.ptr_stride %{{.*}}, %{{.*}} : (!cir.ptr<!s32i>, !s32i) -> !cir.ptr<!s32i>
// NOWRAP:  cir.ptr_stride inbounds %{{.*}}, %{{.*}} : (!cir.ptr<!s32i>, !s32i) -> !cir.ptr<!s32i>
int* ptr_add(int* p, int n) {
  return p + n;
}