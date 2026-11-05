// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++14 -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++14 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Static local variables from enclosing functions must be emitted as
// internal globals with initializers (not external), and the enclosing
// function must be emitted. This tests the fix in CIRGenExpr.cpp where
// isStaticLocal() references use getOrCreateStaticVarDecl to trigger
// deferred emission, matching OGCG (CGExpr.cpp:3579-3583).

static inline auto deduced_return() {
  static int n = 42;
  struct S { int operator()() { return n; } };
  return S();
}

void call_deduced_return_operator() {
  deduced_return()();
}

// The static local must be internal with initializer, not external.
// LLVM-DAG: @_ZZL14deduced_returnvE1n = internal global i32 42
// OGCG-DAG: @_ZZL14deduced_returnvE1n = internal global i32 42

// The enclosing function must be emitted.
// LLVM-DAG: define internal {{.*}}void @_ZL14deduced_returnv()
// OGCG-DAG: define internal {{.*}}void @_ZL14deduced_returnv()

// The nested class operator() must be emitted.
// LLVM-DAG: define internal {{.*}}i32 @_ZZL14deduced_returnvEN1SclEv(
// OGCG-DAG: define internal {{.*}}i32 @_ZZL14deduced_returnvEN1SclEv(
