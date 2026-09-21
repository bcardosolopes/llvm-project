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

// Indexing a token_sequence out of range is not a constant expression, and
// the diagnostic names the index and the sequence's size.

#include <meta>

constexpr std::meta::token_sequence ts = ^^{ a + b };

constexpr std::meta::token_sequence past_end = ts[3];
// expected-error@-1 {{constexpr variable 'past_end' must be initialized by a constant expression}}
// expected-note@-2 {{token index 3 is out of range for a token sequence of 3 tokens}}

constexpr std::meta::token_sequence negative = ts[-1];
// expected-error@-1 {{constexpr variable 'negative' must be initialized by a constant expression}}
// expected-note@-2 {{token index -1 is out of range for a token sequence of 3 tokens}}

constexpr std::meta::token_sequence from_empty = (^^{ })[0];
// expected-error@-1 {{constexpr variable 'from_empty' must be initialized by a constant expression}}
// expected-note@-2 {{token index 0 is out of range for a token sequence of 0 tokens}}
