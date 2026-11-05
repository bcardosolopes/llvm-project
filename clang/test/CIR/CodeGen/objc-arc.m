// RUN: %clang_cc1 -triple x86_64-apple-macos -fclangir -emit-llvm -fobjc-arc %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-apple-macos -emit-llvm -fobjc-arc %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// Comprehensive ObjC ARC test with OGCG cross-check.
// Tests retain, release (via storeStrong), nounwind, and multiple ARC ops.

void use(id);

// Test 1: objc_retain intrinsic on return value
id test_retain(id obj) {
  return obj;
}
// LLVM-LABEL: @test_retain
// LLVM: call ptr @llvm.objc.retain
// OGCG-LABEL: @test_retain
// OGCG: call ptr @llvm.objc.retain

// Test 2: objc_storeStrong for release at scope exit
void test_release(id obj) {
  id local = obj;
  use(local);
}
// LLVM-LABEL: @test_release
// LLVM: call void @llvm.objc.storeStrong(ptr %{{.*}}, ptr null)
// OGCG-LABEL: @test_release
// OGCG: call void @llvm.objc.storeStrong(ptr %{{.*}}, ptr null)

// Test 3: Multiple ARC ops with correct cleanup ordering
void test_multi(id a, id b) {
  id x = a;
  id y = b;
  use(x);
  use(y);
}
// Both storeStrong(null) calls should be present for cleanup
// LLVM-LABEL: @test_multi
// LLVM: call void @llvm.objc.storeStrong
// LLVM: call void @llvm.objc.storeStrong
// OGCG-LABEL: @test_multi
// OGCG: call void @llvm.objc.storeStrong
// OGCG: call void @llvm.objc.storeStrong

// Test 4: nounwind attribute on ARC intrinsic declarations
// LLVM: attributes {{.*}}nounwind
// OGCG: attributes {{.*}}nounwind
