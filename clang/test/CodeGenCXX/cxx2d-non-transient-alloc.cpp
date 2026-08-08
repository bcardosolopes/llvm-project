//===----------------------------------------------------------------------===//
//
// Copyright 2026 Jump Trading, LLC
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// RUN: %clang_cc1 -std=c++2d -triple x86_64-unknown-linux-gnu -emit-llvm -o - %s | FileCheck %s

// P4341: codegen for persisted constexpr allocations. Marked-immutable
// allocations are constant globals; unmarked ones are writable globals; the
// owning variable gets a true constant initializer (no dynamic init) and no
// runtime destructor.

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

// Immutable allocation: read-only storage.
constexpr uptr<int const> ro(new int(40));
// Mutable allocation: writable storage.
constexpr uptr<int> rw(new int(1));
// Cross-TU: external-linkage owner gets linkonce_odr + comdat allocations.
inline constexpr uptr<int const> shared(new int(9));

// Owning variables are constants pointing at the persisted allocations.
// CHECK-DAG: @_ZL2ro = internal constant %struct.uptr{{.*}} { ptr @_ZL2ro.__nta_0 }
// CHECK-DAG: @_ZL2rw = internal constant %struct.uptr{{.*}} { ptr @_ZL2rw.__nta_0 }

// Storage classes: marked -> constant, unmarked -> global (writable).
// CHECK-DAG: @_ZL2ro.__nta_0 = internal constant i32 40
// CHECK-DAG: @_ZL2rw.__nta_0 = internal global i32 1

// External-linkage owner: allocation unifies across TUs.
// CHECK-DAG: @shared.__nta_0 = linkonce_odr constant i32 9, comdat

// Template arguments mangle by (owner, index) identity.
template <int const* P> int f() { return *P; }
int use_template() { return f<&*shared>(); }
// CHECK-DAG: define {{.*}} @_Z1fIXcvPKiadL_Z17__nta__Z6shared_0EEEiv()

int use_all() {
  ++*rw; // writable at runtime
  return *ro + *rw + *shared;
}

// No dynamic initialization and no registered destructors.
// CHECK-NOT: __cxx_global_var_init
// CHECK-NOT: __cxa_atexit
// CHECK-NOT: call {{.*}} @_Znwm
