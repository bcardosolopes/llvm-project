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
// This test exercises the same cases under both consteval-only models, to make
// the difference between them observable:
//   * default: the consteval-only *value* model (reflections are consteval-only
//     values and may not escape to runtime).
//   * -fconsteval-operations: the consteval-only *operations* model (reflections
//     persist to runtime as empty values; only their operations are consteval).
//
// RUN: %clang_cc1 %s -std=c++26 -freflection -verify=value \
// RUN:   -verify-ignore-unexpected=note
// RUN: %clang_cc1 %s -std=c++26 -freflection -fconsteval-operations -verify=ops \
// RUN:   -verify-ignore-unexpected=note

using info = decltype(^^int);

// Representation: in the value model reflections never reach runtime, so the
// type keeps a wide handle; the operations model lowers it to a 1-byte empty.
#if __has_feature(consteval_operations)
static_assert(sizeof(info) == 1);
#else
static_assert(sizeof(info) == 16);
#endif


                  // ===================================
                  // reflections escaping to runtime
                  // ===================================

// Value model: ill-formed (consteval-only value). Operations model: ok.
info g = ^^int;
// value-error@-1 {{expressions involving consteval-only values are only allowed in constant-evaluated contexts}}

info fn() { return ^^int; }
// value-error@-1 {{expressions involving consteval-only values}}

void persist() {
  info r = ^^int;
  // value-error@-1 {{expressions involving consteval-only values}}
  (void)r;
}


                  // ===================================
                  // comparing reflections at runtime
                  // ===================================

// Value model: silently compiles a runtime comparison (the "hole" the
// operations model closes). Operations model: == is consteval, so this requires
// constant operands and is ill-formed here.
bool eq(info a, info b) { return a == b; }
// ops-error@-1 {{comparison of reflections must be a constant expression}}


                  // ===================================
                  // constant contexts: identical both ways
                  // ===================================

constexpr info cr = ^^int;
static_assert(cr == ^^int);

consteval bool ceq(info a, info b) { return a == b; }
static_assert(ceq(^^int, ^^int));
static_assert(!ceq(^^int, ^^void));
