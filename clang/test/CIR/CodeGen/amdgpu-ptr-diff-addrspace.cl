// RUN: %clang_cc1 -x cl -triple amdgcn-amd-amdhsa -cl-std=CL2.0 -fclangir -emit-cir -o %t.cir %s
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -x cl -triple amdgcn-amd-amdhsa -cl-std=CL2.0 -fclangir -emit-llvm -o %t.ll %s
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -x cl -triple amdgcn-amd-amdhsa -cl-std=CL2.0 -emit-llvm -o %t.ogcg.ll %s
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Test that pointer subtraction between different address spaces works
// correctly. The CIR codegen must insert an addrspacecast before
// emitting cir.ptr_diff when the pointer types differ.

long test_ptr_diff(global int *gp, generic int *gen) {
  return gen - gp;
}

// CIR: cir.func {{.*}}@test_ptr_diff
// CIR:   %[[GP:.*]] = cir.load {{.*}} !cir.ptr<!s32i, lang_address_space(offload_global)>
// CIR:   %[[CAST:.*]] = cir.cast address_space %[[GP]] : !cir.ptr<!s32i, lang_address_space(offload_global)> -> !cir.ptr<!s32i, lang_address_space(offload_generic)>
// CIR:   cir.ptr_diff %{{.*}}, %[[CAST]] : !cir.ptr<!s32i, lang_address_space(offload_generic)>

// LLVM: define {{.*}}@test_ptr_diff(ptr addrspace(1) noundef %{{.*}}, ptr noundef %{{.*}})
// LLVM:   addrspacecast ptr addrspace(1) %{{.*}} to ptr
// LLVM:   ptrtoint
// LLVM:   ptrtoint
// LLVM:   sub i64
// LLVM:   ashr exact i64 %{{.*}}, 2
// LLVM:   ret i64

// OGCG: define {{.*}}@test_ptr_diff(ptr addrspace(1) noundef %gp, ptr noundef %gen)
// OGCG:   ptrtoint
// OGCG:   ptrtoint
// OGCG:   sub i64
// OGCG:   ashr exact i64 %{{.*}}, 2
// OGCG:   ret i64
