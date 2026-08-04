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

// <experimental/meta>

// P4340 ext: user-provided reflect_constant customization points, routed
// through both std::meta::reflect_constant and constant template parameters.

#include <experimental/meta>
#include <numeric>

struct Frac {
  int numer;
  int denom;

  consteval auto reflect_constant() const -> std::meta::info;
};

// Note: this cannot be an in-class static member template, since Frac is
// incomplete at that point ("constexpr variable cannot have non-literal
// type 'const Frac'").
template <int N, int D>
constexpr Frac frac_interned{N, D};

consteval auto Frac::reflect_constant() const -> std::meta::info {
  int g = std::gcd(numer, denom);
  return std::meta::substitute(
      ^^frac_interned,
      {std::meta::reflect_constant(numer / g),
       std::meta::reflect_constant(denom / g)});
}

template <Frac F> constexpr int probe() { return F.numer * 100 + F.denom; }
template <Frac F> constexpr const Frac* addr = &F;

// The full design example: interning through substitute().
static_assert(probe<Frac{2, 4}>() == 102);
static_assert(probe<Frac{2, 4}> == probe<Frac{1, 2}>);
static_assert(probe<Frac{2, 4}> != probe<Frac{3, 4}>);
static_assert(addr<Frac{2, 4}> == &frac_interned<1, 2>);

// std::meta::reflect_constant agrees with template-argument conversion.
static_assert(std::meta::reflect_constant(Frac{2, 4}) == ^^frac_interned<1, 2>);
static_assert(std::meta::reflect_constant(Frac{2, 4}) ==
              std::meta::reflect_constant(Frac{1, 2}));
static_assert(std::meta::reflect_constant(Frac{2, 4}) !=
              std::meta::reflect_constant(Frac{3, 4}));

// Subobject normalization, including through std::meta::reflect_constant.
struct Stuff {
  Frac frac;
  int x;
};
static_assert(std::meta::reflect_constant(Stuff{{2, 4}, 6}) ==
              std::meta::reflect_constant(Stuff{{1, 2}, 6}));
static_assert([:std::meta::reflect_constant(Stuff{{2, 4}, 6}):].frac.denom == 2);
static_assert([:std::meta::reflect_constant(Stuff{{2, 4}, 6}):].x == 6);

// The extended structural-type definition is visible to the library.
struct Deleted {
  int i;
  consteval auto reflect_constant() const -> std::meta::info = delete;
};
static_assert(!std::meta::is_structural_type(^^Deleted));
static_assert(std::meta::is_structural_type(^^Frac));

int main() { }
