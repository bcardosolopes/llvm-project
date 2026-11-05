// RUN: %clang_cc1 -triple i386-apple-darwin10 -fobjc-runtime=macosx-fragile-10.5 -emit-llvm -fexceptions -fobjc-exceptions -fcxx-exceptions -fclangir -o - %s | FileCheck %s

void foo(void);

void test0(id obj) {
  @synchronized(obj) {
    foo();
  }
}

// CHECK-LABEL: define{{.*}} void @_Z5test0P11objc_object(
// CHECK-SAME: personality ptr @__gxx_personality_v0
// CHECK: call i32 @objc_sync_enter(ptr [[OBJ:%.*]])
// CHECK: call void @objc_exception_try_enter(ptr [[BUF:%.*]])
// CHECK: call i32 @_setjmp(ptr {{.*}})
// CHECK: call void asm sideeffect "", "*m,*m,*m{{.*}}"
// CHECK: invoke void @_Z3foov()
// CHECK: landingpad { ptr, i32 }
// CHECK-NEXT: cleanup
// CHECK: call void @objc_exception_try_exit(ptr [[BUF]])
// CHECK: [[EH_SYNC:%.*]] = load ptr, ptr
// CHECK: call i32 @objc_sync_exit(ptr [[EH_SYNC]])
// CHECK: resume
// CHECK: call void asm sideeffect "", "=*m,=*m,=*m{{.*}}"
// CHECK: [[OBJC_SYNC:%.*]] = load ptr, ptr
// CHECK: call i32 @objc_sync_exit(ptr [[OBJC_SYNC]])
// CHECK: [[CAUGHT:%.*]] = call ptr @objc_exception_extract(ptr [[BUF]])
// CHECK: call void @objc_exception_throw(ptr [[CAUGHT]])
// CHECK: unreachable
// CHECK: call void @objc_exception_try_exit(ptr [[BUF]])
// CHECK: [[SYNC:%.*]] = load ptr, ptr
// CHECK: call i32 @objc_sync_exit(ptr [[SYNC]])
// CHECK: ret void
