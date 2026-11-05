// RUN: %clang_cc1 -triple thumbv7--windows-itanium -fobjc-runtime=ios -O1 -fexceptions -fobjc-exceptions -fclangir -emit-llvm -o - %s | FileCheck %s
// REQUIRES: arm-registered-target

void (*g)(id);

@interface NSNumber
+ (NSNumber *)numberWithInt:(int)i;
@end

void f(void) {
  @try {
    @throw(@1);
  } @catch (id i) {
    (*g)(i);
  }
}

// CHECK: declare dllimport arm_aapcs_vfpcc ptr @objc_msgSend(ptr, ptr, ...) local_unnamed_addr
// CHECK: declare dllimport arm_aapcs_vfpcc void @objc_exception_throw(ptr) local_unnamed_addr
// CHECK: declare dllimport arm_aapcs_vfpcc ptr @objc_begin_catch(ptr) local_unnamed_addr
// CHECK: declare dllimport arm_aapcs_vfpcc void @objc_end_catch() local_unnamed_addr
// CHECK-LABEL: define dso_local arm_aapcs_vfpcc void @f()
// CHECK-NOT: declare ptr @objc_begin_catch
// CHECK-NOT: declare void @objc_end_catch
