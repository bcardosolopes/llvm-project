// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// Test that reference temporaries for hidden-visibility template static
// data members get hidden visibility.

template <class T>
struct __attribute__((visibility("hidden"))) HiddenClass {
  static const int &ref;
};
template <class T>
const int &HiddenClass<T>::ref = 42;
template struct HiddenClass<void>;

// The reference temporary should have hidden visibility.
// LLVM: @_ZGRN11HiddenClassIvE3refE_ = weak_odr hidden global i32 42
// OGCG: @_ZGRN11HiddenClassIvE3refE_ = weak_odr hidden global i32 42
