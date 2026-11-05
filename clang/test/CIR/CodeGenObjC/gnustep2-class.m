// RUN: %clang_cc1 -triple x86_64-unknown-freebsd -fobjc-runtime=gnustep-2.0 -fclangir -emit-llvm -x objective-c %s -o - | FileCheck %s

@interface Super @end

@interface X : Super
{
  int ivar1;
  id ivar2;
}
@property (readonly) int x;
@property id y;
@end

@implementation X
@synthesize y;

- (int)x { return 12; }
+ (int)clsMeth { return 42; }
- (id)meth { return ivar2; }
@end

// CHECK-DAG: @"__objc_ivar_offset_X.ivar2.\01" = global i32 8, align 4
// CHECK-DAG: @"__objc_ivar_offset_X.y.\01" = hidden global i32 16, align 4
// CHECK: @.objc_method_list = internal global { ptr, i32, i64, [4 x { ptr, ptr, ptr }] }
// CHECK-SAME: ptr @_i_X__y
// CHECK-SAME: ptr @_i_X__setY_
// CHECK: @.objc_ivar_list = private global { i32, i64, [3 x { ptr, ptr, ptr, i32, i32 }] }
// CHECK-SAME: @__objc_ivar_offset_X.ivar1.i
// CHECK-SAME: @"__objc_ivar_offset_X.ivar2.\01"
// CHECK-SAME: @"__objc_ivar_offset_X.y.\01"
// CHECK: private unnamed_addr constant [5 x i8] c"Ti,R\00", align 1
// CHECK: private unnamed_addr constant [6 x i8] c"T@,Vy\00", align 1
// CHECK: @.objc_property_list = internal global { i32, i32, ptr, [2 x { ptr, ptr, ptr, ptr, ptr }] } { i32 2, i32 40, ptr null,
// CHECK: @._OBJC_METACLASS_X = internal global
// CHECK: @._OBJC_CLASS_Super = external global ptr
// CHECK: @._OBJC_CLASS_X = global { ptr, ptr, ptr, i64, i64, i64, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, i64, ptr }
// CHECK-SAME: ptr @._OBJC_METACLASS_X
// CHECK-SAME: ptr @._OBJC_CLASS_Super
// CHECK-SAME: ptr @.objc_property_list
// CHECK: @._OBJC_REF_CLASS_X = global ptr @._OBJC_CLASS_X, section "__objc_class_refs", align 8
// CHECK: define internal ptr @_i_X__y(ptr noundef %0, ptr noundef %1)
// CHECK:   load atomic {{.*}} unordered
// CHECK: define internal void @_i_X__setY_(ptr noundef %0, ptr noundef %1, ptr noundef %2)
// CHECK:   store atomic {{.*}} unordered
