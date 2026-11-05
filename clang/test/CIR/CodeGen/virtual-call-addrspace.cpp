// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -fclangir -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=OGCG

// A virtual function pointer is loaded from the vtable, which lives in the
// globals address space (AMDGCN AS1). It must be converted to the default
// address space for the call with an address-space cast, not a bitcast across
// address spaces (which is invalid). This covers virtual calls, virtual
// destructor calls, and virtual-base access through a base pointer.

struct A { virtual int f() { return 1; } };
struct B : A { int f() override { return 2; } };

B b;

int g() {
  A *a = &b;
  return a->f();
}

// The vtable function-pointer slot is loaded as an addrspace(1) pointer and the
// call is made through a default-address-space pointer; there is no invalid
// cross-address-space bitcast.
// LLVM-LABEL: @_Z1gv
// LLVM: load ptr addrspace(1)
// LLVM: call {{.*}} i32

// OGCG-LABEL: @_Z1gv
// OGCG: load ptr addrspace(1)
