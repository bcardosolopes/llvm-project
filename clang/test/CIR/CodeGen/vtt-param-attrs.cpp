// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s -check-prefix=OGCG
// Previously expected to fail: VTT parameter in constructors with virtual
// bases should have just `noundef`, not the attributes from the following
// parameter. Now fixed.

struct A { virtual void f(); };

struct B : virtual A {
  int x;
  B();
  B(const B&);
};

B::B() : x(42) {}
B::B(const B& o) : x(o.x) {}

// The copy constructor C2 should have vtt with just `noundef`:
//   (ptr noundef nonnull align 8 deref(12) %this, ptr noundef %vtt, ptr noundef nonnull align 8 deref(12) %o)

// CIR-LABEL: define {{.*}} @_ZN1BC2ERKS_
// CIR-SAME: ptr noundef nonnull align 8 dereferenceable(12) %{{[0-9]+}}
// CIR-SAME: ptr noundef %{{[0-9]+}}
// CIR-SAME: ptr noundef nonnull align 8 dereferenceable(12) %{{[0-9]+}}

// OGCG-LABEL: define {{.*}} @_ZN1BC2ERKS_
// OGCG-SAME: ptr noundef nonnull align 8 dereferenceable(12) %{{[a-z]+}},
// OGCG-SAME: ptr noundef %{{[a-z]+}},
// OGCG-SAME: ptr noundef nonnull align 8 dereferenceable(12) %{{[a-z]+}}
