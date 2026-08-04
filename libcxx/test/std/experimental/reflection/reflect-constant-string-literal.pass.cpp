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
// ADDITIONAL_COMPILE_FLAGS: -std=c++2d -freflection

// <experimental/meta>

// P4340 ext: string literals as constant template parameters. Pointers into
// string literals are normalized onto the interned FixedArray specialization
// holding the same characters, which defines their identity (repealing the
// [temp.arg.nontype] ban).

#include <experimental/meta>
#include <string_view>

using namespace std::literals;

template <const char* P> constexpr const char* get() { return P; }

// Top level, with cross-spelling identity.
static_assert(get<"hello">() == get<"hello">());
static_assert(get<"hello">() == "hello"sv);
static_assert(get<"hello">() != get<"world">());

// Subobjects, including with offsets: provenance is preserved.
struct Wrap { const char* p; };
template <Wrap W> constexpr const char* wget() { return W.p; }
static_assert(wget<Wrap{"hello"}>() == wget<Wrap{"hello"}>());
static_assert(wget<Wrap{&"hello"[1]}>() == wget<Wrap{"hello"}>() + 1);
static_assert(wget<Wrap{"hello"}>() == "hello"sv);

// One-past-the-end offsets are valid.
static_assert(wget<Wrap{&"hello"[5]}>() == wget<Wrap{"hello"}>() + 5);

// The interned pointer is exactly define_static_string's result.
static_assert(wget<Wrap{"hello"}>() == std::define_static_string("hello"));

// std::meta::reflect_constant agrees with template-argument conversion.
static_assert(std::meta::reflect_constant(Wrap{"alpha"}) ==
              std::meta::reflect_constant(Wrap{"alpha"}));
static_assert(std::meta::reflect_constant(Wrap{"alpha"}) !=
              std::meta::reflect_constant(Wrap{"beta"}));

// Named constexpr arrays keep today's behavior (they have their own
// identity; no interning happens).
constexpr char named[] = "named";
static_assert(get<named>() == named);
static_assert(get<named>() != get<"named">());

int main() { }
