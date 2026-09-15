//===----------------------------------------------------------------------===//
//
// Copyright 2026 Bloomberg Finance L.P.
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++03 || c++11 || c++14 || c++17 || c++20 || c++23
// ADDITIONAL_COMPILE_FLAGS: -freflection -std=c++2d

// std::meta::test_expression: speculative validity probing from a macro body.

#include <meta>

// A successful probe returns a reflection of the parsed expression, which
// can be interpolated; that reuses the parse and counts as the argument's
// one evaluation.
template <class T>
__macro probe_once(T&& e) {
  auto m = test_expression(^^{ (\(e) + 1) });
  if (!m) return ^^{ 0 };
  return ^^{ \(*m) };
}

int next();
int ok = probe_once!(next());
static_assert(probe_once!(41) == 42);

// The evaluate-once analysis sees through the probed expression: using the
// probe result AND the argument directly is two evaluations.
template <class T>
__macro probe_and_use(T&& e) {
  auto m = test_expression(^^{ (\(e) + 1) });
  if (!m) return ^^{ 0 };
  return ^^{ \(*m) + \(e) };
}
int bad = probe_and_use!(next()); // expected-error {{expansion of expression macro would evaluate this argument more than once}}

// A failed probe reports nullopt and emits no diagnostics, even for wildly
// invalid token soup.
template <class T>
__macro count_valid(T&& e) {
  int n = 0;
  if (test_expression(^^{ (\(e).nonexistent_member()) })) ++n;
  if (test_expression(^^{ + * / })) ++n;  // not even close
  if (test_expression(^^{ (\(e) + 1) 2 3 })) ++n;  // not a single expression
  if (test_expression(^^{ (\(e) + 1) })) ++n;
  return ^^{ \(n) };
}
static_assert(count_valid!(1) == 1);
