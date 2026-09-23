//===----------------------------------------------------------------------===//
//
// Copyright 2026 Jump Trading, LLC
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// RUN: %clang_cc1 %s -std=c++26 -freflection -Wunused-local-typedef -verify

// A reflect-expression counts as a use of its operand.
void fn() {
  using Used = int;
  constexpr auto r = ^^Used;
  (void)r;

  using Unused = int;  // expected-warning {{unused type alias 'Unused'}}
}
