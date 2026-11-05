// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fexceptions -fobjc-exceptions -fclangir -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fexceptions -fobjc-exceptions -fvisibility=hidden -fclangir -o - %s | FileCheck -check-prefix=CHECK-HIDDEN %s

__attribute__((objc_root_class))
@interface Root {
  Class isa;
}
@end

__attribute__((objc_exception))
@interface A : Root
@end

@implementation A
@end
// CHECK: @"OBJC_EHTYPE_$_A" = global { ptr, ptr, ptr } { ptr getelementptr inbounds ({{(ptr, ptr @objc_ehtype_vtable, i32 2|ptr, ptr @objc_ehtype_vtable, i64 2|i8, ptr @objc_ehtype_vtable, i64 16)}}), ptr @OBJC_CLASS_NAME_, ptr @"OBJC_CLASS_$_A" }, section "__DATA{{, ?}}__objc_const", align 8
// CHECK: @objc_ehtype_vtable = external global ptr
// CHECK-HIDDEN: @"OBJC_EHTYPE_$_A" = hidden global { ptr, ptr, ptr } { ptr getelementptr inbounds ({{(ptr, ptr @objc_ehtype_vtable, i32 2|ptr, ptr @objc_ehtype_vtable, i64 2|i8, ptr @objc_ehtype_vtable, i64 16)}}), ptr @OBJC_CLASS_NAME_, ptr @"OBJC_CLASS_$_A" }, section "__DATA{{, ?}}__objc_const", align 8

__attribute__((objc_exception))
__attribute__((visibility("default")))
@interface B : Root
@end

@implementation B
@end
// CHECK: @"OBJC_EHTYPE_$_B" = global { ptr, ptr, ptr } { ptr getelementptr inbounds ({{(ptr, ptr @objc_ehtype_vtable, i32 2|ptr, ptr @objc_ehtype_vtable, i64 2|i8, ptr @objc_ehtype_vtable, i64 16)}}), ptr @OBJC_CLASS_NAME_.1, ptr @"OBJC_CLASS_$_B" }, section "__DATA{{, ?}}__objc_const", align 8
// CHECK-HIDDEN: @"OBJC_EHTYPE_$_B" = global { ptr, ptr, ptr } { ptr getelementptr inbounds ({{(ptr, ptr @objc_ehtype_vtable, i32 2|ptr, ptr @objc_ehtype_vtable, i64 2|i8, ptr @objc_ehtype_vtable, i64 16)}}), ptr @OBJC_CLASS_NAME_.1, ptr @"OBJC_CLASS_$_B" }, section "__DATA{{, ?}}__objc_const", align 8
