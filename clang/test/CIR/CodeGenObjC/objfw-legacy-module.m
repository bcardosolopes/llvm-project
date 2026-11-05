// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -fobjc-runtime=objfw -emit-llvm -fclangir -o - %s | FileCheck %s

@interface Test0
@end

@implementation Test0
@end

// CHECK-DAG: @_OBJC_METACLASS_Test0 = global
// CHECK-DAG: @_OBJC_CLASS_Test0 = global
// CHECK: @.objc_symtab = internal global { i64, ptr, i16, i16, [4 x ptr] } { i64 0, ptr @.objc_selector_list, i16 1, i16 1, [4 x ptr] [ptr @_OBJC_CLASS_Test0, ptr @.objc_protocol_holder_category, ptr null, ptr null] }, align 8
// CHECK: @.objc_source_file_name = private unnamed_addr constant
// CHECK: @.objc_module = internal global { i64, i64, ptr, ptr } { i64 9, i64 32, ptr @.objc_source_file_name, ptr @.objc_symtab }, align 8
// CHECK: @llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @.objc_load_function, ptr null }]
// CHECK-LABEL: define internal void @.objc_load_function() {
// CHECK: call void (ptr, ...) @__objc_exec_class(ptr @.objc_module)
// CHECK: declare void @__objc_exec_class(ptr, ...)
