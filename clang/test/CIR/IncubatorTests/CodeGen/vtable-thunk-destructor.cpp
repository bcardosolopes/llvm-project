// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Test thunk generation for virtual destructors in multiple inheritance

class Base1 {
public:
  virtual ~Base1() {}
  int x;
};

class Base2 {
public:
  virtual ~Base2() {}
  int y;
};

class Derived : public Base1, public Base2 {
public:
  ~Derived() override {}
};

void test() {
  Base2* b2 = new Derived();
  delete b2;  // Uses destructor thunk
}

// ============================================================================
// CIR VTable and Constructor
// ============================================================================

// Vtable globals are external in upstream CIR
// CIR: cir.global "private"  external @_ZTV7Derived : !rec_anon_struct

// Constructor sets up vtable address points for both bases
// CIR: cir.func {{.*}} @_ZN7DerivedC2Ev
// CIR:   cir.vtable.address_point(@_ZTV7Derived, address_point = <index = 0, offset = 2>) : !cir.vptr
// CIR:   cir.vtable.get_vptr %{{.*}} : !cir.ptr<!rec_Derived> -> !cir.ptr<!cir.vptr>
// CIR:   cir.vtable.address_point(@_ZTV7Derived, address_point = <index = 1, offset = 2>) : !cir.vptr
// CIR:   cir.vtable.get_vptr %{{.*}} : !cir.ptr<!rec_Base2> -> !cir.ptr<!cir.vptr>

// ============================================================================
// LLVM IR Output Validation
// ============================================================================

// LLVM: @_ZTV7Derived = external global { [4 x ptr], [4 x ptr] }

// OGCG: @_ZTV7Derived = linkonce_odr {{.*}} constant
// OGCG-DAG: @_ZThn16_N7DerivedD1Ev
// OGCG-DAG: @_ZThn16_N7DerivedD0Ev

// OGCG-LABEL: define linkonce_odr void @_ZThn16_N7DerivedD1Ev
//       OGCG: getelementptr inbounds i8, ptr %{{.*}}, i64 -16
//       OGCG: call void @_ZN7DerivedD1Ev

// OGCG-LABEL: define linkonce_odr void @_ZThn16_N7DerivedD0Ev
//       OGCG: getelementptr inbounds i8, ptr %{{.*}}, i64 -16
//       OGCG: call void @_ZN7DerivedD0Ev
