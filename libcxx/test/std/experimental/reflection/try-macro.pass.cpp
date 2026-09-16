//===----------------------------------------------------------------------===//
//
// Copyright 2026 Jump Trading, LLC
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

// `try_!(e)` as an expression macro: unwrap the "continue" value of a
// try-able (std::expected / std::optional) or early-return the "break" value
// from the *enclosing* function.
//
// The interesting part is that the macro body computes both trait
// specializations at translation time, outside the injected token sequence:
//
//   - CT (traits of the argument) from type_of(e);
//   - RT (traits of the enclosing return type) from
//     return_type_of(macro_expansion_context()) -- the macro body sees where
//     the expansion lands, not where the macro was defined.
//
// macro_expansion_context() can also name a class or a namespace, so a macro
// that only works inside a function says so explicitly.
//
// The expansion only forwards the operand once (into `do`-expression body) and
// relies on a plain `return` inside the `do`-expression to leave the enclosing
// function early.

#include <meta>
#include <cassert>
#include <debugging>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

// ----------------------------------------------------------------------------
// A tiny try_traits protocol, specialized for expected and optional.
// ----------------------------------------------------------------------------
template <class T>
struct try_traits;  // primary left undefined: non-try-able types are ill-formed

template <class T, class E>
struct try_traits<std::expected<T, E>> {
  static constexpr bool should_continue(const std::expected<T, E>& e) {
    return e.has_value();
  }
  template <class Self>
  static constexpr T extract_continue(Self&& e) {
    return *std::forward<Self>(e);
  }
  template <class Self>
  static constexpr auto extract_break(Self&& e) {
    return std::unexpected(std::forward<Self>(e).error());
  }
  template <class G>
  static constexpr std::expected<T, E> from_break(std::unexpected<G>&& u) {
    return std::expected<T, E>(std::move(u));
  }
};

template <class T>
struct try_traits<std::optional<T>> {
  static constexpr bool should_continue(const std::optional<T>& o) {
    return o.has_value();
  }
  template <class Self>
  static constexpr T extract_continue(Self&& o) {
    return *std::forward<Self>(o);
  }
  template <class Self>
  static constexpr std::nullopt_t extract_break(Self&&) {
    return std::nullopt;
  }
  // Note this is RT's from_break: T is the *enclosing function's* optional
  // element type, which need not match the operand's.
  static constexpr std::optional<T> from_break(std::nullopt_t) {
    return std::nullopt;
  }
};

// ----------------------------------------------------------------------------
// try_!(e)
// ----------------------------------------------------------------------------
template <class T>
__macro try_(T&& e) {
  namespace m = std::meta;

  // `try_` only means anything inside a function: it early-returns from one.
  m::info where = m::macro_expansion_context();
  if (!m::is_function(where))
    std::constexpr_error_str("bad-try-context",
                             "try_ must be invoked inside a function");

  // Computed in the macro body -- NOT injected as `using` aliases.
  m::info CT = m::substitute(^^try_traits, {m::remove_cvref(m::type_of(e))});
  m::info RT = m::substitute(^^try_traits, {m::return_type_of(where)});

  return ^^{
    do [__r=\(e)] -> decltype(auto) {
      if (not \(CT)::should_continue(__r)) [[unlikely]] {
        return \(RT)::from_break(\(CT)::extract_break(static_cast<decltype(__r)&&>(__r)));
      }
      do_return \(CT)::extract_continue(static_cast<decltype(__r)&&>(__r));
    }
  };
}

// ----------------------------------------------------------------------------
// Tests: std::expected
// ----------------------------------------------------------------------------
constexpr std::expected<int, std::string> parse(std::string_view s) {
  if (s == "bad")
    return std::unexpected("not a number");
  return static_cast<int>(s.size());
}

constexpr std::expected<int, std::string> sum_lengths(std::string_view a,
                                                      std::string_view b) {
  int x = try_!(parse(a));   // unwrap or early-return the error
  int y = try_!(parse(b));
  return x + y;
}

// ----------------------------------------------------------------------------
// Tests: std::optional
// ----------------------------------------------------------------------------
constexpr std::optional<int> half(int n) {
  if (n % 2 != 0)
    return std::nullopt;
  return n / 2;
}

constexpr std::optional<int> quarter(int n) {
  int h = try_!(half(n));
  return try_!(half(h));
}

// The macro sees THIS function's return type, not sum_lengths'.
constexpr std::optional<std::string> describe(int n) {
  int q = try_!(quarter(n));  // enclosing return is optional<string>
  return std::string(1, static_cast<char>('0' + q));
}

// ----------------------------------------------------------------------------
// macro_expansion_context() describes the INVOCATION SITE, not the definition.
// ----------------------------------------------------------------------------
__macro enclosing_return_name() {
  auto name = std::meta::display_string_of(
      std::meta::return_type_of(std::meta::macro_expansion_context()));
  return ^^{ \(std::meta::str_lit(name)) };
}

struct Widget {};

constexpr void   in_void(std::string_view& o)   { o = enclosing_return_name!(); }
constexpr int    in_int(std::string_view& o)    { o = enclosing_return_name!(); return 0; }
constexpr double in_double(std::string_view& o) { o = enclosing_return_name!(); return 0; }
constexpr Widget in_widget(std::string_view& o) { o = enclosing_return_name!(); return {}; }

struct Host {
  constexpr bool member(std::string_view& o) {
    o = enclosing_return_name!();
    return true;
  }
};

constexpr std::string_view enclosing_of(int which) {
  std::string_view o;
  switch (which) {
    case 0: in_void(o);       break;
    case 1: in_int(o);        break;
    case 2: in_double(o);     break;
    case 3: in_widget(o);     break;
    case 4: Host{}.member(o); break;
  }
  return o;
}

// One macro definition, five different answers.
static_assert(enclosing_of(0) == "void");
static_assert(enclosing_of(1) == "int");
static_assert(enclosing_of(2) == "double");
static_assert(enclosing_of(3) == "Widget");
static_assert(enclosing_of(4) == "bool");

// ----------------------------------------------------------------------------
// The context is not always a function: declaration-position invocations
// expand into a class or a namespace, and the macro can tell which.
// ----------------------------------------------------------------------------
namespace ctx {

// Names the kind of context the macro is expanding into.
consteval const char* context_kind_of(std::meta::info where) {
  if (std::meta::is_function(where))
    return "function";
  if (std::meta::is_type(where))
    return "class";
  if (std::meta::is_namespace(where))
    return "namespace";
  return "other";
}

// In declaration position: declares a constant recording the context kind.
__macro note_context() {
  auto kind = context_kind_of(std::meta::macro_expansion_context());
  return ^^{ static constexpr const char* context_kind = \(std::meta::str_lit(kind)); };
}

// In expression position: expands to the context kind directly.
__macro here() {
  auto kind = context_kind_of(std::meta::macro_expansion_context());
  return ^^{ \(std::meta::str_lit(kind)) };
}

// Namespace scope.
note_context!();
static_assert(std::string_view(context_kind) == "namespace");

// Class scope: the context is the class being defined.
struct InClass {
  note_context!();
};
static_assert(std::string_view(InClass::context_kind) == "class");

// Function scope, via an expression-position invocation.
constexpr std::string_view in_function_body() { return here!(); }
static_assert(in_function_body() == "function");

// A default member initializer is still in the class's context.
struct WithInit {
  const char* k = here!();
};
static_assert(std::string_view(WithInit{}.k) == "class");

// The class context reflects the class type itself, so it is usable as one.
__macro my_name() {
  auto n = std::meta::identifier_of(std::meta::macro_expansion_context());
  return ^^{ \(std::meta::str_lit(n)) };
}

struct Named {
  static constexpr const char* self = my_name!();
};
static_assert(std::string_view(Named::self) == "Named");

}  // namespace ctx

int main() {
  // expected: both operands succeed
  {
    constexpr auto r = sum_lengths("ab", "cde");
    static_assert(r.has_value() && *r == 5);
  }
  // expected: first operand fails -> early return the break value
  {
    constexpr auto r = sum_lengths("bad", "cde");
    static_assert(!r.has_value() && r.error() == "not a number");
  }
  // expected: second operand fails
  {
    constexpr auto r = sum_lengths("ab", "bad");
    static_assert(!r.has_value());
  }

  // optional: success path
  {
    constexpr auto r = quarter(8);
    static_assert(r.has_value() && *r == 2);
  }
  // optional: break at first step (odd) -> nullopt
  {
    constexpr auto r = quarter(3);
    static_assert(!r.has_value());
  }
  // optional: break at second step (half is odd)
  {
    constexpr auto r = quarter(6);  // half=3, then half(3)=nullopt
    static_assert(!r.has_value());
  }

  // The break type of the *argument* differs from the enclosing return, and
  // from_break bridges them: describe returns optional<string>.
  {
    constexpr auto r = describe(8);
    static_assert(r.has_value() && *r == "2");
  }
  {
    constexpr auto r = describe(3);
    static_assert(!r.has_value());
  }

  // Also exercise at runtime (do-expression + early return outside constexpr).
  assert((sum_lengths("x", "yz").value() == 3));
  assert(!sum_lengths("bad", "y").has_value());
  assert(describe(8).value() == "2");
  assert(!describe(5).has_value());

  return 0;
}
