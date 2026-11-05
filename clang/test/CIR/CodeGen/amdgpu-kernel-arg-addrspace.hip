// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -x hip -fcuda-is-device -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -x hip -fcuda-is-device -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// For a HIP device kernel, classic CodeGen coerces kernel pointer arguments from
// the generic address space to the global address space (addrspace 1), gated on
// getLangOpts().HIP. CIR propagates LangOpts.HIP via the cir.hip module
// attribute and must apply the same coercion.

__attribute__((global)) void mykernel(int *p, float *q) {
  *p = 42;
  *q = 1.0f;
}

// LLVM: define{{.*}} amdgpu_kernel void @_Z8mykernelPiPf(ptr addrspace(1) noundef %{{[0-9]+}}, ptr addrspace(1) noundef %{{[0-9]+}})
// OGCG: define{{.*}} amdgpu_kernel void @_Z8mykernelPiPf(ptr addrspace(1) noundef %p.coerce, ptr addrspace(1) noundef %q.coerce)
