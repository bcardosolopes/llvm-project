// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Verify that x86_64 ABI classification correctly handles MethodType
// (pointer-to-member-function). In the Itanium ABI, a member function
// pointer is {fnptr_t, ptrdiff_t} = 16 bytes, classified as Lo=Integer,
// Hi=Integer and passed as two i64 values.

struct S {
  void foo(bool);
};

typedef void (S::*MethodPtr)(bool);

// A standalone member function pointer argument should be passed as
// two i64 values (Lo=Integer, Hi=Integer).
// CIR: define dso_local void @_Z11take_methodM1SFvbE(i64 %{{.*}}, i64 %{{.*}})
// OGCG: define dso_local void @_Z11take_methodM1SFvbE(i64 %{{.*}}, i64 %{{.*}})
void take_method(MethodPtr m) {}

// Returning a member function pointer should also use two i64 values.
// CIR: define dso_local { i64, i64 } @_Z13return_methodv()
// OGCG: define dso_local { i64, i64 } @_Z13return_methodv()
MethodPtr return_method() {
  return &S::foo;
}
