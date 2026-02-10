// RUN: %clang_cc1 -std=c++20 -triple=arm64e-apple-darwin -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

class C {
  public:
    ~C();
};

void t_constant_size_nontrivial() {
  auto p = new C[3];
}

// CHECK:  cir.func{{.*}} @_Z26t_constant_size_nontrivialv()
// CHECK:    %[[#NUM_ELEMENTS:]] = cir.const #cir.int<3> : !u64i
// CHECK:    %[[#ALLOCATION_SIZE:]] = cir.const #cir.int<11> : !u64i
// CHECK:    %[[#ALLOC_PTR:]] = cir.call @_Znam(%[[#ALLOCATION_SIZE]]) : (!u64i) -> !cir.ptr<!void>
// CHECK:    %[[#COOKIE_PTR:]] = cir.cast bitcast %[[#ALLOC_PTR]] : !cir.ptr<!void> -> !cir.ptr<!cir.ptr<!u8i>>
// CHECK:    %[[#COOKIE_AS_U64:]] = cir.cast bitcast %[[#COOKIE_PTR]] : !cir.ptr<!cir.ptr<!u8i>> -> !cir.ptr<!u64i>
// CHECK:    cir.store{{.*}} %[[#NUM_ELEMENTS]], %[[#COOKIE_AS_U64]] : !u64i, !cir.ptr<!u64i>
// CHECK:    %[[#COOKIE_SIZE:]] = cir.const #cir.int<8> : !s32i
// CHECK:    cir.ptr_stride %[[#COOKIE_PTR]], %[[#COOKIE_SIZE]] : (!cir.ptr<!cir.ptr<!u8i>>, !s32i) -> !cir.ptr<!cir.ptr<!u8i>>

class D {
  public:
    int x;
    ~D();
};

void t_constant_size_nontrivial2() {
  auto p = new D[3];
}

// In this test SIZE_WITHOUT_COOKIE isn't used, but it would be if there were
// an initializer.

// CHECK:  cir.func{{.*}} @_Z27t_constant_size_nontrivial2v()
// CHECK:    %[[#NUM_ELEMENTS:]] = cir.const #cir.int<3> : !u64i
// CHECK:    %[[#ALLOCATION_SIZE:]] = cir.const #cir.int<20> : !u64i
// CHECK:    %[[#ALLOC_PTR:]] = cir.call @_Znam(%[[#ALLOCATION_SIZE]]) : (!u64i) -> !cir.ptr<!void>
// CHECK:    %[[#COOKIE_PTR:]] = cir.cast bitcast %[[#ALLOC_PTR]] : !cir.ptr<!void> -> !cir.ptr<!cir.ptr<!u8i>>
// CHECK:    %[[#COOKIE_AS_U64:]] = cir.cast bitcast %[[#COOKIE_PTR]] : !cir.ptr<!cir.ptr<!u8i>> -> !cir.ptr<!u64i>
// CHECK:    cir.store{{.*}} %[[#NUM_ELEMENTS]], %[[#COOKIE_AS_U64]] : !u64i, !cir.ptr<!u64i>
// CHECK:    %[[#COOKIE_SIZE:]] = cir.const #cir.int<8> : !s32i
// CHECK:    cir.ptr_stride %[[#COOKIE_PTR]], %[[#COOKIE_SIZE]] : (!cir.ptr<!cir.ptr<!u8i>>, !s32i) -> !cir.ptr<!cir.ptr<!u8i>>
