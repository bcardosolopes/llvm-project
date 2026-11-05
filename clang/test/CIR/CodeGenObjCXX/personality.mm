// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -fclangir -fexceptions -fobjc-exceptions -fcxx-exceptions -fobjc-runtime=macosx -o - %s | FileCheck %s --check-prefix=CHECK-MAC
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -fclangir -fexceptions -fobjc-exceptions -fcxx-exceptions -fobjc-runtime=gnustep -o - %s | FileCheck %s --check-prefix=CHECK-GNUSTEP
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -fclangir -fexceptions -fobjc-exceptions -fcxx-exceptions -fobjc-runtime=objfw -o - %s | FileCheck %s --check-prefix=CHECK-OBJFW
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -fclangir -fexceptions -fobjc-exceptions -fcxx-exceptions -fobjc-runtime=gcc -o - %s | FileCheck %s --check-prefix=CHECK-GCC

void opaque(void);

// CHECK-MAC-LABEL: define{{.*}} void @_Z4testv()
// CHECK-MAC-SAME: personality ptr @__objc_personality_v0
// CHECK-MAC:      landingpad { ptr, i32 }
// CHECK-MAC-NEXT:   catch ptr @OBJC_EHTYPE_id
// CHECK-MAC:      call ptr @objc_begin_catch(ptr
// CHECK-MAC:      call void @objc_end_catch()

// CHECK-GNUSTEP-LABEL: define{{.*}} void @_Z4testv()
// CHECK-GNUSTEP-SAME: personality ptr @__gnustep_objcxx_personality_v0
// CHECK-GNUSTEP:      landingpad { ptr, i32 }
// CHECK-GNUSTEP-NEXT:   catch ptr @__objc_id_type_info
// CHECK-GNUSTEP:      call ptr @__cxa_begin_catch(ptr
// CHECK-GNUSTEP:      call void @__cxa_end_catch()

// CHECK-OBJFW:       @[[IDSTR:[^ ]+]] = private unnamed_addr constant [4 x i8] c"@id\00", align 1
// CHECK-OBJFW-LABEL: define{{.*}} void @_Z4testv()
// CHECK-OBJFW-SAME: personality ptr @__gnu_objc_personality_v0
// CHECK-OBJFW:      landingpad { ptr, i32 }
// CHECK-OBJFW-NEXT:   catch ptr @[[IDSTR]]
// CHECK-OBJFW:      call i32 @llvm.eh.typeid.for.p0(ptr @[[IDSTR]])
// CHECK-OBJFW-NOT:  begin_catch
// CHECK-OBJFW-NOT:  end_catch

// CHECK-GCC-LABEL: define{{.*}} void @_Z4testv()
// CHECK-GCC-SAME: personality ptr @__gnu_objc_personality_v0
// CHECK-GCC:      landingpad { ptr, i32 }
// CHECK-GCC-NEXT:   catch ptr null
// CHECK-GCC-NOT:  llvm.eh.typeid.for
// CHECK-GCC-NOT:  begin_catch
// CHECK-GCC-NOT:  end_catch

void test(void) {
  @try {
    opaque();
  } @catch (id xception) {
    (void)xception;
  }
}
