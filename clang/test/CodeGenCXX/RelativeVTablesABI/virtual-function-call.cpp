// Check that we call llvm.load.relative() on a vtable function call.

// RUN: %clang_cc1 %s -triple=aarch64-unknown-fuchsia -O3 -o - -emit-llvm | FileCheck %s
// RUN: %if cir-support %{ %clang_cc1 %s -triple=aarch64-unknown-fuchsia -O3 -o - -emit-llvm -fclangir | FileCheck %s --check-prefix=CIR %}

// CHECK:      define{{.*}} void @_Z5A_fooP1A(ptr noundef %a) local_unnamed_addr
// CHECK-NEXT: entry:
// CHECK-NEXT:   %vtable = load ptr, ptr %a
// CHECK-NEXT:   [[func_ptr:%[0-9]+]] = tail call ptr @llvm.load.relative.i32(ptr %vtable, i32 0)
// CHECK-NEXT:   tail call void [[func_ptr]](ptr {{[^,]*}} %a)
// CHECK-NEXT:   ret void
// CHECK-NEXT: }

// CIR:      define{{.*}} void @_Z5A_fooP1A(ptr noundef %[[THIS:[0-9]+]]) local_unnamed_addr
// CIR-NEXT:   %[[VTABLE:[0-9]+]] = load ptr, ptr %[[THIS]], align 8{{.*}}
// CIR-NEXT:   %[[FUNC_PTR:[0-9]+]] = tail call ptr @llvm.load.relative.i32(ptr %[[VTABLE]], i32 0)
// CIR-NEXT:   tail call void %[[FUNC_PTR]](ptr noundef nonnull align 8 dereferenceable(8) %[[THIS]]){{.*}}
// CIR-NEXT:   ret void
// CIR-NEXT: }

class A {
public:
  virtual void foo();
};

void A_foo(A *a) {
  a->foo();
}
