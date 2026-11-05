// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Regression for applyReplacements generic fallback path (>3 mismatches):
// all symbols must still be rewritten correctly when fallback user collection
// is required for multiple symbols.

extern int arr0[];
extern int arr1[];
extern int arr2[];
extern int arr3[];

int *p0 = arr0;
int *p1 = arr1;
int *p2 = arr2;
int *p3 = arr3;

int use0(void) { return arr0[0]; }
int use1(void) { return arr1[1]; }
int use2(void) { return arr2[2]; }
int use3(void) { return arr3[3]; }

int arr0[1] = {10};
int arr1[2] = {20, 21};
int arr2[3] = {30, 31, 32};
int arr3[4] = {40, 41, 42, 43};

// CHECK: cir.global {{.*}} @p0 = #cir.global_view<@arr0> : !cir.ptr<!s32i>
// CHECK: cir.global {{.*}} @p1 = #cir.global_view<@arr1> : !cir.ptr<!s32i>
// CHECK: cir.global {{.*}} @p2 = #cir.global_view<@arr2> : !cir.ptr<!s32i>
// CHECK: cir.global {{.*}} @p3 = #cir.global_view<@arr3> : !cir.ptr<!s32i>
// CHECK-LABEL: cir.func {{.*}} @use0
// CHECK: cir.get_global @arr0 : !cir.ptr<!cir.array<!s32i x 1>>
// CHECK-LABEL: cir.func {{.*}} @use1
// CHECK: cir.get_global @arr1 : !cir.ptr<!cir.array<!s32i x 2>>
// CHECK-LABEL: cir.func {{.*}} @use2
// CHECK: cir.get_global @arr2 : !cir.ptr<!cir.array<!s32i x 3>>
// CHECK-LABEL: cir.func {{.*}} @use3
// CHECK: cir.get_global @arr3 : !cir.ptr<!cir.array<!s32i x 4>>
