// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s

// Regression coverage for no-proto reconciliation user collection:
// keep a typed get_global user but no call users in the TU. The no-proto
// declaration is later defined with a prototype; rewriting should still update
// the get_global type to the real function signature.

int noProtoOnlyGetGlobal();

int (*take_ptr(void))(int) {
  return noProtoOnlyGetGlobal;
}

int noProtoOnlyGetGlobal(int x) { return x + 1; }

// CHECK-LABEL: cir.func{{.*}} @take_ptr
// CHECK: cir.get_global @noProtoOnlyGetGlobal : !cir.ptr<!cir.func<(!s32i) -> !s32i>>
// CHECK-NOT: cir.get_global @noProtoOnlyGetGlobal : !cir.ptr<!cir.func<(...) -> !s32i>>

