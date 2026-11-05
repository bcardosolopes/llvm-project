// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir -emit-llvm %s -o - | FileCheck %s

// Regression: avoid eager system-header implicit template
// instantiation while forward-declared types are still incomplete. The
// static_cast in to_base is only valid once Derived is complete.
# 1 "sys.hpp" 1 3
template <class T>
T &&mymove(T &v) {
  return static_cast<T &&>(v);
}

template <class To, class From>
__attribute__((visibility("hidden"), exclude_from_explicit_instantiation))
To to_base(From &&value) {
  return static_cast<To>(mymove(value));
}
# 2 "test.cpp" 2

struct Base {
  virtual ~Base() = default;
};
struct Derived;

Base *cast_now(Derived *d) {
  return to_base<Base *>(d);
}

struct Derived : Base {};

Base *entry(Derived *d) {
  return cast_now(d);
}

// CHECK: define {{.*}} @_Z8cast_nowP7Derived(
// CHECK: call {{.*}} @_Z7to_baseIP4BaseRP7DerivedET_OT0_(
// CHECK: define linkonce_odr hidden {{.*}} @_Z7to_baseIP4BaseRP7DerivedET_OT0_(
