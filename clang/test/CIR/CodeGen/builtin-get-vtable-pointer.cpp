// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
//
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s

// __builtin_get_vtable_pointer loads the vtable pointer from a
// polymorphic object, matching OGCG's GetVTablePtr.

struct Base {
  virtual void f() {}
};

const void *get_vtable(Base *b) {
  return __builtin_get_vtable_pointer(b);
}

// CIR: cir.vtable.get_vptr
// CIR: cir.cast bitcast

// LLVM: define {{.*}} @_Z10get_vtableP4Base
// LLVM:   load ptr, ptr %{{.*}}
// LLVM:   ret ptr
