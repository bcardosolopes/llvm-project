//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++03, c++11, c++14, c++17, c++20, c++23
// ADDITIONAL_COMPILE_FLAGS: -freflection

// A template string used as a format argument still has its own format
// string checked at compile time: the check is not deferred to the runtime
// formatting of the enclosing call.

#include <format>
#include <string>

#include "test_macros.h"

void f() {
  std::string s;
  // 'd' is not a valid presentation type for a string.
  TEST_IGNORE_NODISCARD std::format("{}", t"{s:d}");
  // expected-error@*:* {{constexpr variable '__checked' must be initialized by a constant expression}}
  // expected-note@*:* {{non-constexpr function '__throw_format_error' cannot be used in a constant expression}}
  // expected-note@*:* 0+ {{in instantiation of}}
  // expected-note@*:* 0+ {{in call to}}
  // expected-note@*:* 0+ {{declared here}}
  // expected-note@* 0+ {{in instantiation of}}
}
