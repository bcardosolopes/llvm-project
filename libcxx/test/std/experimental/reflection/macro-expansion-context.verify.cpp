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

// std::meta::macro_expansion_context() describes where the macro being
// expanded is expanding into. Outside a macro expansion there is no such
// context, and a macro that needs a particular kind of context checks for it.

#include <debugging>
#include <meta>

// Not inside a macro expansion at all.
constexpr std::meta::info outside = std::meta::macro_expansion_context();
// expected-error@-1 {{constexpr variable 'outside' must be initialized by a constant expression}}
// expected-note@*:* {{macro_expansion_context() is only available while expanding an expression macro}}

struct S {
  static constexpr std::meta::info also_outside =
      std::meta::macro_expansion_context();
  // expected-error@-2 {{constexpr variable 'also_outside' must be initialized by a constant expression}}
  // expected-note@*:* {{macro_expansion_context() is only available while expanding an expression macro}}
};

// A macro that only makes sense inside a function declines elsewhere, with its
// own message rather than a confusing failure deeper in the expansion.
__macro needs_function() {
  if (!std::meta::is_function(std::meta::macro_expansion_context()))
    std::constexpr_error_str("needs-function",
                             "this macro must be invoked inside a function");
  return ^^{ 0 };
}

constexpr int fine() { return needs_function!(); }  // OK
static_assert(fine() == 0);

constexpr int at_namespace_scope = needs_function!();
// expected-error@-1 {{expression macro 'needs_function' reported an error}}
// expected-note@*:* {{this macro must be invoked inside a function}}

struct T {
  static constexpr int in_class = needs_function!();
  // expected-error@-1 {{expression macro 'needs_function' reported an error}}
  // expected-note@*:* {{this macro must be invoked inside a function}}
  // expected-error@-3 {{constexpr variable 'in_class' must be initialized by a constant expression}}
};
