// RUN: %clang_cc1 %s -triple=x86_64-unknown-linux-gnu -emit-llvm -std=c++20 -O1 -disable-llvm-passes -fclangir -o - | FileCheck %s

struct A {
  A();
  ~A();
};

void g() {
  // CHECK-LABEL: define dso_local void @_Z1gv()
  // CHECK: load atomic i8, ptr @_ZGVZ1gvE1a acquire, align 8
  // CHECK: call i32 @__cxa_guard_acquire(ptr @_ZGVZ1gvE1a)
  // CHECK: call noalias noundef nonnull ptr @_Znwm(i64 noundef 1)
  // CHECK: call void @_ZN1AC1Ev(ptr noundef nonnull align 1 dereferenceable(1)
  // CHECK: store ptr {{%.*}}, ptr @_ZZ1gvE1a, align 8, !tbaa
  // CHECK-NOT: @llvm.invariant.start
  // CHECK: call void @__cxa_guard_release(ptr @_ZGVZ1gvE1a)
  static A &a = *new A;
}
