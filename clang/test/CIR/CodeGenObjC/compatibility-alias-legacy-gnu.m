// RUN: %clang_cc1 -triple x86_64-unknown-freebsd -fno-use-init-array -fobjc-runtime=gcc -Wno-objc-root-class -fclangir -emit-llvm -x objective-c %s -o - | FileCheck %s

@interface Int1
@end

typedef Int1 Int1Typedef;
@compatibility_alias Int1Alias Int1Typedef;

@implementation Int1Alias
@end

// CHECK: @.objc_alias_name = private unnamed_addr constant [10 x i8] c"Int1Alias\00", align 1
// CHECK: @llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @.objc_load_function, ptr null }]
// CHECK-LABEL: define internal void @.objc_load_function() {
// CHECK: call void (ptr, ...) @__objc_exec_class(ptr @.objc_module)
// CHECK: icmp ne ptr @class_registerAlias_np, null
// CHECK: call void @class_registerAlias_np(ptr @_OBJC_CLASS_Int1, ptr @.objc_alias_name)
// CHECK: ret void
// CHECK: declare extern_weak void @class_registerAlias_np(ptr, ptr)
