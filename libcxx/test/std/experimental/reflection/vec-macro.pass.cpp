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

// RUN: %{build}
// RUN: %{exec} %t.exe

// vec!: Rust's vec! for std::vector -- and an improvement on the
// initializer_list constructor. initializer_list elements are const, so they
// can only ever be *copied* into the vector; here each argument is
// constructed directly in the vector's storage, moved if it is an rvalue,
// so non-copyable element types work:
//
//   auto v = vec![std::make_unique<int>(1), std::make_unique<int>(2)];
//
// The expansion reserves once for the element count and then, this being
// our own vector, uses libc++'s __emplace_back_assume_capacity per element,
// skipping the per-push capacity check as well. The element type is the
// common type of the arguments.
//
// The macro's parameter pack is what makes this expressible: `xs` binds one
// argument expression per element, and a fold over the pack is the
// repetition syntax. Each `\(xs)` inside the fold is its own interpolation,
// so every argument is evaluated exactly once, at its own position.

#include <meta>
#include <cassert>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

template <class... Ts>
__macro vec(Ts&&... xs) {
  std::meta::info T = std::meta::dealias(std::meta::substitute(
      ^^std::common_type_t, {std::meta::remove_cvref(type_of(xs))...}));
  std::meta::list_builder pushes;

  #if 0
  ((pushes += ^^{ __v.__emplace_back_assume_capacity(\(xs)); }), ...);
  #else
  for (std::meta::info expr : {xs...}) {
    pushes += ^^{
      __v.__emplace_back_assume_capacity(\(expr));
    };
  }
  #endif

  return ^^{
    do -> std::vector<\(T)> {
      std::vector<\(T)> __v;
      __v.reserve(\(sizeof...(xs)));
      \(pushes)
      do_return __v;
    }
  };
}

struct Counted {
  static inline int ctors = 0, copies = 0, moves = 0;
  int v;
  Counted(int v) : v(v) { ++ctors; }
  Counted(const Counted& o) : v(o.v) { ++copies; }
  Counted(Counted&& o) noexcept : v(o.v) { ++moves; }
};

int next_value() {
  static int n = 0;
  return ++n;
}

// Constant evaluation too.
static_assert([] {
  auto v = vec![1, 2, 3];
  return v.size() == 3 && v.capacity() == 3 && v[1] == 2;
}());

int main(int, char**) {
  // Non-copyable elements: the motivating case.
  auto u = vec![std::make_unique<int>(1), std::make_unique<int>(2)];
  static_assert(std::is_same_v<decltype(u), std::vector<std::unique_ptr<int>>>);
  assert(u.size() == 2 && *u[0] == 1 && *u[1] == 2);

  // The element type is the arguments' common type.
  auto d = vec![1, 2.5, 3];
  static_assert(std::is_same_v<decltype(d), std::vector<double>>);
  assert(d.size() == 3 && d[1] == 2.5);

  // Lvalues are copied, rvalues moved.
  std::string s = "abc";
  auto strs = vec![s, std::move(s), std::string(3, 'x')];
  static_assert(std::is_same_v<decltype(strs), std::vector<std::string>>);
  assert(strs.size() == 3 && strs[0] == "abc" && strs[1] == "abc" &&
         strs[2] == "xxx");

  // Each element is constructed once and moved once, straight into reserved
  // storage: no copies (initializer_list would copy each), no reallocation.
  auto c = vec![Counted(1), Counted(2), Counted(3)];
  assert(Counted::ctors == 3 && Counted::copies == 0 && Counted::moves == 3);
  assert(c.capacity() == 3 && c[2].v == 3);

  // Each argument is evaluated exactly once, in order.
  auto seq = vec![next_value(), next_value(), next_value()];
  assert(seq[0] == 1 && seq[1] == 2 && seq[2] == 3);

  // A single element, and the other bracket spellings.
  auto one = vec!(42);
  assert(one.size() == 1 && one[0] == 42);
  auto braces = vec!{1, 2};
  assert(braces.size() == 2);

  return 0;
}
