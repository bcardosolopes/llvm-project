// Test -fpass-by-value-is-noalias flag — noalias on indirect pass-by-value
// parameters of trivially copyable types.

// --- Test 1: With -fpass-by-value-is-noalias ---
// RUN: %clang_cc1 -fpass-by-value-is-noalias -triple arm64-apple-iphoneos \
// RUN:     -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=WITH
// RUN: %clang_cc1 -fpass-by-value-is-noalias -triple arm64-apple-iphoneos \
// RUN:     -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG_WITH

// --- Test 2: Without -fpass-by-value-is-noalias ---
// RUN: %clang_cc1 -triple arm64-apple-iphoneos -fclangir -emit-llvm %s -o %t.no.ll
// RUN: FileCheck --input-file=%t.no.ll %s --check-prefix=WITHOUT
// RUN: %clang_cc1 -triple arm64-apple-iphoneos -emit-llvm %s -o %t.no.ogcg.ll
// RUN: FileCheck --input-file=%t.no.ogcg.ll %s --check-prefix=OGCG_WITHOUT

// A trivial struct large enough to be passed indirectly on ARM64.
struct Foo {
  int a, b, c, d, e, f;
};

// Trivially copyable — gets noalias with the flag.
// WITH: define{{.*}} void @_Z4take3Foo(ptr dead_on_return noalias noundef
// OGCG_WITH: define{{.*}} void @_Z4take3Foo(ptr dead_on_return noalias noundef
// WITHOUT: define{{.*}} void @_Z4take3Foo(ptr dead_on_return noundef
// WITHOUT-NOT: noalias
// OGCG_WITHOUT: define{{.*}} void @_Z4take3Foo(ptr dead_on_return noundef
// OGCG_WITHOUT-NOT: noalias
void take(Foo arg) {}

// Non-trivially copyable — does NOT get noalias even with the flag.
struct Bar {
  int a, b, c, d, e, f;
  Bar(const Bar &);
};

// WITH: define{{.*}} void @_Z8take_bar3Bar(ptr dead_on_return noundef
// WITH-NOT: noalias
// OGCG_WITH: define{{.*}} void @_Z8take_bar3Bar(ptr dead_on_return noundef
// OGCG_WITH-NOT: noalias
void take_bar(Bar arg) {}
