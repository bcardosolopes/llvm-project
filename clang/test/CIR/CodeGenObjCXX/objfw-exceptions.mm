// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -emit-llvm -fclangir -fexceptions -fobjc-exceptions -fobjc-runtime=objfw -fcxx-exceptions -o - %s | FileCheck %s

@interface OCType @end
void opaque();

// CHECK: @__objc_eh_str_OCType = private unnamed_addr constant [7 x i8] c"OCType\00", align 1

// CHECK-LABEL: define{{.*}} void @_Z3foov()
// CHECK-SAME: personality ptr @__gnu_objc_personality_v0
void foo() {
  try {
    // CHECK: invoke void @_Z6opaquev
    opaque();
  } catch (OCType *T) {
    // CHECK: landingpad { ptr, i32 }
    // CHECK-NEXT: catch ptr @__objc_eh_str_OCType
    (void)T;
  }
}
