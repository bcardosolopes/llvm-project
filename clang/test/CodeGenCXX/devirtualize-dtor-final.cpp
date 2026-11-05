// RUN: %clang_cc1 -triple i386-unknown-unknown -std=c++11 %s -emit-llvm -o - | FileCheck --check-prefix=CHECK %s
// RUN: %if cir-support %{ %clang_cc1 -triple i386-unknown-unknown -std=c++11 -fclangir %s -emit-llvm -o - | FileCheck --check-prefix=CIR %s %}

namespace Test1 {
  struct A { virtual ~A() {} };
  struct B final : A {};
  struct C : A { virtual ~C() final {} };
  struct D { virtual ~D() final = 0; };
  // CHECK-LABEL: define{{.*}} void @_ZN5Test13fooEPNS_1BE
  // CIR-LABEL: define{{.*}} void @_ZN5Test13fooEPNS_1BE
  void foo(B *b) {
    // CHECK: call void @_ZN5Test11BD1Ev
    // CIR: call void @_ZN5Test11BD1Ev
    delete b;
  }
  // CHECK-LABEL: define{{.*}} void @_ZN5Test14foo2EPNS_1CE
  // CIR-LABEL: define{{.*}} void @_ZN5Test14foo2EPNS_1CE
  void foo2(C *c) {
    // CHECK: call void @_ZN5Test11CD1Ev
    // CIR: call void @_ZN5Test11CD1Ev
    delete c;
  }
  // CHECK-LABEL: define{{.*}} void @_ZN5Test14evilEPNS_1DE
  // CIR-LABEL: define{{.*}} void @_ZN5Test14evilEPNS_1DE
  void evil(D *p) {
    // CHECK-NOT: call void @_ZN5Test11DD1Ev
    // CIR-NOT: call void @_ZN5Test11DD1Ev
    delete p;
  }
}
