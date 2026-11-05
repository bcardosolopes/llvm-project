// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Compile-time regression coverage for symbol-use fast paths:
// user collection should rely on symbol-use queries directly and still only
// rewrite users of the target symbol.

extern int arr[];
extern int other[2];

int *p_arr = arr;
int *p_other = other;

int use_arr(void) { return arr[0]; }
int use_other(void) { return other[1]; }

int arr[4] = {1, 2, 3, 4};
int other[2] = {5, 6};

// CHECK: cir.global {{.*}} @p_arr = #cir.global_view<@arr> : !cir.ptr<!s32i>
// CHECK: cir.global {{.*}} @p_other = #cir.global_view<@other> : !cir.ptr<!s32i>
// CHECK-LABEL: cir.func {{.*}} @use_arr
// CHECK: cir.get_global @arr : !cir.ptr<!cir.array<!s32i x 4>>
// CHECK-LABEL: cir.func {{.*}} @use_other
// CHECK: cir.get_global @other : !cir.ptr<!cir.array<!s32i x 2>>

