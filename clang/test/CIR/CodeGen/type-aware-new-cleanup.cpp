// RUN: %clang_cc1 -triple x86_64-unknown-linux -std=c++26 -fclangir \
// RUN:   -fexceptions -fcxx-exceptions -Wno-ext-cxx-type-aware-allocators \
// RUN:   -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Test that type-aware operator delete receives std::type_identity in the
// exceptional cleanup path of a new-expression (constructor throws).

using size_t = __SIZE_TYPE__;

namespace std {
  template <class T> struct type_identity {
    typedef T type;
  };
  enum class align_val_t : size_t {};
}

template <typename T>
void *operator new(std::type_identity<T>, size_t, std::align_val_t);
template <typename T>
void operator delete(std::type_identity<T>, void *, size_t, std::align_val_t);

struct S {
  S();
  int i;
};

S::S() {
  throw 1;
}

// CHECK-LABEL: @_Z4testv
// CHECK: cir.call @_ZnwI1SEPvSt13type_identityIT_EmSt11align_val_t
// CHECK: cir.call exception @_ZN1SC1Ev
// CHECK: cir.call @_ZdlI1SEvSt13type_identityIT_EPvmSt11align_val_t
void test() {
  S *s = new S;
  (void)s;
}
