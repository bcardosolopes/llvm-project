// RUN: %clang_cc1 -std=c++17 -triple x86_64-apple-darwin10 -mconstructor-aliases -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=CIR %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-apple-darwin10 -mconstructor-aliases -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Constructor/destructor aliases must have unnamed_addr attribute.

struct A {
  A();
};
A::A() { }

// CIR: @_ZN1AC1Ev = unnamed_addr alias void (ptr), ptr @_ZN1AC2Ev
// OGCG: @_ZN1AC1Ev = unnamed_addr alias void (ptr), ptr @_ZN1AC2Ev

struct B {
  virtual ~B();
};
B::~B() { }

// CIR: @_ZN1BD1Ev = unnamed_addr alias void (ptr), ptr @_ZN1BD2Ev
// OGCG: @_ZN1BD1Ev = unnamed_addr alias void (ptr), ptr @_ZN1BD2Ev
