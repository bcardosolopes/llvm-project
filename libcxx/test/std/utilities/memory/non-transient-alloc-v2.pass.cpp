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

// P4341 v2: baseline validation of the immutable_if_constexpr model for
// std::unique_ptr and std::vector — everything that works and everything
// that is runtime-mutable. The interior-pointer misuses that v1 accepted
// (and this test used to pin down as change detectors) are now ill-formed;
// they live in the companion .verify.cpp.

#include <cassert>
#include <memory>
#include <utility>
#include <vector>

// ============================ unique_ptr ==================================

// unique_ptr<const T>: marks. Persists immutable; constant-readable; the
// pointer itself, the pointee, and pointer identity all usable as constants.
constexpr std::unique_ptr<const int> uc(new int(3));
static_assert(*uc == 3);
static_assert(uc != nullptr);
static_assert(uc.get() == &*uc);

// unique_ptr<T>: does not mark. Persists mutable: no constant reads of the
// pointee, but the unique_ptr OBJECT itself (the stored pointer value) is
// constant-readable, and the pointee is runtime-readable and -writable.
constexpr std::unique_ptr<int> um(new int(2));
static_assert(um != nullptr);            // reading the pointer: ok
static_assert(um.get() != nullptr);      // ditto
void bump() { ++*um; }                   // runtime write: ok

// Arrays, both ways.
constexpr std::unique_ptr<const int[]> uca(new int[3]{1, 2, 3});
static_assert(uca[0] == 1 && uca[2] == 3);
constexpr std::unique_ptr<int[]> uma(new int[3]{4, 5, 6});
static_assert(uma != nullptr);
void bump_arr() { ++uma[1]; }

// Null unique_ptr: no allocation, nothing to persist, everything constant.
constexpr std::unique_ptr<int> unull;
static_assert(unull == nullptr);

// unique_ptr<const T> where T is a class with a nontrivial (constexpr)
// destructor: the pointee's destruction reads nothing from an unmarked
// allocation, and the outer allocation is marked.
struct Boxed {
  int a = 1, b = 2;
  constexpr ~Boxed() {}
};
constexpr std::unique_ptr<const Boxed> ub(new Boxed{7, 8});
static_assert(ub->a == 7 && ub->b == 8);

// ============================== vector ====================================

// Always marks: constant-readable through every accessor.
constexpr std::vector<int> vi = {1, 2, 3, 4};
static_assert(vi.size() == 4);
static_assert(vi[0] == 1 && vi.front() == 1 && vi.back() == 4);
static_assert(*vi.data() == 1);
static_assert(vi.begin() + 4 == vi.end());
static_assert(!vi.empty());

// Empty vector: no allocation.
constexpr std::vector<int> ve;
static_assert(ve.empty() && ve.data() == nullptr);

// Spare capacity: raw storage past size() persists as part of the image.
constexpr std::vector<int> vg = [] {
  std::vector<int> r;
  for (int i = 1; i <= 3; ++i)
    r.push_back(i);
  return r;
}();
static_assert(vg.size() == 3 && vg[2] == 3);

// Nested: vector<vector<int>> — inner buffers are separate allocations,
// each marked by its own vector's destructor during the hypothetical walk.
constexpr std::vector<std::vector<int>> vv = {{1}, {2, 3}, {}};
static_assert(vv.size() == 3);
static_assert(vv[1][1] == 3);
static_assert(vv[2].empty());

// Mixed mutability (Ex 7): buffer marked, unique_ptr pointees unmarked.
constexpr auto vu = [] {
  std::vector<std::unique_ptr<int>> v;
  v.push_back(std::make_unique<int>(1));
  v.push_back(std::make_unique<int>(2));
  return v;
}();
static_assert(vu.size() == 2);
static_assert(vu[0] != nullptr);         // reading the handle: ok
void scribble() { *vu[1] = 20; }         // writing the pointee: ok

// Pointer identity into the persisted buffer is a constant.
constexpr const int* vi_first = vi.data();
static_assert(vi_first == vi.data());
static_assert(*vi_first == 1);

int main(int, char**) {
  // Runtime reads of everything persisted.
  assert(*uc == 3);
  assert(uca[2] == 3);
  assert(ub->b == 8);
  assert(vi[3] == 4);
  assert(vg[0] == 1);
  assert(vv[1][0] == 2);
  assert(*vi_first == 1);

  // Runtime mutation of the legitimately-mutable allocations.
  bump();
  assert(*um == 3);
  bump_arr();
  assert(uma[1] == 6);
  scribble();
  assert(*vu[1] == 20);

  return 0;
}
