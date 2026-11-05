// RUN: %clang_cc1 -triple aarch64-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Regression test: member function pointer conversion operators that return
// ABI-coerced types (e.g. [2 x i64] on AArch64 for {i64, i64}) within a
// scope (ExprWithCleanups) previously caused a PHI node type mismatch crash.
// The call return [2 x i64] must be coerced back to {i64, i64} before being
// yielded from the scope.

struct S {
  ~S();
  typedef void (S::*MemFnPtr)();
  operator MemFnPtr() const;
};

void test_with_dtor() {
  S::MemFnPtr fp = S();
}

// Both CIR and OGCG should coerce [2 x i64] call result to {i64, i64}
// via store/load through memory.
// CHECK-LABEL: define {{.*}} @_Z14test_with_dtorv()
// CHECK: call [2 x i64] @_ZNK1ScvMS_FvvEEv
// CHECK: store [2 x i64]
// CHECK: load { i64, i64 }
// CHECK: call void @_ZN1SD1Ev
// CHECK: ret void

// OGCG-LABEL: define {{.*}} @_Z14test_with_dtorv()
// OGCG: call [2 x i64] @_ZNK1ScvMS_FvvEEv
// OGCG: store [2 x i64]
// OGCG: load { i64, i64 }
// OGCG: call void @_ZN1SD1Ev
// OGCG: ret void

// Template conversion operator variant.
template<typename T> using id = T;
struct T {
  template<typename U, typename V>
    operator id<U (V::*)()>() const;
};
void test_template() {
  char (T::*fp)() = T();
}

// CHECK-LABEL: define {{.*}} @_Z13test_templatev()
// CHECK: call [2 x i64] @_ZNK1TcvMT0_FT_vEIcS_EEv
// CHECK: store [2 x i64]
// CHECK: load { i64, i64 }
// CHECK: ret void

// OGCG-LABEL: define {{.*}} @_Z13test_templatev()
// OGCG: call [2 x i64] @_ZNK1TcvMT0_FT_vEIcS_EEv
// OGCG: store [2 x i64]
// OGCG: load { i64, i64 }
// OGCG: ret void
