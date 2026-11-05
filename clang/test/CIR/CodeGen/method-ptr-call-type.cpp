// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Test that member function pointer creation followed by a direct call
// to the same method produces a correct cir.call with matching operand
// count. Previously, the member function pointer path created the FuncOp
// without 'this', causing a verification failure when the direct call
// added 'this' as the first operand.

struct Foo {
  void bar(int x);
  void baz(int x, double y);
  virtual void virt(int x);
};

// Test 1: member function pointer + direct call to same method
void test_basic(Foo &f) {
  void (Foo::*pfn)(int) = &Foo::bar;
  f.bar(42);
}
// CHECK: cir.call @_ZN3Foo3barEi(%{{.*}}, %{{.*}}) : (!cir.ptr<!rec_Foo>, !s32i) -> ()
// CHECK: cir.func private @_ZN3Foo3barEi(!cir.ptr<!rec_Foo> {{.*}}, !s32i {{.*}})

// Test 2: multiple member function pointers + calls
void test_multi(Foo &f) {
  void (Foo::*p1)(int) = &Foo::bar;
  void (Foo::*p2)(int, double) = &Foo::baz;
  f.bar(1);
  f.baz(2, 3.0);
}
// CHECK: cir.call @_ZN3Foo3barEi(%{{.*}}, %{{.*}}) : (!cir.ptr<!rec_Foo>, !s32i) -> ()
// CHECK: cir.call @_ZN3Foo3bazEid(%{{.*}}, %{{.*}}, %{{.*}}) : (!cir.ptr<!rec_Foo>, !s32i, !cir.double) -> ()

// Test 3: virtual member function pointer + direct call
void test_virtual(Foo &f) {
  void (Foo::*pvirt)(int) = &Foo::virt;
  f.virt(42);
}
