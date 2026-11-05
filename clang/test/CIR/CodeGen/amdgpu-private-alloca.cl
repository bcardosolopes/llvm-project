// See also clang/test/CodeGenOpenCL/amdgcn-automatic-variable.cl
// RUN: %clang_cc1 -fclangir -O0 -cl-std=CL1.2 -triple amdgcn-amd-amdhsa -emit-cir %s -o %t.cl12.cir
// RUN: FileCheck %s --input-file=%t.cl12.cir --check-prefix=CIR-CL12
// RUN: %clang_cc1 -fclangir -O0 -cl-std=CL2.0 -triple amdgcn-amd-amdhsa -emit-cir %s -o %t.cl20.cir
// RUN: FileCheck %s --input-file=%t.cl20.cir --check-prefix=CIR-CL20
// RUN: %clang_cc1 -fclangir -O0 -cl-std=CL1.2 -triple amdgcn-amd-amdhsa -emit-llvm %s -o %t.cl12.ll
// RUN: FileCheck %s --input-file=%t.cl12.ll --check-prefix=LLVM-CL12
// RUN: %clang_cc1 -O0 -cl-std=CL1.2 -triple amdgcn-amd-amdhsa -emit-llvm %s -o %t.ogcg.cl12.ll
// RUN: FileCheck %s --input-file=%t.ogcg.cl12.ll --check-prefix=LLVM-CL12
// RUN: %clang_cc1 -fclangir -O0 -cl-std=CL2.0 -triple amdgcn-amd-amdhsa -emit-llvm %s -o %t.cl20.ll
// RUN: FileCheck %s --input-file=%t.cl20.ll --check-prefix=LLVM-CL20
// RUN: %clang_cc1 -O0 -cl-std=CL2.0 -triple amdgcn-amd-amdhsa -emit-llvm %s -o %t.ogcg.cl20.ll
// RUN: FileCheck %s --input-file=%t.ogcg.cl20.ll --check-prefix=LLVM-CL20

// Test that CIR respects the type's address space when creating alloca casts.
// In OpenCL CL1.2 on AMDGCN, the default address space for local variables is
// private (AS5), so __private pointer variables should store/load AS5 pointers
// without any size-changing addrspacecast.
// In CL2.0, the default is generic, so variables use the generic address space.
// Regression test: previously CIR always cast to generic, causing a pointer
// size mismatch (64-bit generic ptr stored into 32-bit AS5 ptr alloca).

// Storing a __private pointer into a __private-qualified pointer variable:
// In CL1.2, both the pointer value and the alloca stay in AS5 — no
// addrspacecast or bitcast should change the pointer width.
void private_ptr_alloca() {
  long var = 5;
  __private long *alloca_addr = &var;
  *alloca_addr = 8;
}

// CIR-CL12: cir.func {{.*}}@private_ptr_alloca
// CIR-CL12: cir.alloca !s64i, !cir.ptr<!s64i, lang_address_space(offload_private)>
// CIR-CL12: cir.alloca !cir.ptr<!s64i, lang_address_space(offload_private)>, !cir.ptr<!cir.ptr<!s64i, lang_address_space(offload_private)>, lang_address_space(offload_private)>
// Store AS5 pointer directly into AS5 alloca, no bitcast to widen/narrow.
// CIR-CL12: cir.store {{.*}} !cir.ptr<!s64i, lang_address_space(offload_private)>, !cir.ptr<!cir.ptr<!s64i, lang_address_space(offload_private)>, lang_address_space(offload_private)>

// CIR-CL20: cir.func {{.*}}@private_ptr_alloca
// In CL2.0, __private-qualified pointers are still AS5.
// CIR-CL20: cir.alloca !cir.ptr<!s64i, lang_address_space(offload_private)>, !cir.ptr<!cir.ptr<!s64i, lang_address_space(offload_private)>, lang_address_space(offload_private)>

// LLVM-CL12: define {{.*}}@private_ptr_alloca
// LLVM-CL12: alloca i64, {{.*}}addrspace(5)
// LLVM-CL12: alloca ptr addrspace(5), {{.*}}addrspace(5)
// LLVM-CL12: store ptr addrspace(5) %{{.*}}, ptr addrspace(5) %{{.*}}
// LLVM-CL12: load ptr addrspace(5), ptr addrspace(5) %{{.*}}

// Generic pointer to local variable in CL2.0 requires addrspacecast.
void generic_ptr_alloca() {
  long var = 5;
  long *alloca_addr_as_generic = &var;
  *alloca_addr_as_generic = 9;
}

// LLVM-CL20: define {{.*}}@generic_ptr_alloca
// LLVM-CL20: alloca i64, {{.*}}addrspace(5)
// LLVM-CL20: alloca ptr, {{.*}}addrspace(5)
// LLVM-CL20: addrspacecast ptr addrspace(5) %{{.*}} to ptr
// LLVM-CL20: store ptr %{{.*}}, ptr addrspace(5) %{{.*}}
