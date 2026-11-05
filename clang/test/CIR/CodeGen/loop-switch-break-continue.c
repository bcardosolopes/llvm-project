// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

int f(int x) {
  int i = x;
  while (i < 8) {
    switch (i & 3) {
    case 0:
      ++i;
      continue;
    case 1:
      ++i;
      break;
    default:
      ++i;
      break;
    }
    i += 10;
  }
  return i;
}

// CHECK-LABEL: define{{.*}} i32 @f(
// CHECK: switch i32 %{{.*}}, label %[[DEF:[^ ]+]] [
// CHECK:   i32 0, label %[[CASE0:[^ ]+]]
// CHECK:   i32 1, label %[[CASE1:[^ ]+]]
// CHECK: ]
// CHECK: [[CASE0]]:
// CHECK: br label %[[LOOP_HDR:[^ ]+]]
// CHECK: [[CASE1]]:
// CHECK: br label %[[AFTER_SWITCH:[^ ]+]]
// CHECK: [[DEF]]:
// CHECK: br label %[[AFTER_SWITCH]]
// CHECK: [[AFTER_SWITCH]]:
// CHECK: br label %[[LOOP_LATCH:[^ ]+]]
// CHECK: [[LOOP_LATCH]]:
// CHECK: br label %[[LOOP_HDR]]
