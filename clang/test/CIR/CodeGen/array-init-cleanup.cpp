// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir \
// RUN:   -fcxx-exceptions -fexceptions -std=c++17
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll \
// RUN:   -fcxx-exceptions -fexceptions -std=c++17
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Test that aggregate-initialized arrays with non-trivially-destructible
// elements do not double-destroy elements during exception unwind.
// The partial-array cleanup (for exception safety during init) must be
// deactivated after initialization completes, since the full-array
// destructor handles cleanup on the normal exit path.

struct S {
  int id;
  S(int i);
  ~S();
};

void may_throw();

// CIR-LABEL: @_Z25test_array_aggregate_initv
void test_array_aggregate_init() {
  S arr[3] = {{10}, {11}, {12}};
  may_throw();
}

// Verify endOfInit tracking alloca exists for partial-array cleanup.
// CIR: cir.alloca !cir.ptr<!rec_S>, {{.*}} ["arrayinit.endOfInit"]

// Verify constructor calls are invoked with exception handling.
// CIR: cir.call exception @_ZN1SC1Ei

// Verify normal-path array destructor exists.
// CIR: cir.call @_ZN1SD1Ev

// At LLVM level, verify invoke is used for constructors (exception-aware).
// LLVM-LABEL: define {{.*}} @_Z25test_array_aggregate_initv
// LLVM: invoke void @_ZN1SC1Ei
