// RUN: %clang_cc1 -triple amdgcn-unknown-unknown -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple amdgcn-unknown-unknown -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple amdgcn-unknown-unknown -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Verify opencl_global_device and opencl_global_host address spaces are
// handled correctly and map to the same target AS as opencl_global (addrspace 1).

// CIR-LABEL: @fd
// CIR: !cir.ptr<!s32i, lang_address_space(offload_global)>

// LLVM-LABEL: @fd
// LLVM: ptr addrspace(1)
// OGCG-LABEL: @fd
// OGCG: ptr addrspace(1)
void fd(__attribute__((opencl_global_device)) int *arg) {}

// CIR-LABEL: @fh
// CIR: !cir.ptr<!s32i, lang_address_space(offload_global)>

// LLVM-LABEL: @fh
// LLVM: ptr addrspace(1)
// OGCG-LABEL: @fh
// OGCG: ptr addrspace(1)
void fh(__attribute__((opencl_global_host)) int *arg) {}
