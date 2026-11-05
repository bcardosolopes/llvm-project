// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

int *ptr;

// CHECK-LABEL: @test_sync_val_cmpxchg_ptr
int *test_sync_val_cmpxchg_ptr(int *expected, int *desired) {
    // The pointer address must be bitcast to an integer pointer type for the
    // atomic op, and the result must be converted back from integer to pointer.
    // CHECK: %[[ADDR:.*]] = cir.get_global @ptr
    // CHECK: cir.cast ptr_to_int %{{.*}} : !cir.ptr<!s32i> -> !s64i
    // CHECK: cir.cast ptr_to_int %{{.*}} : !cir.ptr<!s32i> -> !s64i
    // CHECK: %[[BITCAST:.*]] = cir.cast bitcast %[[ADDR]] : !cir.ptr<!cir.ptr<!s32i>> -> !cir.ptr<!s64i>
    // CHECK: %[[OLD:.*]], %{{.*}} = cir.atomic.cmpxchg success(seq_cst) failure(seq_cst) %[[BITCAST]], %{{.*}}, %{{.*}} align(8) : (!cir.ptr<!s64i>, !s64i, !s64i) -> (!s64i, !cir.bool)
    // CHECK: cir.cast int_to_ptr %[[OLD]] : !s64i -> !cir.ptr<!s32i>
    return __sync_val_compare_and_swap(&ptr, expected, desired);
}

// CHECK-LABEL: @test_sync_bool_cmpxchg_ptr
int test_sync_bool_cmpxchg_ptr(int *expected, int *desired) {
    // CHECK: cir.cast bitcast %{{.*}} : !cir.ptr<!cir.ptr<!s32i>> -> !cir.ptr<!s64i>
    // CHECK: cir.atomic.cmpxchg
    return __sync_bool_compare_and_swap(&ptr, expected, desired);
}
