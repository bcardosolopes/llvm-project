// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Stress the global replacement user-scan path with many declaration-only
// functions. The replacement should still update all non-declaration users.

extern int arr[];

int *sink = arr;

int decl_0(int);
int decl_1(int);
int decl_2(int);
int decl_3(int);
int decl_4(int);
int decl_5(int);
int decl_6(int);
int decl_7(int);
int decl_8(int);
int decl_9(int);
int decl_10(int);
int decl_11(int);
int decl_12(int);
int decl_13(int);
int decl_14(int);
int decl_15(int);
int decl_16(int);
int decl_17(int);
int decl_18(int);
int decl_19(int);

int arr[8] = {1, 2, 3, 4, 5, 6, 7, 8};

int use_arr(void) { return sink[0] + arr[1]; }

// CHECK: cir.global {{.*}} @arr = #cir.const_array<
// CHECK: cir.global {{.*}} @sink = #cir.global_view<@arr>
// CHECK-LABEL: cir.func {{.*}} @use_arr
// CHECK: cir.get_global @arr : !cir.ptr<!cir.array<!s32i x 8>>

