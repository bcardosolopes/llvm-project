// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Test thunk generation with virtual inheritance (diamond problem)

class Base {
public:
  virtual void method() {}
  int a;
};

class Left : public virtual Base {
public:
  virtual void leftMethod() {}
  int b;
};

class Right : public virtual Base {
public:
  virtual void rightMethod() {}
  int c;
};

class Diamond : public Left, public Right {
public:
  void leftMethod() override {}
  void rightMethod() override {}
};

void test() {
  Diamond d;
  Left* l = &d;
  Right* r = &d;
  l->leftMethod();
  r->rightMethod();
}

// ============================================================================
// Upstream CIR - vtable and VTT as external globals
// ============================================================================

// CIR: cir.global "private"  external @_ZTV7Diamond : !rec_anon_struct
// CIR: cir.global "private"  external @_ZTT7Diamond : !cir.array<!cir.ptr<!u8i> x 7>

// CIR: cir.func {{.*}} @_ZN7DiamondC1Ev
// CIR:   cir.vtable.address_point(@_ZTV7Diamond, address_point = <index = 0, offset = 3>) : !cir.vptr
// CIR:   cir.vtable.address_point(@_ZTV7Diamond, address_point = <index = 2, offset = 3>) : !cir.vptr
// CIR:   cir.vtable.address_point(@_ZTV7Diamond, address_point = <index = 1, offset = 3>) : !cir.vptr

// ============================================================================
// LLVM and OGCG Output
// ============================================================================

// LLVM: @_ZTV7Diamond = external global { [5 x ptr], [4 x ptr], [4 x ptr] }
// LLVM: @_ZTT7Diamond = external global [7 x ptr]

//      OGCG: @_ZTV7Diamond = linkonce_odr {{.*}} constant
// OGCG-SAME: @_ZThn16_N7Diamond11rightMethodEv

// OGCG-LABEL: define linkonce_odr void @_ZThn16_N7Diamond11rightMethodEv
//      OGCG: %[[VAR2:[0-9]+]] = getelementptr inbounds i8, ptr %{{.*}}, i64 -16
//      OGCG: call void @_ZN7Diamond11rightMethodEv(ptr {{.*}} %[[VAR2]])
