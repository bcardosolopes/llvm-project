// RUN: %clang_cc1 -triple x86_64-unknown-freebsd -emit-llvm -fobjc-runtime=gnustep-1.5 -fclangir -o - %s | FileCheck %s

@protocol P;

@interface I <P>
@end

@implementation I
@end

// CHECK: @.objc_protocol = internal global
// CHECK: @.objc_protocol_list = internal global { ptr, i64, [0 x ptr] } zeroinitializer, align 8
// CHECK: @.objc_protocol_list.1 = internal global { ptr, i64, [1 x ptr] } { ptr null, i64 1, [1 x ptr] [ptr @.objc_protocol] }, align 8
// CHECK: @_OBJC_CLASS_I = global
// CHECK-SAME: ptr @.objc_protocol_list.1
// CHECK: @.objc_protocol_list.2 = internal global { ptr, i64, [0 x ptr] } zeroinitializer, align 8
// CHECK: @.objc_protocol_holder_category = internal global { ptr, ptr, ptr, ptr, ptr } { ptr @.objc_category_name, ptr @.objc_category_class_name, ptr null, ptr null, ptr @.objc_protocol_list.2 }, align 8
