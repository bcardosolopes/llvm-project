// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -std=c++17 -fclangir -emit-llvm %s \
// RUN:   -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -std=c++17 -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=OGCG

// The guard variable created for a guarded static initialization must live in
// the same address space as the guarded variable. On a target with a nonzero
// global address space (AMDGCN globals live in addrspace(1)) the guard was
// created in the default address space, so the pointer to the guard disagreed
// with its definition and lowering aborted with "'llvm.mlir.addressof' op
// pointer address space must match address space of the referenced global or
// alias".

int compute();

int f() {
  static int x = compute();
  return x;
}

// The guard variable is defined in addrspace(1), matching the guarded variable.
// LLVM: @_ZGVZ1fvE1x = internal addrspace(1) global i64 0
// OGCG: @_ZGVZ1fvE1x = internal addrspace(1) global i64 0
