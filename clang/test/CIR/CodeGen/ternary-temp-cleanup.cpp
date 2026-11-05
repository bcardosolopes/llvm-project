// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

// Test that ternary expressions with temporaries from binary operators
// destroy temporaries only from the evaluated branch.
// Previously, ALL temporaries from BOTH branches were destroyed
// unconditionally, causing use-after-free on unevaluated branch objects.

struct S {
    int* p;
    S(int v) { p = new int(v); }
    S(const S& o) { p = new int(*o.p); }
    ~S() { delete p; }
    S operator+(const S& o) const { return S(*p + *o.p); }
};

int test(bool cond) {
    S result = cond ? S(10) + S(20) : S(30) + S(40);
    return *result.p;
}

// CIR: cir.func {{.*}}@_Z4testb
// Constructors stay in their branches, while the guarded temporary destroys
// happen after the conditional so only the evaluated branch's temporaries fire.
// CIR: cir.if
// CIR:   cir.call @_ZN1SC1Ei
// CIR:   cir.call @_ZN1SC1Ei
// CIR:   cir.call @_ZNK1SplERKS_
// CIR: } else {
// CIR:   cir.call @_ZN1SC1Ei
// CIR:   cir.call @_ZN1SC1Ei
// CIR:   cir.call @_ZNK1SplERKS_
// CIR: } loc(
// CIR: cir.if %{{.*}} {
// CIR:   cir.call @_ZN1SD1Ev
// CIR: } loc(
// CIR: cir.if %{{.*}} {
// CIR:   cir.call @_ZN1SD1Ev
// CIR: } loc(
// CIR: cir.if %{{.*}} {
// CIR:   cir.call @_ZN1SD1Ev
// CIR: } loc(
// CIR: cir.if %{{.*}} {
// CIR:   cir.call @_ZN1SD1Ev
// CIR: } loc(
// CIR:   cir.call @_ZN1SD1Ev

// LLVM: define {{.*}}@_Z4testb
