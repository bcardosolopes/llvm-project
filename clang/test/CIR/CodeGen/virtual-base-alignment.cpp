// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s -check-prefix=OGCG
//
// Fixed — constructors for classes with virtual bases now use
// members get wrong `this` pointer alignment. The alignment from the
// virtual base leaks into the derived class constructor's `this` parameter.
//
// B and C constructors should have `align 8` on `this` (the derived class's
// own alignment), NOT `align 16` (from virtual base A's aligned member).

struct A {
  __attribute__((aligned(16))) double data1;
};

struct B : public virtual A {
  double a;
  int x;
};
B b_inst;

struct C : public virtual B {};
C c_inst;

// B's C1 constructor: this should be align 8, not align 16
// CIR-LABEL: define {{.*}} @_ZN1BC1Ev
// CIR-SAME: ptr noundef nonnull align 8

// OGCG-LABEL: define {{.*}} @_ZN1BC1Ev
// OGCG-SAME: ptr noundef nonnull align 8

// C's C1 constructor: this should be align 8, not align 16
// CIR-LABEL: define {{.*}} @_ZN1CC1Ev
// CIR-SAME: ptr noundef nonnull align 8

// OGCG-LABEL: define {{.*}} @_ZN1CC1Ev
// OGCG-SAME: ptr noundef nonnull align 8
