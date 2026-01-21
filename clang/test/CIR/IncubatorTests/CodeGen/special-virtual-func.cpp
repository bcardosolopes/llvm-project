// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Check that pure and deleted virtual functions are correctly emitted in the
// vtable.
class A {
  A();
  virtual void pure() = 0;
  virtual void deleted() = delete;
};

A::A() = default;

// Vtable includes __cxa_pure_virtual and __cxa_deleted_virtual
// CHECK: cir.global  linkonce_odr comdat @_ZTV1A = #cir.vtable<{
// CHECK-SAME: #cir.global_view<@__cxa_pure_virtual>
// CHECK-SAME: #cir.global_view<@__cxa_deleted_virtual>

// Constructor sets up vtable
// CHECK: cir.func {{.*}} @_ZN1AC2Ev
// CHECK:   cir.vtable.address_point(@_ZTV1A, address_point = <index = 0, offset = 2>) : !cir.vptr
// CHECK:   cir.vtable.get_vptr %{{.*}} : !cir.ptr<!rec_A> -> !cir.ptr<!cir.vptr>
