// RUN: %clang_cc1 -triple x86_64-unknown-freebsd -emit-llvm -fno-use-init-array -fobjc-runtime=gnustep-1.8 -fclangir -o - %s | FileCheck %s

@protocol P
@property int i;
@end

// CHECK: @.objc_protocol = internal global { ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr }
// CHECK-DAG: [[HOLDERLIST:@\.objc_protocol_list\.[0-9]+]] = internal global { ptr, i64, [1 x ptr] } { ptr null, i64 1, [1 x ptr] [ptr @.objc_protocol] }, align 8
// CHECK: @.objc_protocol_holder_category = internal global { ptr, ptr, ptr, ptr, ptr } { ptr @.objc_category_name, ptr @.objc_category_class_name, ptr null, ptr null, ptr [[HOLDERLIST]] }, align 8
