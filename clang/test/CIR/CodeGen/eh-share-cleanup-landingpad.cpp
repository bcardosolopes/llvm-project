// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions -fclangir -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=OGCG

struct D {
  ~D();
};

void may_throw();

void test_shared_lpad_cleanup() {
  try {
    D d;
    may_throw();
    may_throw();
  } catch (...) {
  }
}

// LLVM-LABEL: define dso_local void @_Z24test_shared_lpad_cleanupv(
// LLVM: invoke void @_Z9may_throwv()
// LLVM: invoke void @_Z9may_throwv()
// LLVM: landingpad { ptr, i32 }
// LLVM: call void @_ZN1DD1Ev(
// LLVM-NOT: landingpad { ptr, i32 }

// OGCG-LABEL: define dso_local void @_Z24test_shared_lpad_cleanupv(
// OGCG: invoke void @_Z9may_throwv()
// OGCG: invoke void @_Z9may_throwv()
// OGCG: landingpad { ptr, i32 }
// OGCG: call void @_ZN1DD1Ev(
// OGCG-NOT: landingpad { ptr, i32 }
