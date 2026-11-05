// RUN: %clang_cc1 -triple x86_64-unknown-linux -std=c23 -fdefer-ts -fclangir -emit-llvm %s -o - | FileCheck %s

int return_value(void) {
  int r = 4;
  int *p = &r;
  _Defer { *p = 5; }
  return *p;
}

// CHECK-LABEL: define {{.*}} i32 @return_value()
// CHECK: [[R:%[0-9]+]] = alloca i32, i64 1, align 4
// CHECK: [[P:%[0-9]+]] = alloca ptr, i64 1, align 8
// CHECK: store i32 4, ptr [[R]], align 4
// CHECK: store ptr [[R]], ptr [[P]], align 8
// CHECK: [[RET_PTR:%[0-9]+]] = load ptr, ptr [[P]], align 8
// CHECK: [[RET:%[0-9]+]] = load i32, ptr [[RET_PTR]], align 4
// CHECK: [[DEFER_PTR:%[0-9]+]] = load ptr, ptr [[P]], align 8
// CHECK: store i32 5, ptr [[DEFER_PTR]], align 4
// CHECK: ret i32 [[RET]]
