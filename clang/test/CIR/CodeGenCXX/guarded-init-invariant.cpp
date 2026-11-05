// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -O1 -disable-llvm-passes %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -O1 -disable-llvm-passes %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// After guarded initialization of constant-storage globals,
// emit llvm.invariant.start to tell the optimizer the value is immutable.

extern int foo();

template <typename T> struct S {
  static const int arr[];
};

template <typename T> const int S<T>::arr[] = { 1, 2, foo() };

// Force instantiation.
template struct S<int>;

// CHECK: call ptr @llvm.invariant.start.p0(i64 12, ptr @_ZN1SIiE3arrE)
// OGCG: call ptr @llvm.invariant.start.p0(i64 12, ptr @_ZN1SIiE3arrE)

int use_arr_0() {
  return S<int>::arr[0];
}
// CHECK-LABEL: define{{.*}} i32 @_Z9use_arr_0v
// CHECK: load i32, ptr @_ZN1SIiE3arrE
