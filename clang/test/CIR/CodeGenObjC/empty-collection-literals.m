// RUN: %clang_cc1 -I %S/../../CodeGenObjC/Inputs -triple x86_64-apple-macosx10.10.0 -fobjc-runtime=macosx-10.10.0 -emit-llvm -fblocks -fobjc-arc -fobjc-runtime-has-weak -fclangir -disable-llvm-passes -o - %s | FileCheck -check-prefix=CHECK-WITHOUT-EMPTY-COLLECTIONS %s
// RUN: %clang_cc1 -I %S/../../CodeGenObjC/Inputs -triple x86_64-apple-macosx10.11.0 -fobjc-runtime=macosx-10.11.0 -emit-llvm -fblocks -fobjc-arc -fobjc-runtime-has-weak -fclangir -disable-llvm-passes -o - %s | FileCheck -check-prefix=CHECK-WITH-EMPTY-COLLECTIONS %s
// RUN: %clang_cc1 -I %S/../../CodeGenObjC/Inputs -triple arm64-apple-ios8.0 -fobjc-runtime=ios-8.0 -emit-llvm -fblocks -fobjc-arc -fobjc-runtime-has-weak -fclangir -disable-llvm-passes -o - %s | FileCheck -check-prefix=CHECK-WITHOUT-EMPTY-COLLECTIONS %s
// RUN: %clang_cc1 -I %S/../../CodeGenObjC/Inputs -triple arm64-apple-ios9.0 -fobjc-runtime=ios-9.0 -emit-llvm -fblocks -fobjc-arc -fobjc-runtime-has-weak -fclangir -disable-llvm-passes -o - %s | FileCheck -check-prefix=CHECK-WITH-EMPTY-COLLECTIONS %s

#include "literal-support.h"

void test_empty_array(void) {
  // CHECK-WITHOUT-EMPTY-COLLECTIONS-LABEL: define{{.*}} void @test_empty_array
  // CHECK-WITHOUT-EMPTY-COLLECTIONS-NOT: @__NSArray0__
  // CHECK-WITHOUT-EMPTY-COLLECTIONS: call ptr @objc_msgSend(ptr noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}}, i64 noundef 0)
  // CHECK-WITHOUT-EMPTY-COLLECTIONS: call ptr @llvm.objc.retainAutoreleasedReturnValue(ptr %{{.*}})
  // CHECK-WITHOUT-EMPTY-COLLECTIONS: call void @llvm.objc.storeStrong
  // CHECK-WITHOUT-EMPTY-COLLECTIONS: ret void

  // CHECK-WITH-EMPTY-COLLECTIONS-LABEL: define{{.*}} void @test_empty_array
  // CHECK-WITH-EMPTY-COLLECTIONS-NOT: @objc_msgSend
  // CHECK-WITH-EMPTY-COLLECTIONS: %[[ARRAY0:.*]] = load ptr, ptr @__NSArray0__, align 8, !invariant.load ![[MD:[0-9]+]]
  // CHECK-WITH-EMPTY-COLLECTIONS: call ptr @llvm.objc.retain(ptr %[[ARRAY0]])
  // CHECK-WITH-EMPTY-COLLECTIONS: call void @llvm.objc.storeStrong
  // CHECK-WITH-EMPTY-COLLECTIONS: ret void
  NSArray *arr = @[];
}

void test_empty_dictionary(void) {
  // CHECK-WITHOUT-EMPTY-COLLECTIONS-LABEL: define{{.*}} void @test_empty_dictionary
  // CHECK-WITHOUT-EMPTY-COLLECTIONS-NOT: @__NSDictionary0__
  // CHECK-WITHOUT-EMPTY-COLLECTIONS: call ptr @objc_msgSend(ptr noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}}, i64 noundef 0)
  // CHECK-WITHOUT-EMPTY-COLLECTIONS: call ptr @llvm.objc.retainAutoreleasedReturnValue(ptr %{{.*}})
  // CHECK-WITHOUT-EMPTY-COLLECTIONS: call void @llvm.objc.storeStrong
  // CHECK-WITHOUT-EMPTY-COLLECTIONS: ret void

  // CHECK-WITH-EMPTY-COLLECTIONS-LABEL: define{{.*}} void @test_empty_dictionary
  // CHECK-WITH-EMPTY-COLLECTIONS-NOT: @objc_msgSend
  // CHECK-WITH-EMPTY-COLLECTIONS: %[[DICT0:.*]] = load ptr, ptr @__NSDictionary0__, align 8, !invariant.load ![[MD]]
  // CHECK-WITH-EMPTY-COLLECTIONS: call ptr @llvm.objc.retain(ptr %[[DICT0]])
  // CHECK-WITH-EMPTY-COLLECTIONS: call void @llvm.objc.storeStrong
  // CHECK-WITH-EMPTY-COLLECTIONS: ret void
  NSDictionary *dict = @{};
}
