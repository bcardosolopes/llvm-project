// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++20 -fcxx-exceptions \
// RUN:   -fexceptions -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++20 -fcxx-exceptions \
// RUN:   -fexceptions -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Verify that emission-order-aware TryMarkNoThrow recovers nounwind for the
// C1 wrapper when C2 was fully emitted before C1 started, matching OGCG
// without reintroducing transitive propagation to later-emitted helpers.
//

struct B {};

struct C {
  C(const B&);
};

C::C(const B&) {}

// Both C1 and C2 should have nounwind:
// CIR: define {{.*}} void @_ZN1CC2ERK1B({{.*}}) unnamed_addr #[[ATTR:[0-9]+]]
// CIR: define {{.*}} void @_ZN1CC1ERK1B({{.*}}) unnamed_addr #[[ATTR]]
// CIR: attributes #[[ATTR]] = {{{.*}}nounwind{{.*}}}

// OGCG: define {{.*}} void @_ZN1CC2ERK1B({{.*}}) unnamed_addr #[[ATTR:[0-9]+]]
// OGCG: define {{.*}} void @_ZN1CC1ERK1B({{.*}}) unnamed_addr #[[ATTR]]
// OGCG: attributes #[[ATTR]] = {{{.*}}nounwind{{.*}}}
