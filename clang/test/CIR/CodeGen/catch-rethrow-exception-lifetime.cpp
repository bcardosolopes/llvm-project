// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Regression test: exception objects with destructors were not
// destroyed after catch-by-reference + rethrow because __cxa_end_catch()
// was missing from the rethrow's cleanup landing pad.
//
// The rethrow landing pad must call __cxa_end_catch() to balance the
// handler's __cxa_begin_catch(), ensuring correct handler count bookkeeping
// in the Itanium C++ ABI so the exception object's destructor runs.

struct Ex { ~Ex(); };

void test_rethrow() {
    try {
        try {
            throw Ex{};
        } catch (Ex&) {
            throw;  // rethrow
        }
    } catch (Ex&) {
        // outer catch
    }
}

// CHECK-LABEL: define {{.*}}@_Z12test_rethrowv()
// CHECK: personality ptr @__gxx_personality_v0
//
// The rethrow must be an invoke (not a call) so it can unwind to a
// cleanup landing pad.
// CHECK: call ptr @__cxa_begin_catch(
// CHECK: invoke void @__cxa_rethrow()
// CHECK:   to label %{{.*}} unwind label %[[RETHROW_LP:[0-9]+]]
//
// The rethrow landing pad must call __cxa_end_catch() for correct
// handler count bookkeeping, then resume unwinding.
// CHECK: [[RETHROW_LP]]:
// CHECK:   landingpad { ptr, i32 }
// CHECK:     cleanup
// CHECK:   invoke void @__cxa_end_catch()
