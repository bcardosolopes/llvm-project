// RUN: %clang_cc1 -triple x86_64-unknown-linux -std=c++26 -fclangir \
// RUN:   -Wno-ext-cxx-type-aware-allocators -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux -std=c++26 -fclangir \
// RUN:   -Wno-ext-cxx-type-aware-allocators -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

// Test that type-aware allocation (C++26 std::type_identity) works
// for both new and delete expressions.

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
  int i = 0;
};

void test() {
  S *s = new S;
  delete s;
}

// CIR-LABEL: @_Z4testv
// CIR: cir.call @_ZnwI1SEPvSt13type_identityIT_EmSt11align_val_t
// CIR: cir.call @_ZdlI1SEvSt13type_identityIT_EPvmSt11align_val_t

// LLVM-LABEL: define dso_local void @_Z4testv
// LLVM: call noundef ptr @_ZnwI1SEPvSt13type_identityIT_EmSt11align_val_t(i64 noundef 4, i64 noundef 4)
// LLVM: call void @_ZdlI1SEvSt13type_identityIT_EPvmSt11align_val_t(ptr noundef %{{.+}}, i64 noundef 4, i64 noundef 4)
