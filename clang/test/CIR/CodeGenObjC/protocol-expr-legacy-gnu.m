// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -disable-llvm-passes -fobjc-runtime=gcc -fclangir -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -disable-llvm-passes -fobjc-runtime=gnustep-1.6 -fclangir -o - %s | FileCheck %s

@protocol P0
@end

void *foo(void) {
  return @protocol(P0);
}

// CHECK: @.objc_protocol = internal global
// CHECK-LABEL: define ptr @foo()
// CHECK: ret ptr @.objc_protocol
