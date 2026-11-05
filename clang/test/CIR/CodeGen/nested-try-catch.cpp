// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
//
// Regression test: uninitialized scopeMayThrow
// caused outer catch handlers to be erased in nested try-catch.

void may_throw();

// CHECK-LABEL: define {{.*}}@_Z4testv()
int test() {
    try {
        try {
            may_throw();
        } catch (...) {
            throw;
        }
    } catch (...) {
        return 1;
    }
    return 0;
}

// Inner try: invoke to may_throw
// CHECK: invoke void @_Z9may_throwv()

// Inner catch: landingpad + begin_catch + rethrow
// CHECK: landingpad
// CHECK: call ptr @__cxa_begin_catch
// CHECK: invoke void @__cxa_rethrow()

// Outer catch: second landingpad must exist (was being dropped)
// CHECK: landingpad
// CHECK: invoke {{.*}}@__cxa_end_catch
// CHECK: call ptr @__cxa_begin_catch
