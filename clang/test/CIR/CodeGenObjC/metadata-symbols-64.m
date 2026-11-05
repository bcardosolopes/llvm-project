// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fclangir -emit-llvm -x objective-c %s -o - | FileCheck %s

__attribute__((objc_root_class))
@interface A
+ (void)fm0;
- (void)im0;
@end

@implementation A
+ (void)fm0 {}
- (void)im0 {}
@end

@interface A (Cat)
+ (void)fm1;
- (void)im1;
@end

@implementation A (Cat)
+ (void)fm1 {}
- (void)im1 {}
@end

// CHECK-DAG: @"_OBJC_$_CATEGORY_INSTANCE_METHODS_A_$_Cat" = internal global
// CHECK-DAG: @"_OBJC_$_CATEGORY_CLASS_METHODS_A_$_Cat" = internal global
// CHECK: @"_OBJC_$_CATEGORY_A_$_Cat" = internal global {{.*}} ptr @"OBJC_CLASS_$_A", ptr @"_OBJC_$_CATEGORY_INSTANCE_METHODS_A_$_Cat", ptr @"_OBJC_$_CATEGORY_CLASS_METHODS_A_$_Cat"
// CHECK-NOT: @"OBJC_LABEL_STUB_CATEGORY_$"
// CHECK: @"OBJC_LABEL_CATEGORY_$" = private global [1 x ptr] [ptr @"_OBJC_$_CATEGORY_A_$_Cat"], section "__DATA,__objc_catlist,regular,no_dead_strip", align 8
// CHECK: @llvm.compiler.used = appending global
// CHECK-DAG: ptr @"_OBJC_$_CATEGORY_INSTANCE_METHODS_A_$_Cat"
// CHECK-DAG: ptr @"_OBJC_$_CATEGORY_CLASS_METHODS_A_$_Cat"
// CHECK-DAG: ptr @"_OBJC_$_CATEGORY_A_$_Cat"
// CHECK-DAG: ptr @"OBJC_LABEL_CATEGORY_$"
