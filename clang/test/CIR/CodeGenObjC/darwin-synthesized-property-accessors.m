// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fobjc-dispatch-method=mixed -fclangir -emit-llvm -x objective-c %s -o - | FileCheck %s

@interface NSObject
@end

@interface A : NSObject {
  int _ivar;
}
@property (assign) int ivar;
- (void)im0;
@end

@implementation A
@synthesize ivar = _ivar;

- (void)im0 {
}
@end

// CHECK-LABEL: @"_OBJC_$_INSTANCE_METHODS_A" = internal global
// CHECK-SAME: ptr @"\01-[A im0]"
// CHECK-SAME: ptr @"\01-[A ivar]"
// CHECK-SAME: ptr @"\01-[A setIvar:]"
// CHECK: @"_OBJC_CLASS_RO_$_A" = internal global
// CHECK-SAME: ptr @"_OBJC_$_INSTANCE_METHODS_A"
// CHECK-SAME: ptr @"_OBJC_$_PROP_LIST_A"
// CHECK-LABEL: define internal i32 @"\01-[A ivar]"(ptr noundef %0, ptr noundef %1)
// CHECK: load atomic i32,
// CHECK-LABEL: define internal void @"\01-[A setIvar:]"(ptr noundef %0, ptr noundef %1, i32 noundef %2)
// CHECK: store atomic i32
