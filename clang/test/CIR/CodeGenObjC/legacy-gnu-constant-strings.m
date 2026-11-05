// RUN: %clang_cc1 -triple x86_64-unknown-freebsd -fno-use-init-array -fobjc-runtime=gnustep-1.8 -fclangir -emit-llvm -x objective-c %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-freebsd -fno-use-init-array -fobjc-runtime=gnustep-1.8 -fconstant-string-class NSConstantString -fclangir -emit-llvm -x objective-c %s -o - | FileCheck %s --check-prefix=OVERRIDE

id a = @"Hello World!";
id b = @"hi";
id c = @"Hello World";

// CHECK: @_OBJC_CLASS_NSConstantString = extern_weak global ptr
// CHECK-DAG: @[[STR0:.*]] = private unnamed_addr constant [13 x i8] c"Hello World!\00", align 1
// CHECK-DAG: @[[STR1:.*]] = private unnamed_addr constant [3 x i8] c"hi\00", align 1
// CHECK-DAG: @[[STR2:.*]] = private unnamed_addr constant [12 x i8] c"Hello World\00", align 1
// CHECK-DAG: @.objc_str = internal global { ptr, ptr, i32 } { ptr @_OBJC_CLASS_NSConstantString, ptr @[[STR0]], i32 12 }, align 8
// CHECK-DAG: @.objc_str.1 = internal global { ptr, ptr, i32 } { ptr @_OBJC_CLASS_NSConstantString, ptr @[[STR1]], i32 2 }, align 8
// CHECK-DAG: @.objc_str.2 = internal global { ptr, ptr, i32 } { ptr @_OBJC_CLASS_NSConstantString, ptr @[[STR2]], i32 11 }, align 8
// CHECK-DAG: @a = global ptr @.objc_str, align 8
// CHECK-DAG: @b = global ptr @.objc_str.1, align 8
// CHECK-DAG: @c = global ptr @.objc_str.2, align 8
// CHECK: @.objc_selector_list = internal global [1 x { ptr, ptr }] zeroinitializer, align 8
// CHECK: @.objc_static_class_name = private unnamed_addr constant [17 x i8] c"NXConstantString\00", align 1
// CHECK: @.objc_statics = internal global { ptr, [4 x ptr] } { ptr @.objc_static_class_name, [4 x ptr] [ptr @.objc_str, ptr @.objc_str.1, ptr @.objc_str.2, ptr null] }, align 8
// CHECK: @.objc_statics_ptr = internal global [2 x ptr] [ptr @.objc_statics, ptr null], align 8
// CHECK: @.objc_symtab = internal global { i64, ptr, i16, i16, [3 x ptr] } { i64 0, ptr @.objc_selector_list, i16 0, i16 1, [3 x ptr] [ptr @.objc_protocol_holder_category, ptr @.objc_statics_ptr, ptr null] }, align 8
// CHECK: @.objc_module = internal global { i64, i64, ptr, ptr } { i64 9, i64 32, ptr @.objc_source_file_name, ptr @.objc_symtab }, align 8
// CHECK: @llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @.objc_load_function, ptr null }]
// CHECK-LABEL: define internal void @.objc_load_function() {
// CHECK: call void (ptr, ...) @__objc_exec_class(ptr @.objc_module)
// CHECK: ret void
// CHECK: declare void @__objc_exec_class(ptr, ...)

// OVERRIDE: @_OBJC_CLASS_NSConstantString = extern_weak global ptr
// OVERRIDE: @.objc_static_class_name = private unnamed_addr constant [17 x i8] c"NSConstantString\00", align 1
