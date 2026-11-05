// RUN: %clang_cc1 -triple x86_64-unknown-freebsd -fobjc-runtime=gnustep-2.0 -fclangir -emit-llvm -x objective-c %s -o - | FileCheck %s

@interface X @end

@interface X (Cat)
@property (readonly) int x;
@end

@implementation X (Cat)
- (int)x { return 12; }
@end

// CHECK-DAG: @[[CATEGORY_NAME:.*]] = private unnamed_addr constant [4 x i8] c"Cat\00", align 1
// CHECK-DAG: @[[CLASS_NAME:.*]] = private unnamed_addr constant [2 x i8] c"X\00", align 1
// CHECK-DAG: @[[SELECTOR_NAME:.objc_sel_name_x]] = linkonce_odr hidden constant [2 x i8] c"x\00", comdat, align 1
// CHECK-DAG: @[[SELECTOR_TYPES:".objc_sel_types_i16\\010:8"]] = linkonce_odr hidden constant [8 x i8] c"i16@0:8\00", comdat, align 1
// CHECK-DAG: @[[SELECTOR:".objc_selector_x_i16\\010:8"]] = linkonce_odr hidden global { ptr, ptr } { ptr @[[SELECTOR_NAME]], ptr @[[SELECTOR_TYPES]] }, section "__objc_selectors", comdat, align 8
// CHECK-DAG: @[[METHOD_TYPES:.*]] = private unnamed_addr constant [8 x i8] c"i16@0:8\00", align 1
// CHECK-DAG: define internal i32 @[[METHOD:.*]](ptr noundef %0, ptr noundef %1)
// CHECK-DAG: @[[METHOD_LIST:.*]] = internal global { ptr, i32, i64, [1 x { ptr, ptr, ptr }] } { ptr null, i32 1, i64 {{[0-9]+}}, [1 x { ptr, ptr, ptr }] [{ ptr, ptr, ptr } { ptr @[[METHOD]], ptr @[[SELECTOR]], ptr @[[METHOD_TYPES]] }] }, align 8
// CHECK-DAG: @[[PROTO_LIST:.*]] = internal global { ptr, i64, [0 x ptr] } { ptr null, i64 0, [0 x ptr] {{(undef|zeroinitializer)}} }, align 8
// CHECK-DAG: @[[PROP_NAME:.*]] = private unnamed_addr constant [2 x i8] c"x\00", align 1
// CHECK-DAG: @[[PROP_ATTRS:.*]] = private unnamed_addr constant [5 x i8] c"Ti,R\00", align 1
// CHECK-DAG: @[[PROP_TYPE:.*]] = private unnamed_addr constant [2 x i8] c"i\00", align 1
// CHECK-DAG: @[[PROP_LIST:.*]] = internal global { i32, i32, ptr, [1 x { ptr, ptr, ptr, ptr, ptr }] } { i32 1, i32 {{[0-9]+}}, ptr null, [1 x { ptr, ptr, ptr, ptr, ptr }] [{ ptr, ptr, ptr, ptr, ptr } { ptr @[[PROP_NAME]], ptr @[[PROP_ATTRS]], ptr @[[PROP_TYPE]], ptr @[[SELECTOR]], ptr null }] }, align 8
// CHECK-DAG: @.objc_category_XCat = internal global { ptr, ptr, ptr, ptr, ptr, ptr, ptr } { ptr @[[CATEGORY_NAME]], ptr @[[CLASS_NAME]], ptr @[[METHOD_LIST]], ptr null, ptr @[[PROTO_LIST]], ptr @[[PROP_LIST]], ptr null }, section "__objc_cats", align 8
// CHECK-DAG: @llvm.used = appending global {{.*}}ptr @.objc_category_XCat{{.*}}
