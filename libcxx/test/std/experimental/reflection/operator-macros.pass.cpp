//===----------------------------------------------------------------------===//
//
// Copyright 2026 Bloomberg Finance L.P.
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++03 || c++11 || c++14 || c++17 || c++20
// ADDITIONAL_COMPILE_FLAGS: -freflection -std=c++2d

// RUN: %{build}
// RUN: %{exec} %t.exe

// Operator macros (elems[0] via constant_of, a lazy &&, rewritten and
// reversed ==) and obj.name!(args) member macros.

#include <meta>
#include <cassert>
#include <cstddef>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "test_macros.h"

// fwd! composes with interpolation: applied to \(self) it binds the
// interpolated opaque value, whose type_of is value-category-qualified.
template <class T>
__macro fwd(T&& t) {
  return ^^{ static_cast<\(type_of(t))&&>(\(t)) };
}

struct Tracker {
  bool moved = false;
  Tracker() = default;
  Tracker(const Tracker&) {}
  Tracker(Tracker&&) : moved(true) {}
};

// ------------------------------------------------- operator macros ---------

// Any overloadable operator can be a macro, member or not; overload resolution
// selects it like a function and its expansion replaces the operator
// expression. A member macro binds the object expression to its explicit
// object parameter, and fwd!(\(self)) forwards it.

namespace ops {

// elems[0]: the index has to be a constant expression, because it becomes a
// template argument (which is what lets the result type depend on it). An
// index that is not one takes the run-time path instead.
struct tuple3 {
  int a;
  std::string b;
  double c;

  template <std::size_t I, class Self>
  static constexpr decltype(auto) get(Self&& self) {
    if constexpr (I == 0)
      return (static_cast<Self&&>(self).a);
    else if constexpr (I == 1)
      return (static_cast<Self&&>(self).b);
    else
      return (static_cast<Self&&>(self).c);
  }

  static int runtime_get(const tuple3& t, int i) {
    return i == 0 ? t.a : i == 1 ? static_cast<int>(t.b.size())
                                 : static_cast<int>(t.c);
  }

  template <class Self, class I>
  __macro operator[](this Self&& self, I&& i) {
    if (!is_constant_expression(i))
      return ^^{ tuple3::runtime_get(\(self), \(i)) };
    // fwd! composes with interpolation: the nested invocation binds to the
    // interpolated opaque value, whose type_of is value-category-qualified,
    // so this forwards exactly as static_cast<\(^^Self)&&>(\(self)) would.
    return ^^{ tuple3::get<\(i)>(fwd!(\(self))) };
  }
};

template <std::size_t N, class T>
decltype(auto) nth(T&& t) {
  return static_cast<T&&>(t)[N];  // resolved at instantiation
}

void test_subscript() {
  tuple3 t{1, "two", 3.0};
  static_assert(std::is_same_v<decltype(t[0]), int&>);
  static_assert(std::is_same_v<decltype(t[1]), std::string&>);
  static_assert(std::is_same_v<decltype(std::move(t)[1]), std::string&&>);
  static_assert(std::is_same_v<decltype(std::as_const(t)[2]), const double&>);

  assert(t[0] == 1);
  assert(t[1] == "two");
  t[0] = 10;
  assert(t.a == 10);

  constexpr int one = 1;
  assert(t[one].size() == 3);  // a constexpr variable is a constant expression

  int i = 2;
  assert(t[i] == 3);  // not a constant expression: the run-time path
  static_assert(std::is_same_v<decltype(t[i]), int>);

  assert(nth<2>(t) == 3.0);
  assert(nth<1>(std::move(t)) == "two");
}

// A binary operator macro receives its operands unevaluated, so unlike an
// overloaded operator&& it can be lazy. It is a non-member found by ADL.
struct flag {
  bool on;
};
int rhs_evaluations = 0;

template <class R>
__macro operator&&(flag const& l, R&& r) {
  return ^^{ (\(l).on ? static_cast<bool>(\(r)) : false) };
}

void test_lazy_and() {
  auto rhs = [] { return ++rhs_evaluations, true; };
  flag off{false}, on{true};
  assert(!(off && rhs()));
  assert(rhs_evaluations == 0);  // an operator function would have called it
  assert(on && rhs());
  assert(rhs_evaluations == 1);
}

// Rewritten and reversed candidates apply to macros as to functions: a != b
// is !(a == b) of the expansion, and 1 == v tries v == 1.
struct version {
  int major, minor;
  static bool eq(version const& a, version const& b) {
    return a.major == b.major && a.minor == b.minor;
  }
  __macro operator==(this version const& self, version const& o) {
    return ^^{ version::eq(\(self), \(o)) };
  }
  __macro operator==(this version const& self, int major) {
    return ^^{ \(self).major == \(major) };
  }
};

template <class T, class U>
bool same(T const& t, U const& u) {
  return t == u;
}

void test_rewritten() {
  version v{1, 2};
  assert((v == version{1, 2}));
  assert((v != version{1, 3}));
  assert(v == 1);
  assert(1 == v);
  assert(2 != v);
  assert(same(v, 1));
  assert(same(1, v));
  assert(!same(v, version{0, 0}));
}

// obj.name!(args), obj->name!(args), and name!(args) within a member function
// (an implicit member access). Lookup is in the object's class, so a base
// class's macro is found through a derived object, and -> follows an
// operator-> chain first.
struct counter {
  int n = 0;
  template <class Self>
  __macro bump(this Self&& self, int by) { return ^^{ (\(self).n += \(by)) }; }
  __macro get(this counter const& self) { return ^^{ \(self).n }; }
  __macro add_tokens(this counter& self, std::meta::token_sequence t) {
    return ^^{ (\(self).n += (\(t))) };
  }
  int twice() { return bump!(1), bump!(1); }
};
struct derived : counter {};

// fwd!(\(self)) forwards the object: copy from an lvalue, move from an
// rvalue, per the deduced Self.
struct holder {
  Tracker t;
  template <class Self>
  __macro take(this Self&& self) { return ^^{ Tracker(fwd!(\(self)).t) }; }
};

template <class T>
int bump_twice(T& t) {
  t.bump!(1);         // dependent object expression: deferred
  return t.bump!(1);  // (and not the member 'bump' of some other T)
}

void test_member() {
  counter c;
  c.bump!(2);
  assert(c.get!() == 2);
  counter* p = &c;
  p->bump!(3);
  assert(p->get!() == 5);
  assert(c.twice() == 7);
  c.add_tokens!(1 + 1);
  assert(c.n == 9);
  assert(bump_twice(c) == 11);

  derived d;
  d.bump!(4);
  assert(d.get!() == 4);

  std::unique_ptr<counter> u(new counter);
  u->bump!(1);
  assert(u->n == 1);

  holder h;
  assert(!h.take!().moved);
  assert(holder{}.take!().moved);
  assert(std::move(h).take!().moved);
}

}  // namespace ops

int main(int, char**) {
  ops::test_subscript();
  ops::test_lazy_and();
  ops::test_rewritten();
  ops::test_member();
  return 0;
}
