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

// A macro invoked as an entry of a ctor-initializer expands to zero or more
// mem-initializers. This is how a constructor initializes a data member that
// is only conditionally injected: init_if_member! takes a whole
// mem-initializer and keeps it only if the class has a member of that name.

#include <debugging>
#include <meta>
#include <string>

// Keep 'init' if the class being constructed has a non-static data member
// named by its first token; otherwise expand to nothing.
__macro init_if_member(std::meta::token_sequence init) {
  std::meta::info ctor = std::meta::macro_expansion_context();
  if (!std::meta::is_constructor(ctor))
    std::constexpr_error_str("init-if-member",
                             "init_if_member! must initialize a constructor");
  std::string_view name = identifier_of(init[0]);
  for (std::meta::info m : nonstatic_data_members_of(
           parent_of(ctor), std::meta::access_context::unchecked()))
    if (has_identifier(m) && identifier_of(m) == name)
      return init;
  return ^^{};
}

template <class T>
constexpr bool wants_extra = sizeof(T) > 1;

// The motivating example.
// (The pattern cannot name the injected member, so the checks are made from
// outside, on specializations.)
template <class T, class U>
struct C {
  T always;
  consteval {
    if (wants_extra<T>)
      queue_injection(^^{ U sometimes; });
  }

  // A parameter only named in a mem-initializer macro is not used by the
  // (unexpanded) pattern.
  constexpr C(T t, [[maybe_unused]] U u)
      : always(t), init_if_member!(sometimes(u)) {}
};

static_assert(C<int, long>(1, 2).always == 1);
static_assert(C<int, long>(1, 2).sometimes == 2);
static_assert(C<char, long>('a', 2).always == 'a');
static_assert(sizeof(C<char, long>) == sizeof(char));

// Any mem-initializer form: braces, several arguments, and first position.
struct Point {
  int x, y;
};

template <bool B>
struct D {
  consteval {
    if (B)
      queue_injection(^^{ Point p; std::string s; });
  }
  int tail;

  constexpr D(int a, int b)
      : init_if_member!(p{a, b}), init_if_member!(s(3, 'z')), tail(a + b) {}
};

constexpr bool test_d() {
  D<true> d(1, 2);
  return d.p.x == 1 && d.p.y == 2 && d.s == "zzz" && d.tail == 3;
}
static_assert(test_d());
static_assert(D<false>(1, 2).tail == 3);
static_assert(sizeof(D<false>) == sizeof(int));

// The only entry, expanding to nothing: the member keeps its default member
// initializer.
template <bool B>
struct E {
  consteval {
    if (B)
      queue_injection(^^{ int n = 7; });
  }
  constexpr E([[maybe_unused]] int v) : init_if_member!(n(v)) {}
};
static_assert(E<true>(3).n == 3);
static_assert(sizeof(E<false>) == 1);

// Outside a template, the invocation expands immediately. (The class is
// complete: the ctor-initializer is parsed after the class body.)
struct F {
  int a;
  constexpr F(int v) : init_if_member!(a(v)), init_if_member!(b(v)) {}
};
static_assert(F(4).a == 4);

// A macro expanding to several mem-initializers.
__macro both(std::meta::token_sequence v) {
  return ^^{ x(\(v)), y(\(v) + 1) };
}
struct G {
  int x, y;
  constexpr G(int v) : both!(v) {}
};
static_assert(G(1).x == 1 && G(1).y == 2);

// A constructor template of a class template; out-of-line definition.
template <class T>
struct H {
  consteval {
    if (sizeof(T) >= 4)
      queue_injection(^^{ T big; });
  }
  T small;
  template <class V>
  constexpr H(V v);
};
template <class T>
template <class V>
constexpr H<T>::H(V v) : init_if_member!(big(T(v) * 10)), small(T(v)) {}

static_assert(H<int>(2L).big == 20 && H<int>(2L).small == 2);
static_assert(H<short>(2L).small == 2);
static_assert(sizeof(H<short>) == sizeof(short));

// A base class and a member sharing the ctor-initializer with a macro.
struct Base {
  int b;
};
template <bool B>
struct I : Base {
  consteval {
    if (B)
      queue_injection(^^{ int extra; });
  }
  constexpr I(int v) : Base{v}, init_if_member!(extra{v * 2}) {}
};
static_assert(I<true>(5).b == 5 && I<true>(5).extra == 10);
static_assert(I<false>(5).b == 5);

int main(int, char**) {
  // Run-time construction too.
  C<int, long> c(1, 2);
  D<true> d(1, 2);
  return (c.sometimes == 2 && d.s == "zzz") ? 0 : 1;
}
