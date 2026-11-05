// RUN: %clang_cc1 -fclangir -triple amdgcn -fcuda-is-device -x hip -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll --check-prefix=LLVM
// RUN: %clang_cc1 -triple amdgcn -fcuda-is-device -x hip -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck %s --input-file=%t.ogcg.ll --check-prefix=LLVM

// RUN: %clang_cc1 -fclangir -triple amdgcn -fcuda-is-device -x hip -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir --check-prefix=CIR

// Test that AMDGPU kernel aggregate parameters use byref(addrspace(4)) ABI,
// matching OGCG's IndirectAliased classification.

#include "Inputs/cuda.h"

class A {
public:
  static __global__ void kernel(){}
};

// CIR: cir.func {{.*}} @_Z15template_kernelI1AEvT_(%arg0: !cir.ptr<!rec_A, target_address_space(4)> {cir.align = 1 : i64, cir.byref = !rec_A, cir.noundef}

// LLVM-DAG: define {{.*}}amdgpu_kernel void @_Z15template_kernelI1AEvT_(ptr addrspace(4) noundef byref(%class.A) align 1 %0)
template<class T>
__global__ void template_kernel(T x) {}

// Scalar kernel args should still use direct passing (not byref).
// LLVM-DAG: define {{.*}}amdgpu_kernel void @_Z13scalar_kerneli(i32
__global__ void scalar_kernel(int x) {}

void launch(void *f);

int main() {
  launch((void*)template_kernel<A>);
  launch((void*)scalar_kernel);
  return 0;
}
