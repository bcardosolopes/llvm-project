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

// P4341: baseline validation of what the mark_immutable_if_constexpr model
// REJECTS for std::unique_ptr and std::vector. Everything here must stay
// rejected under the immutable_if_constexpr (v2) model as well — these are
// invariants of the design, not artifacts of the mark. (Notes land inside
// libc++ headers, so only the test-file-anchored errors are matched.)

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

// Mixed mutability persists fine (buffer marked)...
constexpr std::vector<std::unique_ptr<int>> ok_mixed = [] {
  std::vector<std::unique_ptr<int>> v;
  v.push_back(std::make_unique<int>(1));
  return v;
}();
// ...while reading the pointee stays non-constant:
static_assert(*ok_mixed[0] == 1);
// expected-error@-1 {{static assertion expression is not an integral constant expression}}
