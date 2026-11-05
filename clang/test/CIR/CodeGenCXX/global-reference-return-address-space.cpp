// RUN: %clang_cc1 %s -triple amdgcn-amd-amdhsa -fclangir -emit-cir -o %t.amdgpu.cir
// RUN: FileCheck --input-file=%t.amdgpu.cir %s --check-prefix=CIR-AMDGPU
// RUN: %clang_cc1 %s -triple spirv64-amd-amdhsa -fclangir -emit-cir -o %t.spirv.cir
// RUN: FileCheck --input-file=%t.spirv.cir %s --check-prefix=CIR-SPIRV
// RUN: %clang_cc1 %s -triple amdgcn-amd-amdhsa -fclangir -emit-llvm -o %t.amdgpu.ll
// RUN: FileCheck --input-file=%t.amdgpu.ll %s --check-prefix=LLVM-AMDGPU
// RUN: %clang_cc1 %s -triple amdgcn-amd-amdhsa -emit-llvm -o %t.amdgpu.ogcg.ll
// RUN: FileCheck --input-file=%t.amdgpu.ogcg.ll %s --check-prefix=OGCG-AMDGPU
// RUN: %clang_cc1 %s -triple spirv64-amd-amdhsa -fclangir -emit-llvm -o %t.spirv.ll
// RUN: FileCheck --input-file=%t.spirv.ll %s --check-prefix=LLVM-SPIRV
// RUN: %clang_cc1 %s -triple spirv64-amd-amdhsa -emit-llvm -o %t.spirv.ogcg.ll
// RUN: FileCheck --input-file=%t.spirv.ogcg.ll %s --check-prefix=OGCG-SPIRV

struct A { virtual void f(); };
struct B : A { };

B fail;

const B &g() { return fail; }

// CIR-AMDGPU: cir.global  external target_address_space(1) @fail
// CIR-AMDGPU-LABEL: cir.func no_inline optnone {{.*}} @_Z1gv()
// CIR-AMDGPU: %[[GLOBAL:.*]] = cir.get_global @fail : !cir.ptr<!rec_anon_struct{{.*}}, target_address_space(1)>
// CIR-AMDGPU: %[[GLOBAL_TYPED:.*]] = cir.cast bitcast %[[GLOBAL]] : !cir.ptr<!rec_anon_struct{{.*}}, target_address_space(1)> -> !cir.ptr<!rec_B, target_address_space(1)>
// CIR-AMDGPU: %[[RET:.*]] = cir.cast address_space %[[GLOBAL_TYPED]] : !cir.ptr<!rec_B, target_address_space(1)> -> !cir.ptr<!rec_B>
// CIR-AMDGPU: cir.return %[[RET]] : !cir.ptr<!rec_B>

// CIR-SPIRV: cir.global  external target_address_space(1) @fail
// CIR-SPIRV-LABEL: cir.func no_inline optnone @_Z1gv()
// CIR-SPIRV: %[[GLOBAL:.*]] = cir.get_global @fail : !cir.ptr<!rec_anon_struct{{.*}}, target_address_space(1)>
// CIR-SPIRV: %[[GLOBAL_TYPED:.*]] = cir.cast bitcast %[[GLOBAL]] : !cir.ptr<!rec_anon_struct{{.*}}, target_address_space(1)> -> !cir.ptr<!rec_B, target_address_space(1)>
// CIR-SPIRV: %[[RET:.*]] = cir.cast address_space %[[GLOBAL_TYPED]] : !cir.ptr<!rec_B, target_address_space(1)> -> !cir.ptr<!rec_B>
// CIR-SPIRV: cir.return %[[RET]] : !cir.ptr<!rec_B>

// LLVM-AMDGPU: @fail = addrspace(1) global { ptr addrspace(1) }
// LLVM-AMDGPU-LABEL: define dso_local noundef nonnull align 8 dereferenceable(8) ptr @_Z1gv()
// LLVM-AMDGPU-NOT: store ptr addrspacecast (ptr addrspace(1) @fail to ptr), ptr
// LLVM-AMDGPU: ret ptr addrspacecast (ptr addrspace(1) @fail to ptr)

// OGCG-AMDGPU: @fail = addrspace(1) global { ptr addrspace(1) }
// OGCG-AMDGPU-LABEL: define dso_local noundef nonnull align 8 dereferenceable(8) ptr @_Z1gv()
// OGCG-AMDGPU: ret ptr addrspacecast (ptr addrspace(1) @fail to ptr)

// LLVM-SPIRV: @fail = addrspace(1) global { ptr addrspace(1) }
// LLVM-SPIRV-LABEL: define spir_func noundef align 8 dereferenceable(8) ptr addrspace(4) @_Z1gv() addrspace(4)
// LLVM-SPIRV: ret ptr addrspace(4) addrspacecast (ptr addrspace(1) @fail to ptr addrspace(4))

// OGCG-SPIRV: @fail = addrspace(1) global { ptr addrspace(1) }
// OGCG-SPIRV-LABEL: define spir_func noundef align 8 dereferenceable(8) ptr addrspace(4) @_Z1gv() addrspace(4)
// OGCG-SPIRV: ret ptr addrspace(4) addrspacecast (ptr addrspace(1) @fail to ptr addrspace(4))
