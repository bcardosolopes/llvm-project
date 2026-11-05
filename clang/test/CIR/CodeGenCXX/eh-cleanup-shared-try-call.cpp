// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir \
// RUN:   -fcxx-exceptions -fexceptions %s -o %t.cir
// RUN: FileCheck %s --check-prefix=CIR --input-file=%t.cir
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -fcxx-exceptions -fexceptions %s -o %t.ll
// RUN: FileCheck %s --check-prefix=LLVM --input-file=%t.ll

// Regression test: two throwing calls at the same EH cleanup depth share a
// single cir.cleanup.scope -- one cleanup region for both, no per-call cleanup
// -- and final LLVM still routes both invokes to the shared landing pad that
// destroys all in-scope RAII objects.

struct Guard {
  Guard();
  ~Guard();
};

void may_throw_1();
void may_throw_2();

// CIR-LABEL: cir.func{{.*}} @_Z17test_shared_cleanv
// CIR:       cir.cleanup.scope {
// CIR-NEXT:    cir.call exception @_Z11may_throw_1v() : () -> ()
// CIR-NEXT:    cir.call exception @_Z11may_throw_2v() : () -> ()
// CIR-NEXT:    cir.yield
// CIR-NEXT:  } cleanup {
// CIR-NEXT:    cir.call @_ZN5GuardD1Ev
// CIR-NEXT:    cir.yield
// CIR-NEXT:  }

// LLVM-LABEL: define {{.*}}void @_Z17test_shared_cleanv()
void test_shared_clean() {
  Guard g;
  may_throw_1();
  may_throw_2();
}

// Both may_throw_1 and may_throw_2 must be invokes with landing pads that
// call ~Guard(). Since their cleanups are equivalent, they may share a
// single landing pad.
//
// LLVM: invoke void @_Z11may_throw_1v()
// LLVM-NEXT: to label %{{.*}} unwind label %[[LP:.*]]
//
// LLVM: invoke void @_Z11may_throw_2v()
// LLVM-NEXT: to label %{{.*}} unwind label %[[LP]]
//
// The shared landing pad must call the Guard destructor:
// LLVM: [[LP]]:
// LLVM: landingpad
// LLVM: call void @_ZN5GuardD1Ev(
