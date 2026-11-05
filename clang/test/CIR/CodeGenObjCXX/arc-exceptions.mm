// RUN: %clang_cc1 -x objective-c++ -triple x86_64-apple-darwin10 -emit-llvm -fobjc-arc -fexceptions -fobjc-exceptions -fcxx-exceptions -fobjc-runtime-has-weak -fobjc-arc-exceptions -fclangir -o - %s | FileCheck %s

@class Ety;
void helper(void);
__attribute__((ns_returns_retained)) id retainedHelper(unsigned);

// CHECK-LABEL: define{{.*}} void @_Z10testStrongv(
void testStrong(void) {
  try {
    helper();
  } catch (Ety *e) {
  }
}
// CHECK:      %[[E:.*]] = alloca ptr, i64 1, align 8
// CHECK-NEXT: invoke void @_Z6helperv()
// CHECK:      %[[T0:.*]] = call ptr @__cxa_begin_catch(ptr {{%.*}})
// CHECK-NEXT: %[[RET:.*]] = call ptr @llvm.objc.retain(ptr %[[T0]])
// CHECK-NEXT: store ptr %[[RET]], ptr %[[E]], align 8
// CHECK-NEXT: call void @llvm.objc.storeStrong(ptr %[[E]], ptr null)
// CHECK-NEXT: call void @__cxa_end_catch()

// CHECK-LABEL: define{{.*}} void @_Z8testWeakv(
void testWeak(void) {
  try {
    helper();
  } catch (Ety * __weak e) {
  }
}
// CHECK:      %[[E:.*]] = alloca ptr, i64 1, align 8
// CHECK-NEXT: invoke void @_Z6helperv()
// CHECK:      %[[T0:.*]] = call ptr @__cxa_begin_catch(ptr {{%.*}})
// CHECK-NEXT: call ptr @llvm.objc.initWeak(ptr %[[E]], ptr %[[T0]])
// CHECK-NEXT: call void @llvm.objc.destroyWeak(ptr %[[E]])
// CHECK-NEXT: call void @__cxa_end_catch()

// CHECK-LABEL: define{{.*}} void @_Z10testUnsafev(
void testUnsafe(void) {
  try {
    helper();
  } catch (Ety * __unsafe_unretained e) {
  }
}
// CHECK:      %[[E:.*]] = alloca ptr, i64 1, align 8
// CHECK-NEXT: invoke void @_Z6helperv()
// CHECK:      %[[T0:.*]] = call ptr @__cxa_begin_catch(ptr {{%.*}})
// CHECK-NEXT: store ptr %[[T0]], ptr %[[E]], align 8
// CHECK-NEXT: call void @__cxa_end_catch()

// CHECK-LABEL: define{{.*}} void @_Z5test5v(
// Cleanup-only ARC EH does not get a per-function personality split when the
// same TU still uses the ObjC personality for typed catches.
// CHECK-SAME: personality ptr @__objc_personality_v0
void test5(void) {
  id array[][2] = {
    retainedHelper(0),
    retainedHelper(1),
    retainedHelper(2),
    retainedHelper(3),
  };
}
// CHECK:      invoke noundef ptr @_Z14retainedHelperj(i32 noundef 0)
// CHECK:      landingpad { ptr, i32 }
// CHECK:      call void @llvm.objc.storeStrong(ptr {{%.*}}, ptr null)

// CHECK-LABEL: define{{.*}} void @_Z17testAutoreleasingv(
void testAutoreleasing(void) {
  try {
    helper();
  } catch (Ety * __autoreleasing e) {
  }
}
// CHECK:      %[[E:.*]] = alloca ptr, i64 1, align 8
// CHECK-NEXT: invoke void @_Z6helperv()
// CHECK:      %[[T0:.*]] = call ptr @__cxa_begin_catch(ptr {{%.*}})
// CHECK-NEXT: store ptr %[[T0]], ptr %[[E]], align 8
// CHECK-NEXT: call void @__cxa_end_catch()
