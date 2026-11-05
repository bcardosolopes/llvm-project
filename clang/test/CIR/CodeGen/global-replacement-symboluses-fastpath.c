// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Regression for symbol-uses fast path in global replacement reconciliation:
// when a tentative global is completed with a concrete bound, all known user
// categories should still be rewritten correctly.

extern int arr[];

int *p = arr;
int (*q)[] = &arr;
int use_arr(void) { return arr[1]; }

int arr[4] = {1, 2, 3, 4};

// CHECK: cir.global {{.*}} @arr = #cir.const_array<{{.*}}> : !cir.array<!s32i x 4>
// CHECK: cir.global {{.*}} @p = #cir.global_view<@arr> : !cir.ptr<!s32i>
// CHECK: cir.global {{.*}} @q = #cir.global_view<@arr> : !cir.ptr<!cir.array<!s32i x 0>>
// CHECK-LABEL: cir.func {{.*}} @use_arr
// CHECK: cir.get_global @arr : !cir.ptr<!cir.array<!s32i x 4>>

