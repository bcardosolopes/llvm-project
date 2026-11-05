// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir

// Test that a boolean used as an array subscript index is cast to an
// integer type before cir.ptr_stride. CIR's PtrStrideOp requires a
// fundamental integer index (!cir.int), not !cir.bool.

int index_with_bool(int *arr, bool b) {
  return arr[b];
}

// CHECK: cir.func {{.*}} @_Z15index_with_boolPib
// CHECK:   %[[BOOL:.*]] = cir.load {{.*}} : !cir.ptr<!cir.bool>, !cir.bool
// CHECK:   %[[IDX:.*]] = cir.cast bool_to_int %[[BOOL]] : !cir.bool -> !s64i
// CHECK:   cir.ptr_stride inbounds %{{.*}}, %[[IDX]] : (!cir.ptr<!s32i>, !s64i) -> !cir.ptr<!s32i>
