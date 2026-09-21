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

// identifier_of(token_sequence) reads the spelling of a single identifier
// token; anything else is not a constant expression, like identifier_of(info)
// on an entity without an identifier.

#include <meta>
#include <string_view>

static_assert(std::meta::identifier_of(^^{ foo }) == "foo");
static_assert(std::meta::u8identifier_of(^^{ foo }) == u8"foo");
static_assert(std::meta::identifier_of(std::meta::id("bar", 1)) == "bar1");

constexpr std::string_view keyword = std::meta::identifier_of(^^{ int });
// expected-error@-1 {{constexpr variable 'keyword' must be initialized by a constant expression}}
// expected-note@-2 {{a single identifier token}}

constexpr std::string_view punct = std::meta::identifier_of(^^{ + });
// expected-error@-1 {{constexpr variable 'punct' must be initialized by a constant expression}}
// expected-note@-2 {{a single identifier token}}

constexpr std::string_view several = std::meta::identifier_of(^^{ a b });
// expected-error@-1 {{constexpr variable 'several' must be initialized by a constant expression}}
// expected-note@-2 {{a single identifier token}}

constexpr std::string_view none = std::meta::identifier_of(^^{ });
// expected-error@-1 {{constexpr variable 'none' must be initialized by a constant expression}}
// expected-note@-2 {{a single identifier token}}
