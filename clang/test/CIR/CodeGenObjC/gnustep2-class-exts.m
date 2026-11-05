// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -fobjc-runtime=gnustep-2.2 -emit-llvm -fclangir -o - %s | FileCheck %s

@protocol BaseProtocol
@end

@protocol ExtendedProtocol
@end

@interface TestClass <BaseProtocol>

-(void) Meth;
@end

@interface TestClass () <BaseProtocol, ExtendedProtocol>
@end

@implementation TestClass
@end

// CHECK-DAG: [[BASE:@\._OBJC_PROTOCOL_BaseProtocol]] = global
// CHECK-DAG: [[EXT:@\._OBJC_PROTOCOL_ExtendedProtocol]] = global
// CHECK: [[PROTO_LIST:@\.objc_protocol_list\.[0-9]+]] = internal global { ptr, i64, [2 x ptr] } { ptr null, i64 2, [2 x ptr] [ptr [[EXT]], ptr [[BASE]]] }, align 8
// CHECK: @._OBJC_CLASS_TestClass = global {{.*}} ptr [[PROTO_LIST]], ptr null, ptr null, i64 0, ptr null }, align 8
