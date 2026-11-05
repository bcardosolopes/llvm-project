// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s

// Regression coverage for declaration-type repair while the current function
// body is still being emitted. A later block-scope redeclaration must repair
// both earlier direct-call and get_global users in that unfinished body.

int current_repair();

int use_current_repair(int x) {
  int (*fp)(int) = current_repair;
  int y = current_repair(x);
  int current_repair(int);
  return fp(y);
}

int current_repair(int v) { return v + 3; }

// CHECK-LABEL: cir.func {{.*}} @use_current_repair(
// CHECK: cir.get_global @current_repair : !cir.ptr<!cir.func<(!s32i) -> !s32i>>
// CHECK-NOT: cir.get_global @current_repair : !cir.ptr<!cir.func<(...) -> !s32i>>
// CHECK: cir.call @current_repair(%{{.*}}) : (!s32i) -> !s32i
