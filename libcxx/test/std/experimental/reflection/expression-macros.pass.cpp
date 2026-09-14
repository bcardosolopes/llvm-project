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

// Expression macros: __macro declarations invoked as name!(args).
//
//   id!    - typed parameter, grouping of the argument is preserved
//   fwd!   - type_of(param) is decltype of the argument as written
//   check! - decomposition of a comparison, evaluate-once, source text,
//            and macros composing (its expansion invokes fwd!)
//   λ!     - raw token_sequence parameter (anaphoric placeholders)

#include <meta>
#include <algorithm>
#include <cassert>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "test_macros.h"

// ---------------------------------------------------------------- id! ------

__macro id(int x) {
  return ^^{ \(x) };
}

static_assert(id!(1 + 2) * 3 == 9);
static_assert(std::is_same_v<decltype(id!(1)), int>);
static_assert(std::is_same_v<decltype(id!(2L)), int>);

int add_two(int n) { return id!(n + 2); }

// --------------------------------------------------------------- fwd! ------

template <class T>
__macro fwd(T&& t) {
  return ^^{ static_cast<\(std::meta::type_of(t))&&>(\(t)) };
}

struct Tracker {
  bool moved = false;
  Tracker() = default;
  Tracker(const Tracker&) {}
  Tracker(Tracker&&) : moved(true) {}
};

template <class U>
Tracker take(U&& u) {
  return Tracker(fwd!(u));
}

void test_fwd() {
  Tracker t;
  assert(!take(t).moved);
  assert(take(Tracker{}).moved);

  int x = 1;
  int& y = x;
  int&& z = std::move(x);
  static_assert(std::is_same_v<decltype(fwd!(x)), int&&>);
  static_assert(std::is_same_v<decltype(fwd!(y)), int&>);
  static_assert(std::is_same_v<decltype(fwd!(z)), int&&>);
  static_assert(std::is_same_v<decltype(fwd!((z))), int&>);
  static_assert(std::is_same_v<decltype(fwd!(x + 1)), int&&>);
}

// ------------------------------------------------------------- check! ------

namespace test {
struct failure {
  std::string text;
  std::string lhs;
  std::string rhs;
  unsigned line;
};
std::vector<failure> failures;

template <class L, class R>
void fail(const char* text, unsigned line, const L& l, const R& r) {
  failures.push_back({text, std::format("{}", l), std::format("{}", r), line});
}
void fail(const char* text, unsigned line) {
  failures.push_back({text, "", "", line});
}
} // namespace test

consteval bool is_comparison(std::meta::operators op) {
  using enum std::meta::operators;
  return op == op_equals_equals || op == op_exclamation_equals || op == op_less ||
         op == op_greater || op == op_less_equals || op == op_greater_equals;
}

template <class T>
  requires requires(T&& t) { static_cast<bool>(static_cast<T&&>(t)); }
__macro check(T&& cond) {
  auto text = std::meta::str_lit(std::meta::source_text_of(cond));
  unsigned line = std::meta::source_location_of(cond).line();

  if (std::meta::is_binary_operation(cond) &&
      is_comparison(std::meta::operator_of(cond))) {
    auto ops = std::meta::operands_of(cond);
    return ^^{ do {
      auto&& l = \(ops[0]);
      auto&& r = \(ops[1]);
      if (!(fwd!(l) \(std::meta::operator_of(cond)) fwd!(r)))
        ::test::fail(\(text), \(line), l, r);
    } };
  }
  return ^^{ do {
    if (!static_cast<bool>(\(cond)))
      ::test::fail(\(text), \(line));
  } };
}

template <class T>
void check_equal(T a, T b) {
  check!(a == b);
}

void test_check() {
  int a = 1, b = 2;
  unsigned first_line = __LINE__ + 1;
  check!(a == b);
  check!(a + 1 == b);
  check!(a < b);

  std::string s = "hello";
  check!(s == "world");
  check!(s.size() == 5u);

  std::optional<int> o;
  check!(o);
  check!(a == b && a < b);

  int calls = 0;
  auto next = [&] { return ++calls; };
  check!(next() == 1);
  assert(calls == 1);

  check_equal(3, 4);

  assert(test::failures.size() == 5);

  assert(test::failures[0].text == "a == b");
  assert(test::failures[0].lhs == "1");
  assert(test::failures[0].rhs == "2");
  assert(test::failures[0].line == first_line);

  assert(test::failures[1].text == "s == \"world\"");
  assert(test::failures[1].lhs == "hello");
  assert(test::failures[1].rhs == "world");

  assert(test::failures[2].text == "o");
  assert(test::failures[2].lhs == "");

  assert(test::failures[3].text == "a == b && a < b");

  assert(test::failures[4].text == "a == b");
  assert(test::failures[4].lhs == "3");
  assert(test::failures[4].rhs == "4");
}

// ------------------------------------------- token classification ----------

static_assert([] {
  using std::meta::token_kind;
  auto toks = std::meta::tokens_of(^^{ x << 42 });
  return toks.size() == 3 &&
         std::meta::token_kind_of(toks[0]) == token_kind::identifier &&
         std::meta::token_kind_of(toks[1]) == token_kind::punctuator &&
         std::meta::token_kind_of(toks[2]) == token_kind::literal &&
         std::meta::operator_of(toks[1]) ==
             std::meta::operators::op_less_less &&
         std::meta::identifier_of(toks[0]) == std::meta::id("x");
}());

// A keyword is its own kind; alternative tokens are still punctuators; empty or
// multi-token sequences are `unknown`.
static_assert([] {
  using std::meta::token_kind;
  return std::meta::token_kind_of(^^{ int }) == token_kind::keyword &&
         std::meta::token_kind_of(^^{ , }) == token_kind::punctuator &&
         std::meta::token_kind_of(^^{ or }) == token_kind::punctuator &&
         std::meta::token_kind_of(^^{ a b }) == token_kind::unknown &&
         std::meta::token_kind_of(^^{}) == token_kind::unknown;
}());

// ------------------------------------------------------------------ λ! -----

consteval std::optional<int> placeholder_index(std::string_view s) {
  if (s.size() == 2 && s[0] == '_' && s[1] >= '1' && s[1] <= '9')
    return s[1] - '0';
  return std::nullopt;
}

__macro λ(std::meta::token_sequence body) {
  int arity = 0;
  for (std::meta::token_sequence tok : std::meta::tokens_of(body))
    if (auto n = placeholder_index(std::meta::stringize(tok)))
      arity = std::max(arity, *n);

  std::meta::list_builder params(^^{ , });
  for (int i = 1; i <= arity; ++i)
    params += ^^{ auto&& \(std::meta::id("_", i)) };

  return ^^{ [](\(params)) -> decltype(auto) { return \(body); } };
}

void test_lambda() {
  std::vector<int> v = {3, 1, 2};
  std::ranges::sort(v, λ!(_1 > _2));
  assert((v == std::vector<int>{3, 2, 1}));

  assert(λ!(_1 * 2)(21) == 42);
  assert(λ!(std::pair<int, int>{_1, _2}.second)(1, 2) == 2);
  assert(λ!(42)() == 42);
}

int main(int, char**) {
  assert(add_two(40) == 42);
  test_fwd();
  test_check();
  test_lambda();
  return 0;
}
