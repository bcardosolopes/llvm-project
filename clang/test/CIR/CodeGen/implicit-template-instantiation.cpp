// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck %s < %t.ll

// Verify that std::forward, std::move (C++ std:: builtins) are emitted
// as direct pointer operations matching OGCG, rather than function calls.

namespace std {
template <class T> struct remove_reference { typedef T type; };
template <class T> struct remove_reference<T &> { typedef T type; };
template <class T> struct remove_reference<T &&> { typedef T type; };

template <class T>
constexpr T &&forward(typename remove_reference<T>::type &t) noexcept {
  return static_cast<T &&>(t);
}

template <class T>
constexpr typename remove_reference<T>::type &&move(T &&t) noexcept {
  return static_cast<typename remove_reference<T>::type &&>(t);
}
} // namespace std

int test_forward() {
  int x = 42;
  int &&r = std::forward<int>(x);
  return r;
}

struct S {
  int val;
  S(int v) : val(v) {}
  S(S &&o) : val(o.val) { o.val = 0; }
};

int test_move() {
  S a(99);
  S b = std::move(a);
  return b.val;
}

// Nested: a template function that internally uses std::forward.
template <typename T>
T make(T &&arg) {
  return std::forward<T>(arg);
}

int test_nested() {
  int x = 7;
  return make<int>(static_cast<int&&>(x));
}

// std::forward and std::move should not appear as function symbols at all.
// They are lowered to direct pointer operations.
// CHECK-NOT: @_ZSt7forward
// CHECK-NOT: @_ZSt4move
// CHECK: define {{.*}} @_Z12test_forwardv(
// CHECK: define {{.*}} @_Z9test_movev(
// CHECK: define {{.*}} @_Z11test_nestedv(
