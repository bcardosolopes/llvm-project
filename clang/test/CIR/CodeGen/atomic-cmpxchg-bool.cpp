// RUN: %clang_cc1 -std=c++17 -triple aarch64-none-linux-android21 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Test that __sync_val_compare_and_swap on bool correctly converts between
// cir::BoolType and the integer type used by atomic operations.
//
//

static bool flag = false;

bool test_sync_val_compare_and_swap() {
  return __sync_val_compare_and_swap(&flag, false, true);
}
// CHECK: cir.func {{.*}} @_Z30test_sync_val_compare_and_swapv
// CHECK:   %[[FALSE:.*]] = cir.const #false
// CHECK:   %[[TO_INT1:.*]] = cir.cast bool_to_int %[[FALSE]] : !cir.bool -> !u8i
// CHECK:   %[[TRUE:.*]] = cir.const #true
// CHECK:   %[[TO_INT2:.*]] = cir.cast bool_to_int %[[TRUE]] : !cir.bool -> !u8i
// CHECK:   %old, %success = cir.atomic.cmpxchg
// CHECK-SAME: (!cir.ptr<!u8i>, !u8i, !u8i) -> (!u8i, !cir.bool)
// CHECK:   %[[TO_BOOL:.*]] = cir.cast int_to_bool %old : !u8i -> !cir.bool

bool test_sync_bool_compare_and_swap() {
  return __sync_bool_compare_and_swap(&flag, false, true);
}
// CHECK: cir.func {{.*}} @_Z31test_sync_bool_compare_and_swapv
// CHECK:   %[[FALSE2:.*]] = cir.const #false
// CHECK:   %[[TO_INT3:.*]] = cir.cast bool_to_int %[[FALSE2]] : !cir.bool -> !u8i
// CHECK:   %[[TRUE2:.*]] = cir.const #true
// CHECK:   %[[TO_INT4:.*]] = cir.cast bool_to_int %[[TRUE2]] : !cir.bool -> !u8i
// CHECK:   %old{{.*}}, %success{{.*}} = cir.atomic.cmpxchg
// CHECK-SAME: (!cir.ptr<!u8i>, !u8i, !u8i) -> (!u8i, !cir.bool)
