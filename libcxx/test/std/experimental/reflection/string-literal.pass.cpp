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
// ADDITIONAL_COMPILE_FLAGS: -freflection

// <experimental/meta>

// std::is_string_literal / std::string_literal_from

#include <experimental/meta>
#include <string_view>

using namespace std::literals;

constexpr char msg[] = "hello";
constexpr char const* p = "hello";
constexpr char const* q = p + 1;

static_assert(std::is_string_literal("hello"));
static_assert(std::is_string_literal(p));
static_assert(std::is_string_literal(p + 1));
static_assert(std::is_string_literal(q));
static_assert(std::is_string_literal(q + 1));

// One-past-the-end is still a pointer into the literal.
static_assert(std::is_string_literal(p + 5));

// A named constexpr array is not a string literal.
static_assert(!std::is_string_literal(msg));
static_assert(!std::is_string_literal(msg + 1));
static_assert(!std::is_string_literal(nullptr));

static_assert(std::string_literal_from(p) == p);
static_assert(std::string_literal_from(p) == "hello"sv);
static_assert(std::string_literal_from(p + 1) == p);
static_assert(std::string_literal_from(q) == p);
static_assert(std::string_literal_from(q) == "hello"sv);
static_assert(std::string_literal_from(q + 1) == p);
static_assert(std::string_literal_from(msg) == nullptr);

// Provenance: a literal that happens to be a suffix of another is its own
// root, while a pointer formed by arithmetic keeps the original root.
constexpr char const* a = "hello";
constexpr char const* b = "lo";
constexpr char const* c = a + 3;

static_assert(std::is_string_literal(a));
static_assert(std::is_string_literal(b));
static_assert(std::is_string_literal(c));
static_assert(std::string_literal_from(b) == b);
static_assert(std::string_literal_from(c) == a);

int main() { }
