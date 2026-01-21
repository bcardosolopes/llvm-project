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
// CIR - vtable with thunk references and VTT
// ============================================================================

// CIR: cir.global  linkonce_odr comdat @_ZTV7Diamond = #cir.vtable
// CIR-SAME: #cir.global_view<@_ZThn16_N7Diamond11rightMethodEv>
// CIR: cir.global {{.*}} @_ZTT7Diamond

// CIR: cir.func {{.*}} @_ZN7DiamondC1Ev
// CIR:   cir.vtable.address_point(@_ZTV7Diamond, address_point = <index = 0, offset = 3>) : !cir.vptr
// CIR:   cir.vtable.address_point(@_ZTV7Diamond, address_point = <index = 2, offset = 3>) : !cir.vptr
// CIR:   cir.vtable.address_point(@_ZTV7Diamond, address_point = <index = 1, offset = 3>) : !cir.vptr

// CIR: cir.func {{.*}} @_ZThn16_N7Diamond11rightMethodEv
// CIR:   cir.ptr_stride %{{.*}}, %{{.*}} : (!cir.ptr<!u8i>, !s64i) -> !cir.ptr<!u8i>
// CIR:   cir.call @_ZN7Diamond11rightMethodEv

// ============================================================================
// LLVM and OGCG Output
// ============================================================================

// LLVM: @_ZTV7Diamond = linkonce_odr global { [5 x ptr], [4 x ptr], [4 x ptr] }
// LLVM-SAME: @_ZThn16_N7Diamond11rightMethodEv
// LLVM: @_ZTT7Diamond = linkonce_odr global [7 x ptr]

// LLVM: define linkonce_odr void @_ZThn16_N7Diamond11rightMethodEv
// LLVM:   getelementptr i8, ptr %{{.*}}, i64 -16
// LLVM:   call void @_ZN7Diamond11rightMethodEv

//      OGCG: @_ZTV7Diamond = linkonce_odr {{.*}} constant
// OGCG-SAME: @_ZThn16_N7Diamond11rightMethodEv

// OGCG-LABEL: define linkonce_odr void @_ZThn16_N7Diamond11rightMethodEv
//      OGCG: %[[VAR2:[0-9]+]] = getelementptr inbounds i8, ptr %{{.*}}, i64 -16
//      OGCG: call void @_ZN7Diamond11rightMethodEv(ptr {{.*}} %[[VAR2]])
