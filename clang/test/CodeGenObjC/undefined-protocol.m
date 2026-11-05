// RUN: %clang_cc1 -emit-llvm -fobjc-runtime=gcc -o - %s | FileCheck %s
// RUN: %if cir-support %{ %clang_cc1 -emit-llvm -fobjc-runtime=gcc -fclangir -o - %s | FileCheck %s %}

// PR13820
// REQUIRES: LP64

// CHECK-DAG: @.objc_protocol_name = private unnamed_addr constant [15 x i8] c"MadeUpProtocol\00", align 1
// CHECK-DAG: [[PROTO:@.*]] = internal global { ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr } { ptr inttoptr (i{{32|64}} 2 to ptr), ptr @.objc_protocol_name, ptr @.objc_protocol_list, ptr {{.*}}, ptr {{.*}}, ptr {{.*}}, ptr {{.*}}, ptr null, ptr null }, align 8
// CHECK: @.objc_protocol_list.1 = internal global { ptr, i64, [1 x ptr] } { ptr null, i64 1, [1 x ptr] [ptr [[PROTO]]] }, align 8
// CHECK: @_OBJC_CLASS_Object = global {{.*}} ptr @.objc_protocol_list.1{{.*}}, align 8

@protocol MadeUpProtocol;

@interface Object <MadeUpProtocol> @end
@implementation Object @end
