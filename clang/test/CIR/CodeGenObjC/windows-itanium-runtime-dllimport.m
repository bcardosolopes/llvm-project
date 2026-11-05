// RUN: %clang_cc1 -triple i686-windows-itanium -fms-extensions -fobjc-runtime=macosx -fdeclspec -fobjc-exceptions -fclangir -emit-llvm -o - %s | FileCheck %s
// REQUIRES: x86-registered-target

__declspec(dllimport)
@interface I
+ (instancetype)new;
@end

void g(void) {
  @throw([I new]);
}

// CHECK: declare dllimport ptr @objc_msgSend(ptr, ptr, ...)
// CHECK: declare dllimport void @objc_exception_throw(ptr)
// CHECK-NOT: declare dso_local ptr @objc_msgSend
// CHECK-NOT: declare dso_local void @objc_exception_throw
