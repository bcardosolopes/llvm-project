// RUN: %clang_cc1 -triple x86_64-unknown-linux -std=c++11 -fclangir -emit-llvm -o - %s \
// RUN:   | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux -std=c++11 -emit-llvm -o - %s \
// RUN:   | FileCheck %s

// Verify that virtual thunks do not get the mustprogress attribute.
// OGCG's StartThunk passes empty GlobalDecl to StartFunction, so
// mustprogress (added in GenerateCode) is never set for thunks.

struct A {
  virtual ~A();
};
struct B {
  virtual ~B();
};
struct C : A, B {
  ~C() override;
};
C::~C() {}

// The thunk should NOT have mustprogress.
// CHECK: define {{.*}}void @_ZThn8_N1CD1Ev({{.*}}) unnamed_addr #[[THUNK:[0-9]+]]
// CHECK: attributes #[[THUNK]] = {
// CHECK-NOT: mustprogress
// CHECK-SAME: noinline
// CHECK-SAME: }
