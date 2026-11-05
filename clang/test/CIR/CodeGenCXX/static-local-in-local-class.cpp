// RUN: %clang_cc1 -triple x86_64-linux -fclangir -emit-llvm -std=c++14 %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-linux -emit-llvm -std=c++14 %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

// When a local class method references a static local variable from
// its enclosing function, and only the local class method is used (not the
// enclosing function), CIR must still emit the enclosing function's
// definition so the static local variable gets initialized.

static auto deduced_return() {
  static int n = 42;
  struct S { int *operator()() { return &n; } };
  return S();
}
extern "C" int call_deduced_return_operator() {
  return *decltype(deduced_return())()();
}

// The static local must be internal with initializer 42, not external.
// LLVM-DAG: @_ZZL14deduced_returnvE1n = internal global i32 42
// OGCG-DAG: @_ZZL14deduced_returnvE1n = internal global i32 42

// The enclosing function must be emitted (even though never directly called).
// LLVM-DAG: define internal void @_ZL14deduced_returnv()
// OGCG-DAG: define internal void @_ZL14deduced_returnv()

// S::operator() must be emitted.
// LLVM-DAG: define internal {{.*}}ptr @_ZZL14deduced_returnvEN1SclEv(
// OGCG-DAG: define internal {{.*}}ptr @_ZZL14deduced_returnvEN1SclEv(
