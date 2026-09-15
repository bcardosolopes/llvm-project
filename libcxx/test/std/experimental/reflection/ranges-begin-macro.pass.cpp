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

// A reduced model of [range.access.begin] as an expression macro (elided:
// the borrowed-range gate for rvalues, incomplete array elements, and
// noexcept propagation). The rungs whose validity is expressible from R are
// ordinary requires-expressions -- test_expression is not needed for this
// CPO -- and the "ill-formed" rung is a constexpr_error, observable through
// requires as false.
//
// The expansion reproduces the CPO's semantics exactly: as_lvalue(r) plays
// the role of binding the argument to the CPO's named parameter t (a named
// forwarding reference, hence an lvalue), and auto(...) is the decay-copy.

#include <meta>
#include <cassert>
#include <debugging>
#include <iterator>
#include <type_traits>
#include <utility>

#include "test_macros.h"

// ------------------------------------------------------- as_lvalue ---------

// as_lvalue views an expression as if bound to `auto&& tmp` and then named.
template <class T>
__macro lv(T&& x) {
  return ^^{ \(as_lvalue(x)) };
}

struct Tracker {
  bool moved = false;
  Tracker() = default;
  Tracker(const Tracker&) {}
  Tracker(Tracker&&) : moved(true) {}
  int probe() & { return 1; }    // lvalue-only member
  int probe() && { return 2; }
};

void test_as_lvalue() {
  int n = 0;
  static_assert(std::is_same_v<decltype(lv!(n)), int&>);              // lvalue: identity
  static_assert(std::is_same_v<decltype(lv!(std::move(n))), int&>);   // xvalue: viewed as lvalue
  static_assert(std::is_same_v<decltype(lv!(n + 1)), int&>);          // prvalue: materialized

  lv!(n) = 42;
  assert(n == 42);

  // The lvalue view selects the & overload even for rvalue operands, and a
  // materialized temporary is copied from, not moved from.
  Tracker t;
  assert(lv!(t).probe() == 1);
  assert(lv!(std::move(t)).probe() == 1);
  assert(lv!(Tracker{}).probe() == 1);
  assert(!Tracker(lv!(Tracker{})).moved);
}

// ---------------------------------------------------- the begin CPO --------

namespace rng {

template <class T>
constexpr bool enable_borrowed_range = false;

namespace impl {

// Poison pill: ADL-only lookup for begin, as for the real CPO. The helper
// also performs the decay-copy, and is SFINAE-friendly through its return
// type, so the ladder can use it both to *ask* (in a requires-expression)
// and to *emit*.
void begin(auto&) = delete;

template <class R>
constexpr auto adl_begin(R&& r) noexcept(noexcept(auto(begin(r))))
    -> decltype(auto(begin(r))) {
  return auto(begin(r));
}

} // namespace impl

inline constexpr struct begin_fn {
  template <class R>
  __macro operator()(this begin_fn, R&& r) {
    if (not std::is_lvalue_reference_v<R> and not enable_borrowed_range<std::remove_cv_t<R>>) {
      std::constexpr_error_str("no-begin", "rvalue range is not borrowed");
    }
    else if (std::is_array_v<std::remove_reference_t<R>>) {
      if (not is_complete_type(remove_all_extents(^^std::remove_reference_t<R>))) {
        std::constexpr_error_str("no-begin", "T is an array with incomplete element type");
      } else {
        return ^^{ (\(as_lvalue(r)) + 0) };
      }
    }
    else if (requires(R&& t) { { auto(t.begin()) } -> std::input_or_output_iterator; }) {
      return ^^{ auto(\(as_lvalue(r)).begin()) };
    }
    else if (requires(R&& t) { { impl::adl_begin(t) } -> std::input_or_output_iterator; }) {
      return ^^{ ::rng::impl::adl_begin(\(as_lvalue(r))) };
    }
    else {
      std::constexpr_error_str("no-begin", "no viable begin for this type");
    }

    return ^^{};  // unreachable: the error produces no expansion
  }
} begin{};

} // namespace rng

// The range types live outside rng: a hidden friend 'begin' would otherwise
// collide with the CPO object (as it would with std::ranges::begin).
namespace rng_test {

struct HasMember {
  int store[3] = {1, 2, 3};
  constexpr int* begin() { return store; }
};
struct RefBegin {
  int* p = nullptr;
  int*& begin() { return p; }  // decay-copy: the CPO must return int*, not int*&
};
struct RvalueOnlyBegin {
  int* begin() && { return nullptr; }  // t is an lvalue: this never matches
};
struct AdlOnly {
  int store[2] = {4, 5};
  friend constexpr int* begin(AdlOnly& a) { return a.store; }
};
struct BadBegin {
  int begin() const { return 0; }  // valid, but not an iterator
};
struct NoBegin {};

template <class T>
concept can_begin = requires(T& t) { rng::begin(t); };

void test() {
  HasMember m;
  assert(rng::begin(m) == m.store);
  static_assert(std::is_same_v<decltype(rng::begin(m)), int*>);

  int x;
  RefBegin rb{&x};
  static_assert(std::is_same_v<decltype(rng::begin(rb)), int*>);  // decayed
  assert(rng::begin(rb) == &x);

  AdlOnly a;
  assert(rng::begin(a) == a.store);

  int arr[4] = {};
  assert(rng::begin(arr) == &arr[0]);

  static_assert(can_begin<HasMember>);
  static_assert(can_begin<AdlOnly>);
  static_assert(can_begin<int[4]>);
  static_assert(!can_begin<NoBegin>);         // the constexpr_error rung
  static_assert(!can_begin<BadBegin>);        // begin() is not an iterator
  static_assert(!can_begin<RvalueOnlyBegin>); // t is an lvalue, as in the CPO
  static_assert(!can_begin<int>);
}

} // namespace rng_test

int main(int, char**) {
  test_as_lvalue();
  rng_test::test();
  return 0;
}
