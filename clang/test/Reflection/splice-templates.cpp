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
// RUN: %clang_cc1 %s -std=c++26 -freflection -verify

using info = decltype(^^int);


template <typename T1, typename T2>
constexpr bool is_same_v = false;

template <typename T1>
constexpr bool is_same_v<T1, T1> = true;

                                 // ===========
                                 // idempotency
                                 // ===========

// Check idempotency of the splice operator composed with reflection.
namespace idempotency {
template <typename T> struct TCls {};
template <typename T> using TAlias = TCls<T>;
template <typename T> void TFn() {}
template <typename T> static constexpr int TVar = 0;
template <typename T> concept Concept = requires { requires true; };
struct S {
  template <typename T> struct TInner {};
  template <typename T> void TFn();
  template <typename T> static constexpr int TMemVar = 0;
};

static_assert(is_same_v<typename [:^^TCls:]<int>, TCls<int>>);
static_assert(is_same_v<typename [:^^TAlias:]<int>, TCls<int>>);
static_assert(&template [:^^TFn:]<int> == &TFn<int>);
static_assert(&template [:^^TVar:]<int> == &TVar<int>);
static_assert(is_same_v<typename [:^^S::TInner:]<int>, S::TInner<int>>);
static_assert(&template [:^^S::TFn:]<int> == &S::TFn<int>);
static_assert(&template [:^^S::TMemVar:]<int> == &S::TMemVar<int>);
}  // namespace idempotency

                                // =============
                                // non_dependent
                                // =============

namespace non_dependent {
template <typename T> struct TCls {
  T value;
  static constexpr T zero = 0;
  using type = T;
  template <typename U> constexpr U tmemfn(U v) const { return v; }
  template <typename U> static constexpr int tsmem = 13;
};
template <typename T> TCls(T) -> TCls<T>;

template <typename T> using TAlias = TCls<T>;
template <typename T> consteval int TFn(T value) { return value; }
template <int Value> constexpr int TVar = Value;
template <int Value> concept IsOdd = requires { requires Value % 2 == 1; };

constexpr [:^^TCls:]<int> obj1 = {1};
static_assert(obj1.value == 1);
constexpr [:^^TAlias:]<int> obj2 = {2};
static_assert(obj2.value == 2);

constexpr [:^^TCls:] obj3 = {3};
static_assert(obj3.value == 3);

constexpr [:^^TAlias:] obj4 = {4};
static_assert(obj4.value == 4);

static_assert(template [:^^TCls:]<int>::zero == 0);
static_assert(is_same_v<typename [:^^TCls:]<int>::type, int>);
static_assert(template [:^^TAlias:]<int>::zero == 0);
static_assert(is_same_v<typename [:^^TAlias:]<int>::type, int>);

static_assert(template [:^^TFn:]<char>('a') == 'a');
static_assert(template [:^^TFn:]<>('b') == 'b');
static_assert(template [:^^TFn:]('c') == 'c');
static_assert(template [:^^TVar:]<41> == 41);

static_assert(template [:^^IsOdd:]<13>);
static_assert(!template [:^^IsOdd:]<10>);

static_assert(obj1.template [:^^TCls<int>::tmemfn:]<int>(4) == 4);
static_assert(obj1.template [:^^TCls<int>::tmemfn:]<>(3) == 3);
static_assert(obj1.template [:^^TCls<int>::tmemfn:](2) == 2);
static_assert(((&obj1)->*(&template [:^^TCls<int>::tmemfn:]<int>))(1) == 1);

static_assert(template [:^^TCls<int>::tsmem:]<int> == 13);
static_assert(obj1.template [:^^TCls<int>::tsmem:]<int> == 13);
static_assert((&obj1)->template [:^^TCls<int>::tsmem:]<int> == 13);

// TODO(P2996): Test splicing concepts as type constraints.
}  // namespace non_dependent

namespace dependent {
template <info R> consteval auto DepTClsFn(int value) {
  typename [:R:]<int> obj = {value};
  return obj;
}
template <info R> consteval auto DepTClsStaticMember() {
  typename [:R:]<int>::type result = template [:R:]<int>::zero;
  return result;
}
template <info R> consteval auto DepTClsCTAD(int value) {
  typename [:R:] obj = {value};
  return obj;
}
template <info R> consteval int DepTFnFn(int value) {
  return template [:R:]<int>(value) + template [:R:]<>(value) +
         template [:R:](value);
}
template <info R, int Value> consteval int DepTVarFn() {
  return template [:R:]<Value>;
}
template <info R> consteval bool DepConceptFn() {
  return template [:R:]<13>;
}
template <info R>
consteval int DepTMemFn(const non_dependent::TCls<int> &self, int v) {
  return self.template [:R:]<int>(v) + self.template [:R:]<>(v) +
         self.template [:R:](v);
}
template <info R>
consteval info TMemFnWithDepScope() { return ^^[:R:]::template tmemfn; }

static_assert(DepTClsFn<^^non_dependent::TCls>(11).value == 11);
static_assert(DepTClsFn<^^non_dependent::TAlias>(12).value == 12);
static_assert(DepTClsCTAD<^^non_dependent::TCls>(13).value == 13);
static_assert(DepTClsCTAD<^^non_dependent::TAlias>(15).value == 15);
static_assert(DepTClsStaticMember<^^non_dependent::TCls>() == 0);
static_assert(DepTClsStaticMember<^^non_dependent::TAlias>() == 0);
static_assert(DepTFnFn<^^non_dependent::TFn>(3) == 9);
static_assert(DepTVarFn<^^non_dependent::TVar, 15>() == 15);
static_assert(DepConceptFn<^^non_dependent::IsOdd>());

constexpr non_dependent::TCls<int> obj1 = {1};
static_assert(DepTMemFn<^^non_dependent::TCls<int>::tmemfn>(obj1, 4) == 12);

static_assert(obj1.template [:TMemFnWithDepScope<^^decltype(obj1)>():](3) == 3);

// TODO(P2996): Test splicing concepts as type constraints.
}  // namespace dependent

                        // ============================
                        // less_than_operator_ambiguity
                        // ============================

namespace less_than_operator_ambiguity {
constexpr int x = 3;
static_assert([:^^x:] < 5);  // make sure this parses.
}  // namespace less_than_operator_ambiguity

                           // =======================
                           // silly_typename_template
                           // =======================

namespace silly_typename_template {
template <info R>
consteval auto tfn() {
  return typename template [:R:]<int>::Ty{}.value;
}

template <typename T>
struct TCls {
  struct Ty {
    int value = 14;
  };
};

static_assert(tfn<^^TCls>() == 14);
}  // namespace silly_typename_template

                               // ==============
                               // tomasz_example
                               // ==============

namespace tomasz_example {
template<template<class> class X>
struct S {
  typename [:^^X:]<int, float> m;
    // expected-error@-1 {{too many template arguments}}
};

template<class> struct V1 {};  // expected-note {{template is declared here}}
template<class, class = int> struct V2 {};

S<V1> s1;  // expected-note {{in instantiation of template class}}
S<V2> s2;
}  // namespace tomasz_example

// =============
// barry_example
// =============

namespace barry_example {
template <int...> struct V {};

template <template <int> class X>
struct S {
  X<0> x;
  [: ^^X :]<1> y;
};

S<V> s;
}  // namespace barry_example
                        // ==================================
                        // constant_operand_dependent_arguments
                        // ==================================

// A splice of a constant template reflection with dependent template
// arguments designates a dependent specialization of a *known* template. It
// must behave exactly like the spelled name would: be deducible in a partial
// specialization, be distinct from the same form over a different template,
// and be usable as a nested-name-specifier in a dependent context.
namespace constant_operand_dependent_arguments {
template <typename T> struct W { using type = T; static constexpr int n = 1; };
template <typename T> struct Z { using type = T*; static constexpr int n = 2; };
constexpr info RW = ^^W;
constexpr info RZ = ^^Z;

template <typename T> struct trait { static constexpr int v = 0; };
template <typename... Ts> struct trait<typename [:RW:]<Ts...>> {
  static constexpr int v = 1;
};
template <typename... Ts> struct trait<typename [:RZ:]<Ts...>> {
  static constexpr int v = 2;
};
static_assert(trait<int>::v == 0);
static_assert(trait<W<int>>::v == 1);
static_assert(trait<Z<int>>::v == 2);

// Partial ordering: the splice form is exactly as specialized as the name.
template <typename T> struct ordered { static constexpr int v = 0; };
template <typename T> requires (sizeof(T) > 0)
struct ordered<T> { static constexpr int v = 1; };
template <typename... Ts> struct ordered<typename [:RW:]<Ts...>> {
  static constexpr int v = 2;
};
static_assert(ordered<W<int>>::v == 2);

// Nested-name-specifier forms.
template <typename... Ts>
constexpr int n_of = template [:RW:]<Ts...>::n;
static_assert(n_of<int> == 1);

template <typename... Ts>
using type_of_t = typename [:RZ:]<Ts...>::type;
static_assert(is_same_v<type_of_t<int>, int*>);

template <typename T>
constexpr int fn() { return template [:RW:]<T>::n + template [:RZ:]<T>::n; }
static_assert(fn<char>() == 3);

// The operand becomes constant only after the outer template is
// instantiated, while the member template's arguments remain dependent.
template <info R>
struct outer {
  template <typename... Ts>
  static constexpr int n() { return template [:R:]<Ts...>::n; }
  template <typename... Ts>
  using type = typename [:R:]<Ts...>::type;
  template <typename... Ts>
  static constexpr bool same = is_same_v<typename [:R:]<Ts...>, W<Ts...>>;
};
static_assert(outer<RW>::n<int>() == 1);
static_assert(outer<RZ>::n<int>() == 2);
static_assert(is_same_v<outer<RZ>::type<int>, int*>);
static_assert(outer<RW>::same<int>);
static_assert(!outer<RZ>::same<int>);
}  // namespace constant_operand_dependent_arguments

                                 // ===========
                                 // error_cases
                                 // ===========

namespace error_cases {
template [:^^Nonexistent:]<3>::type u;
  // expected-error@-1 {{use of undeclared identifier 'Nonexistent'}} \
  // expected-error@-1 {{expected unqualified-id}}

template [:^^non_dependent::TCls:] t = {1};
  // expected-error@-1 {{expected unqualified-id}}
}  // namespace error_cases
