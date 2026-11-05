// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Exercise GlobalViewAttr scanning in getOrCreateCIRGlobal replacement paths
// with several references in one constant aggregate. Compile-time optimization
// short-circuits the attribute walk as soon as the target symbol is
// found; semantics should remain unchanged.

extern int arr[];
struct Holder {
  int *a;
  int *b;
  int *c;
};

struct Holder h = {arr, arr, arr};
int *p0 = arr;
int *p1 = arr;
int use_arr0(void) { return arr[0]; }
int use_arr1(void) { return arr[1]; }

int arr[8];

// CHECK: cir.global {{.*}} @arr = #cir.zero : !cir.array<!s32i x 8>
// CHECK: cir.global {{.*}} @h = #cir.const_record<{#cir.global_view<@arr>
// CHECK: cir.global {{.*}} @p0 = #cir.global_view<@arr>
// CHECK: cir.global {{.*}} @p1 = #cir.global_view<@arr>
// CHECK-LABEL: cir.func {{.*}} @use_arr0
// CHECK: cir.get_global @arr : !cir.ptr<!cir.array<!s32i x 8>>
// CHECK-LABEL: cir.func {{.*}} @use_arr1
// CHECK: cir.get_global @arr : !cir.ptr<!cir.array<!s32i x 8>>
