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
// ADDITIONAL_COMPILE_FLAGS: -std=c++29 -freflection

// P4341 ext: std::vector and std::basic_string as constant template
// parameters. Their reflect_constant customization points (constrained to the
// default allocator, and for vector to structural element types) intern the
// contents via reflect_constant_array and return a __ctp_container
// specialization holding a persisted copy.

#include <experimental/meta>
#include <string>
#include <vector>

// == vector<int> ============================================================

static_assert(__is_structural(std::vector<int>));

template <std::vector<int> V> constexpr auto sum() {
  int r = 0;
  for (int i : V)
    r += i;
  return r;
}

static_assert(sum<std::vector{1, 2, 3}>() == 6);
static_assert(sum<std::vector<int>{}>() == 0);

// Identity: equal vectors name the same specialization; unequal ones don't.
template <std::vector<int> V> struct TagV {};
static_assert(std::is_same_v<TagV<std::vector{1, 2, 3}>,
                             TagV<std::vector{1, 2, 3}>>);
static_assert(!std::is_same_v<TagV<std::vector{1, 2, 3}>,
                              TagV<std::vector{1, 2}>>);
static_assert(!std::is_same_v<TagV<std::vector{1, 2, 3}>,
                              TagV<std::vector<int>{}>>);

// The template parameter object is a real persisted vector: its contents are
// readable, and address identity holds across mentions.
template <std::vector<int> V> constexpr const int* data_of = V.data();
static_assert(data_of<std::vector{4, 5}> == data_of<std::vector{4, 5}>);
static_assert(*data_of<std::vector{4, 5}> == 4);

// == string =================================================================

static_assert(__is_structural(std::string));

template <std::string S> constexpr auto size_of() { return S.size(); }

// Short (SSO) and long strings both work: the customization interns the
// contents either way, so persistence never depends on __is_long().
static_assert(size_of<std::string("hi")>() == 2);
static_assert(size_of<std::string("a considerably longer string, no SSO")>() ==
              36);

template <std::string S> struct TagS {};
static_assert(std::is_same_v<TagS<std::string("hello")>,
                             TagS<std::string("hello")>>);
static_assert(!std::is_same_v<TagS<std::string("hello")>,
                              TagS<std::string("world")>>);

// Equal contents via different construction paths: same specialization.
constexpr std::string make_hello() {
  std::string s;
  s += "hel";
  s += "lo";
  return s;
}
static_assert(std::is_same_v<TagS<std::string("hello")>, TagS<make_hello()>>);

// == nesting ================================================================

// vector<string> is structural because string is (recursively via the
// customization points).
static_assert(__is_structural(std::vector<std::string>));

template <std::vector<std::string> V> constexpr auto total_len() {
  std::size_t r = 0;
  for (const std::string& s : V)
    r += s.size();
  return r;
}
static_assert(total_len<std::vector<std::string>{"a", "bc", "def"}>() == 6);

// == constraint gating ======================================================

// Non-default allocator: no opt-in, not structural, and not an error.
template <class T>
struct MyAlloc : std::allocator<T> {
  template <class U> struct rebind { using other = MyAlloc<U>; };
};
static_assert(!__is_structural(std::vector<int, MyAlloc<int>>));
static_assert(
    !__is_structural(std::basic_string<char, std::char_traits<char>,
                                       MyAlloc<char>>));

// Non-structural element type: no opt-in for vector.
struct NotStructural {
  int x;
private:
  [[maybe_unused]] int y;
};
static_assert(!__is_structural(std::vector<NotStructural>));

int main(int, char**) { return 0; }
