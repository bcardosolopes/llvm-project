// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// CIR must respect tail-padding in memcpy for copy/move constructors
// with virtual bases. When a base subobject's tail padding is occupied by a
// virtual base, the memcpy size must use getDataSize() (excluding tail padding)
// instead of getSize() (including tail padding). PR36992.

namespace Implicit {
  struct A { char c; A(const A&); };
  struct B { int n; char c[3]; ~B(); };
  struct C : B, virtual A {};
  C f(C c) { return c; }
}

// The move constructor for C should copy 7 bytes (B's data size),
// not 8 (B's full size), to avoid overwriting virtual base A.
// LLVM: define {{.*}} @_ZN8Implicit1CC1EOS0_
// LLVM: call void @llvm.memcpy{{.*}}i64 7
// OGCG: define {{.*}} @_ZN8Implicit1CC1EOS0_
// OGCG: call void @llvm.memcpy{{.*}}i64 7
