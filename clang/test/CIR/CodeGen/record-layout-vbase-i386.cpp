// RUN: %clang_cc1 -triple i386-unknown-unknown -fclangir -clangir-disable-passes -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple i386-unknown-unknown -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// Regression: i386 + virtual inheritance used to assert in
// CIRRecordLayoutBuilder during CIR generation.
//
// Keep CIR-emission coverage with passes disabled, and also lock in that the
// current direct-CIR LLVM path handles the same virtual-inheritance surface.

struct A {
  virtual void f();
  int a;
};

struct B : virtual A {
  virtual void f();
  int b;
};

void B::f() {}

// CIR: cir.func{{.*}} @_ZN1B1fEv
// LLVM: @_ZTV1B = unnamed_addr constant
// LLVM: @_ZTT1B = unnamed_addr constant
// LLVM: define dso_local void @_ZN1B1fEv
// LLVM: define dso_local void @_ZTv0_n12_N1B1fEv
// LLVM: tail call void @_ZN1B1fEv
