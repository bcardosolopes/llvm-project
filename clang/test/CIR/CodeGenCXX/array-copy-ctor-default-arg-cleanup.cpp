// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.og.ll %s

// Regression test: When copying an array member whose element copy
// constructor has default arguments with non-trivial destructors, the cleanup
// destructor calls for the temporaries must be emitted BEFORE cir.yield in the
// do-while loop body.  Previously, RunCleanupsScope wrapped the entire body
// builder lambda, so its destructor fired after createYield, placing cleanup
// ops after the yield terminator and violating CIR's invariant that yield must
// be the last op in its parent block.

struct Dtor {
  Dtor();
  ~Dtor();
  double d;
};

struct Elem {
  Elem(const Elem &other, const Dtor &d1 = Dtor(), const Dtor &d2 = Dtor());
};

struct Aggregate : Elem {
  Elem a1;
  Elem a2;
  Elem arr[10];
};

// Force synthesized copy constructor.
void force_copy(Aggregate x) {
  Aggregate y = x;
}

// CIR-LABEL: @_ZN9AggregateC2ERKS_
// The do-while loop copying arr[10]:
// CIR: cir.do {
// Construct default-arg temporaries, call copy ctor, then destroy temps
// BEFORE yield.
// CIR: cir.call @_ZN4DtorC1Ev
// CIR: cir.call @_ZN4DtorC1Ev
// CIR: cir.call @_ZN4ElemC1ERKS_RK4DtorS4_
// CIR: cir.call @_ZN4DtorD1Ev
// CIR: cir.call @_ZN4DtorD1Ev
// CIR: cir.yield
// CIR-NEXT: } while {

// LLVM-LABEL: define {{.*}} @_ZN9AggregateC2ERKS_
// LLVM: call void @_ZN4ElemC1ERKS_RK4DtorS4_
// LLVM: call void @_ZN4DtorD1Ev
// LLVM: call void @_ZN4DtorD1Ev

// OGCG-LABEL: define {{.*}} @_ZN9AggregateC2ERKS_
// OGCG: call void @_ZN4ElemC1ERKS_RK4DtorS4_
// OGCG: call void @_ZN4DtorD1Ev
// OGCG: call void @_ZN4DtorD1Ev
