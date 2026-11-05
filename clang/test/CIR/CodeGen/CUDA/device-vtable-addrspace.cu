#include "../Inputs/cuda.h"

// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx906 -fclangir \
// RUN:            -fcuda-is-device -emit-llvm -target-sdk-version=12.3 \
// RUN:            %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// On a target with a nonzero global address space (AMDGCN AS1), every vtable
// entry uses that address space. The RTTI slot is null in device code (no RTTI),
// and that null pointer must also use the component address space so it matches
// the vtable array element type. Before the fix the null RTTI entry was emitted
// in the default address space, mismatching the [3 x ptr addrspace(1)] element
// type and aborting in ConstantArray::get ("Wrong type in array element
// initializer").

struct A {
  __device__ virtual void vf() {}
};

__global__ void kern() { A a; }

// LLVM: @_ZTV1A = {{.*}}addrspace(1) constant { [3 x ptr addrspace(1)] } { [3 x ptr addrspace(1)] [ptr addrspace(1) null, ptr addrspace(1) null, ptr addrspace(1) addrspacecast (ptr @_ZN1A2vfEv to ptr addrspace(1))] }
