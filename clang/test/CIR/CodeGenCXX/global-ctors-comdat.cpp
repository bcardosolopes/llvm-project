// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++14 -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s

// CIR now generates individual comdat-associated entries for
// template static variable initializers in @llvm.global_ctors.

int foo();

template<typename T> struct A { static int a; };
template<typename T> int A<T>::a = foo();
template struct A<int>;

// CHECK: @llvm.global_ctors = appending global
// There should be more than 1 entry — at minimum an individual entry
// with comdat key @_ZN1AIiE1aE, plus the _GLOBAL__sub_I_ aggregate.
// CHECK-SAME: @_ZN1AIiE1aE
