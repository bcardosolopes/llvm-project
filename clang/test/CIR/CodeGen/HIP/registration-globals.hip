#include "../Inputs/cuda.h"

// HIP device compilation must emit the @__hip_cuid_ global (so host and device
// binaries from the same TU can be associated) and add it to
// @llvm.compiler.used. Verify ClangIR matches classic CodeGen (OGCG),
// including the AMDGPU default globals address space (addrspace(1)).

// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx1030 -fclangir \
// RUN:            -fcuda-is-device -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM-DEVICE --input-file=%t.ll %s

// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx1030 \
// RUN:            -fcuda-is-device -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG-DEVICE --input-file=%t.ll %s

__global__ void k(int *p) { *p = 1; }

// LLVM-DEVICE: @__hip_cuid_ = addrspace(1) global i8 0
// LLVM-DEVICE: @llvm.compiler.used = appending addrspace(1) global [1 x ptr] [ptr addrspacecast (ptr addrspace(1) @__hip_cuid_ to ptr)], section "llvm.metadata"

// OGCG-DEVICE: @__hip_cuid_ = addrspace(1) global i8 0
// OGCG-DEVICE: @llvm.compiler.used = appending addrspace(1) global [1 x ptr] [ptr addrspacecast (ptr addrspace(1) @__hip_cuid_ to ptr)], section "llvm.metadata"
