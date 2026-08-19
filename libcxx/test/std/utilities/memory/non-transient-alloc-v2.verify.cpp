//===----------------------------------------------------------------------===//
//
// Copyright 2026 Jump Trading, LLC
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++03 || c++11 || c++14 || c++17 || c++20 || c++23
// ADDITIONAL_COMPILE_FLAGS: -std=c++2d -Xclang -verify-ignore-unexpected=note

// P4341 v2: what the immutable_if_constexpr model REJECTS for
// std::unique_ptr and std::vector — the design invariants carried over from
// v1, plus the interior-pointer misuses (row 5: conflicting declared
// intent) that v1 silently accepted. (Notes land inside libc++ headers, so
// only the test-file-anchored errors are matched.)

#include <memory>
#include <vector>

// A raw owning pointer leaks (nothing runs a destructor): no persistence.
constexpr int* leak = new int(1);
// expected-error@-1 {{must be initialized by a constant expression}}

// unique_ptr<T> (unmarked, mutable persistence): pointee reads are not
// constant. The unique_ptr object itself is readable; the pointee is not.
constexpr std::unique_ptr<int> um(new int(2));
static_assert(*um == 2);
// expected-error@-1 {{static assertion expression is not an integral constant expression}}

// Ex 4: unique_ptr<unique_ptr<int>> — destroying the outer handle must READ
// the inner unique_ptr object, which lives in an unmarked, mutably-reachable
// allocation. The hypothetical destruction itself fails.
constexpr std::unique_ptr<std::unique_ptr<int>> uu(
    new std::unique_ptr<int>(new int(4)));
// expected-error@-2 {{must be initialized by a constant expression}}

// Mutable persistence requires static storage duration: a local non-static
// constexpr variable cannot persist an unmarked allocation.
void local_unmarked() {
  constexpr std::unique_ptr<int> lp(new int(5));
  // expected-error@-1 {{must be initialized by a constant expression}}
}

// Marked allocations are fine in local constexpr variables (shareable, like
// string literals) — this one must NOT be diagnosed.
void local_marked() {
  constexpr std::unique_ptr<const int> lc(new int(6));
  static_assert(*lc == 6);
}

// Mixed mutability persists fine (buffer blessed via vector's members)...
constexpr std::vector<std::unique_ptr<int>> ok_mixed = [] {
  std::vector<std::unique_ptr<int>> v;
  v.push_back(std::make_unique<int>(1));
  return v;
}();
// ...while reading the pointee stays non-constant:
static_assert(*ok_mixed[0] == 1);
// expected-error@-1 {{static assertion expression is not an integral constant expression}}

// ==== Row 5: interior mutable pointers into blessed allocations ==========
// v1 accepted all three of these (the mark waived the whole allocation) and
// the stray pointer was a runtime .rodata timebomb. Under v2 they are
// ill-formed: the allocation is declared immutable via the library's
// immutable_if_constexpr members, but also reachable as mutable.

// (a) Mutable interior pointer in a sibling member of an enclosing
// aggregate.
struct A {
  std::vector<int> v;
  int* p;
};
constexpr A a = [] {
  std::vector<int> v = {1, 2, 3};
  int* p = v.data();
  return A{.v = static_cast<std::vector<int>&&>(v), .p = p};
}();
// expected-error@-5 {{must be initialized by a constant expression}}

// (b) Mutable interior pointer stored INSIDE the allocation:
// self-referential element type.
struct B {
  int i;
  int* p;
  constexpr B(int i) : i(i), p(&this->i) {}
  constexpr B(B const& rhs) : i(rhs.i), p(&i) {}
};
constexpr std::vector<B> bs = {1, 2, 3};
// expected-error@-1 {{must be initialized by a constant expression}}

// (c) unique_ptr<const T> whose allocation is ALSO mutably reachable via a
// sibling: the conditional blessing on __ptr_ conflicts with the raw path.
struct C {
  std::unique_ptr<const int> u;
  int* p;
};
constexpr C c = [] {
  auto* raw = new int(5);
  return C{std::unique_ptr<const int>(raw), raw};
}();
// expected-error@-4 {{must be initialized by a constant expression}}
