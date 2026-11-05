// Multiple inheritance.

// RUN: %clang_cc1 %s -triple=aarch64-unknown-fuchsia -O1 -o - -emit-llvm -fhalf-no-semantic-interposition | FileCheck %s
// RUN: %if cir-support %{ %clang_cc1 %s -triple=aarch64-unknown-fuchsia -O1 -o - -emit-llvm -fhalf-no-semantic-interposition -fclangir | FileCheck %s %}

// VTable for C contains 2 sub-vtables (represented as 2 structs). The first contains the components for B and the second contains the components for C. The RTTI ptr in both arrays still point to the RTTI struct for C.
// The component for bar() instead points to a thunk which redirects to C::bar() which overrides B::bar().
// Now that we have a class with 2 parents, the offset to top in the second array is non-zero.
// CHECK: @_ZTV1C.local = internal unnamed_addr constant { [4 x i32], [3 x i32] }

// CHECK: @_ZTV1C ={{.*}} unnamed_addr alias { [4 x i32], [3 x i32] }, ptr @_ZTV1C.local

// CHECK:      define{{.*}} void @_Z8C_foobarP1C(ptr noundef %{{[a-z0-9]+}}) local_unnamed_addr
// CHECK:        [[vtable:%[a-z0-9]+]] = load ptr, ptr %{{[a-z0-9]+}}, align 8

// Offset 0 to get first method
// CHECK:        [[ptr1:%[0-9]+]] = tail call ptr @llvm.load.relative.i32(ptr [[vtable]], i32 0)
// CHECK-NEXT:   {{(tail )?}}call void [[ptr1]](ptr {{[^,]*}} %{{[a-z0-9]+}})
// CHECK-NEXT:   [[vtable2:%[a-z0-9]+]] = load ptr, ptr %{{[a-z0-9]+}}, align 8

// Offset by 4 to get the next bar()
// CHECK-NEXT:   [[ptr2:%[0-9]+]] = tail call ptr @llvm.load.relative.i32(ptr [[vtable2]], i32 4)
// CHECK-NEXT:   {{(tail )?}}call void [[ptr2]](ptr {{[^,]*}} %{{[a-z0-9]+}})
// CHECK-NEXT:   ret void
// CHECK-NEXT: }

class A {
public:
  virtual void foo();
};

class B {
  virtual void bar();
};

class C : public A, public B {
public:
  void foo() override;
  void bar() override;
};

void C::foo() {}
void C::bar() {}

void C_foobar(C *c) {
  c->foo();
  c->bar();
}
