// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s

// Regression: when a no-proto declaration is later defined with a matching
// prototype and existing get_global users are already typed to that prototype,
// reconciliation should not perturb the get_global type.

int alreadyTyped();

int (*typed_ptr(void))(int) {
  return alreadyTyped;
}

int alreadyTyped(int x) { return x + 7; }

// CHECK-LABEL: cir.func{{.*}} @typed_ptr
// CHECK: cir.get_global @alreadyTyped : !cir.ptr<!cir.func<(!s32i) -> !s32i>>
// CHECK-NOT: cir.get_global @alreadyTyped : !cir.ptr<!cir.func<(...) -> !s32i>>
