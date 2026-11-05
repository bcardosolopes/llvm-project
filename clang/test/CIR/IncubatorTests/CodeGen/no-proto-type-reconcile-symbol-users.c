// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s

// Exercise no-proto type reconciliation when a symbol has mixed users.
// This covers both:
//  - get_global users (global initializer + indirect call path)
//  - direct symbol references in the same TU
// and ensures reconciliation remains stable/no-crash.

int rec_user();
struct T;
int (*rec_fp)(struct T *) = rec_user;

int call_rec(struct T *p) { return rec_user(p) + rec_fp(p); }

struct T {
  int v;
};
int rec_user(struct T *p) { return p->v; }

// CHECK: cir.global {{.*}}@rec_fp = #cir.global_view<@rec_user> : !cir.ptr<!cir.func<(!cir.ptr<!rec_T>) -> !s32i>>
// CHECK-LABEL: @call_rec(
// CHECK: cir.call @rec_user(%{{.*}})
// CHECK: cir.get_global @rec_fp : !cir.ptr<!cir.ptr<!cir.func<(!cir.ptr<!rec_T>) -> !s32i>>>
