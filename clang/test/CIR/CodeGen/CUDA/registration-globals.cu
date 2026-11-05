#include "../Inputs/cuda.h"

// Device-side __device__/__constant__ variables that are ODR-used must be added
// to @llvm.compiler.used so optimization does not drop variables that are only
// referenced by host code. Verify ClangIR matches classic CodeGen (OGCG).

// RUN: %clang_cc1 -triple nvptx64-nvidia-cuda -fclangir \
// RUN:            -fcuda-is-device -emit-llvm -target-sdk-version=12.3 \
// RUN:            %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM-DEVICE --input-file=%t.ll %s

// RUN: %clang_cc1 -triple nvptx64-nvidia-cuda \
// RUN:            -fcuda-is-device -emit-llvm -target-sdk-version=12.3 \
// RUN:            %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG-DEVICE --input-file=%t.ll %s

__constant__ int cst = 3;
__device__ int gv = 7;

__global__ void k(int *p) { *p = cst + gv; }

// LLVM-DEVICE: @cst = addrspace(4) externally_initialized constant i32 3
// LLVM-DEVICE: @gv = addrspace(1) externally_initialized global i32 7
// LLVM-DEVICE: @llvm.compiler.used = appending global [2 x ptr] [ptr addrspacecast (ptr addrspace(4) @cst to ptr), ptr addrspacecast (ptr addrspace(1) @gv to ptr)], section "llvm.metadata"

// OGCG-DEVICE: @cst = addrspace(4) externally_initialized constant i32 3
// OGCG-DEVICE: @gv = addrspace(1) externally_initialized global i32 7
// OGCG-DEVICE: @llvm.compiler.used = appending global [2 x ptr] [ptr addrspacecast (ptr addrspace(4) @cst to ptr), ptr addrspacecast (ptr addrspace(1) @gv to ptr)], section "llvm.metadata"
