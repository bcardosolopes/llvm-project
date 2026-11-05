// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Regression test for rethrow inside nested try-catch within catch handler.
// When throw; is inside a try-catch nested within a catch handler, RAII
// objects in the inner try scope must be destroyed during the rethrow.
// The rethrow cleanup count heuristic must correctly identify RAII cleanups
// that are separated from __cxa_end_catch by an inner CatchScope.

struct S { ~S(); };

void test_rethrow_nested_cleanup() {
    try {
        try {
            throw 42;
        } catch (int) {
            try {
                S deep;
                throw;  // rethrow — deep must be destroyed
            } catch (...) {
                // catches the rethrown exception
            }
        }
    } catch (...) {
        // outer catch
    }
}

// CHECK-LABEL: define {{.*}}@_Z27test_rethrow_nested_cleanupv()
// CHECK: personality ptr @__gxx_personality_v0
//
// The rethrow must be an invoke (exception can propagate).
// CHECK: invoke void @__cxa_rethrow()
// CHECK-NEXT: to label %{{.*}} unwind label %[[LPAD:.*]]
//
// The landing pad from the rethrow must call ~S() for cleanup before
// dispatching to the catch-all handler.
// CHECK: [[LPAD]]:
// CHECK-NEXT: %{{.*}} = landingpad
// CHECK: call void @_ZN1SD1Ev(
