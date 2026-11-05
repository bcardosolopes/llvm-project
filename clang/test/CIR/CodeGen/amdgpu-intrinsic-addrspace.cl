// RUN: %clang_cc1 -x cl -triple amdgcn-amd-amdhsa -target-feature +vmem-to-lds-load-insts -fclangir -emit-cir -o %t.cir %s
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -x cl -triple amdgcn-amd-amdhsa -target-feature +vmem-to-lds-load-insts -fclangir -emit-llvm -o %t.ll %s
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s

// Test that LLVM intrinsics with address-space-qualified pointer arguments
// correctly use target address spaces (not mismatched lang address spaces).
// The IIT (Intrinsic Info Table) specifies target address spaces directly,
// which should map to TargetAddressSpaceAttr in CIR.

void test_global_load_lds(global unsigned *src, local unsigned *dst) {
  __builtin_amdgcn_global_load_lds(src, dst, /*size=*/4, /*offset=*/0, /*aux=*/0);
}

// CIR: cir.func {{.*}}@test_global_load_lds
// CIR:   cir.call_llvm_intrinsic "amdgcn.global.load.lds"
// CIR-SAME: !cir.ptr<!void, target_address_space(1)>
// CIR-SAME: !cir.ptr<!void, target_address_space(3)>

// LLVM: define {{.*}}@test_global_load_lds(ptr addrspace(1) noundef {{.*}}, ptr addrspace(3) noundef {{.*}})
// LLVM:   call void @llvm.amdgcn.global.load.lds(ptr addrspace(1) %{{.*}}, ptr addrspace(3) %{{.*}}, i32 4, i32 0, i32 0)
