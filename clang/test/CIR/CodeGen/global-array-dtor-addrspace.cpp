// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -std=c++17 -fclangir -emit-llvm %s \
// RUN:   -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -std=c++17 -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=OGCG

// Destroying a global array of objects with a non-trivial destructor emits an
// array-destruction loop over the array base. On a target with a nonzero global
// address space (AMDGCN globals live in addrspace(1)) the element pointers stay
// in addrspace(1); the destruction loop's element type and the anonymous-struct
// bitcast (for arrays of polymorphic objects) must keep that address space, or
// the lowering aborts with "'cir.cast' op result type address space does not
// match the address space of the operand".

struct S {
  ~S();
  int x;
};
S g_arr[4];

// Array of polymorphic objects: the global's storage type is an anonymous
// struct (with vptr), so the array-dtor must bitcast within addrspace(1).
struct B {
  virtual ~B() {}
};
B g_barr[2];

// LLVM: @g_arr = addrspace(1) global
// LLVM: @g_barr = addrspace(1) global
// LLVM: @__cxx_global_array_dtor
// The destruction loop strides element pointers in addrspace(1).
// LLVM: ptr addrspace(1) {{.*}}@g_arr

// OGCG: @g_arr = addrspace(1) global
// OGCG: @g_barr = addrspace(1) global
// OGCG: @__cxx_global_array_dtor
