// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcxx-exceptions -fexceptions -std=c++20 %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fcxx-exceptions -fexceptions -std=c++20 %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

// When a throwing call inside a catch handler unwinds, __cxa_end_catch must
// run BEFORE outer RAII destructors on the unwind path. This matches OGCG's
// ordering.

struct S { ~S(); };
void may_throw();

void test() {
  S s;
  try { may_throw(); }
  catch (...) {
    may_throw();
  }
}

// CIR-LABEL: cir.func {{.*}}@_Z4testv()
// CIR:   cir.try
// CIR:   catch all {
// CIR:     cir.call exception @_Z9may_throwv() : () -> () cleanup {
// CIR:       cir.catch_param end
// CIR:       cir.call @_ZN1SD1Ev
// CIR:       cir.yield

// On the EH unwind path, __cxa_end_catch must come BEFORE ~S().
// LLVM-LABEL: define {{.*}}@_Z4testv()
// LLVM:   invoke void @_Z9may_throwv()
// LLVM:     to label %{{.*}} unwind label %[[LPAD:[0-9]+]]
// LLVM: [[LPAD]]:
// LLVM:   landingpad { ptr, i32 }
// LLVM:     cleanup
// LLVM:   invoke void @__cxa_end_catch()
// LLVM:     to label %[[CONT:[0-9]+]] unwind label %
// LLVM: [[CONT]]:
// LLVM:   call void @_ZN1SD1Ev(
// LLVM:   resume { ptr, i32 }
