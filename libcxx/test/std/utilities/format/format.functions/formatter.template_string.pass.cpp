//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++03 || c++11 || c++14 || c++17 || c++20 || c++23
// UNSUPPORTED: no-localization
// ADDITIONAL_COMPILE_FLAGS: -freflection

// Template strings (P3951) and rebound_format as format *arguments*: a
// formatter specialization renders them as std::format(s) would, directly
// into the output, with a fill-and-align / width format-spec.

#include <cassert>
#include <format>
#include <locale>
#include <ostream>
#include <print>
#include <sstream>
#include <string>
#include <string_view>

#include "test_macros.h"
#include "assert_macros.h"
#include "concat_macros.h"

static void check_eq(std::string_view actual, std::string_view expected) {
  TEST_REQUIRE(expected == actual,
               TEST_WRITE_CONCATENATED("Expected output ", expected,
                                       "\nActual output  ", actual, '\n'));
}

struct grouped : std::numpunct<char> {
  char do_thousands_sep() const override { return '_'; }
  std::string do_grouping() const override { return "\3"; }
};

int main(int, char**) {
  int x = 42;
  std::string who = "world";

  // A template string is formattable, and "{}" renders it.
  static_assert(std::formattable<decltype(t"{x}"), char>);
  check_eq(std::format("[{}]", t"x={x}, hello {who}"), "[x=42, hello world]");
  check_eq(std::format("{} and {}", t"{x}", t"{who:?}"), "42 and \"world\"");

  // The literal's own format string is checked at compile time, and its
  // specifiers apply: nothing about the argument path is type-erased away.
  check_eq(std::format("{}", t"{x:#06x}"), "0x002a");

  // fill-and-align and width apply to the rendered text; dynamic width too.
  check_eq(std::format("[{:>10}]", t"x={x}"), "[      x=42]");
  check_eq(std::format("[{:*^10}]", t"x={x}"), "[***x=42***]");
  check_eq(std::format("[{:10}]", t"x={x}"), "[x=42      ]"); // left by default
  check_eq(std::format("[{:>{}}]", t"x={x}", 8), "[    x=42]");
  check_eq(std::format("[{:3}]", t"x={x}"), "[x=42]");        // never truncates

  // Nested template strings: the inner one is just a formattable member.
  check_eq(std::format(t"outer({t"inner({x})"})"), "outer(inner(42))");
  check_eq(std::format(t"[{t"{who}":>8}]"), "[   world]");

  // Works wherever a format argument does.
  std::string out;
  std::format_to(std::back_inserter(out), "<{}>", t"{x}");
  check_eq(out, "<42>");
  std::ostringstream os;
  std::print(os, "{}|{:>4}", t"{x}", t"{x}");
  check_eq(os.str(), "42|  42");

  // A rebound_format is formattable the same way; its pattern is runtime.
  auto rebound = std::rebind_format(std::string("{1}, then {0}"), t"{x} {who}");
  static_assert(std::formattable<decltype(rebound), char>);
  check_eq(std::format("[{}]", rebound), "[world, then 42]");
  check_eq(std::format("[{:>16}]", rebound), "[  world, then 42]");

  // The enclosing call's locale reaches the nested formatting; without one,
  // the nested formatting is not locale-specific either.
  int big = 1234567;
  std::locale loc(std::locale::classic(), new grouped);
  check_eq(std::format(loc, "{}", t"{big:L}"), "1_234_567");
  check_eq(std::format(loc, "[{:>12}]", t"{big:L}"), "[   1_234_567]");
  check_eq(std::format("{}", t"{big:L}"), std::format("{:L}", big));
  check_eq(std::format(loc, "{}", std::rebind_format("{0:L}", t"{big}")), "1_234_567");

#ifndef TEST_HAS_NO_EXCEPTIONS
  // Only fill-and-align and width are accepted.
  auto ts = t"{x}";
  try {
    (void)std::vformat("{:.2}", std::make_format_args(ts));
    assert(false);
  } catch (const std::format_error&) {
  }
  try {
    (void)std::vformat("{:?}", std::make_format_args(ts));
    assert(false);
  } catch (const std::format_error&) {
  }
#endif

  return 0;
}
