// RUN: %clang_cc1 -triple x86_64-unknown-linux -std=c23 -fdefer-ts -fclangir -emit-llvm %s -o - -O1 -disable-llvm-passes | FileCheck %s

// Direct-CIR currently emits deferred cleanups inline along each exit edge
// instead of routing through OGCG's cleanup.dest.slot dispatch. Keep this test
// structural: nested _Defer bodies must still lower their own lifetime-managed
// cleanups without crashing or dropping calls.

void g(void);

// CHECK-LABEL: define {{.*}} void @f1()
// CHECK: [[I:%[0-9]+]] = alloca i32, i64 1, align 4
// CHECK: [[J_BREAK:%[0-9]+]] = alloca i32, i64 1, align 4
// CHECK: [[J_CONT:%[0-9]+]] = alloca i32, i64 1, align 4
// CHECK: call void @llvm.lifetime.start.p0(ptr [[I]])
// CHECK: [[I_VAL:%[0-9]+]] = load i32, ptr [[I]], align 4
// CHECK: icmp eq i32 [[I_VAL]], 1
// CHECK: call void @llvm.lifetime.start.p0(ptr [[J_BREAK]])
// CHECK: store i32 0, ptr [[J_BREAK]], align 4
// CHECK: [[J_BREAK_VAL:%[0-9]+]] = load i32, ptr [[J_BREAK]], align 4
// CHECK: icmp ne i32 [[J_BREAK_VAL]], 1
// CHECK: call void @g()
// CHECK: call void @llvm.lifetime.end.p0(ptr [[J_BREAK]])
// CHECK: call void @llvm.lifetime.start.p0(ptr [[J_CONT]])
// CHECK: store i32 0, ptr [[J_CONT]], align 4
// CHECK: [[J_CONT_VAL:%[0-9]+]] = load i32, ptr [[J_CONT]], align 4
// CHECK: icmp ne i32 [[J_CONT_VAL]], 1
// CHECK: call void @g()
// CHECK: call void @llvm.lifetime.end.p0(ptr [[J_CONT]])
// CHECK: [[I_INC:%[0-9]+]] = load i32, ptr [[I]], align 4
// CHECK: add nsw i32 [[I_INC]], 1
// CHECK: call void @llvm.lifetime.end.p0(ptr [[I]])
void f1(void) {
  for (int i = 0;; i++) {
    _Defer {
      for (int j = 0; j != 1; j++) {
        g();
      }
    }
    if (i == 1)
      break;
  }
}

// CHECK-LABEL: define {{.*}} void @f2()
// CHECK: [[I:%[0-9]+]] = alloca i32, i64 1, align 4
// CHECK: [[J_BREAK:%[0-9]+]] = alloca i32, i64 1, align 4
// CHECK: [[K_BREAK_BREAK:%[0-9]+]] = alloca i32, i64 1, align 4
// CHECK: [[K_BREAK_CONT:%[0-9]+]] = alloca i32, i64 1, align 4
// CHECK: [[J_CONT:%[0-9]+]] = alloca i32, i64 1, align 4
// CHECK: [[K_CONT_BREAK:%[0-9]+]] = alloca i32, i64 1, align 4
// CHECK: [[K_CONT_CONT:%[0-9]+]] = alloca i32, i64 1, align 4
// CHECK: call void @llvm.lifetime.start.p0(ptr [[I]])
// CHECK: [[I_VAL:%[0-9]+]] = load i32, ptr [[I]], align 4
// CHECK: icmp eq i32 [[I_VAL]], 1
// CHECK: call void @llvm.lifetime.start.p0(ptr [[J_BREAK]])
// CHECK: store i32 0, ptr [[J_BREAK]], align 4
// CHECK: [[J_BREAK_VAL:%[0-9]+]] = load i32, ptr [[J_BREAK]], align 4
// CHECK: icmp eq i32 [[J_BREAK_VAL]], 1
// CHECK: call void @llvm.lifetime.start.p0(ptr [[K_BREAK_BREAK]])
// CHECK: store i32 0, ptr [[K_BREAK_BREAK]], align 4
// CHECK: [[K_BREAK_BREAK_VAL:%[0-9]+]] = load i32, ptr [[K_BREAK_BREAK]], align 4
// CHECK: icmp ne i32 [[K_BREAK_BREAK_VAL]], 1
// CHECK: call void @g()
// CHECK: call void @llvm.lifetime.end.p0(ptr [[K_BREAK_BREAK]])
// CHECK: call void @llvm.lifetime.start.p0(ptr [[K_BREAK_CONT]])
// CHECK: store i32 0, ptr [[K_BREAK_CONT]], align 4
// CHECK: [[K_BREAK_CONT_VAL:%[0-9]+]] = load i32, ptr [[K_BREAK_CONT]], align 4
// CHECK: icmp ne i32 [[K_BREAK_CONT_VAL]], 1
// CHECK: call void @g()
// CHECK: call void @llvm.lifetime.end.p0(ptr [[K_BREAK_CONT]])
// CHECK: [[J_BREAK_INC:%[0-9]+]] = load i32, ptr [[J_BREAK]], align 4
// CHECK: add nsw i32 [[J_BREAK_INC]], 1
// CHECK: call void @llvm.lifetime.end.p0(ptr [[J_BREAK]])
// CHECK: call void @llvm.lifetime.start.p0(ptr [[J_CONT]])
// CHECK: store i32 0, ptr [[J_CONT]], align 4
// CHECK: [[J_CONT_VAL:%[0-9]+]] = load i32, ptr [[J_CONT]], align 4
// CHECK: icmp eq i32 [[J_CONT_VAL]], 1
// CHECK: call void @llvm.lifetime.start.p0(ptr [[K_CONT_BREAK]])
// CHECK: store i32 0, ptr [[K_CONT_BREAK]], align 4
// CHECK: [[K_CONT_BREAK_VAL:%[0-9]+]] = load i32, ptr [[K_CONT_BREAK]], align 4
// CHECK: icmp ne i32 [[K_CONT_BREAK_VAL]], 1
// CHECK: call void @g()
// CHECK: call void @llvm.lifetime.end.p0(ptr [[K_CONT_BREAK]])
// CHECK: call void @llvm.lifetime.start.p0(ptr [[K_CONT_CONT]])
// CHECK: store i32 0, ptr [[K_CONT_CONT]], align 4
// CHECK: [[K_CONT_CONT_VAL:%[0-9]+]] = load i32, ptr [[K_CONT_CONT]], align 4
// CHECK: icmp ne i32 [[K_CONT_CONT_VAL]], 1
// CHECK: call void @g()
// CHECK: call void @llvm.lifetime.end.p0(ptr [[K_CONT_CONT]])
// CHECK: [[J_CONT_INC:%[0-9]+]] = load i32, ptr [[J_CONT]], align 4
// CHECK: add nsw i32 [[J_CONT_INC]], 1
// CHECK: call void @llvm.lifetime.end.p0(ptr [[J_CONT]])
// CHECK: [[I_INC:%[0-9]+]] = load i32, ptr [[I]], align 4
// CHECK: add nsw i32 [[I_INC]], 1
// CHECK: call void @llvm.lifetime.end.p0(ptr [[I]])
void f2(void) {
  for (int i = 0;; i++) {
    _Defer {
      for (int j = 0;; j++) {
        _Defer {
          for (int k = 0; k != 1; k++) {
            g();
          }
        }
        if (j == 1)
          break;
      }
    }
    if (i == 1)
      break;
  }
}
