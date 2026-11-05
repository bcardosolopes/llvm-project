// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Ensure global replacement updates all supported user kinds when a tentative
// declaration is completed with a concrete array bound:
// - cir.get_global users in functions
// - GlobalOp initializer users (global_view in constants)
// - ConstantOp users lowered from global initializers
//
// This also covers compile-time optimization paths that collect replacement
// users without SymbolTable getSymbolUses.

extern int arr[];
int *p = arr;
int (*q)[] = &arr;

int use_arr(void) { return arr[0]; }

int arr[4];

// Nested function-body constant users should also be rewritten when arr's type
// changes from tentative unknown bound to complete bound.
int *use_const_addr(void) {
  static int *kArr = arr;
  return kArr;
}

// CHECK: cir.global {{.*}} @arr = #cir.zero : !cir.array<!s32i x 4>
// CHECK: cir.global {{.*}} @p = #cir.global_view<@arr> : !cir.ptr<!s32i>
// CHECK: cir.global {{.*}} @q = #cir.global_view<@arr> : !cir.ptr<!cir.array<!s32i x 0>>
// CHECK-LABEL: cir.func {{.*}} @_Z7use_arrv()
// CHECK: cir.get_global @arr : !cir.ptr<!cir.array<!s32i x 4>>
// CHECK-LABEL: cir.func {{.*}} @_Z14use_const_addrv()
// CHECK: cir.get_global @_ZZ14use_const_addrvE4kArr : !cir.ptr<!cir.ptr<!s32i>>
