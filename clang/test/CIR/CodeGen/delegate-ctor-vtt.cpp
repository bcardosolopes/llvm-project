// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=CIR %s
// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Delegating constructor in a class with virtual inheritance.
// The base constructor (C2) receives a VTT parameter, which must be
// skipped when building the delegate argument list.

struct A { virtual void f(); };

struct B : virtual A {
  int x;
  B(int v);
  B();
};

B::B(int v) : x(v) {}
B::B() : B(42) {}

// The base constructor B::B() delegates to B::B(int) and forwards the VTT.
// CIR-LABEL: define {{.*}} @_ZN1BC2Ev(
// CIR-SAME: ptr noundef nonnull align 8 dereferenceable(12) %{{[0-9]+}},
// CIR-SAME: ptr noundef %{{[0-9]+}})
// CIR: call void @_ZN1BC2Ei(ptr {{.*}}, ptr noundef %{{[0-9]+}}, i32 noundef 42)

// OGCG-LABEL: define {{.*}} @_ZN1BC2Ev(
// OGCG-SAME: ptr noundef nonnull align 8 dereferenceable(12) %{{[a-z.]+}},
// OGCG-SAME: ptr noundef %{{[a-z.]+}})
// OGCG: call void @_ZN1BC2Ei(ptr {{.*}}, ptr noundef %{{[a-z0-9.]+}}, i32 noundef 42)
