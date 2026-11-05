// RUN: %clang_cc1 -triple x86_64-unknown-freebsd -emit-llvm -fobjc-runtime=gnustep-1.8 -fclangir -o - %s | FileCheck %s

// This used to assert while synthesizing the property getter/setter because the
// old GNUstep ivar-offset helper in CIRGenFunction drifted from the expression
// path and tried to give a pointer-typed offset global an integer initializer.
@interface X
@property (retain) id iProp;
@end

@implementation X
@synthesize iProp;
@end

// CHECK: @__objc_ivar_offset_X.iProp = global ptr getelementptr (i8, ptr @.objc_ivar_list, i64 24), align 8
// CHECK: @.objc_method_list = internal global { ptr, i32, [2 x { ptr, ptr, ptr }] }
// CHECK: define internal ptr @_i_X__iProp(ptr noundef %0, ptr noundef %1)
// CHECK: load ptr, ptr @__objc_ivar_offset_X.iProp, align 8
// CHECK: define internal void @_i_X__setIProp_(ptr noundef %0, ptr noundef %1, ptr noundef %2)
// CHECK: load ptr, ptr @__objc_ivar_offset_X.iProp, align 8
