// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -fobjc-runtime=objfw -emit-llvm -fclangir -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -fobjc-runtime=gcc -emit-llvm -fclangir -o - %s | FileCheck %s

@interface Foo @end

@protocol Proto
@property (readonly) int proto_property;
@end

@interface Foo (Category) <Proto> @end

@implementation Foo (Category)
- (int)proto_property { return 0; }
@end

// CHECK-DAG: [[PROTO_EMPTY:@\.objc_protocol_list(\.[0-9]+)?]] = internal global { ptr, i64, [0 x ptr] } zeroinitializer, align 8
// CHECK-DAG: [[PROTO:@\.objc_protocol(\.[0-9]+)?]] = internal global { ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr } { ptr {{.*}}, ptr {{.*}}, ptr [[PROTO_EMPTY]], ptr {{.*}}, ptr {{.*}}, ptr {{.*}}, ptr {{.*}}, ptr {{.*}}, ptr null }, align 8
// CHECK-DAG: [[METHOD_LIST:@\.objc_method_list\.[0-9]+]] = internal global { ptr, i32, [1 x { ptr, ptr, ptr }] } { ptr null, i32 1, [1 x { ptr, ptr, ptr }] [{{.*}}] }, align 8
// CHECK-DAG: [[CATEGORY_PROTO_LIST:@\.objc_protocol_list\.[0-9]+]] = internal global { ptr, i64, [1 x ptr] } { ptr null, i64 1, [1 x ptr] [ptr [[PROTO]]] }, align 8
// CHECK-DAG: @.objc_category_FooCategory = internal global { ptr, ptr, ptr, ptr, ptr } { ptr {{.*}}, ptr {{.*}}, ptr [[METHOD_LIST]], ptr null, ptr [[CATEGORY_PROTO_LIST]] }, align 8
// CHECK-DAG: [[HOLDER_LIST:@\.objc_protocol_list\.[0-9]+]] = internal global { ptr, i64, [1 x ptr] } { ptr null, i64 1, [1 x ptr] [ptr [[PROTO]]] }, align 8
// CHECK-DAG: @.objc_protocol_holder_category = internal global { ptr, ptr, ptr, ptr, ptr } { ptr {{.*}}, ptr {{.*}}, ptr null, ptr null, ptr [[HOLDER_LIST]] }, align 8
// CHECK: @.objc_symtab = internal global { i64, ptr, i16, i16, [4 x ptr] } { i64 0, ptr @.objc_selector_list, i16 0, i16 2, [4 x ptr] [ptr @.objc_category_FooCategory, ptr @.objc_protocol_holder_category, ptr null, ptr null] }, align 8
