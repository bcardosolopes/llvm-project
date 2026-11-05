// RUN: %clang_cc1 -triple x86_64-unknown-freebsd -fobjc-runtime=gnustep-2.0 -fclangir -emit-llvm -x objective-c %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-freebsd -fno-use-init-array -fobjc-runtime=gnustep-1.8 -fclangir -emit-llvm -x objective-c %s -o - | FileCheck %s --check-prefix=CHECK-OLD

@interface X @end
@implementation X @end

// CHECK-DAG: @._OBJC_REF_CLASS_X = global ptr @._OBJC_CLASS_X, section "__objc_class_refs", align 8
// CHECK-DAG: @._OBJC_INIT_CLASS_X = global ptr @._OBJC_CLASS_X, section "__objc_classes", align 8
// CHECK-DAG: @__start___objc_selectors = external hidden global ptr
// CHECK-DAG: @__stop___objc_selectors = external hidden global ptr
// CHECK-DAG: @__start___objc_classes = external hidden global ptr
// CHECK-DAG: @__stop___objc_classes = external hidden global ptr
// CHECK-DAG: @__start___objc_class_refs = external hidden global ptr
// CHECK-DAG: @__stop___objc_class_refs = external hidden global ptr
// CHECK-DAG: @__start___objc_cats = external hidden global ptr
// CHECK-DAG: @__stop___objc_cats = external hidden global ptr
// CHECK-DAG: @__start___objc_protocols = external hidden global ptr
// CHECK-DAG: @__stop___objc_protocols = external hidden global ptr
// CHECK-DAG: @__start___objc_protocol_refs = external hidden global ptr
// CHECK-DAG: @__stop___objc_protocol_refs = external hidden global ptr
// CHECK-DAG: @__start___objc_class_aliases = external hidden global ptr
// CHECK-DAG: @__stop___objc_class_aliases = external hidden global ptr
// CHECK-DAG: @__start___objc_constant_string = external hidden global ptr
// CHECK-DAG: @__stop___objc_constant_string = external hidden global ptr
// CHECK: @.objc_init = linkonce_odr hidden global { i64, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr } { i64 0, ptr @__start___objc_selectors, ptr @__stop___objc_selectors, ptr @__start___objc_classes, ptr @__stop___objc_classes, ptr @__start___objc_class_refs, ptr @__stop___objc_class_refs, ptr @__start___objc_cats, ptr @__stop___objc_cats, ptr @__start___objc_protocols, ptr @__stop___objc_protocols, ptr @__start___objc_protocol_refs, ptr @__stop___objc_protocol_refs, ptr @__start___objc_class_aliases, ptr @__stop___objc_class_aliases, ptr @__start___objc_constant_string, ptr @__stop___objc_constant_string }, comdat, align 8
// CHECK: @.objc_ctor = linkonce hidden global ptr @.objcv2_load_function, section ".init_array", comdat, align 8
// CHECK-DAG: @.objc_null_selector = linkonce_odr hidden global { ptr, ptr } zeroinitializer, section "__objc_selectors", comdat, align 8
// CHECK-DAG: @.objc_null_category = linkonce_odr hidden global { ptr, ptr, ptr, ptr, ptr, ptr, ptr } zeroinitializer, section "__objc_cats", comdat, align 8
// CHECK-DAG: @.objc_null_protocol = linkonce_odr hidden global { ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr } zeroinitializer, section "__objc_protocols", comdat, align 8
// CHECK-DAG: @.objc_null_protocol_ref = linkonce_odr hidden global { ptr } zeroinitializer, section "__objc_protocol_refs", comdat, align 8
// CHECK-DAG: @.objc_null_class_alias = linkonce_odr hidden global { ptr, ptr } zeroinitializer, section "__objc_class_aliases", comdat, align 8
// CHECK-DAG: @.objc_null_constant_string = linkonce_odr hidden global { ptr, i32, i32, i32, i32, ptr } zeroinitializer, section "__objc_constant_string", comdat, align 8
// CHECK: @llvm.used = appending global [8 x ptr]
// CHECK-SAME: ptr @._OBJC_INIT_CLASS_X
// CHECK-SAME: ptr @.objc_ctor
// CHECK: @llvm.compiler.used = appending global [1 x ptr] [ptr @.objcv2_load_function], section "llvm.metadata"
// CHECK: define linkonce_odr hidden void @.objcv2_load_function() comdat {
// CHECK: call void @__objc_load(ptr @.objc_init)

// CHECK-OLD-DAG: @.objc_protocol_list.1 = internal global { ptr, i64, [0 x ptr] } zeroinitializer, align 8
// CHECK-OLD-DAG: @[[HOLDER_CAT_NAME:.*]] = private unnamed_addr constant [12 x i8] c"AnotherHack\00", align 1
// CHECK-OLD-DAG: @[[HOLDER_CLASS_NAME:.*]] = private unnamed_addr constant [33 x i8] c"__ObjC_Protocol_Holder_Ugly_Hack\00", align 1
// CHECK-OLD-DAG: @.objc_protocol_holder_category = internal global { ptr, ptr, ptr, ptr, ptr } { ptr @[[HOLDER_CAT_NAME]], ptr @[[HOLDER_CLASS_NAME]], ptr null, ptr null, ptr @.objc_protocol_list.1 }, align 8
// CHECK-OLD: @.objc_selector_list = internal global [1 x { ptr, ptr }] zeroinitializer, align 8
// CHECK-OLD: @.objc_symtab = internal global { i64, ptr, i16, i16, [4 x ptr] } { i64 0, ptr @.objc_selector_list, i16 1, i16 1, [4 x ptr] [ptr @_OBJC_CLASS_X, ptr @.objc_protocol_holder_category, ptr null, ptr null] }, align 8
// CHECK-OLD: @.objc_source_file_name = private unnamed_addr constant
// CHECK-OLD: @.objc_module = internal global { i64, i64, ptr, ptr } { i64 9, i64 32, ptr @.objc_source_file_name, ptr @.objc_symtab }, align 8
// CHECK-OLD: @llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @.objc_load_function, ptr null }]
// CHECK-OLD: define internal void @.objc_load_function() {
// CHECK-OLD: call void (ptr, ...) @__objc_exec_class(ptr @.objc_module)
// CHECK-OLD: declare void @__objc_exec_class(ptr, ...)
