// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fobjc-arc -fobjc-runtime-has-weak -fblocks -disable-llvm-passes -fclangir -o - %s | FileCheck %s --check-prefix=CHECK --check-prefix=CHECK-O0
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fobjc-arc -fobjc-runtime-has-weak -fblocks -O2 -disable-llvm-passes -fclangir -o - %s | FileCheck %s --check-prefix=CHECK --check-prefix=CHECK-O2

extern id test49_helper(void);

void test49(void) {
  __autoreleasing id x = x = test49_helper();
}

// CHECK-LABEL: define{{.*}} void @test49()
// CHECK: %[[X:.*]] = alloca ptr
// CHECK: store ptr null, ptr %[[X]]
// CHECK-O0: %[[CALL:.*]] = call ptr @test49_helper()
// CHECK-O0: %[[RETAINED:.*]] = call ptr @llvm.objc.retainAutoreleasedReturnValue(ptr %[[CALL]])
// CHECK-O0: %[[AR:.*]] = call ptr @llvm.objc.autorelease(ptr %[[RETAINED]])
// CHECK-O0: store ptr %[[AR]], ptr %[[X]]
// CHECK-O0: %[[RA:.*]] = call ptr @llvm.objc.retainAutorelease(ptr %[[AR]])
// CHECK-O0: store ptr %[[RA]], ptr %[[X]]
// CHECK-O2: %[[CALL:.*]] = call ptr @test49_helper() [ "clang.arc.attachedcall"(ptr @llvm.objc.retainAutoreleasedReturnValue) ]
// CHECK-O2: call void (...) @llvm.objc.clang.arc.noop.use(ptr noundef %[[CALL]])
// CHECK-O2: %[[AR:.*]] = call ptr @llvm.objc.autorelease(ptr %[[CALL]])
// CHECK-O2: store ptr %[[AR]], ptr %[[X]]
// CHECK-O2: %[[RA:.*]] = call ptr @llvm.objc.retainAutorelease(ptr %[[AR]])
// CHECK-O2: store ptr %[[RA]], ptr %[[X]]
