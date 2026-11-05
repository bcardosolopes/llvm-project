// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=LLVM

// Verify that variables with non-trivial destructors declared in unbraced loop
// bodies have their destructors called at the end of each iteration, not once
// after the loop. This matches OGCG's RunCleanupsScope behavior.

struct S {
  S(int i);
  ~S();
};

// CIR-LABEL: cir.func{{.*}} @_Z8for_loopv
// CIR:         cir.for
// CIR:         } body {
// CIR-NEXT:      cir.scope {
// CIR:             cir.call @_ZN1SC1Ei
// CIR:             cir.call @_ZN1SD1Ev
// CIR:           }

// LLVM-LABEL: define{{.*}} @_Z8for_loopv
// LLVM:         call void @_ZN1SC1Ei
// LLVM-NEXT:    call void @_ZN1SD1Ev
void for_loop() {
  for (int i = 0; i < 5; ++i)
    S s(i);
}

// CIR-LABEL: cir.func{{.*}} @_Z10while_loopv
// CIR:         cir.while
// CIR:         } do {
// CIR-NEXT:      cir.scope {
// CIR:             cir.call @_ZN1SC1Ei
// CIR:             cir.call @_ZN1SD1Ev
// CIR:           }

// LLVM-LABEL: define{{.*}} @_Z10while_loopv
// LLVM:         call void @_ZN1SC1Ei
// LLVM-NEXT:    call void @_ZN1SD1Ev
void while_loop() {
  int i = 0;
  while (i++ < 5)
    S s(i);
}

// CIR-LABEL: cir.func{{.*}} @_Z13do_while_loopv
// CIR:         cir.do {
// CIR-NEXT:      cir.scope {
// CIR:             cir.call @_ZN1SC1Ei
// CIR:             cir.call @_ZN1SD1Ev
// CIR:           }

// LLVM-LABEL: define{{.*}} @_Z13do_while_loopv
// LLVM:         call void @_ZN1SC1Ei
// LLVM-NEXT:    call void @_ZN1SD1Ev
void do_while_loop() {
  int i = 0;
  do
    S s(i);
  while (i++ < 5);
}
