// RUN: %clang_cc1 -x objective-c++ -triple x86_64-apple-darwin10 -emit-llvm -fcxx-exceptions -fexceptions -fobjc-exceptions -fclangir -o - %s | FileCheck %s

@interface OCType @end
void opaque();

namespace test0 {

// CHECK-LABEL: define{{.*}} void @_ZN5test03fooEv(
// CHECK-SAME: personality ptr @__objc_personality_v0
void foo() {
  try {
    // CHECK: invoke void @_Z6opaquev
    opaque();
  } catch (OCType *T) {
    // CHECK: landingpad { ptr, i32 }
    // CHECK-NEXT: catch ptr @"OBJC_EHTYPE_$_OCType"
    // CHECK: [[TYPEID:%.*]] = call i32 @llvm.eh.typeid.for.p0(ptr @"OBJC_EHTYPE_$_OCType")
    // CHECK: call ptr @__cxa_begin_catch(ptr {{%.*}})
    // CHECK: call void @__cxa_end_catch()
    (void)T;
  }
}

} // namespace test0

@interface NSException
+ new;
@end

namespace test1 {

// CHECK-LABEL: define{{.*}} void @_ZN5test13barEv(
// CHECK-SAME: personality ptr @__objc_personality_v0
void bar() {
  @try {
    // CHECK: [[CALL:%.*]] = invoke noundef ptr @objc_msgSend
    // CHECK: invoke void @objc_exception_throw(ptr [[CALL]]) #[[NR:[0-9]+]]
    throw [NSException new];
  } @catch (id i) {
    (void)i;
  }
}

} // namespace test1

// CHECK: attributes #[[NR]] = { noreturn }
