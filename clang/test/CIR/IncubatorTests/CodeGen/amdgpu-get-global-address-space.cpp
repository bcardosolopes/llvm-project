// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -fclangir -emit-cir -std=c++20 \
// RUN:   -fcxx-exceptions -fexceptions %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Test that cir.get_global preserves the address space of the global when the
// global's type is replaced (e.g., when the initializer type differs from the
// variable's declared type). Previously, the GetGlobalOp result type lost its
// address space during the type replacement in getOrCreateCIRGlobal.

struct D {
  virtual void f();
};
void D::f() { }

static struct : D { } e;

// The global @_ZL1e should be in target address space 1 (amdgcn global).
// CHECK: cir.global {{.*}} target_address_space(1) @_ZL1e

void use_e() {
  e.f();
}

// The cir.get_global must carry the same target address space as the global.
// CHECK: cir.get_global @_ZL1e : !cir.ptr<{{.*}}, target_address_space(1)>
