// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -std=c++17 -fclangir -emit-llvm %s \
// RUN:   -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -std=c++17 -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=OGCG

// An extern array referenced in a function body creates a placeholder global in
// the global address space; completing it with a definition can place it in a
// different address space (AMDGCN puts a const array in the constant address
// space, addrspace(4)). When the placeholder is replaced, the existing
// get_global users must be reconciled across the address-space change, otherwise
// the array-to-pointer decay cast (built against the placeholder's address
// space) disagrees with the new definition and CIR verification aborts with
// "'cir.cast' op result type address space does not match the address space of
// the operand".

extern const char kName[];

const char *predef_ptr() { return kName; }

const char kName[] = "example_name";

// The reference resolves to the constant-address-space definition and is
// address-space cast to the generic space, with no cross-address-space cast.
// LLVM: @kName = addrspace(4) constant
// LLVM-LABEL: @_Z10predef_ptrv
// LLVM: addrspacecast (ptr addrspace(4) @kName to ptr)

// OGCG: @kName = addrspace(4) constant
// OGCG-LABEL: @_Z10predef_ptrv
// OGCG: addrspacecast (ptr addrspace(4) @kName to ptr)
