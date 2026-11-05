// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fobjc-runtime=gnustep-1.6 -fclangir -o - %s | FileCheck %s

@protocol P
@property int i;
@end

@protocol P1
@property int i1;
@end

@protocol P2 <P1>
@property int i2;
@end

@interface C1 { id isa; } @end

@interface C2 : C1 <P, P2> {
  int i;
}
@property int i2;
@end

@implementation C1
+(void)initialize { }
@end

@implementation C2
@synthesize i;
@synthesize i1;
@synthesize i2;
@end

// CHECK-DAG: @.objc_protocol = internal global { ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr }
// CHECK-DAG: [[P1NAME:@\.objc_protocol_name\.[0-9]+]] = private unnamed_addr constant [3 x i8] c"P1\00", align 1
// CHECK-DAG: [[P2NAME:@\.objc_protocol_name\.[0-9]+]] = private unnamed_addr constant [3 x i8] c"P2\00", align 1
// CHECK-DAG: [[PROTO_P1:@\.objc_protocol\.[0-9]+]] = internal global { ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr } { ptr {{.*}}, ptr [[P1NAME]], ptr [[P1LIST:@\.objc_protocol_list\.[0-9]+]], ptr {{.*}}, ptr {{.*}}, ptr {{.*}}, ptr {{.*}}, ptr {{.*}}, ptr null }, align 8
// CHECK-DAG: [[PROTO_P2:@\.objc_protocol\.[0-9]+]] = internal global { ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr } { ptr {{.*}}, ptr [[P2NAME]], ptr [[P2LIST:@\.objc_protocol_list\.[0-9]+]], ptr {{.*}}, ptr {{.*}}, ptr {{.*}}, ptr {{.*}}, ptr {{.*}}, ptr null }, align 8
// CHECK-DAG: [[P1LIST]] = internal global { ptr, i64, [0 x ptr] } zeroinitializer, align 8
// CHECK-DAG: [[P2LIST]] = internal global { ptr, i64, [1 x ptr] } { ptr null, i64 1, [1 x ptr] [ptr [[PROTO_P1]]] }, align 8
// CHECK-DAG: [[CLASSLIST:@\.objc_protocol_list\.[0-9]+]] = internal global { ptr, i64, [2 x ptr] } { ptr null, i64 2, [2 x ptr] [ptr @.objc_protocol, ptr [[PROTO_P2]]] }, align 8
// CHECK: @_OBJC_CLASS_C2 = global {{.*}} ptr [[CLASSLIST]], ptr null, i64 1, ptr @.ivar.offsets.1, ptr @.objc_property_list{{(\.[0-9]+)?}}, i64 1, i64 1 }, align 8
// CHECK-DAG: [[HOLDERLIST:@\.objc_protocol_list\.[0-9]+]] = internal global { ptr, i64, [3 x ptr] } { ptr null, i64 3, [3 x ptr] [ptr @.objc_protocol, ptr [[PROTO_P1]], ptr [[PROTO_P2]]] }, align 8
// CHECK: @.objc_protocol_holder_category = internal global { ptr, ptr, ptr, ptr, ptr } { ptr @.objc_category_name, ptr @.objc_category_class_name, ptr null, ptr null, ptr [[HOLDERLIST]] }, align 8
