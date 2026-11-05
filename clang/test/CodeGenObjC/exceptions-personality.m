// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu  -emit-llvm -fobjc-runtime=gnustep-2.0 -fexceptions -fobjc-exceptions -o %t %s
// RUN: FileCheck --check-prefixes=CHECK-MINGW-OBJC2 < %t %s
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-w64-windows-gnu  -emit-llvm -fobjc-runtime=gnustep-2.0 -fexceptions -fobjc-exceptions -fclangir -o %t %s %}
// RUN: FileCheck --check-prefixes=CHECK-MINGW-OBJC2 < %t %s

// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu  -emit-llvm -fobjc-runtime=gcc -fexceptions -fobjc-exceptions -o %t %s
// RUN: FileCheck --check-prefixes=CHECK-MINGW-GCC < %t %s
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-w64-windows-gnu  -emit-llvm -fobjc-runtime=gcc -fexceptions -fobjc-exceptions -fclangir -o %t %s %}
// RUN: FileCheck --check-prefixes=CHECK-MINGW-GCC < %t %s

// RUN: %clang_cc1 -triple x86_64-w64-windows-msvc  -emit-llvm -fobjc-runtime=gnustep-2.0 -fexceptions -fobjc-exceptions -o %t %s
// RUN: FileCheck --check-prefixes=CHECK-MSVC-OBJC2 < %t %s
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-w64-windows-msvc  -emit-llvm -fobjc-runtime=gnustep-2.0 -fexceptions -fobjc-exceptions -fclangir -o %t %s %}
// RUN: FileCheck --check-prefixes=CHECK-MSVC-OBJC2 < %t %s

// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu  -emit-llvm -fobjc-runtime=gnustep-2.0 -fexceptions -fobjc-exceptions -o %t %s
// RUN: FileCheck --check-prefixes=CHECK-LINUX-OBJC2 < %t %s
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-pc-linux-gnu  -emit-llvm -fobjc-runtime=gnustep-2.0 -fexceptions -fobjc-exceptions -fclangir -o %t %s %}
// RUN: FileCheck --check-prefixes=CHECK-LINUX-OBJC2 < %t %s

// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu  -emit-llvm -fobjc-runtime=gcc -fexceptions -fobjc-exceptions -o %t %s
// RUN: FileCheck --check-prefixes=CHECK-LINUX-GCC < %t %s
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-pc-linux-gnu  -emit-llvm -fobjc-runtime=gcc -fexceptions -fobjc-exceptions -fclangir -o %t %s %}
// RUN: FileCheck --check-prefixes=CHECK-LINUX-GCC < %t %s
@interface Foo @end

// CHECK-MINGW-OBJC2-DAG: @"__start_.objcrt$SEL" = linkonce_odr hidden global {{.*}} zeroinitializer, section ".objcrt$SEL$a", comdat, align 8
// CHECK-MINGW-OBJC2: @.objc_init = linkonce_odr hidden global { i64, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr } { i64 0, ptr @"__start_.objcrt$SEL", ptr @"__stop.objcrt$SEL", ptr @"__start_.objcrt$CLS", ptr @"__stop.objcrt$CLS", ptr @"__start_.objcrt$CLR", ptr @"__stop.objcrt$CLR", ptr @"__start_.objcrt$CAT", ptr @"__stop.objcrt$CAT", ptr @"__start_.objcrt$PCL", ptr @"__stop.objcrt$PCL", ptr @"__start_.objcrt$PCR", ptr @"__stop.objcrt$PCR", ptr @"__start_.objcrt$CAL", ptr @"__stop.objcrt$CAL", ptr @"__start_.objcrt$STR", ptr @"__stop.objcrt$STR" }, comdat, align 8
// CHECK-MINGW-OBJC2: @.objc_ctor = linkonce hidden global ptr @.objcv2_load_function, section ".CRT$XCLz", comdat
// CHECK-MINGW-OBJC2: @llvm.used = appending global [1 x ptr] [ptr @.objc_ctor], section "llvm.metadata"
// CHECK-MINGW-OBJC2: @llvm.compiler.used = appending global [1 x ptr] [ptr @.objcv2_load_function], section "llvm.metadata"

// CHECK-MSVC-OBJC2-DAG: @"__start_.objcrt$SEL" = linkonce_odr hidden global {{.*}} zeroinitializer, section ".objcrt$SEL$a", comdat, align 8
// CHECK-MSVC-OBJC2: @.objc_init = linkonce_odr hidden global { i64, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr } { i64 0, ptr @"__start_.objcrt$SEL", ptr @"__stop.objcrt$SEL", ptr @"__start_.objcrt$CLS", ptr @"__stop.objcrt$CLS", ptr @"__start_.objcrt$CLR", ptr @"__stop.objcrt$CLR", ptr @"__start_.objcrt$CAT", ptr @"__stop.objcrt$CAT", ptr @"__start_.objcrt$PCL", ptr @"__stop.objcrt$PCL", ptr @"__start_.objcrt$PCR", ptr @"__stop.objcrt$PCR", ptr @"__start_.objcrt$CAL", ptr @"__stop.objcrt$CAL", ptr @"__start_.objcrt$STR", ptr @"__stop.objcrt$STR" }, comdat, align 8
// CHECK-MSVC-OBJC2: @.objc_ctor = linkonce hidden global ptr @.objcv2_load_function, section ".CRT$XCLz", comdat
// CHECK-MSVC-OBJC2: @llvm.used = appending global [1 x ptr] [ptr @.objc_ctor], section "llvm.metadata"
// CHECK-MSVC-OBJC2: @llvm.compiler.used = appending global [1 x ptr] [ptr @.objcv2_load_function], section "llvm.metadata"

// CHECK-LINUX-OBJC2: @.objc_init = linkonce_odr hidden global { i64, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr } { i64 0, ptr @__start___objc_selectors, ptr @__stop___objc_selectors, ptr @__start___objc_classes, ptr @__stop___objc_classes, ptr @__start___objc_class_refs, ptr @__stop___objc_class_refs, ptr @__start___objc_cats, ptr @__stop___objc_cats, ptr @__start___objc_protocols, ptr @__stop___objc_protocols, ptr @__start___objc_protocol_refs, ptr @__stop___objc_protocol_refs, ptr @__start___objc_class_aliases, ptr @__stop___objc_class_aliases, ptr @__start___objc_constant_string, ptr @__stop___objc_constant_string }, comdat, align 8
// CHECK-LINUX-OBJC2: @.objc_ctor = linkonce hidden global ptr @.objcv2_load_function, section ".init_array", comdat
// CHECK-LINUX-OBJC2-DAG: @.objc_null_selector = linkonce_odr hidden global { ptr, ptr } zeroinitializer, section "__objc_selectors", comdat, align 8
// CHECK-LINUX-OBJC2-DAG: @.objc_null_constant_string = linkonce_odr hidden global { ptr, i32, i32, i32, i32, ptr } zeroinitializer, section "__objc_constant_string", comdat, align 8
// CHECK-LINUX-OBJC2: @llvm.used = appending global [9 x ptr] [ptr @.objc_ctor, ptr @.objc_null_selector{{.*}}ptr @.objc_null_constant_string], section "llvm.metadata"
// CHECK-LINUX-OBJC2: @llvm.compiler.used = appending global [1 x ptr] [ptr @.objcv2_load_function], section "llvm.metadata"

void throwing(void) {
  @try
  {
    // CHECK-MINGW-OBJC2: personality ptr @__gxx_personality_seh0
    // CHECK-MINGW-OBJC2: invoke void @objc_exception_throw

    // CHECK-MINGW-GCC: personality ptr @__gnu_objc_personality_v0
    // CHECK-MINGW-GCC: invoke void @objc_exception_throw

    // CHECK-MSVC-OBJC2: personality ptr @__CxxFrameHandler3
    // CHECK-MSVC-OBJC2: %[[EXNSLOT:[0-9A-Za-z_.]+]] = alloca ptr, align 8
    // CHECK-MSVC-OBJC2: invoke void @objc_exception_throw

    // CHECK-LINUX-OBJC2: personality ptr @__gnustep_objc_personality_v0
    // CHECK-LINUX-OBJC2: invoke void @objc_exception_throw

    // CHECK-LINUX-GCC: personality ptr @__gnu_objc_personality_v0
    @throw(@"error!");
  }
  @catch(...)
  {
    // CHECK-MINGW-OBJC2: call ptr @__cxa_begin_catch
    // CHECK-MINGW-OBJC2: invoke ptr @__cxa_rethrow
    // CHECK-MINGW-OBJC2: invoke void @__cxa_end_catch

    // CHECK-MINGW-GCC: call void @objc_exception_throw

    // CHECK-MSVC-OBJC2: catchswitch within none [label %[[CATCH:[0-9A-Za-z_.]+]]] unwind to caller
    // CHECK-MSVC-OBJC2: [[CATCH]]:
    // CHECK-MSVC-OBJC2: %[[PAD:[0-9A-Za-z_.]+]] = catchpad within %{{[0-9A-Za-z_.]+}} [ptr null, i32 64, ptr %[[EXNSLOT]]]
    // CHECK-MSVC-OBJC2: call void @objc_exception_rethrow(){{.*}}[ "funclet"(token %[[PAD]]) ]
    // CHECK-MSVC-OBJC2-NEXT: unreachable

    // CHECK-LINUX-OBJC2: call ptr @objc_begin_catch
    // CHECK-LINUX-OBJC2: invoke void @objc_exception_throw
    // CHECK-LINUX-OBJC2: invoke void @objc_end_catch()

    // CHECK-LINUX-GCC: invoke void @objc_exception_throw

    @throw;
  }
}

// CHECK-MINGW-OBJC2-LABEL: define linkonce_odr hidden void @.objcv2_load_function() comdat {
// CHECK-MINGW-OBJC2: call void @__objc_load(ptr @.objc_init)

// CHECK-MSVC-OBJC2-LABEL: define linkonce_odr hidden void @.objcv2_load_function() comdat {
// CHECK-MSVC-OBJC2: call void @__objc_load(ptr @.objc_init)

// CHECK-LINUX-OBJC2-LABEL: define linkonce_odr hidden void @.objcv2_load_function() comdat {
// CHECK-LINUX-OBJC2: call void @__objc_load(ptr @.objc_init)
