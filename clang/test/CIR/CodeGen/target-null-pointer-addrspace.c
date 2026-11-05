// NOTE: Keep this test in C mode to avoid C++-specific name mangling noise.

// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM-AMDGPU
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG-AMDGPU

// RUN: %clang_cc1 -triple nvptx64-nvidia-cuda -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM-NVPTX
// RUN: %clang_cc1 -triple nvptx64-nvidia-cuda -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG-NVPTX

// AMDGPU: null in private AS is non-zero and is represented through
// addrspacecast from generic null (matching OGCG target hook behavior).
__attribute__((address_space(5))) int *amdgpu_private_null = 0;
__attribute__((address_space(1))) int *amdgpu_global_null = 0;

// NVPTX: null in local AS is represented through addrspacecast from generic
// null, while global AS uses plain null.
__attribute__((address_space(3))) int *nvptx_local_null = 0;
__attribute__((address_space(1))) int *nvptx_global_null = 0;

// LLVM-AMDGPU-DAG: @amdgpu_private_null ={{.*}} ptr addrspace(5) addrspacecast (ptr null to ptr addrspace(5))
// LLVM-AMDGPU-DAG: @amdgpu_global_null ={{.*}} ptr addrspace(1) null
// OGCG-AMDGPU-DAG: @amdgpu_private_null ={{.*}} ptr addrspace(5) addrspacecast (ptr null to ptr addrspace(5))
// OGCG-AMDGPU-DAG: @amdgpu_global_null ={{.*}} ptr addrspace(1) null

// LLVM-NVPTX-DAG: @nvptx_local_null ={{.*}} ptr addrspace(3) addrspacecast (ptr null to ptr addrspace(3))
// LLVM-NVPTX-DAG: @nvptx_global_null ={{.*}} ptr addrspace(1) null
// OGCG-NVPTX-DAG: @nvptx_local_null ={{.*}} ptr addrspace(3) addrspacecast (ptr null to ptr addrspace(3))
// OGCG-NVPTX-DAG: @nvptx_global_null ={{.*}} ptr addrspace(1) null
