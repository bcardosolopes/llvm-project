// Verify that CIR emits EH cleanup for implicit copy constructors of structs
// with array members. When an element copy constructor throws, the already-
// copied elements must be destroyed.
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -fcxx-exceptions -fexceptions %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -fcxx-exceptions -fexceptions %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

struct A {
  A();
  A(const A&);
  ~A();
};

struct B {
  A a[3];
};

void test() {
  B b1;
  B b2(b1);
}

// The copy constructor B(const B&) must use invoke for element copy ctors
// and have a cleanup landingpad that destroys already-copied elements.

// CHECK: define linkonce_odr void @_ZN1BC2ERKS_
// CHECK-SAME: personality ptr @__gxx_personality_v0
// CHECK: invoke void @_ZN1AC1ERKS_
// CHECK-NEXT: to label %[[OK:.*]] unwind label %[[LP:.*]]
// CHECK: [[LP]]:
// CHECK-NEXT: landingpad
// CHECK-NEXT: cleanup
// CHECK: call void @_ZN1AD1Ev

// OGCG: define linkonce_odr void @_ZN1BC2ERKS_
// OGCG-SAME: personality ptr @__gxx_personality_v0
// OGCG: invoke void @_ZN1AC1ERKS_
// OGCG-NEXT: to label %[[OK:.*]] unwind label %[[LP:.*]]
// OGCG: [[LP]]:
// OGCG-NEXT: landingpad
// OGCG-NEXT: cleanup
// OGCG: call void @_ZN1AD1Ev
