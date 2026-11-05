// RUN: %clang_cc1 -triple x86_64-unknown-linux -std=c23 -fdefer-ts -fclangir -emit-cir %s -o - | FileCheck %s

void side_effect(int);

// CHECK-LABEL: cir.func{{.*}} @defer_basic
// CHECK: cir.call @side_effect(%{{.*}}){{.*}} : (!s32i) -> ()
// CHECK: cir.call @side_effect(%{{.*}}){{.*}} : (!s32i) -> ()
// CHECK: cir.call @side_effect(%{{.*}}){{.*}} : (!s32i) -> ()
void defer_basic(void) {
  _Defer side_effect(1);
  _Defer side_effect(2);
  side_effect(3);
}

// CHECK-LABEL: cir.func{{.*}} @defer_nested
// CHECK: cir.call @side_effect(%{{.*}}){{.*}} : (!s32i) -> ()
// CHECK: cir.call @side_effect(%{{.*}}){{.*}} : (!s32i) -> ()
// CHECK: cir.call @side_effect(%{{.*}}){{.*}} : (!s32i) -> ()
void defer_nested(void) {
  _Defer {
    _Defer side_effect(1);
    side_effect(2);
  };
  side_effect(3);
}
