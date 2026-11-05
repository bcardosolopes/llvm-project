// RUN: %clang_cc1 -triple x86_64-apple-macos -fclangir -emit-llvm -fobjc-arc -O2 -disable-llvm-passes -o - %s | FileCheck %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-apple-macos -emit-llvm -fobjc-arc -O2 -disable-llvm-passes -o - %s | FileCheck %s -check-prefix=OGCG

// Test that objc_precise_lifetime controls !clang.imprecise_release metadata.
// At -O2, the imprecise destroy path emits load + objc_release (with metadata),
// while the precise destroy path emits load + objc_release (without metadata).

#define PRECISE_LIFETIME __attribute__((objc_precise_lifetime))

id test_helper(void) __attribute__((ns_returns_retained));
void use(id);

// Test 1: Precise lifetime — no !clang.imprecise_release on release calls.
void test_precise(void) {
  PRECISE_LIFETIME id x = test_helper();
  use(x);
}
// LLVM-LABEL: @test_precise
// LLVM: call void @llvm.objc.release(ptr %{{.*}})
// LLVM-NOT: clang.imprecise_release
// LLVM: ret void
// OGCG-LABEL: @test_precise
// OGCG: call void @llvm.objc.release(ptr %{{.*}})
// OGCG-NOT: clang.imprecise_release
// OGCG: ret void

// Test 2: Imprecise lifetime (default) — release has !clang.imprecise_release.
void test_imprecise(void) {
  id x = test_helper();
  use(x);
}
// LLVM-LABEL: @test_imprecise
// LLVM: call void @llvm.objc.release(ptr %{{.*}}){{.*}}, !clang.imprecise_release
// LLVM: ret void
// OGCG-LABEL: @test_imprecise
// OGCG: call void @llvm.objc.release(ptr %{{.*}}){{.*}}, !clang.imprecise_release
// OGCG: ret void
