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
// ADDITIONAL_COMPILE_FLAGS: -std=c++2d

// P4341: non-transient constexpr allocation with the standard library types
// that mark their allocations (unique_ptr for const element types; vector and
// basic_string always). See ideas/non-transient-alloc.md.

#include <cassert>
#include <memory>
#include <string>
#include <vector>

// ==== Ex 2/3: unique_ptr ====

// Marked (const element): persists and is constant-readable.
constexpr std::unique_ptr<const int> p3(new int(3));
static_assert(*p3 == 3);

// Unmarked (mutable element): persists, runtime-mutable, not constant-readable.
constexpr std::unique_ptr<int> p2(new int(2));
void bump() { ++*p2; }

// unique_ptr<T[]>: marks iff element type is const.
constexpr std::unique_ptr<const int[]> arr(new int[3]{1, 2, 3});
static_assert(arr[1] == 2);

// ==== Ex 5: vector<string>, the flagship recursion ====

constexpr std::vector<std::string> v = {"this", "is", "so", "cool",
                                        "and long enough to allocate"};
static_assert(v[1] == "is");
static_assert(v.size() == 5);
static_assert(v[4].size() > 22);

// ==== vector<int> and a long string on their own ====

constexpr std::vector<int> ints = {1, 2, 3, 4};
static_assert(ints[2] == 3);

constexpr std::string s = "a string that is long enough to allocate";
static_assert(s[2] == 's');
static_assert(s.size() == 40);

// SSO string: no allocation at all; already legal before this feature.
constexpr std::string sso = "hi";
static_assert(sso[0] == 'h');

// ==== Ex 7: vector<unique_ptr<int>>: buffer marked, pointees unmarked ====

constexpr auto vu = [] {
  std::vector<std::unique_ptr<int>> v;
  v.push_back(std::make_unique<int>(1));
  v.push_back(std::make_unique<int>(2));
  return v;
}();
static_assert(vu.size() == 2);            // buffer readable
static_assert(vu[0] != nullptr);          // the unique_ptr objects readable
void scribble() { *vu[1] = 20; }          // pointees runtime-mutable

// ==== Spare capacity: vectors whose buffer has uninitialized tail bytes ====
// A vector built by push_back (or after reserve()) typically has
// capacity() > size(). The bytes past size() are raw storage; they must not
// prevent the allocation from persisting.

constexpr std::vector<int> grown = [] {
  std::vector<int> r;
  for (int i = 1; i <= 3; ++i)
    r.push_back(i);   // geometric growth: capacity likely 4
  return r;
}();
static_assert(grown.size() == 3);
static_assert(grown[0] == 1 && grown[2] == 3);

constexpr std::vector<int> reserved = [] {
  std::vector<int> r = {1, 2, 3};
  r.reserve(8);
  return r;
}();
static_assert(reserved.size() == 3 && reserved.capacity() == 8);
static_assert(reserved.back() == 3);

int main(int, char**) {
  // Runtime reads of everything, including the runtime-mutable parts.
  assert(*p3 == 3);
  bump();
  assert(*p2 == 3);
  assert(v[3] == "cool");
  assert(s.back() == 'e');
  assert(*vu[0] == 1);
  scribble();
  assert(*vu[1] == 20);
  assert(grown.size() == 3 && grown[1] == 2);
  assert(reserved.capacity() == 8 && reserved[0] == 1);
  return 0;
}
