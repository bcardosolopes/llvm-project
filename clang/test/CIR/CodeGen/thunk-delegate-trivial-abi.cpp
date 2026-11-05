// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll --check-prefix=CIR %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Verify that delegated argument forwarding supports callee-destructed
// (trivial_abi) record parameters for both:
//  - virtual thunks (this-adjusting forwarding call)
//  - delegating constructors

struct [[clang::trivial_abi]] S {
  int x;
  ~S();
};

struct B2 {
  virtual void g();
};
struct B1 {
  virtual void f(S);
};

struct D : B2, B1 {
  void g() override;
  void f(S) override;
};

void D::g() {}
void D::f(S) {}

struct C {
  C(S s);
  C(int x, S s);
};

C::C(S s) : C(0, s) {}
C::C(int x, S s) {}

// CIR-LABEL: define{{.*}} void @_ZThn8_N1D1fE1S(ptr noundef
// CIR: getelementptr inbounds i8, ptr %{{.*}}, i64 -8
// CIR-NOT: call void @_ZN1SD1Ev
// CIR: tail call void @_ZN1D1fE1S(
// CIR: ret void

// OGCG-LABEL: define{{.*}} void @_ZThn8_N1D1fE1S(ptr noundef
// OGCG: getelementptr inbounds i8, ptr %{{.*}}, i64 -8
// OGCG-NOT: call void @_ZN1SD1Ev
// OGCG: tail call void @_ZN1D1fE1S(
// OGCG: ret void

// CIR-LABEL: define{{.*}} void @_ZN1CC2E1S(
// CIR: call void @_ZN1CC2Ei1S(
// CIR: call void @_ZN1SD1Ev(
// CIR: ret void

// OGCG-LABEL: define{{.*}} void @_ZN1CC2E1S(
// OGCG: call void @_ZN1CC2Ei1S(
// OGCG: call void @_ZN1SD1Ev(
// OGCG: ret void
