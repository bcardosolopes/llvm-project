// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// For a plain (non-HIP) C amdgcn kernel, classic CodeGen keeps kernel pointer
// arguments in the generic address space (addrspace 0). The generic->global
// coercion is gated on getLangOpts().HIP, which is false here, so CIR must match
// and leave the pointers in the generic address space.

__attribute__((amdgpu_kernel)) void mykernel(int *p, float *q) {
  *p = 42;
  *q = 1.0f;
}

// LLVM: define{{.*}} amdgpu_kernel void @mykernel(ptr noundef %{{[0-9]+}}, ptr noundef %{{[0-9]+}})
// OGCG: define{{.*}} amdgpu_kernel void @mykernel(ptr noundef %p, ptr noundef %q)
