// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG

// Verify that nomerge is propagated to call-site attributes, matching OGCG.
// Two sources: __attribute__((nomerge)) on the declaration, and
// [[clang::nomerge]] as a statement attribute.

void bar();

__attribute__((nomerge)) void nomerge_func();

void test_decl_nomerge() {
  nomerge_func();
  // LLVM: call void @_Z12nomerge_funcv() #[[NOMERGE:[0-9]+]]
  // OGCG: call void @_Z12nomerge_funcv() #[[NOMERGE:[0-9]+]]
}

void test_stmt_nomerge() {
  [[clang::nomerge]] bar();
  // LLVM: call void @_Z3barv() #[[NOMERGE]]
  // OGCG: call void @_Z3barv() #[[NOMERGE]]
}

void test_no_nomerge() {
  bar();
  // LLVM: call void @_Z3barv(){{$}}
  // OGCG: call void @_Z3barv(){{$}}
}

// LLVM: attributes #[[NOMERGE]] = { nomerge }
// OGCG: attributes #[[NOMERGE]] = { nomerge }
