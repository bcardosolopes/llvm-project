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
// ADDITIONAL_COMPILE_FLAGS: -std=c++2d -freflection

// P4341 + P2996: constexpr vectors of reflections, including ones returned
// from metafunctions like nonstatic_data_members_of, persist their
// allocations. Such vectors typically carry spare capacity (the query builds
// them with push_back), so this also covers persisting a buffer whose tail
// is raw storage. This is the canonical setup for expansion statements over
// the members of a type without needing define_static_array.

#include <experimental/meta>
#include <vector>

using namespace std::meta;

constexpr std::vector<int> v1 = {1, 2, 3};
constexpr std::vector<info> v2 = {^^int, ^^char, ^^double};

struct C {
  int x;
  int y;
};

constexpr std::vector<info> v3 =
    nonstatic_data_members_of(^^C, access_context::current());

static_assert(v1.size() == 3);
static_assert(v2.size() == 3 && v2[0] == ^^int);
static_assert(v3.size() == 2);
static_assert(identifier_of(v3[0]) == "x");
static_assert(identifier_of(v3[1]) == "y");

// The motivating use: expansion statement directly over the constexpr
// vector, no define_static_array required.
consteval int count_members() {
  int n = 0;
  template for (constexpr info m : v3) {
    static_assert(is_nonstatic_data_member(m));
    ++n;
  }
  return n;
}
static_assert(count_members() == 2);

// And member-by-member offsets, the typical real-world shape.
consteval bool check_offsets() {
  std::size_t expected = 0;
  template for (constexpr info m : v3) {
    if (offset_of(m).bytes != expected)
      return false;
    expected += size_of(type_of(m));
  }
  return true;
}
static_assert(check_offsets());

int main(int, char**) { return 0; }
