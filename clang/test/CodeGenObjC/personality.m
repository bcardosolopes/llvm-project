// RUN: %clang_cc1 -triple i686-unknown-linux-gnu -fexceptions -fobjc-exceptions -fobjc-runtime=macosx-fragile -emit-llvm %s -o - | FileCheck %s -check-prefix CHECK-MACOSX-FRAGILE
// RUN: %clang_cc1 -triple i686-unknown-linux-gnu -fexceptions -fobjc-exceptions -fobjc-runtime=ios -emit-llvm %s -o - | FileCheck %s -check-prefix CHECK-NS
// RUN: %clang_cc1 -triple i686-unknown-linux-gnu -fexceptions -fobjc-exceptions -fobjc-runtime=macosx -emit-llvm %s -o - | FileCheck %s -check-prefix CHECK-NS
// RUN: %clang_cc1 -triple i686-unknown-linux-gnu -fexceptions -fobjc-exceptions -fobjc-runtime=watchos -emit-llvm %s -o - | FileCheck %s -check-prefix CHECK-NS
// RUN: %clang_cc1 -triple i686-unknown-linux-gnu -fexceptions -fobjc-exceptions -fobjc-runtime=gnustep-1.7 -emit-llvm %s -o - | FileCheck %s -check-prefix CHECK-GNUSTEP-1_7
// RUN: %clang_cc1 -triple i686-unknown-linux-gnu -fexceptions -fobjc-exceptions -fobjc-runtime=gnustep -emit-llvm %s -o - | FileCheck %s -check-prefix CHECK-GNUSTEP
// RUN: %clang_cc1 -triple i686-unknown-linux-gnu -fexceptions -fobjc-exceptions -fobjc-runtime=gcc -emit-llvm %s -o - | FileCheck %s -check-prefix CHECK-GCC
// RUN: %clang_cc1 -triple i686-unknown-linux-gnu -fexceptions -exception-model=seh -fobjc-exceptions -fobjc-runtime=gcc -emit-llvm %s -o - | FileCheck %s -check-prefix CHECK-GCC-SEH
// RUN: %clang_cc1 -triple i686-unknown-linux-gnu -fexceptions -exception-model=sjlj -fobjc-exceptions -fobjc-runtime=gcc -emit-llvm %s -o - | FileCheck %s -check-prefix CHECK-GCC-SJLJ
// RUN: %clang_cc1 -triple i686-unknown-linux-gnu -fexceptions -fobjc-exceptions -fobjc-runtime=objfw -emit-llvm %s -o - | FileCheck %s -check-prefix CHECK-OBJFW
// RUN: %clang_cc1 -triple i686-unknown-linux-gnu -fexceptions -exception-model=seh -fobjc-exceptions -fobjc-runtime=objfw -emit-llvm %s -o - | FileCheck %s -check-prefix CHECK-OBJFW-SEH
// RUN: %clang_cc1 -triple i686-unknown-linux-gnu -fexceptions -exception-model=sjlj -fobjc-exceptions -fobjc-runtime=objfw -emit-llvm %s -o - | FileCheck %s -check-prefix CHECK-OBJFW-SJLJ

// RUN: %clang_cc1 -triple i686-unknown-windows-msvc -fexceptions -fobjc-exceptions -fobjc-runtime=macosx-fragile -emit-llvm %s -o - | FileCheck %s -check-prefix CHECK-WIN-MSVC
// RUN: %clang_cc1 -triple i686-unknown-windows-msvc -fexceptions -fobjc-exceptions -fobjc-runtime=ios -emit-llvm %s -o - | FileCheck %s -check-prefix CHECK-WIN-MSVC
// RUN: %if cir-support %{ %clang_cc1 -triple i686-unknown-windows-msvc -fexceptions -fobjc-exceptions -fobjc-runtime=ios -emit-llvm -fclangir %s -o - | FileCheck %s -check-prefix CHECK-WINMSVC-FUNCLET-WRAPPED %}
// RUN: %clang_cc1 -triple i686-unknown-windows-msvc -fexceptions -fobjc-exceptions -fobjc-runtime=macosx -emit-llvm %s -o - | FileCheck %s -check-prefix CHECK-WIN-MSVC
// RUN: %if cir-support %{ %clang_cc1 -triple i686-unknown-windows-msvc -fexceptions -fobjc-exceptions -fobjc-runtime=macosx -emit-llvm -fclangir %s -o - | FileCheck %s -check-prefix CHECK-WINMSVC-FUNCLET-WRAPPED %}
// RUN: %clang_cc1 -triple i686-unknown-windows-msvc -fexceptions -fobjc-exceptions -fobjc-runtime=watchos -emit-llvm %s -o - | FileCheck %s -check-prefix CHECK-WIN-MSVC
// RUN: %if cir-support %{ %clang_cc1 -triple i686-unknown-windows-msvc -fexceptions -fobjc-exceptions -fobjc-runtime=watchos -emit-llvm -fclangir %s -o - | FileCheck %s -check-prefix CHECK-WINMSVC-FUNCLET-WRAPPED %}
// RUN: %clang_cc1 -triple i686-unknown-windows-msvc -fexceptions -fobjc-exceptions -fobjc-runtime=gnustep-1.7 -emit-llvm %s -o - | FileCheck %s -check-prefix CHECK-WIN-MSVC
// RUN: %if cir-support %{ %clang_cc1 -triple i686-unknown-windows-msvc -fexceptions -fobjc-exceptions -fobjc-runtime=gnustep-1.7 -emit-llvm -fclangir %s -o - | FileCheck %s -check-prefix CHECK-WINMSVC-FUNCLET %}
// RUN: %clang_cc1 -triple i686-unknown-windows-msvc -fexceptions -fobjc-exceptions -fobjc-runtime=gnustep -emit-llvm %s -o - | FileCheck %s -check-prefix CHECK-WIN-MSVC
// RUN: %if cir-support %{ %clang_cc1 -triple i686-unknown-windows-msvc -fexceptions -fobjc-exceptions -fobjc-runtime=gnustep -emit-llvm -fclangir %s -o - | FileCheck %s -check-prefix CHECK-WINMSVC-FUNCLET %}
// RUN: %clang_cc1 -triple i686-unknown-windows-msvc -fexceptions -fobjc-exceptions -fobjc-runtime=gcc -emit-llvm %s -o - | FileCheck %s -check-prefix CHECK-WIN-MSVC
// RUN: %if cir-support %{ %clang_cc1 -triple i686-unknown-windows-msvc -fexceptions -fobjc-exceptions -fobjc-runtime=gcc -emit-llvm -fclangir %s -o - | FileCheck %s -check-prefix CHECK-WINMSVC-FUNCLET %}
// RUN: %clang_cc1 -triple i686-unknown-windows-msvc -fexceptions -fobjc-exceptions -fobjc-runtime=objfw -emit-llvm %s -o - | FileCheck %s -check-prefix CHECK-WIN-MSVC
// RUN: %if cir-support %{ %clang_cc1 -triple i686-unknown-windows-msvc -fexceptions -fobjc-exceptions -fobjc-runtime=objfw -emit-llvm -fclangir %s -o - | FileCheck %s -check-prefix CHECK-WINMSVC-FUNCLET %}

// RUN: %clang_cc1 -triple i686-unknown-windows-gnu -fexceptions -fobjc-exceptions -fobjc-runtime=macosx-fragile -emit-llvm %s -o - | FileCheck %s -check-prefix CHECK-MACOSX-FRAGILE
// RUN: %clang_cc1 -triple i686-unknown-windows-gnu -fexceptions -exception-model=dwarf -fobjc-exceptions -fobjc-runtime=macosx-fragile -emit-llvm %s -o - | FileCheck %s -check-prefix CHECK-MACOSX-FRAGILE-MINGW-DWARF
// RUN: %clang_cc1 -triple i686-unknown-windows-gnu -fexceptions -exception-model=seh -fobjc-exceptions -fobjc-runtime=macosx-fragile -emit-llvm %s -o - | FileCheck %s -check-prefix CHECK-MACOSX-FRAGILE-MINGW-SEH
// RUN: %clang_cc1 -triple i686-unknown-windows-gnu -fexceptions -exception-model=sjlj -fobjc-exceptions -fobjc-runtime=macosx-fragile -emit-llvm %s -o - | FileCheck %s -check-prefix CHECK-MACOSX-FRAGILE-MINGW-SJLJ
// RUN: %clang_cc1 -triple i686-unknown-windows-gnu -fexceptions -fobjc-exceptions -fobjc-runtime=ios -emit-llvm %s -o - | FileCheck %s -check-prefix CHECK-NS
// RUN: %if cir-support %{ %clang_cc1 -triple i686-unknown-windows-gnu -fexceptions -fobjc-exceptions -fobjc-runtime=ios -emit-llvm -fclangir %s -o - | FileCheck %s -check-prefix CHECK-NS %}
// RUN: %clang_cc1 -triple i686-unknown-windows-gnu -fexceptions -fobjc-exceptions -fobjc-runtime=macosx -emit-llvm %s -o - | FileCheck %s -check-prefix CHECK-NS
// RUN: %clang_cc1 -triple i686-unknown-windows-gnu -fexceptions -fobjc-exceptions -fobjc-runtime=watchos -emit-llvm %s -o - | FileCheck %s -check-prefix CHECK-NS
// RUN: %clang_cc1 -triple i686-unknown-windows-gnu -fexceptions -fobjc-exceptions -fobjc-runtime=gnustep-1.7 -emit-llvm %s -o - | FileCheck %s -check-prefix CHECK-WIN-GNU
// RUN: %clang_cc1 -triple i686-unknown-windows-gnu -fexceptions -fobjc-exceptions -fobjc-runtime=gnustep -emit-llvm %s -o - | FileCheck %s -check-prefix CHECK-WIN-GNU
// RUN: %clang_cc1 -triple i686-unknown-windows-gnu -fexceptions -fobjc-exceptions -fobjc-runtime=gcc -emit-llvm %s -o - | FileCheck %s -check-prefix CHECK-GCC
// RUN: %clang_cc1 -triple i686-unknown-windows-gnu -fexceptions -exception-model=seh -fobjc-exceptions -fobjc-runtime=gcc -emit-llvm %s -o - | FileCheck %s -check-prefix CHECK-GCC-SEH
// RUN: %clang_cc1 -triple i686-unknown-windows-gnu -fexceptions -exception-model=sjlj -fobjc-exceptions -fobjc-runtime=gcc -emit-llvm %s -o - | FileCheck %s -check-prefix CHECK-GCC-SJLJ
// RUN: %clang_cc1 -triple i686-unknown-windows-gnu -fexceptions -fobjc-exceptions -fobjc-runtime=objfw -emit-llvm %s -o - | FileCheck %s -check-prefix CHECK-OBJFW
// RUN: %clang_cc1 -triple i686-unknown-windows-gnu -fexceptions -exception-model=seh -fobjc-exceptions -fobjc-runtime=objfw -emit-llvm %s -o - | FileCheck %s -check-prefix CHECK-OBJFW-SEH
// RUN: %clang_cc1 -triple i686-unknown-windows-gnu -fexceptions -exception-model=sjlj -fobjc-exceptions -fobjc-runtime=objfw -emit-llvm %s -o - | FileCheck %s -check-prefix CHECK-OBJFW-SJLJ

void g(void);

// CHECK-MACOSX-FRAGILE: personality ptr @__gcc_personality_v0
// CHECK-NS: personality ptr @__objc_personality_v0
// CHECK-GNUSTEP-1_7: personality ptr @__gnustep_objc_personality_v0
// CHECK-GNUSTEP: personality ptr @__gnu_objc_personality_v0
// CHECK-GCC: personality ptr @__gnu_objc_personality_v0
// CHECK-GCC-SEH: personality ptr @__gnu_objc_personality_seh0
// CHECK-GCC-SJLJ: personality ptr @__gnu_objc_personality_sj0
// CHECK-OBJFW: personality ptr @__gnu_objc_personality_v0
// CHECK-OBJFW-SEH: personality ptr @__gnu_objc_personality_seh0
// CHECK-OBJFW-SJLJ: personality ptr @__gnu_objc_personality_sj0

// CHECK-WIN-MSVC: personality ptr @__CxxFrameHandler3
// CHECK-WINMSVC-FUNCLET-LABEL: define dso_local void @f()
// CHECK-WINMSVC-FUNCLET-SAME: personality ptr @__CxxFrameHandler3 {
// CHECK-WINMSVC-FUNCLET: %[[EXNSLOT:[0-9A-Za-z_.]+]] = alloca ptr, align 4
// CHECK-WINMSVC-FUNCLET: invoke void @g()
// CHECK-WINMSVC-FUNCLET: catchswitch within none [label %[[CATCH:[0-9A-Za-z_.]+]]] unwind to caller
// CHECK-WINMSVC-FUNCLET: [[CATCH]]:
// CHECK-WINMSVC-FUNCLET: catchpad within %{{[0-9A-Za-z_.]+}} [ptr null, i32 0, ptr %[[EXNSLOT]]]
// CHECK-WINMSVC-FUNCLET: load ptr, ptr %[[EXNSLOT]], align 4
// CHECK-WINMSVC-FUNCLET: catchret from %{{[0-9A-Za-z_.]+}} to label %[[CATCHRETDEST:[0-9A-Za-z_.]+]]
// CHECK-WINMSVC-FUNCLET: [[CATCHRETDEST]]:
// CHECK-WINMSVC-FUNCLET-WRAPPED-LABEL: define dso_local void @f()
// CHECK-WINMSVC-FUNCLET-WRAPPED-SAME: personality ptr @__CxxFrameHandler3 {
// CHECK-WINMSVC-FUNCLET-WRAPPED: %[[EXNSLOT:[0-9A-Za-z_.]+]] = alloca ptr, align 4
// CHECK-WINMSVC-FUNCLET-WRAPPED: invoke void @g()
// CHECK-WINMSVC-FUNCLET-WRAPPED: catchswitch within none [label %[[CATCH:[0-9A-Za-z_.]+]]] unwind to caller
// CHECK-WINMSVC-FUNCLET-WRAPPED: [[CATCH]]:
// CHECK-WINMSVC-FUNCLET-WRAPPED: %[[PAD:[0-9A-Za-z_.]+]] = catchpad within %{{[0-9A-Za-z_.]+}} [ptr null, i32 0, ptr %[[EXNSLOT]]]
// CHECK-WINMSVC-FUNCLET-WRAPPED: %[[EXN:[0-9A-Za-z_.]+]] = load ptr, ptr %[[EXNSLOT]], align 4
// CHECK-WINMSVC-FUNCLET-WRAPPED: call ptr @objc_begin_catch(ptr %[[EXN]]){{.*}}[ "funclet"(token %[[PAD]]) ]
// CHECK-WINMSVC-FUNCLET-WRAPPED: call void @objc_end_catch(){{.*}}[ "funclet"(token %[[PAD]]) ]
// CHECK-WINMSVC-FUNCLET-WRAPPED: catchret from %[[PAD]] to label %[[CATCHRETDEST:[0-9A-Za-z_.]+]]
// CHECK-WINMSVC-FUNCLET-WRAPPED: [[CATCHRETDEST]]:
// CHECK-WIN-GNU: personality ptr @__gxx_personality_seh0

// CHECK-MACOSX-FRAGILE-MINGW-DWARF: personality ptr @__gcc_personality_v0
// CHECK-MACOSX-FRAGILE-MINGW-SEH: personality ptr @__gcc_personality_seh0
// CHECK-MACOSX-FRAGILE-MINGW-SJLJ: personality ptr @__gcc_personality_sj0

void f(void) {
  @try {
    g();
  } @catch (...) {
  }
}

#if defined(__SEH_EXCEPTIONS__)
// CHECK-WIN-SEH-X86: personality ptr @_except_handler3
// CHECK-WIN-SEH-X64: personality ptr @__C_specific_handler

void h(void) {
  __try {
    i();
  } __finally {
  }
}
#endif
