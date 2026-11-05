// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -O2 -fclangir -emit-llvm %s -o - | FileCheck %s

// Regression: when an inline destructor of a system-header class template is
// referenced only from a vtable initializer, ClangIR must still emit it.
# 1 "sys.hpp" 1 3
struct B { virtual ~B() = default; };

template <class T>
struct D : B {
  ~D() override = default;
  T value{};
};
# 2 "test.cpp" 2

B *make_b() { return new D<int>(); }
void kill_b(B *p) { delete p; }

// CHECK: define linkonce_odr{{.*}} @_ZN1DIiED1Ev
// CHECK: define linkonce_odr{{.*}} @_ZN1DIiED0Ev
// CHECK-NOT: declare{{.*}} @_ZN1DIiED1Ev
// CHECK-NOT: declare{{.*}} @_ZN1DIiED0Ev
