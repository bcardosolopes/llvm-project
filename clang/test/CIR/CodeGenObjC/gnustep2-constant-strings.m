// REQUIRES: x86-registered-target
// RUN: %clang_cc1 -triple x86_64-unknown-freebsd -fobjc-runtime=gnustep-2.0 -fclangir -emit-llvm -x objective-c %s -o - | FileCheck %s

id a = @"Hello World!";
id b = @"hi";
id c = @"Hello World";
const __NSConstantString *d = __builtin___NSStringMakeConstantString("hi");

// CHECK: target triple = "x86_64-unknown-freebsd"
// CHECK-NOT: @__CFConstantStringClassReference
// CHECK-NOT: section "cfstring"
// CHECK: @._OBJC_CLASS_NSConstantString = external global ptr
// CHECK: @[[STR0:.*]] = private unnamed_addr constant [13 x i8] c"Hello World!\00", align 1
// CHECK: @.objc_string = private global { ptr, i32, i32, i32, i32, ptr } { ptr @._OBJC_CLASS_NSConstantString, i32 0, i32 12, i32 12, i32 0, ptr @[[STR0]] }, section "__objc_constant_string", align 8
// CHECK: @a = global ptr @.objc_string, align 8
// CHECK: @b = global ptr inttoptr (i64 -3340545023602065388 to ptr), align 8
// CHECK: @[[STR1:.*]] = private unnamed_addr constant [12 x i8] c"Hello World\00", align 1
// CHECK: @.objc_str_Hello_World = linkonce_odr hidden global { ptr, i32, i32, i32, i32, ptr } { ptr @._OBJC_CLASS_NSConstantString, i32 0, i32 11, i32 11, i32 0, ptr @[[STR1]] }, section "__objc_constant_string", comdat, align 8
// CHECK: @c = global ptr @.objc_str_Hello_World, align 8
// CHECK: @d = global ptr inttoptr (i64 -3340545023602065388 to ptr), align 8
