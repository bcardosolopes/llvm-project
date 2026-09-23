//===----------------------------------------------------------------------===//
//
// Copyright 2024 Bloomberg Finance L.P.
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// RUN: %clang_cc1 %s -std=c++26 -freflection -Wno-unused-value -verify
//
// Also check a target where size_t is 32 bits; the expansion index is a
// size_t-typed template argument and must be built at the target's width.
// RUN: %clang_cc1 %s -triple i686-linux-gnu -std=c++26 -freflection -Wno-unused-value -verify


                          // ================
                          // basic_expansions
                          // ================

struct empty_t {};
struct struct_t { int i, j, k; };

template <int... Vs>
struct rng_t {
  int vs[sizeof...(Vs)] = {Vs...};
};

template <int... Vs>
constexpr const int *begin(const rng_t<Vs...> &r) { return &r.vs[0]; }
template <int... Vs>
constexpr const int *end(const rng_t<Vs...> &r) { return &r.vs[sizeof...(Vs)]; }

namespace basic_expansions {

void runtime_fn(auto) {}
consteval void compiletime_fn(auto) {}

int run_tests() {
  // empty expansions are legal, but should not expand.
  template for (auto e : {}) static_assert(false);
  template for (constexpr auto e : {}) static_assert(false);
  template for (int init = 0; auto e : {}) static_assert(false);
  template for (constexpr auto init = 0; auto e : {}) static_assert(false);

  template for (int arr[0]; auto e : arr) static_assert(false);
  template for (constexpr int arr[0] {};
                constexpr auto e : arr) static_assert(false);
  template for (empty_t empty; auto e : empty) static_assert(false);
  template for (constexpr empty_t empty;
                constexpr auto e : empty) static_assert(false);

  template for (auto e : rng_t<>{}) static_assert(false);
  template for (constexpr auto e : rng_t<>{}) static_assert(false);
  template for (rng_t<> r; auto e : r) static_assert(false);
  template for (constexpr rng_t<> r; auto e : r) static_assert(false);

  // basic expansions with runtime expansion variables.
  template for (auto e : {1, 'a', false})
    runtime_fn(e);
  template for (int init = 0; int e : {1, 2, 3})
    runtime_fn((init++) + e);
  template for (int arr[] = {1, 2, 3}; auto e : arr)
    runtime_fn(e);
  template for (struct_t s = {1, 2, 3}; auto e : s)
    runtime_fn(e);
  template for (rng_t<1, 2, 3> r; auto e : r)
    runtime_fn(e);

  // basic expansions with constexpr expansion variables.
  template for (constexpr auto e : {1, 'a', false})
    compiletime_fn(e);
  template for (int init = 0; constexpr int e : {1, 2, 3})
    runtime_fn(e + init);
  template for (constexpr int arr[] = {1, 2, 3}; constexpr auto e : {1, 2, 3})
    runtime_fn(e);
  template for (constexpr struct_t s = {1, 2, 3}; constexpr auto e : s)
    runtime_fn(e);
  template for (constexpr struct_t s = {1, 2, 3}; constexpr auto e : s)
    compiletime_fn(e);
  template for (constexpr rng_t<1, 2, 3> r; constexpr auto e : r)
    compiletime_fn(e);

  return 0;
}
}  // namespace basic_expansions

                           // =======================
                           // in_constant_evaluations
                           // =======================

namespace in_constant_evaluations {

consteval int fn() {
  int result = 0;

  template for (result = 5; int e : {1, 3, 5, 7, 9})
    result += e;
  template for (int arr[] = {1, 3, 5}; int e : arr)
    result += e;
  template for (struct_t s = {1, 3, 5}; int e : s)
    result += e;
  template for (rng_t<1, 2, 3> r; int e : r)
    result += e;

  return result;
}
static_assert(fn() == 54);
}  // namespace in_constant_evaluations

                              // =================
                              // initializer_lists
                              // =================

namespace initializer_lists {

struct S { int first, second; };

consteval S fn() {
    S result = {0, 0};
    template for (S s : {{1, 2}, {.first=2, .second=3}}) {
        result.first += s.first;
        result.second += s.second;
    }
    return result;
}
constexpr S s = fn();
static_assert(s.first == 3);
static_assert(s.second == 5);
}  // namespace initializer_lists

                       // ==============================
                       // dependent_expansion_init_lists
                       // ==============================

namespace dependent_expansion_init_lists {

template <typename T>
struct S {
  static constexpr int dependent_type_fn() {
    int result = 0;
    template for (using Alias = T; Alias e : {1, 2, 3})
      result += e;
    return result;
  }

  template <T V>
  static constexpr int dependent_element_in_list_of_known_size_fn() {
    int result = 0;
    template for (auto e : {1, V, 2})
      result += e;
    return result;
  }

  template <auto... Vs>
  static constexpr int dependent_pack_fn() {
    int result = 0;
    template for (auto e : {1, Vs..., 100})
      result += static_cast<int>(e);
    return result;
  }

  static constexpr int dependent_type_parm_var_fn(auto Base) {
    int result = 0;
    template for (auto e : {1, 2, 3})
      result += e * Base;
    return result;
  }
};

static_assert(S<int>::dependent_type_fn() == 6);
static_assert(S<int>::dependent_element_in_list_of_known_size_fn<10>() == 13);
static_assert(S<int>::dependent_pack_fn<1, 2.0f, 3l>() == 107);
static_assert(S<int>::dependent_type_parm_var_fn(10) == 60);

void run_tests() {
  (void) S<int>::dependent_type_fn();
  (void) S<int>::dependent_element_in_list_of_known_size_fn<0>();
  (void) S<int>::dependent_pack_fn<1, 2.0f, 3l>();
  (void) S<int>::dependent_type_parm_var_fn(10);
}
}  // namespace dependent_expansion_init_lists

                     // ===================================
                     // dependent_expansion_destructurables
                     // ===================================

namespace dependent_expansion_destructurables {

template <typename T>
struct S {
  template <T V>
  static constexpr int dependent_value_fn() {
    int result = 0;
    template for (auto e : V)
      result += e;
    return result;
  }

  template <T... Vs>
  static constexpr int dependent_arr_fn() {
    int result = 0;
    template for (T arr[] = {Vs...}; auto e : arr)
      result += e;
    return result;
  }

  static constexpr int dependent_sum_from_param(T v) {
    int result = 0;
    template for (auto e : v)
      result += e;
    return result;
  }
};

constexpr struct_t s {1, 2, 3};
static_assert(S<struct_t>::template dependent_value_fn<s>() == 6);
static_assert(S<int>::template dependent_arr_fn<1, 3, 5>() == 9);
static_assert(S<struct_t>::dependent_sum_from_param(s) == 6);

void run_tests() {
  (void) S<struct_t>::template dependent_value_fn<s>();
  (void) S<int>::template dependent_arr_fn<1, 3, 5>();
  (void) S<struct_t>::dependent_sum_from_param(s);
}
}  // namespace dependent_expansion_destructurables

                        // =============================
                        // dependent_expansion_iterables
                        // =============================

namespace dependent_expansion_iterables {

template <typename T>
struct S {
  template <T V>
  static constexpr int dependent_value_fn() {
    int result = 0;
    template for (auto e : V)
      result += e;
    return result;
  }

  static constexpr int dependent_sum_from_param(T v) {
    int result = 0;
    template for (auto e : v)
      result += e;
    return result;
  }
};

constexpr rng_t<1, 2, 3> r;
static_assert(S<rng_t<1, 2, 3>>::template dependent_value_fn<r>() == 6);
static_assert(S<rng_t<1, 2, 3>>::dependent_sum_from_param(r) == 6);

}  // namespace dependent_expansion_iterables

                         // ===========================
                         // nested_expansion_statements
                         // ===========================

namespace nested_expansion_statements {

template <auto... Vs>
constexpr int nested_expansion_init_lists() {
  int result = 0;
  template for (auto r : {1, 2, 3, 4})
    template for (auto s : {Vs...})
      result += r * s;
  return result;
}
static_assert(nested_expansion_init_lists<4, 3, 2, 1>() == 100);

template <struct_t s>
consteval int nested_expansion_destructurables() {
  int result = 0;
  template for (auto e : s)
    template for (int arr[] = {2, 4, 6}; auto f : arr)
      result += e * f;
  return result;
}
constexpr struct_t s {1, 3, 5};
static_assert(nested_expansion_destructurables<s>() == 108);

template <auto R>
consteval int nested_expansion_iterables() {
  int result = 0;
  template for (auto e : {1, 2, 3, 4})
    template for (auto f : R)
      result += e * f;
  return result;
}
constexpr rng_t<1, 2, 3> r;
static_assert(nested_expansion_iterables<r>() == 60);

void run_tests() {
  (void) nested_expansion_init_lists<4, 3, 2, 1>();
  (void) nested_expansion_destructurables<s>();
  (void) nested_expansion_iterables<r>();
}

}  // namespace nested_expansion_statements

                             // ==================
                             // break_and_continue
                             // ==================

namespace break_and_continue {

constexpr int fn() {
  int result = 0;
  template for (auto r : {1, 2, 3, 4, 5}) {
    if (r % 2 == 0) continue;
    template for (auto s : {5, 4, 3, 2, 1}) {
      if (s < r) break;
      result += (r * s);
    }
  }
  return result;
}
static_assert(fn() == 76);

void run_tests() {
  (void) fn();
}

}  // namespace break_and_continue

                         // ===========================
                         // destructurable_by_reference
                         // ===========================

namespace destructurable_by_reference {

consteval int fn() {
  struct S { int a; int b; int c; };

  S s = {1, 2, 3};
  template for (auto m : s) m *= 2;
  template for (auto &m : s) m *= 2;

  return s.a + s.b + s.c;
}
static_assert(fn() == 12);
}  // namespace destructurable_by_reference

                          // ========================
                          // paper_example_regression
                          // ========================

namespace paper_example_regression {
consteval int f(auto const&... Containers) {
  int result = 0;
  template for (auto const& c : {Containers...}) {
    result += c[0];
  }
  return result;
}
constexpr int c1[] = {1, 2, 3};
constexpr int c2[] = {4, 3, 2, 1};
static_assert(f(c1, c2) == 5);

}  // namespace paper_example_regression

                          // ========================
                          // issue_209_regression
                          // ========================
constexpr bool testIssue209() {
    static constexpr int arr[]{1, 2, 3};
    [n = 42]<class = void>() {
        template for (constexpr auto _ : arr) {
            (void)n;
        }
    }();
    return true;
}

static_assert(testIssue209());

// classes inside of expansion statements are still local
auto some_function() -> void {
  template for (constexpr int I : {0}) {
    struct S {
      static constexpr int value = 42; // expected-error {{not allowed in local struct}}
    };
  }
}

// Issue handling captures within expansion statements
constexpr auto lambda_capture_in_expansion1(int i) -> int {
    template for (constexpr int I : {0, 1, 2}) {
        [&]{ i += I; }();
        [&](int v){ i += I * v; }(I);
        [&](auto v){ i += I * v; }(I);
        i += []{ return I; }();
    }
    return i;
}

constexpr auto lambda_capture_in_expansion2(auto i) -> int {
    template for (constexpr int I : {0, 1, 2}) {
        [&]{ i += I; }();
        [&](int v){ i += I * v; }(I);
        [&](auto v){ i += I * v; }(I);
        i += []{ return I; }();
    }
    return i;
}

static_assert(lambda_capture_in_expansion1(1) == 17);
static_assert(lambda_capture_in_expansion2(1) == 17);

namespace deep_const_range {

// Test a range where both non-const begin() and const begin() are viable.
// This catches the bug where the range variable was typed without const,
// causing overload resolution to select the deleted non-const begin().
template <int... Vs>
struct deep_const_rng {
  int vs[sizeof...(Vs)] = {Vs...};

  constexpr int *begin() { return &vs[0]; }
  constexpr int *end() { return &vs[sizeof...(Vs)]; }

  constexpr const int *begin() const { return &vs[0]; }
  constexpr const int *end() const { return &vs[sizeof...(Vs)]; }
};

template <int... Vs>
consteval int sum_const_required_range() {
  int result = 0;
  template for (constexpr auto e : deep_const_rng<Vs...>{})
    result += e;
  return result;
}
static_assert(sum_const_required_range<1, 2, 3, 4, 5>() == 15);

}  // namespace deep_const_range

using info = decltype(^^::);

consteval auto process(info) -> int {
  return 42;
}

auto parse_options() -> void {
  template for (constexpr auto dm : {^^int}) {
    auto lam = [](int x){
      constexpr int p = process(dm);
      return x + p;
    };
    (void)lam;
  }
}

// An expansion statement must be a range-based for; the classic three-part
// form used to crash the parser (the caller unconditionally cast the result
// to CXXExpansionStmt).
namespace not_range_for {
void classic_form() {
  template for (int i = 0; i < 3; ++i) { // expected-error {{expansion statement must be a range-based 'for' statement}}
    (void)i;
  }
}
void degenerate_form() {
  template for (;;) { } // expected-error {{expansion statement must be a range-based 'for' statement}}
}
}  // namespace not_range_for

// A structured binding pack declared inside the body of an expansion
// statement is re-instantiated once per iteration, and references to it must
// resolve to that iteration's bindings. Declarations in an expansion
// statement's body live in an ExpansionStmtDecl rather than in the enclosing
// function, so they used to be missed by the search for local instantiations
// and left dependent, which then reached CodeGen.
namespace binding_pack_in_expansion {
struct triple { int a, b, c; };

constexpr int sum(auto... vs) { return (0 + ... + vs); }

// Non-template function: nothing outside the expansion statement is dependent.
constexpr int non_template() {
  int total = 0;
  template for (constexpr int I : {0, 1, 2}) {
    auto [...xs] = triple{I, I + 1, I + 2};
    total += sum(xs...);
  }
  return total;
}
static_assert(non_template() == 18);

// A pack declared outside the statement and indexed inside it, alongside one
// declared inside it: both the enclosing instantiation and the per-iteration
// one have to be resolved.
template <class T>
constexpr int outer_and_inner(T t) {
  auto &[...outer] = t;
  int total = 0;
  template for (constexpr int I : {0, 1}) {
    auto [...inner] = triple{I, I + 1, I + 2};
    total += outer...[I] * sum(inner...);
  }
  return total;
}
static_assert(outer_and_inner(triple{4, 7, 9}) == 54);

}  // namespace binding_pack_in_expansion

// A constexpr binding pack declared inside an expansion statement, used as the
// index of a pack declared outside it. This is the shape that reaches the pack
// expansion machinery with a binding whose size is only known per-iteration.
using size_t = decltype(sizeof(0));

namespace indexed_by_inner_pack {
template <size_t... Is> struct indices {};

template <size_t I, size_t... Is>
constexpr size_t get(indices<Is...>) { return Is...[I]; }
}  // namespace indexed_by_inner_pack

namespace std {
template <class> struct tuple_size;
template <size_t, class> struct tuple_element;

template <size_t... Is>
struct tuple_size<::indexed_by_inner_pack::indices<Is...>> {
  static constexpr size_t value = sizeof...(Is);
};
template <class T> struct tuple_size<const T> : tuple_size<T> {};
template <size_t I, size_t... Is>
struct tuple_element<I, ::indexed_by_inner_pack::indices<Is...>> {
  using type = size_t;
};
template <size_t I, class T>
struct tuple_element<I, const T> : tuple_element<I, T> {};
}  // namespace std

namespace indexed_by_inner_pack {
template <class T>
constexpr int by_inner_pack(T t) {
  auto &[...outer] = t;
  int total = 0;
  template for (constexpr int I : {0, 1}) {
    constexpr auto [...js] = indices<0, 1>{};
    total += binding_pack_in_expansion::sum(outer...[I + js]...);
  }
  return total;
}
static_assert(by_inner_pack(binding_pack_in_expansion::triple{4, 7, 9}) == 27);
}  // namespace indexed_by_inner_pack
