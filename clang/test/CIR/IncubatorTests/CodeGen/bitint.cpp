// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fno-clangir-call-conv-lowering -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

using i10 = signed _BitInt(10);
using u10 = unsigned _BitInt(10);

unsigned _BitInt(1) GlobSize1 = 0;
// CHECK: cir.global external @GlobSize1 = #cir.int<0> : !cir.int<u, 1>

i10 test_signed(i10 arg) {
  return arg;
}

// CHECK: cir.func {{.*}} @_Z11test_signedDB10_(%arg0: !cir.int<s, 10> loc({{.*}}) -> !cir.int<s, 10>
// CHECK: }

u10 test_unsigned(u10 arg) {
  return arg;
}

// CHECK: cir.func {{.*}} @_Z13test_unsignedDU10_(%arg0: !cir.int<u, 10> loc({{.*}}) -> !cir.int<u, 10>
// CHECK: }

i10 test_init() {
  return 42;
}

//      CHECK: cir.func {{.*}} @_Z9test_initv() -> !cir.int<s, 10>
//      CHECK:   %{{.+}} = cir.const #cir.int<42> : !cir.int<s, 10>
//      CHECK: }

void test_init_for_mem() {
  i10 x = 42;
}

//      CHECK: cir.func {{.*}} @_Z17test_init_for_memv()
//      CHECK:   %[[#INIT:]] = cir.const #cir.int<42> : !s16i
// CHECK-NEXT:   cir.store{{.*}} %[[#INIT]], %{{.+}} : !s16i, !cir.ptr<!s16i>
//      CHECK: }

i10 test_arith(i10 lhs, i10 rhs) {
  return lhs + rhs;
}

//      CHECK: cir.func {{.*}} @_Z10test_arithDB10_S_(%arg0: !cir.int<s, 10> loc({{.+}}), %arg1: !cir.int<s, 10> loc({{.+}})) -> !cir.int<s, 10>
//      CHECK:   %[[#LHS_STORAGE:]] = cir.load{{.*}} %{{.+}} : !cir.ptr<!s16i>, !s16i
// CHECK-NEXT:   %[[#LHS:]] = cir.cast integral %[[#LHS_STORAGE]] : !s16i -> !cir.int<s, 10>
// CHECK-NEXT:   %[[#RHS_STORAGE:]] = cir.load{{.*}} %{{.+}} : !cir.ptr<!s16i>, !s16i
// CHECK-NEXT:   %[[#RHS:]] = cir.cast integral %[[#RHS_STORAGE]] : !s16i -> !cir.int<s, 10>
// CHECK-NEXT:   %{{.+}} = cir.binop(add, %[[#LHS]], %[[#RHS]]) nsw : !cir.int<s, 10>
//      CHECK: }

void Size1ExtIntParam(unsigned _BitInt(1) A) {
  unsigned _BitInt(1) B[5];
  B[2] = A;
}

//      CHECK: cir.func {{.*}} @_Z16Size1ExtIntParamDU1_
//      CHECK:   %[[#A_STORAGE:]] = cir.load{{.*}} %{{.+}} : !cir.ptr<!u8i>, !u8i
// CHECK-NEXT:   %[[#A:]] = cir.cast integral %[[#A_STORAGE]] : !u8i -> !cir.int<u, 1>
// CHECK-NEXT:   %[[#IDX:]] = cir.const #cir.int<2> : !s32i
// CHECK-NEXT:   %[[#ELEM:]] = cir.get_element %1[%[[#IDX]] : !s32i] : !cir.ptr<!cir.array<!u8i x 5>> -> !cir.ptr<!u8i>
// CHECK-NEXT:   %[[#CAST:]] = cir.cast integral %[[#A]] : !cir.int<u, 1> -> !u8i
// CHECK-NEXT:   cir.store{{.*}} %[[#CAST]], %[[#ELEM]] : !u8i, !cir.ptr<!u8i>
//      CHECK: }

struct S {
  _BitInt(17) A;
  _BitInt(10) B;
  _BitInt(17) C;
};

void OffsetOfTest(void) {
  int A = __builtin_offsetof(struct S,A);
  int B = __builtin_offsetof(struct S,B);
  int C = __builtin_offsetof(struct S,C);
}

// CHECK: cir.func {{.*}} @_Z12OffsetOfTestv()
// CHECK:   %{{.+}} = cir.const #cir.int<0> : !s32i
// CHECK:   %{{.+}} = cir.const #cir.int<4> : !s32i
// CHECK:   %{{.+}} = cir.const #cir.int<8> : !s32i
// CHECK: }

_BitInt(2) ParamPassing(_BitInt(15) a, _BitInt(31) b) {}

// CHECK: cir.func {{.*}} @_Z12ParamPassingDB15_DB31_(%arg0: !cir.int<s, 15> loc({{.+}}), %arg1: !cir.int<s, 31> loc({{.+}})) -> !cir.int<s, 2>
