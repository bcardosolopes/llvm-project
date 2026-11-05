// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s

// Regression coverage for declaration-type mismatch reconciliation in
// getOrCreateCIRFunction: when a no-proto declaration is seen before a typed
// declaration for the same symbol, user repair must still retarget both calls
// and get_global users.

int protoFast();
int (*protoFastPtr)(int) = protoFast;
int test_proto_fast(int x) { return protoFast(x); }
int protoFast(int x) { return x + 5; }

// CHECK-LABEL: cir.global {{.*}} @protoFastPtr
// CHECK-SAME: #cir.global_view<@protoFast> : !cir.ptr<!cir.func<(!s32i) -> !s32i>>
// CHECK-LABEL: cir.func {{.*}} @test_proto_fast
// CHECK: cir.call @protoFast(%{{.*}}) : (!s32i) -> !s32i

