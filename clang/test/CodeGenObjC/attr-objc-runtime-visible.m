// RUN: %clang_cc1 -triple x86_64-apple-darwin -fobjc-runtime=macosx-10.9.0 -emit-llvm %s -o - | FileCheck %s --check-prefix=CHECK
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-apple-darwin -fobjc-runtime=macosx-10.9.0 -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=CIR %}

// RUN: %clang_cc1 -triple i386-apple-darwin -fobjc-runtime=macosx-fragile-10.9.0 -emit-llvm %s -o - | FileCheck %s --check-prefix=CHECK
// RUN: %if cir-support %{ %clang_cc1 -triple i386-apple-darwin -fobjc-runtime=macosx-fragile-10.9.0 -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=CIR %}

@interface Root
+(Class)class;
@end

__attribute__((objc_runtime_visible))
__attribute__((objc_runtime_name("MyRuntimeVisibleClass")))
@interface A : Root
@end

// CHECK: [[CLASSNAME:@.*]] = private unnamed_addr constant [22 x i8] c"MyRuntimeVisibleClass
// CHECK: define{{.*}} ptr @getClass() #0 {
Class getClass(void) {
  // CHECK: call ptr @objc_lookUpClass(ptr [[CLASSNAME]]) #2
  // CIR: [[CLASSNAME:@.*]] = private unnamed_addr constant [22 x i8] c"MyRuntimeVisibleClass
  // CIR-NOT: OBJC_CLASSLIST_REFERENCES
  // CIR-NOT: OBJC_CLASS_$_MyRuntimeVisibleClass
  // CIR: define{{.*}} ptr @getClass() #[[ATTRS:[0-9]+]] {
  // CIR: call ptr @objc_lookUpClass(ptr [[CLASSNAME]])
  return [A class];
}
