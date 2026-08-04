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

// P4340 ext (reflect-constant.md): standard library types with defaulted
// reflect_constant customization points, usable as constant template
// parameters: tuple, basic_string_view, span, optional, expected, variant,
// chrono types, complex, the comparison categories, bitset, and
// reference_wrapper.

#include <experimental/meta>
#include <bitset>
#include <chrono>
#include <compare>
#include <complex>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>
#include <variant>

using namespace std::literals;

// ==== tuple ====

template <std::tuple<int, char> T> constexpr int tup() {
  return std::get<0>(T) * 100 + std::get<1>(T);
}
static_assert(tup<std::tuple{3, 'a'}>() == 300 + 'a');
static_assert(tup<std::tuple{3, 'a'}> == tup<std::tuple{3, 'a'}>);
static_assert(tup<std::tuple{3, 'a'}> != tup<std::tuple{4, 'a'}>);

// ==== basic_string_view (over an interned literal) ====

template <std::string_view SV> constexpr std::string_view sv() { return SV; }
static_assert(sv<"hello"sv>() == "hello"sv);
static_assert(sv<"hello"sv> == sv<"hello"sv>);
static_assert(sv<"hello"sv> != sv<"world"sv>);
// Distinct spellings of value-equal views over interned literals agree.
static_assert(sv<std::string_view("hello")> == sv<"hello"sv>);

// ==== span (over a named constexpr array) ====

constexpr int arr[3] = {1, 2, 3};
template <std::span<const int, 3> S> constexpr int spn() { return S[1]; }
static_assert(spn<std::span<const int, 3>(arr)>() == 2);
static_assert(spn<std::span<const int, 3>(arr)> ==
              spn<std::span<const int, 3>(arr)>);

// ==== optional ====

template <std::optional<int> O> constexpr int opt() { return O ? *O : -1; }
static_assert(opt<std::optional<int>(7)>() == 7);
static_assert(opt<std::optional<int>{}>() == -1);
static_assert(opt<std::optional<int>(7)> == opt<std::optional<int>(7)>);
static_assert(opt<std::optional<int>(7)> != opt<std::optional<int>(8)>);
static_assert(opt<std::optional<int>(7)> != opt<std::optional<int>{}>);

// ==== expected ====

template <std::expected<int, char> E> constexpr int exp() {
  return E ? *E : -E.error();
}
static_assert(exp<std::expected<int, char>(5)>() == 5);
static_assert(exp<std::expected<int, char>(std::unexpected('x'))>() == -'x');
static_assert(exp<std::expected<int, char>(5)> ==
              exp<std::expected<int, char>(5)>);
static_assert(exp<std::expected<int, char>(5)> !=
              exp<std::expected<int, char>(std::unexpected('x'))>);

// expected<void, E>
template <std::expected<void, int> E> constexpr int vexp() {
  return E ? 0 : E.error();
}
static_assert(vexp<std::expected<void, int>{}>() == 0);
static_assert(vexp<std::expected<void, int>(std::unexpected(3))>() == 3);

// ==== variant ====

template <std::variant<int, char> V> constexpr int var() {
  return V.index() == 0 ? std::get<0>(V) : std::get<1>(V);
}
static_assert(var<std::variant<int, char>(42)>() == 42);
static_assert(var<std::variant<int, char>('c')>() == 'c');
static_assert(var<std::variant<int, char>(42)> ==
              var<std::variant<int, char>(42)>);
// Same value, different alternative: distinct specializations.
static_assert(var<std::variant<int, char>(99)> !=
              var<std::variant<int, char>(static_cast<char>(99))>);

// ==== composition: customized type inside a library type ====

namespace frac {
struct Frac {
  int numer;
  int denom;
  consteval auto reflect_constant() const -> std::meta::info;
};
template <int N, int D> constexpr Frac interned{N, D};
consteval auto Frac::reflect_constant() const -> std::meta::info {
  int g = [](int a, int b) {
    while (b) { int t = a % b; a = b; b = t; }
    return a;
  }(numer, denom);
  return std::meta::substitute(
      ^^interned,
      {std::meta::reflect_constant(numer / g),
       std::meta::reflect_constant(denom / g)});
}
} // namespace frac

// optional<Frac>: the Frac subobject normalizes inside the optional.
template <std::optional<frac::Frac> O> constexpr int ofrac() {
  return O->denom;
}
static_assert(ofrac<std::optional<frac::Frac>(frac::Frac{2, 4})>() == 2);
static_assert(ofrac<std::optional<frac::Frac>(frac::Frac{2, 4})> ==
              ofrac<std::optional<frac::Frac>(frac::Frac{1, 2})>);

// tuple<Frac, string_view>: both member normalizations compose.
template <std::tuple<frac::Frac, std::string_view> T> constexpr auto tf() {
  return std::get<0>(T).denom;
}
static_assert(tf<std::tuple{frac::Frac{2, 4}, "hi"sv}>() == 2);
static_assert(tf<std::tuple{frac::Frac{2, 4}, "hi"sv}> ==
              tf<std::tuple{frac::Frac{1, 2}, "hi"sv}>);
static_assert(tf<std::tuple{frac::Frac{1, 2}, "hi"sv}> !=
              tf<std::tuple{frac::Frac{1, 2}, "yo"sv}>);

// ==== reflect_constant agreement ====

static_assert(std::meta::reflect_constant(std::tuple{3, 'a'}) ==
              std::meta::reflect_constant(std::tuple{3, 'a'}));
static_assert(std::meta::reflect_constant(std::optional<frac::Frac>(frac::Frac{2, 4})) ==
              std::meta::reflect_constant(std::optional<frac::Frac>(frac::Frac{1, 2})));
static_assert(std::meta::is_structural_type(^^std::tuple<int, char>));
static_assert(std::meta::is_structural_type(^^std::string_view));
static_assert(std::meta::is_structural_type(^^std::optional<frac::Frac>));

// ==== chrono: duration / time_point ====

using namespace std::chrono_literals;

template <std::chrono::milliseconds D> constexpr auto dur() { return D.count(); }
static_assert(dur<5ms>() == 5);
static_assert(dur<5ms> == dur<5ms>);
static_assert(dur<5ms> != dur<6ms>);

template <std::chrono::sys_seconds TP> constexpr auto tp() {
  return TP.time_since_epoch().count();
}
static_assert(tp<std::chrono::sys_seconds(42s)>() == 42);
static_assert(tp<std::chrono::sys_seconds(42s)> ==
              tp<std::chrono::sys_seconds(42s)>);

// ==== chrono: calendar types ====

template <std::chrono::day D> constexpr unsigned cd() { return unsigned(D); }
static_assert(cd<std::chrono::day(4)>() == 4);
static_assert(cd<std::chrono::day(4)> == cd<std::chrono::day(4)>);

template <std::chrono::year_month_day YMD> constexpr int ymd() {
  return int(YMD.year()) * 10000 + unsigned(YMD.month()) * 100 +
         unsigned(YMD.day());
}
static_assert(ymd<std::chrono::year_month_day(std::chrono::year(2026),
                                              std::chrono::month(8),
                                              std::chrono::day(4))>() ==
              20260804);
static_assert(ymd<2026y / 8 / 4> == ymd<2026y / 8 / 4>);
static_assert(ymd<2026y / 8 / 4> != ymd<2026y / 8 / 5>);

template <std::chrono::weekday W> constexpr unsigned wd() {
  return W.c_encoding();
}
static_assert(wd<std::chrono::Tuesday>() == 2);

// ==== complex ====

template <std::complex<double> C> constexpr double cre() { return C.real(); }
static_assert(cre<std::complex<double>(1.5, 2.5)>() == 1.5);
static_assert(cre<std::complex<double>(1.5, 2.5)> ==
              cre<std::complex<double>(1.5, 2.5)>);
static_assert(cre<std::complex<double>(1.5, 2.5)> !=
              cre<std::complex<double>(1.5, 3.5)>);

template <std::complex<float> C> constexpr float cfre() { return C.real(); }
static_assert(cfre<std::complex<float>(1.0f, 2.0f)>() == 1.0f);

// ==== comparison categories ====

template <std::strong_ordering O> constexpr bool is_eq_v() {
  return O == std::strong_ordering::equal;
}
static_assert(is_eq_v<std::strong_ordering::equal>());
static_assert(!is_eq_v<std::strong_ordering::less>());
static_assert(is_eq_v<std::strong_ordering::less> ==
              is_eq_v<std::strong_ordering::less>);
static_assert(is_eq_v<std::strong_ordering::less> !=
              is_eq_v<std::strong_ordering::greater>);

template <std::partial_ordering O> constexpr bool is_unord() {
  return O == std::partial_ordering::unordered;
}
static_assert(is_unord<std::partial_ordering::unordered>());

// ==== bitset ====

template <std::bitset<130> B> constexpr bool bit(unsigned i) { return B[i]; }
static_assert(bit<std::bitset<130>(0b1010)>(1));
static_assert(!bit<std::bitset<130>(0b1010)>(0));
static_assert(bit<std::bitset<130>(0b1010)> == bit<std::bitset<130>(0b1010)>);
static_assert(bit<std::bitset<130>(0b1010)> != bit<std::bitset<130>(0b1011)>);

// Small bitset (single-word specialization) too.
template <std::bitset<8> B> constexpr unsigned long bval() {
  return B.to_ulong();
}
static_assert(bval<std::bitset<8>(0x5a)>() == 0x5a);

// ==== reference_wrapper ====

constexpr int refd = 17;
template <std::reference_wrapper<const int> R> constexpr int rw() {
  return R.get();
}
static_assert(rw<std::ref(refd)>() == 17);
static_assert(rw<std::ref(refd)> == rw<std::ref(refd)>);

// Identity: the wrapper's value is the address of the referent.
constexpr int refd2 = 17;
static_assert(rw<std::ref(refd)> != rw<std::ref(refd2)>);

// ==== batch-2 structural checks ====

static_assert(std::meta::is_structural_type(^^std::chrono::milliseconds));
static_assert(std::meta::is_structural_type(^^std::chrono::sys_seconds));
static_assert(std::meta::is_structural_type(^^std::chrono::year_month_day));
static_assert(std::meta::is_structural_type(^^std::complex<double>));
static_assert(std::meta::is_structural_type(^^std::strong_ordering));
static_assert(std::meta::is_structural_type(^^std::bitset<130>));
static_assert(std::meta::is_structural_type(^^std::reference_wrapper<const int>));

// check template parameter objects
template <auto V> inline constexpr auto const& ref = V;
template <char const* P> inline constexpr char const* lit = P;

static_assert(ref<"hello"sv>.data() == lit<"hello">);
static_assert(&ref<std::optional<int>(7)> == &[: std::meta::reflect_constant(std::optional<int>(7)) :]);

int main() { }
