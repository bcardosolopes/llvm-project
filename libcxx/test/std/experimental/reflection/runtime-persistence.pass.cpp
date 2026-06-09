//===----------------------------------------------------------------------===//
//
// Copyright 2025 Bloomberg Finance L.P.
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++03 || c++11 || c++14 || c++17 || c++20
// ADDITIONAL_COMPILE_FLAGS: -freflection -fconsteval-operations

// <experimental/reflection>
//
// In the consteval-only *operations* model, a meta::info computed at compile
// time may persist to runtime as a stateless, 1-byte empty value. This test
// exercises that runtime persistence end-to-end (it is compiled AND executed):
// reflections are stored, copied, assigned, passed by value, returned, placed
// in arrays/structs/containers, and used across runtime control flow. The
// values carry no observable state at runtime, so the assertions check
// structural properties (sizes/counts) and, above all, that the program
// codegens and runs to completion without crashing.

#include <meta>

#include <array>
#include <cassert>
#include <vector>

using info = decltype(^^int);

// A reflection computed entirely at compile time, returned by value.
consteval info type_of_index(int i) {
  info types[] = {^^int, ^^char, ^^double};
  return types[i];
}

// Ordinary runtime functions that take and return reflections by value.
info choose(bool b, info a, info c) { return b ? a : c; }
info identity(info r) { return r; }

struct Holder {
  int tag;
  info r;
};

struct ThreeReflections {
  info a, b, c;
};

int main(int argc, char**) {
  // Representation: stateless, trivially copyable, one byte.
  static_assert(sizeof(info) == 1);
  static_assert(__is_trivially_copyable(info));

  // Locals: default-construct, copy-construct, copy-assign at runtime.
  info r = ^^int;
  info r2 = r;   // copy
  info r3{};     // value-initialize
  r3 = r2;       // assignment
  (void)r;
  (void)r2;
  (void)r3;

  // Arrays of reflections persist to runtime.
  info arr[3] = {^^int, ^^char, ^^double};
  assert(sizeof(arr) == 3);
  info arr_copy[3];
  for (int i = 0; i < 3; ++i)
    arr_copy[i] = arr[i]; // element-wise runtime copy
  (void)arr_copy;

  // Structs containing reflections copy correctly at runtime.
  Holder h{42, ^^int};
  Holder h2 = h; // copies the (empty) reflection member along with the tag
  assert(h2.tag == 42);

  ThreeReflections t{^^int, type_of_index(1), ^^double};
  ThreeReflections t2 = t;
  (void)t2;

  // Runtime control flow selecting between reflections. 'argc' is only known
  // at runtime, so the branch and the calls genuinely execute (not folded).
  bool runtime_flag = argc > 100000; // practically always false, but unknown
  info picked = choose(runtime_flag, ^^int, type_of_index(2));
  picked = identity(picked); // pass-by-value round trip
  (void)picked;

  // Reflections in a standard container, sized/filled at runtime. The palette
  // holds compile-time reflections; indexing/copying it is a pure runtime op.
  info palette[3] = {^^int, ^^char, ^^double};
  std::vector<info> v;
  for (int i = 0; i < argc + 3; ++i)
    v.push_back(palette[i % 3]);
  assert(v.size() == static_cast<std::size_t>(argc + 3));
  std::vector<info> v_copy = v; // copies a container full of reflections
  assert(v_copy.size() == v.size());

  // A compile-time reflection query drives a runtime data-structure size.
  constexpr std::size_t member_count =
      []() consteval {
        return members_of(^^Holder, std::meta::access_context::current()).size();
      }();
  std::array<info, member_count> members{};
  assert(members.size() == member_count);

  return 0;
}
