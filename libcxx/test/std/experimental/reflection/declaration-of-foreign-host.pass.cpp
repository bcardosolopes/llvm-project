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

// Cloning constrained members of a class template specialization into a host
// whose own template arguments are UNRELATED to the source's.
//
// A member of Source<char, 3> keeps its constraint expressions
// unsubstituted, written against Source's template parameters; the clone
// must bake Source<char, 3>'s arguments into the cloned constraints at
// splice time. It must NOT rely on the host's enclosing arguments lining up
// with the source's (they famously do in wrappers like
// LoggingVector<T> around std::vector<T>, and famously don't here).

#include <meta>
#include <concepts>
#include <cstddef>

#define CHECK(...) do { if (!(__VA_ARGS__)) return 0; } while (false)

// ----------------------------------------------------------------------------
// The source: every constraint spelling, all referencing T and/or N.
// ----------------------------------------------------------------------------
template <class T, int N>
struct Source {
  // Type-constraint spelling on the parameter.
  template <std::same_as<T> U>
  constexpr int type_constrained(U) const { return 1; }

  // Template-head requires-clause, mixing both enclosing parameters.
  template <class U> requires (sizeof(U) == sizeof(T) + N)
  constexpr int head_constrained(U) const { return 2; }

  // Trailing requires-clause on a member template.
  template <class U>
  constexpr int trailing_constrained(U) const
    requires std::convertible_to<U, T*> { return 3; }

  // Trailing requires-clause on a NON-template member.
  constexpr int plain_constrained() const requires std::integral<T> {
    return 4;
  }
};

// ----------------------------------------------------------------------------
// The generator (as in declaration-of.pass.cpp).
// ----------------------------------------------------------------------------
consteval auto wrapper_members_of(std::meta::info U) -> std::meta::token_sequence {
  auto v = members_of(U, std::meta::access_context::current());
  std::erase_if(v, [](std::meta::info m) {
    if (is_function_template(m))
      return false;
    return not is_function(m) or is_special_member_function(m) or
           is_static_member(m);
  });

  std::meta::token_sequence out = ^^{};
  for (std::meta::info m : v) {
    auto d = declaration_of(m);
    auto call = forwarding_call_for(d, ^^{ impl });
    out += ^^{
      \(d) {
        return \(call);
      }
    };
  }
  return out;
}

// ----------------------------------------------------------------------------
// The host: a template head that shares nothing with Source's -- different
// arity, kinds, and arguments. Under an enclosing-argument-based scheme the
// cloned constraints would be checked against <A, B> instead of <char, 3>.
// ----------------------------------------------------------------------------
template <class A, class B>
struct Wrap {
  Source<char, 3> impl;

  consteval {
    queue_injection(wrapper_members_of(^^Source<char, 3>));
  }
};

using W = Wrap<double, void*>;

// ----------------------------------------------------------------------------
// The cloned constraints answer for Source<char, 3>, not for the host.
// ----------------------------------------------------------------------------
template <class V, class U>
constexpr bool can_type = requires(V& w, U u) { w.type_constrained(u); };
static_assert( can_type<W, char>);
static_assert(!can_type<W, double>);  // host's A; accepted iff T leaked
static_assert(!can_type<W, int>);

template <class V, class U>
constexpr bool can_head = requires(V& w, U u) { w.head_constrained(u); };
static_assert( can_head<W, int>);     // sizeof == sizeof(char) + 3
static_assert(!can_head<W, char>);
static_assert(!can_head<W, double>);

template <class V, class U>
constexpr bool can_trailing =
    requires(V& w, U u) { w.trailing_constrained(u); };
static_assert( can_trailing<W, char*>);
static_assert(!can_trailing<W, double*>);  // host's A*; accepted iff T leaked
static_assert(!can_trailing<W, int>);

// integral<char> holds; integral<double> (the host's A) does not.
template <class V>
constexpr bool can_plain = requires(V& w) { w.plain_constrained(); };
static_assert(can_plain<W>);

// A second host whose first argument WOULD satisfy the source's constraints
// if it leaked in: same answers regardless.
using W2 = Wrap<char, int>;
static_assert( can_type<W2, char>);
static_assert(!can_type<W2, int>);
static_assert( can_head<W2, int>);
static_assert( can_trailing<W2, char*>);
static_assert(can_plain<W2>);

// ----------------------------------------------------------------------------
// The satisfied clones actually forward.
// ----------------------------------------------------------------------------
constexpr int use_all() {
  W w{};
  CHECK(w.type_constrained('x') == 1);
  CHECK(w.head_constrained(0) == 2);
  CHECK(w.trailing_constrained(static_cast<char*>(nullptr)) == 3);
  CHECK(w.plain_constrained() == 4);
  return 1;
}
static_assert(use_all() == 1);

int main(int, char**) {
  return use_all() == 1 ? 0 : 1;
}
