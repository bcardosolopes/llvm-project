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

// 'template for (consteval ...)' over a vector<info> in a function that is
// emitted: the compile-time range variable (a consteval variable whose value
// persists the vector's allocation) must not reach code generation. (It used
// to: the persisted allocation errored as unemittable and codegen crashed.)

#include <meta>
#include <cassert>
#include <string>
#include <string_view>

enum class Color { red, green, blue };

template <class E>
  requires std::is_enum_v<E>
std::string name_of(E e) {
  template for (consteval std::meta::info r : enumerators_of(^^E)) {
    if (e == [:r:])
      return std::string(identifier_of(r));
  }
  return "<unknown>";
}

int main(int, char**) {
  assert(name_of(Color::red) == "red");
  assert(name_of(Color::blue) == "blue");
  assert(name_of(static_cast<Color>(42)) == "<unknown>");

  // The same shape with a runtime-consumed constant body value.
  int total = 0;
  template for (consteval std::meta::info r : enumerators_of(^^Color)) {
    total += static_cast<int>([:r:]);
  }
  assert(total == 0 + 1 + 2);
  return 0;
}
