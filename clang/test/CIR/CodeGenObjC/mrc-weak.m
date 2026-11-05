// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fobjc-runtime=macosx-10.10 -emit-llvm -fobjc-weak -disable-llvm-passes -fclangir -o - %s | FileCheck %s

@interface Object
- (instancetype)retain;
@end

@interface Foo : Object
@end

void test_load_weak(__weak id *p) {
  id y = *p;
}

// CHECK-LABEL: define{{.*}} void @test_load_weak(
// CHECK: %[[P:.*]] = load ptr, ptr %{{.*}}
// CHECK: %[[WEAK:.*]] = call ptr @llvm.objc.loadWeak(ptr %[[P]])
// CHECK: %[[PHI:.*]] = phi ptr [ %[[WEAK]], %{{.*}} ]
// CHECK-NEXT: store ptr %[[PHI]], ptr %{{.*}}
// CHECK-NOT: @objc_msgSend

void test_load_weak_retained(__weak id *p) {
  id y = [*p retain];
}

// CHECK-LABEL: define{{.*}} void @test_load_weak_retained(
// CHECK: %[[P:.*]] = load ptr, ptr %{{.*}}
// CHECK: %[[WEAK:.*]] = call ptr @llvm.objc.loadWeakRetained(ptr %[[P]])
// CHECK: %[[PHI:.*]] = phi ptr [ %[[WEAK]], %{{.*}} ]
// CHECK-NEXT: store ptr %[[PHI]], ptr %{{.*}}
// CHECK-NOT: @objc_msgSend

void test_typed_load_weak_retained(__weak Foo **p) {
  Foo *y = [*p retain];
}

// CHECK-LABEL: define{{.*}} void @test_typed_load_weak_retained(
// CHECK: %[[P:.*]] = load ptr, ptr %{{.*}}
// CHECK: %[[WEAK:.*]] = call ptr @llvm.objc.loadWeakRetained(ptr %[[P]])
// CHECK: %[[PHI:.*]] = phi ptr [ %[[WEAK]], %{{.*}} ]
// CHECK-NEXT: store ptr %[[PHI]], ptr %{{.*}}
// CHECK-NOT: @objc_msgSend
