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

// std::meta::declaration_of + std::meta::forwarding_call_for: clone member
// function declarations (including member templates with their heads and
// constraints) into a wrapper class, with generated forwarding bodies.
//
// This recovers what the per-name deducing-this forwarder loses (see
// logging-vector.pass.cpp): explicit template arguments, braced-init-list
// arguments, real parameter types and default arguments, and named
// constraints.

#include <meta>
#include <concepts>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#define CHECK(...) do { if (!(__VA_ARGS__)) return 0; } while (false)

// ----------------------------------------------------------------------------
// The wrapped class: one of each interesting shape.
// ----------------------------------------------------------------------------
struct Pair { int a; int b; };

struct Impl {
  int state = 100;

  // Concrete member; by-value and by-reference parameters.
  constexpr int plain(int x, const int& y) { return state + x + y; }

  // Default argument; const-qualified.
  constexpr int defaulted(int a, int b = 40) const { return a + b; }

  // Braced-init-list argument needs a real parameter type.
  constexpr int take(Pair p) { return p.a + p.b; }

  // Variadic member template.
  template <class... Args>
  constexpr int sum(Args... args) { return (0 + ... + args); }

  // Explicit NTTP argument: impossible for a deducing-this forwarder.
  template <std::size_t N>
  constexpr int get() const { return int(N) * 10; }

  // Mixed template parameter kinds, explicitly supplied.
  template <class T, int N>
  constexpr T scale(T v) const { return v * N; }

  // Constrained (trailing requires).
  template <class T>
  constexpr T only_int(T v) requires std::integral<T> { return v; }

  // Constrained parameter (type-constraint spelling).
  template <std::integral T>
  constexpr T twice(T v) { return v + v; }

  // Ref-qualified.
  constexpr int lref() & { return 1; }
  constexpr int rref() && { return 2; }

  // An overload set: each declaration clones separately.
  constexpr int pick(int) { return 1; }
  constexpr int pick(double) { return 2; }
};

// ----------------------------------------------------------------------------
// The generator.
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
        ++calls;
        return \(call);
      }
    };
  }
  return out;
}

struct Logged {
  Impl impl;
  mutable int calls = 0;

  consteval {
    queue_injection(wrapper_members_of(^^Impl));
  }
};

// ----------------------------------------------------------------------------
// Behavior: everything forwards, and every call is counted.
// ----------------------------------------------------------------------------
constexpr int use_all() {
  Logged lg;

  CHECK(lg.plain(1, 2) == 103);

  // Cloned default argument.
  CHECK(lg.defaulted(2) == 42);
  CHECK(lg.defaulted(2, 3) == 5);

  // Braced-init-list argument binds the real parameter type.
  CHECK(lg.take({3, 4}) == 7);

  // Variadic member template.
  CHECK(lg.sum(1, 2, 3) == 6);
  CHECK(lg.sum() == 0);

  // Explicit template arguments work against the cloned heads.
  CHECK(lg.get<2>() == 20);
  CHECK(lg.scale<double, 3>(1.5) == 4.5);

  // Constraints cloned.
  CHECK(lg.only_int(21) == 21);
  CHECK(lg.twice(4) == 8);

  // Ref-qualifiers cloned; the rvalue-qualified clone moves the receiver.
  CHECK(lg.lref() == 1);
  CHECK(std::move(lg).rref() == 2);

  // Overload sets: each member cloned individually.
  CHECK(lg.pick(1) == 1);
  CHECK(lg.pick(1.5) == 2);

  CHECK(lg.calls == 14);
  return 1;
}
static_assert(use_all() == 1);

// The cloned constraints are *named* constraints, participating in overload
// viability the same way the originals do.
template <class T>
constexpr bool can_only_int = requires(Logged& lg, T v) { lg.only_int(v); };
static_assert(can_only_int<int>);
static_assert(!can_only_int<double>);

template <class T>
constexpr bool can_twice = requires(Logged& lg, T v) { lg.twice(v); };
static_assert(can_twice<long>);
static_assert(!can_twice<std::string>);

// The rvalue-qualified member is not callable on an lvalue (cloned
// ref-qualifier, not a deduced collapse).
template <class T>
constexpr bool can_rref_on_lvalue = requires(T& lg) { lg.rref(); };
static_assert(!can_rref_on_lvalue<Logged>);

// ----------------------------------------------------------------------------
// Renaming.
// ----------------------------------------------------------------------------
struct Renamed {
  Impl impl;
  mutable int calls = 0;

  consteval {
    auto d = declaration_of(^^Impl::plain, {.name = ^^{ renamed_plain }});
    auto call = forwarding_call_for(d, ^^{ impl });
    queue_injection(^^{
      \(d) {
        ++calls;
        return \(call);
      }
    });
  }
};

static_assert([] {
  Renamed r;
  CHECK(r.renamed_plain(1, 2) == 103);
  CHECK(r.calls == 1);
  return 1;
}());

// A renamed clone still forwards to the source member's name on the
// receiver: renaming controls what the wrapper exposes, not what it calls.
static_assert(std::meta::is_declaration_spec(std::meta::declaration_of(^^Impl::plain)));
static_assert(!std::meta::is_declaration_spec(^^Impl::plain));

// ----------------------------------------------------------------------------
// The real thing: the three vector member templates from the LoggingVector
// exercise, cloned with their actual heads.
// ----------------------------------------------------------------------------
consteval auto vec_wrappers(std::meta::info V,
                            std::initializer_list<std::string_view> names)
    -> std::meta::token_sequence {
  std::meta::token_sequence out = ^^{};
  for (std::meta::info m : members_of(V, std::meta::access_context::current())) {
    if (!has_identifier(m))
      continue;
    bool wanted = false;
    for (std::string_view n : names)
      wanted |= (identifier_of(m) == n);
    if (!wanted)
      continue;
    if (!is_function_template(m) &&
        (!is_function(m) || is_special_member_function(m) ||
         is_static_member(m)))
      continue;

    auto d = declaration_of(m);
    auto call = forwarding_call_for(d, ^^{ impl });
    out += ^^{
      \(d) {
        ++calls;
        return \(call);
      }
    };
  }
  return out;
}

struct LoggedVec {
  std::vector<int> impl;
  mutable int calls = 0;

  consteval {
    queue_injection(vec_wrappers(
        ^^std::vector<int>, {"emplace_back", "emplace", "push_back", "size"}));
  }
};

constexpr int use_vec() {
  LoggedVec lv;
  lv.push_back(1);
  int& r = lv.emplace_back(2);
  CHECK(r == 2);
  lv.emplace(lv.impl.begin(), 0);
  CHECK(lv.size() == 3);
  CHECK(lv.impl[0] == 0 && lv.impl[1] == 1 && lv.impl[2] == 2);
  CHECK(lv.calls == 4);
  return 1;
}
static_assert(use_vec() == 1);

int main() {
  CHECK(use_all() == 1);
  CHECK(use_vec() == 1);
  return 0;
}
