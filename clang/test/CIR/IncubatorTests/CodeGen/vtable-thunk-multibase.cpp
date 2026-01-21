// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Test thunk generation with multiple base classes
// This validates thunks for void-returning methods (no return adjustment).
// Full covariant return adjustment for pointer-returning methods is NYI.

class Base1 {
public:
  virtual void foo() {}
};

class Base2 {
public:
  virtual void bar() {}
  int data;
};

class Derived : public Base1, public Base2 {
public:
  void bar() override {}
};

void test() {
  Derived d;
  Base2* b2 = &d;
  b2->bar();  // Needs this-adjusting thunk (no return adjustment)
}

// ============================================================================
// CIR Output - Vtable with thunk references
// ============================================================================

// CIR: cir.global  linkonce_odr comdat @_ZTV7Derived = #cir.vtable
// CIR-SAME: #cir.global_view<@_ZThn8_N7Derived3barEv>

// CIR: cir.func {{.*}} @_ZN7DerivedC2Ev
// CIR:   cir.vtable.address_point(@_ZTV7Derived, address_point = <index = 0, offset = 2>) : !cir.vptr
// CIR:   cir.vtable.get_vptr %{{.*}} : !cir.ptr<!rec_Derived> -> !cir.ptr<!cir.vptr>
// CIR:   cir.vtable.address_point(@_ZTV7Derived, address_point = <index = 1, offset = 2>) : !cir.vptr
// CIR:   cir.vtable.get_vptr %{{.*}} : !cir.ptr<!rec_Base2> -> !cir.ptr<!cir.vptr>

// CIR: cir.func {{.*}} @_ZThn8_N7Derived3barEv
// CIR:   cir.ptr_stride %{{.*}}, %{{.*}} : (!cir.ptr<!u8i>, !s64i) -> !cir.ptr<!u8i>
// CIR:   cir.call @_ZN7Derived3barEv

// ============================================================================
// LLVM and OGCG Output
// ============================================================================

// LLVM: @_ZTV7Derived = linkonce_odr global { [4 x ptr], [3 x ptr] }
// LLVM-SAME: @_ZThn8_N7Derived3barEv

// LLVM: define linkonce_odr void @_ZThn8_N7Derived3barEv
// LLVM:   getelementptr i8, ptr %{{.*}}, i64 -8
// LLVM:   call void @_ZN7Derived3barEv

//      OGCG: @_ZTV7Derived = linkonce_odr {{.*}} constant
// OGCG-SAME: @_ZThn8_N7Derived3barEv

// OGCG-LABEL: define linkonce_odr void @_ZThn8_N7Derived3barEv
//       OGCG: getelementptr inbounds i8, ptr %{{.*}}, i64 -8
//       OGCG: call void @_ZN7Derived3barEv
