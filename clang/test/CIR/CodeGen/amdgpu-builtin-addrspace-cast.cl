// RUN: %clang_cc1 -x cl -triple amdgcn-amd-amdhsa -fclangir -emit-cir -o %t.cir %s
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -x cl -triple amdgcn-amd-amdhsa -fclangir -emit-llvm -o %t.ll %s
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -x cl -triple amdgcn-amd-amdhsa -emit-llvm -o %t.ogcg.ll %s
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// AMDGPU ClangIR support is incomplete and several CHECK lines predate the
// current LLVM output (e.g. captures(none), LLVM-IR arg names); this
// dormant test needs the AMDGPU backend to create a TargetMachine
// for -emit-llvm. Re-enable as amdgcn CIRGen support and CHECK lines are
// refreshed.
// REQUIRES: amdgpu-registered-target
// XFAIL: *

// Test that AMDGCN builtins taking pointer arguments handle address space
// differences correctly (getCorrectedPtr must handle same-pointee different-AS).

bool test_is_shared(global int *p) {
  return __builtin_amdgcn_is_shared(p);
}

// CIR: cir.func {{.*}}@test_is_shared
// CIR:   cir.cast address_space %{{.*}} : !cir.ptr<!s32i, lang_address_space(offload_global)> -> !cir.ptr<!void, lang_address_space(offload_generic)>
// CIR:   cir.cast address_space %{{.*}} : !cir.ptr<!void, lang_address_space(offload_generic)> -> !cir.ptr<!void>
// CIR:   cir.call_llvm_intrinsic "amdgcn.is.shared" %{{.*}} : (!cir.ptr<!void>) -> !cir.int<s, 1>

// LLVM: define {{.*}}@test_is_shared(ptr addrspace(1) noundef readnone %{{.*}})
// LLVM:   addrspacecast ptr addrspace(1) %{{.*}} to ptr
// LLVM:   tail call i1 @llvm.amdgcn.is.shared(ptr %{{.*}})
// LLVM:   ret i1

// OGCG: define {{.*}}@test_is_shared(ptr addrspace(1) noundef readnone %p)
// OGCG:   addrspacecast ptr addrspace(1) %p to ptr
// OGCG:   tail call i1 @llvm.amdgcn.is.shared(ptr %{{.*}})
// OGCG:   ret i1

bool test_is_private(global int *p) {
  return __builtin_amdgcn_is_private(p);
}

// CIR: cir.func {{.*}}@test_is_private
// CIR:   cir.cast address_space %{{.*}} : !cir.ptr<!s32i, lang_address_space(offload_global)> -> !cir.ptr<!void, lang_address_space(offload_generic)>
// CIR:   cir.cast address_space %{{.*}} : !cir.ptr<!void, lang_address_space(offload_generic)> -> !cir.ptr<!void>
// CIR:   cir.call_llvm_intrinsic "amdgcn.is.private" %{{.*}} : (!cir.ptr<!void>) -> !cir.int<s, 1>

// LLVM: define {{.*}}@test_is_private(ptr addrspace(1) noundef readnone %{{.*}})
// LLVM:   addrspacecast ptr addrspace(1) %{{.*}} to ptr
// LLVM:   tail call i1 @llvm.amdgcn.is.private

// OGCG: define {{.*}}@test_is_private(ptr addrspace(1) noundef readnone %p)
// OGCG:   addrspacecast ptr addrspace(1) %p to ptr
// OGCG:   tail call i1 @llvm.amdgcn.is.private
