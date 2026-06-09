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
// RUN: %clang_cc1 %s -std=c++26 -freflection -fconsteval-operations -verify

// In the consteval-only *operations* model, reflections persist to runtime but
// equality on them (==, !=) is an immediate (consteval) operation: it must form
// a constant expression, escalating an enclosing immediate-escalating context
// or being diagnosed otherwise.

using info = decltype(^^int);

info gr = ^^int;  // ok: reflections persist to runtime


                    // =====================================
                    // constant_operands_ok (folds at runtime)
                    // =====================================

namespace constant_operands_ok {
bool a = (^^int == ^^int);
bool b = (^^int != ^^void);
static_assert(^^int == ^^int);

consteval bool eq(info x, info y) { return x == y; }
static_assert(eq(^^int, ^^int));

constexpr bool c = (^^int == ^^int);
}  // namespace constant_operands_ok


                  // =========================================
                  // nonconstant_operands_error (runtime scope)
                  // =========================================

namespace nonconstant_operands_error {
info r = ^^int;

bool b1 = (r == ^^int);
// expected-error@-1 {{comparison of reflections must be a constant expression}}
// expected-note@-2 {{read of non-constexpr variable 'r' is not allowed in a constant expression}}
// expected-note@44 {{declared here}}

void fn(info p) {
  bool b2 = (p == ^^int);
  // expected-error@-1 {{comparison of reflections must be a constant expression}}
  // expected-note@-2 {{function parameter 'p' with unknown value cannot be used in a constant expression}}
  // expected-note@51 {{declared here}}
}

}  // namespace nonconstant_operands_error


                           // ====================
                           // immediate_escalation
                           // ====================

namespace immediate_escalation {
// A non-constant comparison inside an immediate-escalating context (a lambda)
// escalates it to consteval rather than being an error. The escalated lambda is
// then usable from a constant-evaluated context.
constexpr auto cmp = [](info x, info y) { return x == y; };
static_assert(cmp(^^int, ^^int));
static_assert(!cmp(^^int, ^^void));

}  // namespace immediate_escalation
