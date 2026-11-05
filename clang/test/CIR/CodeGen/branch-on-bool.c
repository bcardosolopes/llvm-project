// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -O0 -o - %s | FileCheck %s

void foo(void);
void bar(void);

void fold_if(int a, int b) {
  // CHECK-LABEL: define{{.*}} @fold_if(
  // Short-circuit && with else is optimized: no phi, direct branches.
  // CHECK: br i1 %{{.*}}, label %[[EVAL_B:[0-9]+]], label %[[ELSE:[0-9]+]]
  // CHECK: [[EVAL_B]]:
  // CHECK: br i1 %{{.*}}, label %[[THEN:[0-9]+]], label %[[ELSE]]
  // CHECK: [[THEN]]:
  // CHECK: call void @foo
  // CHECK: [[ELSE]]:
  // CHECK: call void @bar
  // CHECK-NOT: phi
  if (a && b)
    foo();
  else
    bar();
}
