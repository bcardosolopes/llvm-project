//===----------------------------------------------------------------------===//
//
// Copyright 2026 Jump Trading, LLC
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++03 || c++11 || c++14 || c++17 || c++20
// ADDITIONAL_COMPILE_FLAGS: -freflection -std=c++2d

// token_sequence is itself a range: directly iterable with range-for
// (begin/end/size found by ADL -- the compiler associates token_sequence
// with std::meta) and directly indexable (ts[i] is evaluated by the
// compiler). tokens_of() predates this and is now just a materialized copy.

#include <meta>
#include <algorithm>
#include <cassert>
#include <ranges>
#include <string_view>
#include <vector>

namespace m = std::meta;

constexpr m::token_sequence ts = ^^{ a + b };

// ----------------------------------------------------------------------------
// size / empty (unqualified: ADL).
// ----------------------------------------------------------------------------
static_assert(size(ts) == 3);
static_assert(!empty(ts));
static_assert(empty(^^{ }));
static_assert(size(^^{ }) == 0);

// ----------------------------------------------------------------------------
// Direct indexing.
// ----------------------------------------------------------------------------
static_assert(m::token_kind_of(ts[0]) == m::token_kind::identifier);
static_assert(m::token_kind_of(ts[1]) == m::token_kind::punctuator);
static_assert(m::token_kind_of(ts[2]) == m::token_kind::identifier);
static_assert(ts[0] == m::id("a"));  // id() is a single-identifier-token
static_assert(ts[2] == m::id("b"));  // sequence; tokens compare directly

// The result is an ordinary single-token sequence: it splices, compares,
// and concatenates like any other.
static_assert(ts[0] + ts[1] + ts[2] == ^^{ a + b });
static_assert(ts[1] == ^^{ + });

// ----------------------------------------------------------------------------
// Range-for.
// ----------------------------------------------------------------------------
consteval int count_identifiers(m::token_sequence seq) {
  int n = 0;
  for (m::token_sequence tok : seq)
    if (m::token_kind_of(tok) == m::token_kind::identifier)
      ++n;
  return n;
}
static_assert(count_identifiers(ts) == 2);
static_assert(count_identifiers(^^{ }) == 0);
static_assert(count_identifiers(^^{ f(x, y) + z }) == 4);

// Rebuild a sequence token by token.
consteval m::token_sequence roundtrip(m::token_sequence seq) {
  m::token_sequence out = ^^{ };
  for (m::token_sequence tok : seq)
    out += tok;
  return out;
}
static_assert(roundtrip(^^{ if (x) { return 1; } }) ==
              ^^{ if (x) { return 1; } });

// ----------------------------------------------------------------------------
// Iterator arithmetic (random access).
// ----------------------------------------------------------------------------
static_assert([] {
  auto it = begin(ts);
  assert(*it == ^^{ a });
  assert(it[2] == ^^{ b });
  ++it;
  assert(*it == ^^{ + });
  assert(*(it + 1) == ^^{ b });
  assert(*(1 + it) == ^^{ b });
  --it;
  assert(*it == ^^{ a });
  it += 2;
  assert(*it == ^^{ b });
  it -= 2;
  assert(*it == ^^{ a });
  assert(end(ts) - begin(ts) == 3);
  assert(begin(ts) != end(ts));
  assert(begin(ts) < end(ts));
  assert(begin(ts) + 3 == end(ts));
  auto post = it++;
  assert(*post == ^^{ a } && *it == ^^{ + });
  return true;
}());

// ----------------------------------------------------------------------------
// token_sequence is a bona fide std::ranges range: the access CPOs consider
// its ADL customizations, and it is borrowed (compilation-owned storage, so
// iterators from an rvalue sequence cannot dangle).
// ----------------------------------------------------------------------------
static_assert(std::ranges::random_access_range<m::token_sequence>);
static_assert(std::ranges::sized_range<m::token_sequence>);
static_assert(std::ranges::borrowed_range<m::token_sequence>);
static_assert(std::ranges::viewable_range<m::token_sequence>);
static_assert(!std::ranges::contiguous_range<m::token_sequence>);  // proxy deref

static_assert([]() consteval {
  assert(std::ranges::size(ts) == 3);
  assert(std::ranges::distance(ts) == 3);
  assert(!std::ranges::empty(ts));

  // Algorithms.
  assert(std::ranges::count_if(ts, [](m::token_sequence tok) {
           return m::token_kind_of(tok) == m::token_kind::identifier;
         }) == 2);
  assert(std::ranges::find(ts, ^^{ + }) - std::ranges::begin(ts) == 1);

  // Views compose; borrowed-ness lets an rvalue sequence flow in directly.
  int idents = 0;
  for ([[maybe_unused]] m::token_sequence tok :
       ^^{ f(x) + g(y) } | std::views::filter([](m::token_sequence t) {
         return m::token_kind_of(t) == m::token_kind::identifier;
       }))
    ++idents;
  assert(idents == 4);

  m::token_sequence reversed = ^^{ };
  for (m::token_sequence tok : ts | std::views::reverse)
    reversed += tok;
  assert(reversed == ^^{ b + a });
  return true;
}());

// ----------------------------------------------------------------------------
// tokens_of is now a materialized copy of the range.
// ----------------------------------------------------------------------------
static_assert([]() consteval {
  auto v = m::tokens_of(ts);
  assert(v.size() == size(ts));
  for (size_t i = 0; i != v.size(); ++i)
    assert(v[i] == ts[i]);
  return true;
}());

int main(int, char**) { return 0; }
