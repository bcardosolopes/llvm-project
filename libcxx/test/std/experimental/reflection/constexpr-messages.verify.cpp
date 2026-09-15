//===----------------------------------------------------------------------===//
//
// Copyright 2026 Bloomberg Finance L.P.
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++03 || c++11 || c++14 || c++17 || c++20 || c++23
// ADDITIONAL_COMPILE_FLAGS: -freflection -std=c++2d

// P2758: std::constexpr_print_str / constexpr_warning_str /
// constexpr_error_str, and their interaction with expression macros: a
// constexpr_error in a macro body means the macro produces no expansion, so
// during substitution the invocation is an invalid expression (a
// requires-expression evaluates to false), while a plain use reports the
// macro's message.

#include <debugging>
#include <meta>
#include <string_view>

using namespace std::string_view_literals;

// --- the library facade, outside of macros -------------------------------

constexpr int reject_zero(int a) {
  if (a == 0)
    std::constexpr_error_str("reject-zero", "can't call with a == 0"); // expected-error@*:* {{constexpr message with tag 'reject-zero': can't call with a == 0}}
  return a;
}

constexpr int ok = reject_zero(2);
constexpr int bad = reject_zero(0); // the error is emitted, evaluation still succeeds
static_assert(bad == 0);

constexpr bool warn_odd(int a) {
  if (a % 2 == 1)
    std::constexpr_warning_str("odd-argument", u8"argument is odd"); // expected-warning@*:* {{constexpr message with tag 'odd-argument': argument is odd}}
  return true;
}
static_assert(warn_odd(2));
static_assert(warn_odd(3));

constexpr bool say() {
  std::constexpr_print_str("hello from constant evaluation"sv); // expected-note@*:* {{constexpr message: hello from constant evaluation}}
  return true;
}
static_assert(say());

// The tag is validated at construction.
constexpr bool bad_tag() {
  std::constexpr_warning_str("no spaces", "x"); // expected-error {{is not a constant expression}} expected-note@*:* {{tag of a constexpr message may only contain letters, digits, '_', '-', and '='}}
  return true;
}

// --- explicit failure in an expression macro ------------------------------

template <class T>
__macro int_only(T&& x) { // expected-note {{in call to}}
  if constexpr (!std::is_same_v<std::remove_cvref_t<T>, int>)
    std::constexpr_error_str("int-only", "only int is supported"); // expected-note@*:* {{constexpr message with tag 'int-only': only int is supported}}
  return ^^{ \(x) };
}

int a = int_only!(1);
long b = int_only!(1L); // expected-error {{expression macro 'int_only<long>' reported an error}}

template <class T>
concept can_int_only = requires(T t) { int_only!(t); };

static_assert(can_int_only<int>);
static_assert(!can_int_only<long>);   // explicit failure -> unsatisfied
static_assert(!can_int_only<double>); // ...in any specialization
