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

// RUN: %{build}
// RUN: %{exec} %t.exe

// Generating macros: one member macro per log level --
//
//   log.info!("x={} y={}", x, y);
//
// -- each checking its format string at compile time against the argument
// types (basic_format_string<char, Args...>) before calling the formatting
// sink. Two things are exercised:
//
// * Two pack expansions in one expansion: the *type* basic_format_string<
//   char, Args...> is built as a reflection (substitute with the pack
//   expanded in the body) and interpolated; the *call arguments* are a fold
//   into a list_builder.
//
// * Nested token sequences: the generator's literal contains the macro's
//   literal. An interpolation binds to the innermost literal enclosing it,
//   so \(args), \(fs), \(raw), \(self) and \(call_args) are the macro's and
//   are kept as tokens until the macro runs; the generator reaches its own
//   level from inside the inner literal with one more backslash, \\(name).
//   (Nested backquote rules.)

#include <meta>
#include <cassert>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

enum class LogLevel { debug, info, error };

struct Log {
  std::vector<std::pair<LogLevel, std::string>> entries;

  void do_log(LogLevel level, std::string_view fmt, auto&&... args) {
    entries.emplace_back(level,
                         std::vformat(fmt, std::make_format_args(args...)));
  }

  consteval {
    for (std::meta::info e : enumerators_of(^^LogLevel)) {
      auto name = std::meta::id(identifier_of(e));
      queue_injection(^^{
        template <class... Args>
        __macro \(name)(this Log& self, std::string_view raw, Args&&... args) {
          auto fs = substitute(^^::std::basic_format_string, {^^char, ^^Args...});

          // The whole argument list goes through the builder, so an empty
          // pack leaves no trailing comma behind.
          auto call_args = std::meta::list_builder(^^{ , });
          call_args += ^^{ fmt };
          ((call_args += ^^{ \(args) }), ...);
          return ^^{
            do {
              constexpr std::string_view fmt = \(fs)(\(raw)).get();
              \(self).do_log(::LogLevel::\\(name), \(call_args));
            }
          };
        }
      });
    }
  }
};

int main(int, char**) {
  Log log;
  int x = 5;
  log.debug!("x={} y={}", x, 6);
  log.info!("{:>3}|{}", "ab", 2.5);
  log.error!("plain");

  assert(log.entries.size() == 3);
  assert(log.entries[0].first == LogLevel::debug && log.entries[0].second == "x=5 y=6");
  assert(log.entries[1].first == LogLevel::info && log.entries[1].second == " ab|2.5");
  assert(log.entries[2].first == LogLevel::error && log.entries[2].second == "plain");
  return 0;
}
