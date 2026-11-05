// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -std=c++17 -fclangir -emit-llvm %s \
// RUN:   -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -std=c++17 -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=OGCG

// A pointer-to-data-member access on an object in a non-default address space
// must keep the member access in that address space. The runtime-member result
// previously dropped the object's address space (using the default), which made
// the access cross address spaces and lower to an invalid cast.

struct S { int x; int y; };
S s;

int g() {
  int S::*p = &S::y;
  return s.*p;
}

// The member offset is applied to the addrspace(1) object pointer and the load
// stays in addrspace(1); no cross-address-space cast is emitted.
// LLVM-LABEL: @_Z1gv
// LLVM: getelementptr inbounds i8, ptr addrspace(1) @s
// LLVM: load i32, ptr addrspace(1)

// OGCG-LABEL: @_Z1gv
// OGCG: getelementptr inbounds i8, ptr {{.*}}@s
