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

// The forwarding-wrapper metaclass: LoggingVector<T> grows a logging
// forwarder for every public member function *name* of std::vector<T> --
// including the member function templates (emplace_back's variadic pack,
// append_range's constraint, emplace's mixed concrete/pack parameters),
// whose signatures reflection cannot introspect and does not need to.
//
// One deducing-this forwarder per name reproduces the behavior of the whole
// overload set: the inner call performs the real overload resolution, the
// decltype return type mirrors the constraints' extension (SFINAE) and the
// alias-typed returns (reference, iterator) without ever spelling them, and
// ((Self&&)self).impl propagates the object's cv/value category.
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

template <class U>
struct logged {
  consteval auto inject_members(std::meta::info) const
      -> std::meta::token_sequence {
    std::vector<std::string_view> names;
    for (std::meta::info m :
         members_of(^^U, std::meta::access_context::unchecked())) {
      if (!is_public(m) || is_static_member(m))
        continue;
      if (!is_function(m) && !is_function_template(m))
        continue;
      if (is_special_member_function(m) || is_constructor(m) ||
          is_constructor_template(m) || is_destructor(m))
        continue;
      if (is_operator_function(m) || is_operator_function_template(m) ||
          is_conversion_function(m) || is_conversion_function_template(m))
        continue;
      if (!has_identifier(m))
        continue;

      std::string_view name = identifier_of(m);
      // Don't forward customization points: a member named reflect_constant
      // would make the *wrapper* a (malformed) customization.
      if (name == "reflect_constant")
        continue;
      if (std::find(names.begin(), names.end(), name) != names.end())
        continue;
      names.push_back(name);
    }

    std::meta::list_builder out;
    out += ^^{ public: };  // injected members default to the class's default access
    for (std::string_view name : names) {
      out += ^^{
        template <class Self, class... Args>
        constexpr auto \(std::meta::id(name))(this Self&& self,
                                              Args&&... args)
            noexcept(noexcept(
                ((Self&&)self).impl.\(std::meta::id(name))((Args&&)args...)))
            -> decltype(
                ((Self&&)self).impl.\(std::meta::id(name))((Args&&)args...)) {
          ::log_call(\(std::meta::str_lit(name)));
          return ((Self&&)self).impl.\(std::meta::id(name))((Args&&)args...);
        }
      };
    }
    return out;
  }
};

template <class T>
class [[=logged<std::vector<T>>{}]] LoggingVector {
  std::vector<T> impl;

public:
  LoggingVector(std::vector<T> v) : impl(std::move(v)) {}
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

  // deducing-this propagates constness: const begin() is const_iterator
  const LoggingVector<int>& clv = lv;
  static_assert(
      !std::is_same_v<decltype(lv.begin()), decltype(clv.begin())>);

  // plain members ride the same forwarders
  assert(lv.size() == 8);
  assert(lv.front() == 7);
  assert(lv.back() == 9);
  lv.pop_back();

  assert(calls.size() >= 9);
  assert(calls[0] == "emplace_back");
  assert(calls[2] == "emplace");  // calls[1] is the begin() argument
  return 0;
}
