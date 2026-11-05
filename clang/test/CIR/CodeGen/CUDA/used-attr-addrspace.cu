#include "../Inputs/cuda.h"

// RUN: %clang_cc1 -triple nvptx64-nvidia-cuda -fclangir \
// RUN:            -fcuda-is-device -emit-llvm -target-sdk-version=12.3 \
// RUN:            %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// A __device__ global lives in a non-default address space. When it is added to
// @llvm.compiler.used (whose elements are default-address-space pointers), its
// address must be addrspacecast to the default address space, matching OGCG.
// Before the fix the used-list element was emitted as a raw addrspace(1)
// pointer, which mismatched the array element type and aborted in
// ConstantArray::get ("Wrong type in array element initializer").

__device__ __attribute__((used)) int dev_used = 42;

// LLVM: @dev_used = addrspace(1) externally_initialized global i32 42
// LLVM: @llvm.compiler.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(1) @dev_used to ptr)], section "llvm.metadata"
