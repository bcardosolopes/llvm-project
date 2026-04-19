//===----------------------------------------------------------------------===//
//
// Copyright 2025 Bloomberg Finance L.P.
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// RUN: %clang_cc1 %s -std=c++26 -freflection -verify

using info = decltype(^^int);
struct S { info m = {}; };

                                 // ===========
                                 // valid_cases
                                 // ===========

namespace valid_cases {
constexpr info r1 = ^^int;
static constexpr info r2 = ^^int;

constexpr S s1;
constexpr S s2{};
constexpr S s3 = {^^int};

constexpr info *p1 = nullptr;
constexpr const info *p2 = &r1;

void fn1() { static constexpr info r = ^^int; }
void fn2() { extern info r; }
consteval info cfn1() { return ^^int; }
consteval void cfn2() { (void) static_cast<const void *>(p2); }
consteval info cfn3() { return info(); }

}  // namespace valid_cases


                           // ======================
                           // non_consteval_contexts
                           // ======================

namespace non_consteval_contexts {
info r1;  // ok
info r2 {};  // ok
info r3 = ^^int;  // expected-error {{constant-evaluated contexts}}
info r4 = valid_cases::cfn1();  // expected-error {{constant-evaluated contexts}}
info r5 = valid_cases::cfn3(); // ok
unsigned sz = sizeof(^^int);  // ok

S s1;  // ok
S s2{};  // ok
S s3 = {^^int};  // expected-error {{constant-evaluated contexts}}

const info *p1; // ok (null)
const info *p2 = &valid_cases::r1;
// expected-error@-1 {{expressions involving consteval-only values are only allowed in constant-evaluated contexts}}

info fn1() { return ^^int; }
// expected-error@-1 {{expressions involving consteval-only values}}

info fn2() { return valid_cases::cfn1(); }
// expected-error@-1 {{expressions involving consteval-only values}}

void fn3() { (void) valid_cases::r1; }
// expected-error@-1 {{expressions involving consteval-only values}}

void fn4() { (void) valid_cases::s1.m; } // ok

void fn5() { (void) static_cast<const void *>(valid_cases::p2); }
// expected-error@-1 {{expressions}}

void fn6() { (void) [:^^valid_cases::r1:]; }
// expected-error@-1 {{expressions involving consteval-only values}}

void fn7() {
  (void) info{}; // ok
  (void) ^^int; // expected-error {{expressions involving consteval-only values}}
  (void) new info{}; // ok
  (void) new info(^^int); // expected-error {{expressions involving consteval-only values}}
}

consteval bool is_null(info R) {
  return R == info{} ? 1 : 0;
}

template <info R>
void fn() {
  constexpr auto S = R;
  (void) is_null(S);
}

}  // namespace non_consteval_contexts


                            // ====================
                            // immediate_escalation
                            // ====================

namespace immediate_escalation {
void fn() {
  [] { info r1; }();
  [] { info r2 {}; }();
  [] { info r3 = ^^int; }();
  [] { info r4 = valid_cases::cfn1(); }();

  [] { S s1; }();
  [] { S s2{}; }();
  [] { S s3 = {^^int}; }();

  [] { const info *p1; }();
  [] { const info *p2 = &valid_cases::r1; }();

  (void) [] { return ^^int; }();
  (void) [] { return valid_cases::cfn1(); }();
  [] { (void) valid_cases::r1; }();
  [] { (void) valid_cases::s1.m; }();
  [] { (void) static_cast<const void *>(valid_cases::p2); }();
  [] { (void) [:^^valid_cases::r1:]; }();

  [] { (void) info{}; }();
  [] { (void) ^^int; }();
  [] { (void) new info{}; }();
}

}  // namespace immediate_escalation


                               // ===============
                               // alias_smuggling
                               // ===============

namespace alias_smuggling {
struct Base { };
struct Derived : Base {
  info k;
  consteval Derived() : Base(), k(^^int) {}
};
consteval const Base &fn1() {
  static constexpr Derived d;
  return d;
}
constexpr auto &ref = fn1(); // implicitly consteval

consteval auto &ref2 = fn1(); // ok (consteval)

consteval void *fn2() {
  static constexpr auto v = ^^int;
  return (void *)&v;
}
constexpr const void *ptr = fn2(); // implicitly consteval
consteval const void *ptr2 = fn2(); // ok (consteval)

}  // namespace alias_smuggling
