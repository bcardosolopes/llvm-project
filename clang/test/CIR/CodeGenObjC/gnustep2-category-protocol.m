// RUN: %clang_cc1 -triple x86_64-unknown-freebsd -fobjc-runtime=gnustep-2.0 -fclangir -emit-llvm -x objective-c %s -o - | FileCheck %s

@interface X
{
id isa;
}
@end
@implementation X
@end

@protocol Y @end

@interface X (y) <Y>
@end
@implementation X (y) @end

// CHECK-DAG: @._OBJC_PROTOCOL_Y = global {{.*}}, section "__objc_protocols", comdat, align 8
// CHECK-DAG: @[[PROTO_LIST:.*]] = internal global { ptr, i64, [1 x ptr] } { ptr null, i64 1, [1 x ptr] [ptr @._OBJC_PROTOCOL_Y] }, align 8
// CHECK: @.objc_category_Xy = internal global { ptr, ptr, ptr, ptr, ptr, ptr, ptr } { ptr {{.*}}, ptr {{.*}}, ptr null, ptr null, ptr @[[PROTO_LIST]], ptr null, ptr null }, section "__objc_cats", align 8
// CHECK-DAG: @llvm.used = appending global {{.*}}ptr @.objc_category_Xy{{.*}}
