// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fcxx-exceptions -fexceptions -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fcxx-exceptions -fexceptions -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s

// Verify that pre-existing RAII scopes (~Guard) are NOT destroyed
// twice on the constructor unwind path during throw expressions. Previously,
// ~Guard was called both in the per-call cleanup and in the enclosing
// TryOp handler, causing double-destroy (undefined behavior).

struct Guard {
  Guard();
  ~Guard();
};

struct Error {
  Error(int);
};

struct X {
  X();
  ~X();
  operator int();
};

void foo() {
  Guard g;
  throw Error(X());
}

// The key invariant: ~Guard (GuardD1Ev) must be called exactly once on each
// unwind path. It should appear only in the common resume block, not in
// individual per-call cleanup landing pads.

// CIR: Each per-call landing pad should NOT call ~Guard — it should flow
// to a common block that calls ~Guard once before resume.

// CHECK-LABEL: define {{.*}} @_Z3foov()
// CHECK: invoke void @_ZN1XC1Ev(
// CHECK-NEXT: to label %[[XCONT:.*]] unwind label %[[XLP:.*]]

// X() constructor landing pad — should NOT call ~Guard here.
// CHECK: [[XLP]]:
// CHECK: landingpad
// CHECK: call void @__cxa_free_exception(
// CHECK-NOT: call void @_ZN5GuardD1Ev(
// CHECK: br label %[[COMMON:.*]]

// Common resume block — ~Guard is called exactly once here.
// CHECK: [[COMMON]]:
// CHECK: call void @_ZN5GuardD1Ev(
// CHECK: resume

// OGCG confirms: ~Guard is called exactly once in the common ehcleanup block.
// OGCG-LABEL: define {{.*}} @_Z3foov()
// OGCG: call void @_ZN5GuardD1Ev(
// OGCG-NOT: call void @_ZN5GuardD1Ev(
// OGCG: resume
