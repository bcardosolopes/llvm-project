// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck %s < %t.ll

int foo();

template <class T> struct A {
  static int x;
};

template <class T> int A<T>::x = foo();
template struct A<int>;

// The guard variable must exist
// CHECK: @_ZGVN1AIiE1xE = weak_odr global i64 0, comdat($_ZN1AIiE1xE)

// The init function should check the guard
// CHECK: define internal void @__cxx_global_var_init{{.*}}comdat
// CHECK: load {{.*}} @_ZGVN1AIiE1xE
// CHECK: icmp eq
// CHECK: store {{.*}} @_ZGVN1AIiE1xE
