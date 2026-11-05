// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Fixed. CIR was emitting a spurious partial-destroy loop on the
// normal path, causing double destruction. Now the partial-destroy cleanup
// is EH-only, matching OGCG's behavior.

struct A {
  A();
  ~A();
};

// There should be exactly 2 destructor call sites: one on the normal path
// (end-of-scope destroy) and one on the exception path (partial-destroy).
// CIR and OGCG may order blocks differently.

// CHECK-LABEL: @_Z4testv
// CHECK: landingpad
// CHECK: call void @_ZN1AD1Ev
// The second destructor call site (normal path end-of-scope destroy):
// CHECK: call void @_ZN1AD1Ev
// No more destructor calls (would indicate double-destroy bug):
// CHECK-NOT: call void @_ZN1AD1Ev
// CHECK: }

// OGCG-LABEL: @_Z4testv
// OGCG: call void @_ZN1AD1Ev
// OGCG: landingpad
// OGCG: call void @_ZN1AD1Ev
// OGCG-NOT: call void @_ZN1AD1Ev
// OGCG: }
void test() {
  A as[10];
}
