// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.og.ll %s

// Test that CIR thunk generation matches original CodeGen behavior

class Base1 {
public:
  virtual void foo() {}
};

class Base2 {
public:
  virtual void bar() {}
};

class Derived : public Base1, public Base2 {
public:
  void bar() override {}
};

void test() {
  Derived d;
  Base2* b2 = &d;
  b2->bar();
}

// Upstream: vtable is external global, no thunks at CIR level
// CIR: cir.global "private"  external @_ZTV7Derived : !rec_anon_struct

// CIR: cir.func {{.*}} @_ZN7DerivedC2Ev
// CIR:   cir.vtable.address_point(@_ZTV7Derived, address_point = <index = 0, offset = 2>) : !cir.vptr
// CIR:   cir.vtable.address_point(@_ZTV7Derived, address_point = <index = 1, offset = 2>) : !cir.vptr

// In upstream, vtable is external (thunks not yet generated)
// LLVM: @_ZTV7Derived = external global { [4 x ptr], [3 x ptr] }

// Check original CodeGen LLVM output matches
// OGCG-DAG: @_ZTV7Derived = linkonce_odr unnamed_addr constant {{.*}} @_ZThn{{[0-9]+}}_N7Derived3barEv
// OGCG-DAG: define linkonce_odr void @_ZThn{{[0-9]+}}_N7Derived3barEv
