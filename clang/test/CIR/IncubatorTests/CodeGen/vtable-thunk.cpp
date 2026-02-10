// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -mconstructor-aliases -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -mconstructor-aliases -fclangir -emit-llvm -fno-clangir-call-conv-lowering %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Test basic thunk generation for multiple inheritance with non-virtual thunks

class Base1 {
public:
  virtual void foo() {}
  int x;
};

class Base2 {
public:
  virtual void bar() {}
  int y;
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

// ============================================================================
// CIR VTable Structure
// ============================================================================

// Vtable is an external global in upstream CIR
// CIR: cir.global "private"  external @_ZTV7Derived : !rec_anon_struct

// ============================================================================
// CIR Constructor - VTable Address Point Setup
// ============================================================================

// CIR: cir.func {{.*}} @_ZN7DerivedC2Ev
// CIR:   cir.vtable.address_point(@_ZTV7Derived, address_point = <index = 0, offset = 2>) : !cir.vptr
// CIR:   cir.vtable.get_vptr %{{.*}} : !cir.ptr<!rec_Derived> -> !cir.ptr<!cir.vptr>
// CIR:   cir.vtable.address_point(@_ZTV7Derived, address_point = <index = 1, offset = 2>) : !cir.vptr
// CIR:   cir.vtable.get_vptr %{{.*}} : !cir.ptr<!rec_Base2> -> !cir.ptr<!cir.vptr>

// ============================================================================
// CIR Virtual Call - Using vtable.get_virtual_fn_addr
// ============================================================================

// CIR: cir.func {{.*}} @_Z4testv
// CIR:   cir.vtable.get_vptr %{{.*}} : !cir.ptr<!rec_Base2> -> !cir.ptr<!cir.vptr>
// CIR:   cir.vtable.get_virtual_fn_addr %{{.*}}[0] : !cir.vptr

// ============================================================================
// LLVM IR Output Validation
// ============================================================================

// LLVM: @_ZTV7Derived = external global { [4 x ptr], [3 x ptr] }

// ============================================================================
// Test Multiple Base Classes (Different Offsets)
// ============================================================================

class A {
public:
  virtual void methodA() {}
  long long a;  // 8 bytes
};

class B {
public:
  virtual void methodB() {}
  long long b;  // 8 bytes
};

class C {
public:
  virtual void methodC() {}
  long long c;  // 8 bytes
};

class Multi : public A, public B, public C {
public:
  void methodB() override {}
  void methodC() override {}
};

void test_multi() {
  Multi m;
  B* pb = &m;
  C* pc = &m;
  pb->methodB();
  pc->methodC();
}

// Multi vtable has 3 address point indices (A, B, C)
// CIR: cir.func {{.*}} @_ZN5MultiC2Ev
// CIR:   cir.vtable.address_point(@_ZTV5Multi, address_point = <index = 0, offset = 2>) : !cir.vptr
// CIR:   cir.vtable.address_point(@_ZTV5Multi, address_point = <index = 1, offset = 2>) : !cir.vptr
// CIR:   cir.vtable.address_point(@_ZTV5Multi, address_point = <index = 2, offset = 2>) : !cir.vptr

// LLVM: @_ZTV5Multi = external global { [5 x ptr], [3 x ptr], [3 x ptr] }
