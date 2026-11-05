// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s

// Ensure no-proto rewrite keeps function-pointer get_global users well-typed
// and avoids introducing any extra cast churn when the get_global already has
// the final rewritten pointee type.

int npr_g();
int (*npr_fp)(int) = npr_g;

int use_npr(int x) { return npr_g(x) + npr_fp(x); }

int npr_g(int x) { return x + 1; }

// CHECK: cir.global {{.*}}@npr_fp = #cir.global_view<@npr_g> : !cir.ptr<!cir.func<(!s32i) -> !s32i>>
// CHECK-LABEL: cir.func {{.*}} @use_npr(
// CHECK: cir.call @npr_g(%{{.*}})
// CHECK: %[[GFP:.*]] = cir.get_global @npr_fp : !cir.ptr<!cir.ptr<!cir.func<(!s32i) -> !s32i>>>
