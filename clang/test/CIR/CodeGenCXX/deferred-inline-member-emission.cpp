// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s

// Regression test: CIRGenModule::release() must still emit
// referenced inline special members even when using the sema/template
// deferred-emission path.

struct S {
  S();
  ~S();
};

struct T {
  S s;
  T() = default;
  ~T() = default;
};

void use() {
  T t;
}

// CIR-LABEL: cir.func {{.*}} @_ZN1TC1Ev
// CIR:       cir.call @_ZN1TC2Ev
// CIR-LABEL: cir.func {{.*}} @_ZN1TD1Ev
// CIR:       cir.call @_ZN1TD2Ev
// CIR-LABEL: cir.func {{.*}} @_Z3usev
// CIR:       cir.call @_ZN1TC1Ev
// CIR:       cir.call @_ZN1TD1Ev
