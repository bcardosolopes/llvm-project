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
// ADDITIONAL_COMPILE_FLAGS: -freflection -std=c++2d

// Refusals of std::meta::declaration_of and std::meta::forwarding_call_for:
// unsupported inputs produce a diagnosed reason, never wrong output.

#include <meta>

struct S {
  S();
  static void s();
  int field;
  void ok(int);
  void variadic(int, ...);
  template <class Self> void explicit_obj(this Self&& self);
  template <class... Ts, class V> void nonterminal_pack(V, Ts...);
};

void free_fn();

template <class T>
struct Dependent {
  void member();
};

// declaration_of refusals.
constexpr auto r1 = std::meta::declaration_of(^^S);
// expected-error@-1 {{constexpr variable 'r1' must be initialized by a constant expression}}
// expected-note@*:* {{cannot produce a declaration description: operand does not designate a function or function template}}

constexpr auto r2 = std::meta::declaration_of(^^S::s);
// expected-error@-1 {{constexpr variable 'r2' must be initialized by a constant expression}}
// expected-note@*:* {{cannot produce a declaration description: only non-static member functions are supported}}

constexpr auto r3 = std::meta::declaration_of(^^free_fn);
// expected-error@-1 {{constexpr variable 'r3' must be initialized by a constant expression}}
// expected-note@*:* {{cannot produce a declaration description: only non-static member functions are supported}}

constexpr auto r4 = std::meta::declaration_of(^^S::field);
// expected-error@-1 {{constexpr variable 'r4' must be initialized by a constant expression}}
// expected-note@*:* {{cannot produce a declaration description: operand does not designate a function or function template}}

constexpr auto r5 = std::meta::declaration_of(
    ^^S::ok, {.name = ^^{ not one token }});
// expected-error@-2 {{constexpr variable 'r5' must be initialized by a constant expression}}
// expected-note@*:* {{cannot produce a declaration description: the replacement name must be a single identifier token}}

constexpr auto r6 = std::meta::declaration_of(^^Dependent<int>::member);
// OK: member of a concrete specialization.
static_assert(std::meta::is_declaration_spec(r6));

// forwarding_call_for refusals.
constexpr auto f1 = std::meta::forwarding_call_for(
    std::meta::declaration_of(^^S::variadic), ^^{ impl });
// expected-error@-2 {{constexpr variable 'f1' must be initialized by a constant expression}}
// expected-note@*:* {{cannot generate a forwarding call: a C-style variadic parameter list cannot be forwarded}}

constexpr auto f2 = std::meta::forwarding_call_for(
    std::meta::declaration_of(^^S::explicit_obj), ^^{ impl });
// expected-error@-2 {{constexpr variable 'f2' must be initialized by a constant expression}}
// expected-note@*:* {{cannot generate a forwarding call: explicit object member functions are not supported}}

constexpr auto f3 = std::meta::forwarding_call_for(
    std::meta::declaration_of(^^S::nonterminal_pack), ^^{ impl });
// expected-error@-2 {{constexpr variable 'f3' must be initialized by a constant expression}}
// expected-note@*:* {{cannot generate a forwarding call: a template parameter pack followed by more template parameters cannot be forwarded}}

constexpr auto f4 = std::meta::forwarding_call_for(^^S::ok, ^^{ impl });
// expected-error@-1 {{constexpr variable 'f4' must be initialized by a constant expression}}
// expected-note@*:* {{cannot generate a forwarding call: operand is not a declaration description}}

// Declaration transformation refusals.
constexpr auto t1 = std::meta::make_override(
    std::meta::declaration_of(^^S::nonterminal_pack));
// expected-error@-2 {{constexpr variable 't1' must be initialized by a constant expression}}
// expected-note@*:* {{cannot produce a declaration description: a member function template cannot be declared override}}

constexpr auto t2 = std::meta::make_override(^^S::ok);
// expected-error@-1 {{constexpr variable 't2' must be initialized by a constant expression}}
// expected-note@*:* {{cannot produce a declaration description: a declaration transformation requires a declaration description}}

constexpr auto t3 = std::meta::make_noexcept(^^S);
// expected-error@-1 {{constexpr variable 't3' must be initialized by a constant expression}}
// expected-note@*:* {{cannot produce a declaration description: a declaration transformation requires a declaration description}}

// A well-formed transformation composes and is a new description.
constexpr auto t4 =
    std::meta::make_noexcept(std::meta::make_override(
        std::meta::declaration_of(^^S::ok)));
static_assert(std::meta::is_declaration_spec(t4));
static_assert(t4 != std::meta::declaration_of(^^S::ok));
