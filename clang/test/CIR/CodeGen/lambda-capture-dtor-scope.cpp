// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify that lambda captured fields with non-trivial destructors are NOT
// destroyed prematurely. The lambda closure destructor handles destroying
// captured fields at scope exit.
//
// Previously, pushDestroy emitted an inline destructor call for each captured
// field right after copy-construction, AND the lambda closure destructor also
// destroyed the field at scope exit — premature + double destruction.

struct G {
    G();
    G(const G&);
    ~G();
};

int test_lambda_capture_scope() {
    G g;
    auto f = [g]() {};
    f();
    return 0;
}

// CHECK: define {{.*}}@_Z25test_lambda_capture_scopev
// After copy construction, the normal path should branch without calling
// a destructor. The destructor for g is only at scope exit.
// CHECK: invoke void @_ZN1GC1ERKS_
// CHECK-NEXT: to label %[[NORM:[0-9]+]] unwind
// The normal successor should NOT call a destructor prematurely.
// CHECK: [[NORM]]:
// CHECK-NOT: @_ZN1GD
// The lambda call itself is known nounwind in direct-CIR, so the critical
// invariant here is that destruction still happens only at scope exit.
// CHECK: call void @"_ZZ25test_lambda_capture_scopevENK3$_0clEv"
// CHECK: call void @"_ZZ25test_lambda_capture_scopevEN3$_0D1Ev"
// CHECK: call void @_ZN1GD1Ev
