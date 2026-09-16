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
// ADDITIONAL_COMPILE_FLAGS: -freflection

// RUN: %{build}
// RUN: %{exec} %t.exe

// The two-phase annotation lifecycle: inject_members runs right before the
// class is completed (and its returned members participate in completion);
// on_complete runs after, with the complete type.

#include <meta>
#include <cassert>

#include "test_macros.h"

using std::meta::info;

// --------------------------- the phase contract ----------------------------

struct phases {
  consteval auto inject_members(info r) const -> std::meta::token_sequence {
    if (is_complete_type(r))
      throw "inject_members must receive the incomplete type";
    return ^^{ int second = 2; };
  }

  consteval auto on_complete(info r) const -> void {
    if (!is_complete_type(r))
      throw "on_complete must receive the complete type";
    // The injected member participated in completion.
    if (size_of(r) != 2 * sizeof(int))
      throw "the injected member must participate in layout";
  }
};

struct [[=phases{}]] Two {
  int one = 1;
};

static_assert(sizeof(Two) == 2 * sizeof(int));

// ------------------------- the motivating facility -------------------------

// Inject a defaulted hidden-friend operator== -- the member-injection shape
// that namespace-scope injection cannot express.
struct derive_eq {
  consteval auto inject_members(info r) const -> std::meta::token_sequence {
    return ^^{
      friend constexpr bool operator==(\(r) const&, \(r) const&) = default;
    };
  }
};

struct [[=derive_eq{}]] Point {
  int x;
  int y;
};

static_assert(Point{1, 2} == Point{1, 2});
static_assert(Point{1, 2} != Point{2, 1});

// On a class template the callback fires per specialization, receiving the
// concrete type, so \(r) spells e.g. Pair<int>.
template <class T>
struct [[=derive_eq{}]] Pair {
  T first;
  T second;
};

static_assert(Pair<int>{1, 2} == Pair<int>{1, 2});
static_assert(Pair<char>{'a', 'b'} != Pair<char>{'b', 'a'});

// ------------------------------- Eq ----------------------------------------

// The metaclass classic: [[=Eq]] implements the defaulted-operator== rules
// by hand. inject_members walks the subobjects (bases, then members, in
// order); if every one is equality-comparable it injects a memberwise ==,
// otherwise a deleted one -- and a != to match. The subobject walk works
// because member/base introspection is allowed on the being-defined class,
// and the comparability question is asked of the *subobject types* (which
// are complete), never of the class itself.

template <class T>
inline constexpr bool eq_comparable_v = requires(T const& t) {
  static_cast<bool>(t == t);
};
template <class T>
inline constexpr bool ne_comparable_v = requires(T const& t) {
  static_cast<bool>(t != t);
};

struct Eq_t {
  consteval auto inject_members(info r) const -> std::meta::token_sequence {
    auto ctx = std::meta::access_context::unchecked();
    auto comparable = [](info type) {
      return extract<bool>(substitute(^^eq_comparable_v, {type}));
    };

    bool ok = true;
    bool any = false;
    std::meta::list_builder cmp(^^{ && });

    // Bases then members, in order -- and a base-specifier splices in member
    // access position just like a field, so one loop covers both.
    for (info s : subobjects_of(r, ctx)) {
      ok = ok && comparable(type_of(s));
      any = true;
      cmp += ^^{ (__lhs.[:\(s):] == __rhs.[:\(s):]) };
    }

    if (!ok)
      return ^^{
        friend constexpr bool operator==(\(r) const&, \(r) const&) = delete;
        friend constexpr bool operator!=(\(r) const&, \(r) const&) = delete;
      };

    if (!any)
      cmp += ^^{ true };

    return ^^{
      friend constexpr bool operator==(\(r) const& __lhs [[maybe_unused]],
                                       \(r) const& __rhs [[maybe_unused]]) {
        return \(cmp);
      }
      friend constexpr bool operator!=(\(r) const& __lhs, \(r) const& __rhs) {
        return !(__lhs == __rhs);
      }
    };
  }
};
inline constexpr Eq_t Eq{};

struct [[=Eq]] P2 {
  int x;
  int y;
};
static_assert(P2{1, 2} == P2{1, 2});
static_assert(P2{1, 2} != P2{1, 3});

// Bases participate, before members.
struct [[=Eq]] B2 {
  int b;
};
struct [[=Eq]] D2 : B2 {
  int d;
};
static_assert(D2{{1}, 2} == D2{{1}, 2});
static_assert(D2{{1}, 2} != D2{{9}, 2});
static_assert(D2{{1}, 2} != D2{{1}, 9});

// Composition: a member whose == was itself injected by Eq.
struct [[=Eq]] Outer {
  P2 p;
  int z;
};
static_assert(Outer{{1, 2}, 3} == Outer{{1, 2}, 3});
static_assert(Outer{{1, 2}, 3} != Outer{{1, 9}, 3});

// An empty class is vacuously comparable.
struct [[=Eq]] Empty {};
static_assert(Empty{} == Empty{});

// A subobject without == makes both operators deleted, exactly as
// = default would.
struct NoCmp {};
struct [[=Eq]] Bad {
  NoCmp n;
};
static_assert(!eq_comparable_v<Bad>);
static_assert(!ne_comparable_v<Bad>);

// On a class template, Eq fires per specialization with the concrete
// member types -- so one Wrap gets a real memberwise ==, another the
// deleted pair. (Firing on the dependent pattern would see no subobjects
// and inject a vacuous 'return true' comparison for everybody.)
template <class T>
struct [[=Eq]] Wrap {
  T t;
};
struct NotCmp2 {};

static_assert(Wrap<int>{1} == Wrap<int>{1});
static_assert(Wrap<int>{1} != Wrap<int>{2});  // not vacuously true!
static_assert(!eq_comparable_v<Wrap<NotCmp2>>);
static_assert(!ne_comparable_v<Wrap<NotCmp2>>);

// Private members are fine: the injected operators are friends, and the
// walk uses an unchecked access context.
class [[=Eq]] Sealed {
  int secret;
public:
  constexpr Sealed(int s) : secret(s) {}
};
static_assert(Sealed{1} == Sealed{1});
static_assert(Sealed{1} != Sealed{2});

int main(int, char**) {
  Two t;
  assert(t.one == 1);
  assert(t.second == 2);

  assert((Pair<double>{1.5, 2.5} == Pair<double>{1.5, 2.5}));

  P2 p{4, 5};
  assert(p == (P2{4, 5}));
  assert(p != (P2{5, 4}));
  return 0;
}
