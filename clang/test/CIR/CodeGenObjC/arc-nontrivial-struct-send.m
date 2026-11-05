// RUN: %clang_cc1 -triple arm64e-apple-ios15.0.0-bc -fobjc-arc -disable-llvm-passes -fclangir -emit-llvm -o - %s | FileCheck %s

@protocol NSObject
@end

@interface NSObject <NSObject>
@end

@interface WidgetTester : NSObject
@end

typedef struct {
  NSObject *impl;
} widget_t;

@implementation WidgetTester

- (_Bool)withWidget:(widget_t)widget {
  return 0;
}

- (_Bool)testWidget:(widget_t)widget {
  return [self withWidget:widget];
}

@end

// CHECK-LABEL: define{{.*}} i1 @{{.*}}withWidget:
// CHECK: call void @__destructor_8_s0(ptr noundef

// CHECK-LABEL: define linkonce_odr hidden void @__destructor_8_s0(
// CHECK: call void @llvm.objc.storeStrong(ptr
//
// CHECK-LABEL: define{{.*}} i1 @{{.*}}testWidget
// CHECK: call ptr @llvm.objc.retain(ptr
// CHECK: call zeroext i1 @objc_msgSend
// CHECK: call void @__destructor_8_s0(ptr noundef
