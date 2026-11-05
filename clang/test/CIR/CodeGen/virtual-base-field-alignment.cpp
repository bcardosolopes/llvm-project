// RUN: %clang_cc1 -std=c++17 -triple x86_64-apple-darwin10 -mconstructor-aliases -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=CIR %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-apple-darwin10 -mconstructor-aliases -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Field store alignment in base constructors (C2) must use non-virtual
// alignment. CIR correctly uses align 8 in B::C2, matching OGCG.

struct A {
  __attribute__((aligned(16))) double data1;
};

struct B : public virtual A {
  B() : x(123) {}
  double a;
  int x;
};

struct C : public virtual B {};

void test() { B b; C c; }

// B's complete constructor (C1): x gets align 16 (complete object is 16-aligned)
// CIR: define{{.*}} void @_ZN1BC1Ev
// CIR: store i32 123, ptr %{{.*}}, align 16

// OGCG: define{{.*}} void @_ZN1BC1Ev
// OGCG: store i32 123, ptr %{{.*}}, align 16

// B's base constructor (C2): x should get align 8 (this might be subobject)
// CIR: define{{.*}} void @_ZN1BC2Ev
// CIR: store i32 123, ptr %{{.*}}, align 8

// OGCG: define{{.*}} void @_ZN1BC2Ev
// OGCG: store i32 123, ptr %{{.*}}, align 8
