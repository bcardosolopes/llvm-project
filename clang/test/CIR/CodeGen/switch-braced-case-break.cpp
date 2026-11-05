// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

// Verify that break after brace-scoped case/default labels compiles
// correctly. The braces around the label create a scope, and the break
// must still target the enclosing switch.

void foo();

// CIR-LABEL: @_Z5test1v
// CIR-NOT: cir.switch
// CIR: cir.scope
// CIR: cir.call @_Z3foov
//
// LLVM-LABEL: @_Z5test1v
// LLVM-NOT: switch i32
// LLVM: call void @_Z3foov()
void test1() {
  switch (1) {
    { default: ; }
    foo();
    break;
  }
}

// CIR-LABEL: @_Z5test2v
// CIR-NOT: cir.switch
// CIR: cir.scope
// CIR: cir.call @_Z3foov
//
// LLVM-LABEL: @_Z5test2v
// LLVM-NOT: switch i32
// LLVM: call void @_Z3foov()
void test2() {
  switch (0) {
    { case 0: ; }
    foo();
    break;
  }
}
