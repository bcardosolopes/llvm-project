//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++03 || c++11 || c++14 || c++17 || c++20
// ADDITIONAL_COMPILE_FLAGS: -freflection

// Test error cases for base specifier splicing

#include <meta>

struct B {
  int x = 42;
};

struct D : B {
  int y = 100;
};

struct Unrelated {
  int z = 0;
};

// Test 1: Cannot splice a type reflection as a member access
void test_type_splice_rejected() {
  D d;
  d.[:^^B:]; // expected-error {{reflection not usable in a splice expression}}
}

// Test 2: Cannot use a base specifier from a different class hierarchy
consteval auto get_d_base() {
  return bases_of(^^D, std::meta::access_context::unchecked())[0];
}

void test_wrong_derived_class() {
  Unrelated u;
  constexpr auto r = get_d_base();  // Base specifier for D, not Unrelated
  u.[:r:]; // expected-error {{class 'Unrelated' not derived from 'D'}}
}

// Test 3: Cannot use a base specifier on a completely unrelated type
void test_unrelated_type() {
  int i = 42;
  constexpr auto r = get_d_base();
  i.[:r:]; // expected-error {{member reference base type 'int' is not a structure or union}}
}

// Test 4: Using base specifier with wrong direction (child as base of parent)
struct E : D { };

consteval auto get_e_base() {
  return bases_of(^^E, std::meta::access_context::unchecked())[0];  // D as base of E
}

void test_wrong_direction() {
  D d;  // D is not derived from E
  constexpr auto r = get_e_base();  // r says "E derives from D"
  d.[:r:]; // expected-error {{class 'D' not derived from 'E'}}
}
