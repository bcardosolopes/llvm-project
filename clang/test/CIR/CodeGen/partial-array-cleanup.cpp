// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcxx-exceptions -fexceptions %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -emit-llvm -fcxx-exceptions -fexceptions %s -o %t-ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t-ogcg.ll %s

// Test that partial array destruction cleanup uses NormalAndEHCleanup,
// matching OGCG. The cleanup should appear both on the EH path (inside
// cir.try cleanup blocks) and on the normal exit path (scope destructor
// loop after all elements are constructed).

struct S {
  S(int);
  ~S();
};

// CIR-LABEL: @_Z4testv
// CIR: "arrayinit.endOfInit"
// CIR: cir.call exception @_ZN1SC1Ei
// CIR: cir.call exception @_ZN1SC1Ei
// CIR: cir.call exception @_ZN1SC1Ei
// CIR: cir.call @_ZN1SD1Ev

// LLVM-LABEL: @_Z4testv
// LLVM: invoke void @_ZN1SC1Ei
// LLVM: call void @_ZN1SD1Ev

// OGCG-LABEL: @_Z4testv
// OGCG: invoke void @_ZN1SC1Ei
// OGCG: call void @_ZN1SD1Ev
void test() {
  S arr[] = {S(1), S(2), S(3)};
}
