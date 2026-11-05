// RUN: %clang_cc1 -triple aarch64-windows-msvc -x c -fclangir \
// RUN:   -fno-clangir-call-conv-lowering -emit-cir %s -o - | FileCheck %s

typedef struct {
  short a;
  int b;
  short c;
} S;

S getS(void);

void f(void) {
  S s = getS();
}

// CHECK: !rec_S = !cir.record<struct "S" {!s16i, !s32i, !s16i}>
// CHECK: cir.func no_inline optnone dso_local @f()
// CHECK: %[[SLOT:.+]] = cir.alloca !rec_S, !cir.ptr<!rec_S>, ["s", init]
// CHECK: %[[VAL:.+]] = cir.call @getS() : () -> !rec_S
// CHECK: cir.store align(4) %[[VAL]], %[[SLOT]] : !rec_S, !cir.ptr<!rec_S>
