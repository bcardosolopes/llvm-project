//===----------------------------------------------------------------------===//
//
// Copyright 2026 Jump Trading, LLC
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// RUN: %clang_cc1 -std=c++2d -emit-pch %s -o %t
// RUN: %clang_cc1 -std=c++2d -include-pch %t -verify %s
// expected-no-diagnostics

// P4341: persisted constexpr allocations must survive PCH round-trips with
// their identity intact: a specialization on a pointer into a persistent
// allocation named in the PCH and again in the use TU must be the same
// specialization (deserialized PersistentAllocDecls register in the
// ASTContext identity map and merge with locally-minted ones).

#ifndef HEADER
#define HEADER

namespace std {
  template <class T>
  constexpr void mark_immutable_if_constexpr(T* p) {
    __builtin_mark_immutable_if_constexpr(
        const_cast<void*>(static_cast<const void*>(p)));
  }
}

template <class T>
struct uptr {
  T* p;
  constexpr uptr(T* p) : p(p) {}
  uptr(const uptr&) = delete;
  constexpr ~uptr() {
    if constexpr (__is_const(T))
      std::mark_immutable_if_constexpr(p);
    delete p;
  }
  constexpr T& operator*() const { return *p; }
};

inline constexpr uptr<int const> hp(new int(5));

template <int const* P> struct X { };
// Instantiate in the PCH.
using XInPCH = X<&*hp>;

#else

// Name the same specialization in the use TU: must be the same type, and the
// persisted contents must still be constant-readable.
using XInUse = X<&*hp>;
static_assert(__is_same(XInPCH, XInUse));
static_assert(*hp == 5);

#endif
