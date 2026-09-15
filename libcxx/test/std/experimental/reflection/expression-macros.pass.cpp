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
//   id!        - typed parameter, grouping of the argument is preserved
//   fwd!       - type_of(param) is decltype of the argument as written
//   check!     - decomposition of a comparison, evaluate-once, source text,
//                and macros composing (its expansion invokes fwd!)
//   λ!         - raw token_sequence parameter (anaphoric placeholders)
//   define_op! - a declaration macro: expansion is a queue_injection, and the
//                pattern is classified with token_kind_of
//
// Operator/member macros and the ranges::begin ladder live in
// operator-macros.pass.cpp and ranges-begin-macro.pass.cpp.

#include <meta>
#include <algorithm>
#include <cassert>
#include <cstddef>
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

// Metafunctions taking a reflection are found by ADL, so most of these tests
// can drop the `std::meta::` qualification. Functions whose arguments are
// strings (`id`, `str_lit`) still need it.
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
  auto text = std::meta::str_lit(source_text_of(cond));
  unsigned line = source_location_of(cond).line();

  if (is_binary_operation(cond) && is_comparison(operator_of(cond))) {
    auto ops = operands_of(cond);
    return ^^{ do {
      auto&& l = \(ops[0]);
      auto&& r = \(ops[1]);
      if (!(fwd!(l) \(operator_of(cond)) fwd!(r)))
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

// -------------------------------- class prvalue operands (codegen) ---------

// Binding `auto&& r = \(operand)` where the operand is a class prvalue with a
// non-trivial destructor used to crash codegen: interpolation produces a
// *prvalue* OpaqueValueExpr under the MaterializeTemporaryExpr, and
// EmitMaterializeTemporaryExpr assumed any record-type OVE was already bound
// to an existing object. The temporary must be materialized once, live until
// the end of the extending scope, and be destroyed exactly once.

namespace prvalue_operand {
int ctors = 0, dtors = 0, live = 0;

struct V {
  int n;
  V(std::initializer_list<int> il) : n(static_cast<int>(il.size())) {
    ++ctors;
    ++live;
  }
  V(const V& o) : n(o.n) {
    ++ctors;
    ++live;
  }
  ~V() {
    ++dtors;
    --live;
  }
};
bool operator==(const V& a, const V& b) { return a.n == b.n; }

template <class T>
__macro bind_operands(T&& cond) {
  auto ops = operands_of(cond);
  return ^^{ do {
    auto&& l = \(ops[0]);
    auto&& r = \(ops[1]);
    assert(live == 2);       // both operands alive inside the scope
    assert(r.n == 3);        // the temporary is intact, not a dangling copy
    assert(!(l == r));
  } };
}

void test() {
  {
    V a{1};
    int c0 = ctors, d0 = dtors;
    bind_operands!(a == V{1, 2, 3});
    assert(ctors - c0 == 1);  // exactly one temporary constructed
    assert(dtors - d0 == 1);  // ...and destroyed by scope exit
    assert(live == 1);        // only `a` remains
  }
  assert(live == 0);
  assert(ctors == dtors);
}
} // namespace prvalue_operand

// ------------------------------------------- token classification ----------

static_assert([] {
  using std::meta::token_kind;
  auto toks = tokens_of(^^{ x << 42 });
  return toks.size() == 3 &&
         token_kind_of(toks[0]) == token_kind::identifier &&
         token_kind_of(toks[1]) == token_kind::punctuator &&
         token_kind_of(toks[2]) == token_kind::literal &&
         operator_of(toks[1]) == std::meta::operators::op_less_less &&
         identifier_of(toks[0]) == std::meta::id("x");
}());

// A keyword is its own kind; alternative tokens are still punctuators; empty or
// multi-token sequences are `unknown`.
static_assert([] {
  using std::meta::token_kind;
  return token_kind_of(^^{ int }) == token_kind::keyword &&
         token_kind_of(^^{ , }) == token_kind::punctuator &&
         token_kind_of(^^{ or }) == token_kind::punctuator &&
         token_kind_of(^^{ a b }) == token_kind::unknown &&
         token_kind_of(^^{}) == token_kind::unknown;
}());

// Alternative tokens keep their spelling, so token identity distinguishes them
// even though both are punctuators spelling the same operator.
static_assert(^^{ or } != ^^{ || });
static_assert(operator_of(^^{ or }) == operator_of(^^{ || }));

// ------------------------------------------------------------------ λ! -----

consteval std::optional<int> placeholder_index(std::string_view s) {
  if (s.size() == 2 && s[0] == '_' && s[1] >= '1' && s[1] <= '9')
    return s[1] - '0';
  return std::nullopt;
}

__macro λ(std::meta::token_sequence body) {
  int arity = 0;
  for (std::meta::token_sequence tok : tokens_of(body))
    if (auto n = placeholder_index(stringize(tok)))
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

// ------------------------------------------------------- define_op! --------

// A *declaration* macro: the expansion is a queue_injection, so invoking it in
// a consteval block injects a declaration instead of producing an expression.
// The pattern is one of `x op y` (binary), `op x` (prefix), or `x op`
// (postfix); which one it is falls out of token_kind_of on the first token.
__macro define_op(std::meta::token_sequence name,
                  std::meta::token_sequence pattern) {
  auto toks = tokens_of(pattern);

  if (toks.size() == 3) {  // id op id
    auto body = ^^{ fwd!(l) } + toks[1] + ^^{ fwd!(r) };
    return ^^{ queue_injection(^^{
      struct \(name) {
        template <class L, class R>
        constexpr decltype(auto) operator()(L&& l, R&& r) const {
          return (\(body));
        }
      };
    }) };
  }

  std::meta::token_sequence body =
      token_kind_of(toks[0]) == std::meta::token_kind::identifier
          ? ^^{ fwd!(t) } + toks[1]   // id op  (postfix)
          : toks[0] + ^^{ fwd!(t) };  // op id  (prefix)

  return ^^{ queue_injection(^^{
    struct \(name) {
      template <class T>
      constexpr decltype(auto) operator()(T&& t) const {
        return (\(body));
      }
    };
  }) };
}

consteval {
  define_op!(left_shift, x << y);
  define_op!(negate, -x);
  define_op!(post_inc, x++);
}

void test_define_op() {
  static_assert(left_shift{}(1, 4) == 16);
  static_assert(negate{}(5) == -5);

  int n = 3;
  assert(post_inc{}(n) == 3);
  assert(n == 4);
}

int main(int, char**) {
  assert(add_two(40) == 42);
  test_fwd();
  test_check();
  prvalue_operand::test();
  test_lambda();
  test_define_op();
  return 0;
}
