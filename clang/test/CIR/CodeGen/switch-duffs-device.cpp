// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

// Duff's device pattern: default label inside do-while within switch.
// This exercises cross-scope case labels where the case/default label
// is nested inside a loop that is inside a switch case.

// CIR-LABEL: @_Z24test_default_in_do_whilev
// LLVM-LABEL: @_Z24test_default_in_do_whilev
void test_default_in_do_while() {
  switch(0) {
  case 0:
    do {
    // CIR: cir.case(default, [])
    default:;
    } while(0);
  }
}
// LLVM: switch i32 0
// LLVM: i32 0, label

// Duff's device pattern: case label inside do-while within switch.

// CIR-LABEL: @_Z21test_case_in_do_whilev
// LLVM-LABEL: @_Z21test_case_in_do_whilev
void test_case_in_do_while() {
  switch(1) {
  case 0:
    do {
    // CIR: cir.case(equal, [#cir.int<1>
    case 1:;
    } while(0);
  }
}
// LLVM: switch i32 1
// LLVM: i32 0, label
// LLVM: i32 1, label
