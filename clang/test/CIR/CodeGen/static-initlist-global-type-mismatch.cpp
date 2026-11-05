// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:   -emit-cir -o %t.cir %s
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:   -emit-llvm -o %t.ll %s
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s

// Regression test: Address assertion failure when a static local
// initializer_list's backing array global has a constant initializer whose
// type differs from convertTypeForMem (e.g. anonymous struct vs named pair).
// The fix inserts a pointer bitcast (withElementType) to reconcile the types.

namespace std {
  typedef decltype(sizeof(int)) size_t;
  template <class _E>
  class initializer_list {
    const _E* __begin_;
    size_t    __size_;
    initializer_list(const _E* __b, size_t __s)
      : __begin_(__b), __size_(__s) {}
  public:
    initializer_list() : __begin_(nullptr), __size_(0) {}
    size_t size() const { return __size_; }
    const _E* begin() const { return __begin_; }
    const _E* end() const { return __begin_ + __size_; }
  };
}

template <class T1, class T2> struct pair {
  T1 first;
  T2 second;
  constexpr pair() : first(), second() {}
  constexpr pair(T1 a, T2 b) : first(a), second(b) {}
};

enum E { ENUM_CONSTANT };

// The key scenario: a static local initializer_list with a constant
// initializer. The constant emitter produces an anonymous struct type for
// the backing array, but convertTypeForMem produces the named pair type.
// CIR must insert a bitcast to reconcile them.

// CIR-LABEL: @_Z4testv
// CIR: cir.get_global @_ZGRZ4testvE1p_
// CIR: cir.cast bitcast

// LLVM-LABEL: define {{.*}}@_Z4testv
void test() {
  static std::initializer_list<pair<const char *, E>> a, p[2] =
      {a, {{"", ENUM_CONSTANT}}};
}
