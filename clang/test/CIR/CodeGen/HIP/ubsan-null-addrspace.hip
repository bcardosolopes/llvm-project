// REQUIRES: amdgpu-registered-target

// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -x hip -fclangir -fcuda-is-device \
// RUN:            -fsanitize=null -emit-llvm -disable-llvm-passes %s -o %t.cir.ll
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir.ll %s

// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -x hip -fcuda-is-device \
// RUN:            -fsanitize=null -emit-llvm -disable-llvm-passes %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// On AMDGCN the default globals address space is 1, so a UBSan handler's
// info-data global is emitted as addrspace(1) and the handler's data-pointer
// parameter must use that address space. Before the fix the parameter was
// hardcoded to the default (addrspace 0), so the call argument and parameter
// pointer types mismatched and aborted with "Calling a function with a bad
// signature!".

#define __device__ __attribute__((device))

__device__ int foo(char *p) {
  *p = 0;
  return 3;
}

// CIR: declare void @__ubsan_handle_type_mismatch_v1_abort(ptr addrspace(1), i64)
// OGCG: declare void @__ubsan_handle_type_mismatch_v1_abort(ptr addrspace(1), i64)
