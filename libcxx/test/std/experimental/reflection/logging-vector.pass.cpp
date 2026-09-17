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

// The forwarding-wrapper metaclass: LoggingVector<T> grows a logging clone
// of every public member function *declaration* of std::vector<T>, via
// std::meta::declaration_of + forwarding_call_for -- including the member
// function templates (emplace_back's variadic pack, append_range's
// constraint, emplace's mixed concrete/pack parameters), whose heads are
// cloned by substitution rather than introspected.
//
// Per-declaration clones preserve the wrapped interface exactly where the
// earlier per-name deducing-this forwarder (see git history) approximated
// it: real parameter types (so braced-init-list arguments work), cloned
// default arguments, per-clone cv/ref-qualifiers, and *named* constraints
// rather than substitution-failure mirrors.
//
// The annotation value logged<std::vector<T>>{} is dependent, so
// inject_members fires per specialization, where std::vector<T> is concrete
// and fully enumerable.

#include <meta>
#include <cassert>
#include <string_view>
#include <vector>

#include "test_macros.h"

std::vector<std::string_view> calls;
void log_call(std::string_view name) { calls.push_back(name); }

template <class T>
class LoggingVector {
  std::vector<T> impl;

public:
  LoggingVector(std::vector<T> v) : impl(std::move(v)) {}

  consteval {
    for (std::meta::info m : members_of(^^std::vector<T>, std::meta::access_context::unprivileged())) {
      if (is_static_member(m)
          or is_special_member_function(m)
          or is_constructor(m)
          or is_constructor_template(m)
          or is_operator_function(m) or is_operator_function_template(m)
          or is_conversion_function(m) or is_conversion_function_template(m)
          or not has_identifier(m)
          or identifier_of(m) == "reflect_constant"
          or (not is_function(m) and not is_function_template(m))) {
        continue;
      }

      auto d = std::meta::declaration_of(m);
      auto call = std::meta::forwarding_call_for(d, ^^{ impl });
      queue_injection(^^{
      public:
        \(d) {
          ::log_call(\(std::meta::str_lit(identifier_of(m))));
          return \(call);
        }
      });
    }
  }
};

// The constraint's extension is mirrored through the forwarder.
template <class V, class... A>
constexpr bool can_append = requires(V& v, A&&... a) {
  v.append_range((A&&)a...);
};
static_assert(can_append<LoggingVector<int>, int(&)[2]>);
static_assert(!can_append<LoggingVector<int>, int>);
static_assert(!can_append<LoggingVector<int>, char const*, char const*>);

int main(int, char**) {
  LoggingVector<int> lv(std::vector<int>{1, 2, 3});

  // variadic member template; returns vector<int>::reference (int&)
  int& r = lv.emplace_back(4);
  assert(r == 4);
  ASSERT_SAME_TYPE(decltype(lv.emplace_back(5)), int&);

  // mixed concrete + pack parameters: the const_iterator argument converts
  // in the inner call
  auto it = lv.emplace(lv.begin(), 0);
  assert(*it == 0);

  // a whole overload set through one forwarder
  lv.insert(lv.begin(), 7);

  // the constrained template, with a real range
  int more[] = {8, 9};
  lv.append_range(more);

  // the const overloads are their own clones: const begin() is const_iterator
  const LoggingVector<int>& clv = lv;
  static_assert(
      !std::is_same_v<decltype(lv.begin()), decltype(clv.begin())>);

  // plain members are cloned too
  assert(lv.size() == 8);
  assert(lv.front() == 7);
  assert(lv.back() == 9);
  lv.pop_back();

  assert(calls.size() >= 9);
  assert(calls[0] == "emplace_back");
  assert(calls[2] == "emplace");  // calls[1] is the begin() argument

  // A braced-init-list argument deduces against the clone's *real* parameter
  // type (initializer_list<int>) -- the per-name deducing-this forwarder
  // could not do this at all.
  lv.assign({5, 6});
  assert(calls.back() == "assign");
  assert(lv.size() == 2 && lv.front() == 5 && lv.back() == 6);
  return 0;
}
