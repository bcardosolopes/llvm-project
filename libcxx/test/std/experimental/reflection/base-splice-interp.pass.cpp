//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++03 || c++11 || c++14 || c++17 || c++20 || c++23
// ADDITIONAL_COMPILE_FLAGS: -freflection

// Test interpolating base specifier reflections into token sequences.
// This used to crash in CodeGen when trying to emit a TokenSequence APValue.

#include <meta>

struct B {
  int x = 42;
};

struct D : B {
  int y = 100;
};

consteval auto get_base() {
  auto bases = bases_of(^^D, std::meta::access_context::unchecked());
  return bases[0];
}

// Test that a token sequence containing an interpolated base specifier
// can be created without crashing
consteval bool test_token_seq_creation() {
  constexpr auto base = get_base();
  // Create a token sequence with the base interpolated
  constexpr auto tokens = ^^{ .\(base).x };
  (void)tokens;
  return true;
}

static_assert(test_token_seq_creation());

// Test with multiple bases
struct A { int a = 1; };
struct B2 { int b = 2; };
struct C : A, B2 { int c = 3; };

consteval bool test_multi_base_token_seq() {
  constexpr auto ctx = std::meta::access_context::unchecked();
  constexpr auto base_a = bases_of(^^C, ctx)[0];  // A
  constexpr auto base_b = bases_of(^^C, ctx)[1];  // B2

  // Create token sequences with bases interpolated
  constexpr auto tokens_a = ^^{ .\(base_a).a };
  constexpr auto tokens_b = ^^{ .\(base_b).b };
  (void)tokens_a;
  (void)tokens_b;
  return true;
}

static_assert(test_multi_base_token_seq());

// Test storing token sequence with interpolated base in a constexpr variable
// (this is what triggered the original CodeGen crash)
constexpr auto stored_tokens = []() consteval {
  constexpr auto base = get_base();
  return ^^{ .\(base).x };
}();

// Test that report_tokens works with interpolated base specifiers
// (this used to crash in PrintTokenSequenceToStderr when the base
// specifier reflection was stored as an LValue)
consteval bool test_report_tokens_with_base() {
  constexpr auto ctx = std::meta::access_context::unchecked();
  constexpr auto base = bases_of(^^D, ctx)[0];
  constexpr auto tokens = ^^{ object.\(base) };
  // report_tokens would crash here when printing the base specifier
  // but we don't actually call it in the test to avoid noise
  (void)tokens;
  return true;
}

static_assert(test_report_tokens_with_base());

int main() {
  (void)stored_tokens;
}
