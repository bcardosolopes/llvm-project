// RUN: %clang_cc1 -triple aarch64-none-linux-android26 -fclangir -emit-llvm %s -o - | FileCheck %s

// Simulate a system-header template that returns a user-defined class by value
// and is explicitly instantiated in this TU. The inline constructor carries an
// ABI tag and must not be left as an unresolved call.
# 1 "sys.hpp" 1 3
struct Mb { int x; };

template <class S>
struct FPos {
  S st;
  long long off;
  __attribute__((visibility("hidden"), exclude_from_explicit_instantiation,
                 abi_tag("fn190106")))
  FPos(long long o = 0) : st(), off(o) {}
};

template <class C>
struct Traits {
  using pos_type = FPos<Mb>;
  using off_type = long long;
};

template <class C, class Tr>
struct BasicStreambuf {
  using pos_type = typename Tr::pos_type;
  using off_type = typename Tr::off_type;
  virtual pos_type seekoff(off_type, int, unsigned = 0) {
    return pos_type(off_type(-1));
  }
  virtual pos_type seekpos(pos_type, unsigned = 0) {
    return pos_type(off_type(-1));
  }
};
# 2 "test.cpp" 2

template class BasicStreambuf<char, Traits<char>>;

// CHECK: define weak_odr [2 x i64] @_ZN14BasicStreambufIc6TraitsIcEE7seekoffExij
// CHECK: call void @_ZN4FPosI2MbEC1B8fn190106Ex
// CHECK: define linkonce_odr hidden void @_ZN4FPosI2MbEC2B8fn190106Ex
// CHECK-NOT: declare hidden void @_ZN4FPosI2MbEC2B8fn190106Ex
