// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -std=c++17 -fclangir -emit-llvm %s \
// RUN:   -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -std=c++17 -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=OGCG

// A conditional operator whose arms are pointers into a non-default address
// space (AMDGCN globals live in addrspace(1)) yields a generic-address-space
// pointer. Converting each arm to the result type must use an address-space
// cast, not a plain bitcast (which cannot cross address spaces and previously
// aborted CIR verification with "'cir.cast' op result type address space does
// not match the address space of the operand").

struct T { int x; };
T ga, gb;

// Cheap arms: lowered as a select.
T *sel(bool c) { return c ? &ga : &gb; }

// LLVM-LABEL: @_Z3sel
// LLVM: select i1 %{{.*}}, ptr addrspacecast (ptr addrspace(1) @ga to ptr), ptr addrspacecast (ptr addrspace(1) @gb to ptr)

// OGCG-LABEL: @_Z3sel
// OGCG: select i1 %{{.*}}, ptr addrspacecast (ptr addrspace(1) @ga to ptr), ptr addrspacecast (ptr addrspace(1) @gb to ptr)

T *side();

// One non-cheap arm: lowered as control flow with a phi.
T *sel_cf(bool c) { return c ? side() : &gb; }

// LLVM-LABEL: @_Z6sel_cf
// LLVM: phi ptr [ addrspacecast (ptr addrspace(1) @gb to ptr), %{{.*}} ], [ %{{.*}}, %{{.*}} ]

// OGCG-LABEL: @_Z6sel_cf
// OGCG: phi ptr [ %{{.*}}, %{{.*}} ], [ addrspacecast (ptr addrspace(1) @gb to ptr), %{{.*}} ]
