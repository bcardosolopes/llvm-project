// REQUIRES: x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -fclangir %s -o %t.cir.ll -fno-strict-return
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll -fno-strict-return
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

struct Trivial { int x; };
struct NonTrivial { int x; ~NonTrivial(); };

// With -fno-strict-return, trivially-copyable return types should NOT
// get noundef on the return (MayDropFunctionReturn returns true).

// CIR: define {{.*}}i32 @_Z10int_returnv()
// CIR-NOT: noundef

// OGCG: define {{.*}}i32 @_Z10int_returnv()
// OGCG-NOT: noundef
int int_return() { return 42; }
