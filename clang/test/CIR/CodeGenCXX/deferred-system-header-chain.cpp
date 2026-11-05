// RUN: split-file %s %t
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:   -emit-llvm -isystem %t %t/main.cpp -o - | FileCheck %s

// Regression coverage follow-up:
// When deferred system-header functions are emitted in a batch, later
// functions in the batch may introduce new references to earlier declaration-
// only symbols. Ensure the whole dependency chain is emitted.

//--- hdr.h
#pragma once

namespace sys {

template <class S>
struct Pos {
  Pos(long long);
  operator long long() const { return off; }
  long long off;
};

template <class S>
inline Pos<S>::Pos(long long x) : off(x) {}

template <class C>
struct Traits {
  using pos_type = Pos<int>;
  using off_type = long long;
};

template <class C, class T>
struct Buf {
  using pos_type = typename T::pos_type;
  using off_type = typename T::off_type;
  virtual pos_type seekoff(off_type off) { return pos_type(off); }
};

} // namespace sys

//--- main.cpp
#include <hdr.h>

long long use() {
  sys::Buf<char, sys::Traits<char>> b;
  return b.seekoff(7);
}

// CHECK: define linkonce_odr void @_ZN3sys3PosIiEC2Ex
// CHECK: define linkonce_odr void @_ZN3sys3PosIiEC1Ex
// CHECK: define linkonce_odr i64 @_ZN3sys3BufIcNS_6TraitsIcEEE7seekoffEx
// CHECK: call void @_ZN3sys3PosIiEC1Ex
