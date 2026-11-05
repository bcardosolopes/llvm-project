// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// nullptr_t is represented as ptr in LLVM IR, but unlike regular pointer
// types, it must not get noundef. Match OGCG DetermineNoUndef behavior.

typedef decltype(nullptr) nullptr_t;
typedef int (*arrptr)[32];

arrptr ret_arrptr() { return nullptr; }
nullptr_t ret_npt() { return nullptr; }
void pass_npt(nullptr_t p) {}
int *ret_iptr() { return nullptr; }

// CIR: define dso_local noundef ptr @_Z10ret_arrptrv()
// CIR: define dso_local ptr @_Z7ret_nptv()
// CIR: define dso_local void @_Z8pass_nptDn(ptr %
// CIR: define dso_local noundef ptr @_Z8ret_iptrv()

// OGCG: define dso_local noundef ptr @_Z10ret_arrptrv()
// OGCG: define dso_local ptr @_Z7ret_nptv()
// OGCG: define dso_local void @_Z8pass_nptDn(ptr %
// OGCG: define dso_local noundef ptr @_Z8ret_iptrv()
