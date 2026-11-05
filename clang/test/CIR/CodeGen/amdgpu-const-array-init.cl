// RUN: %clang_cc1 -fclangir -cl-std=CL1.2 -triple amdgcn-amd-amdhsa -emit-cir %s -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir --check-prefix=CIR
// RUN: %clang_cc1 -fclangir -O0 -cl-std=CL1.2 -triple amdgcn-amd-amdhsa -emit-cir %s -o %t.o0.cir
// RUN: FileCheck %s --input-file=%t.o0.cir --check-prefix=CIR-O0
// RUN: %clang_cc1 -fclangir -O0 -cl-std=CL1.2 -triple amdgcn-amd-amdhsa -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll --check-prefix=LLVM
// RUN: %clang_cc1 -O0 -cl-std=CL1.2 -triple amdgcn-amd-amdhsa -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck %s --input-file=%t.ogcg.ll --check-prefix=LLVM

// Test that constant array initialization on AMDGCN preserves address spaces.
// Ordinary local constant aggregate init should use a cross-address-space
// constant-global copy here, even at OpenCL's default optimization level.
// The source global is in the constant address space (AS4) while the alloca
// is in private (AS5). cir.copy must carry both address spaces without an
// invalid addrspacecast.

void const_array_init(int *out) {
  const int arr[] = {4, 5, 6};
  *out = arr[0];
}

// At OpenCL's default optimization level: fallback uses cross-AS cir.copy.
// CIR: cir.func {{.*}}@const_array_init
// CIR: %[[ALLOCA:.*]] = cir.alloca !cir.array<!s32i x 3>, !cir.ptr<!cir.array<!s32i x 3>, lang_address_space(offload_private)>
// CIR: %[[GLOBAL:.*]] = cir.get_global @__const.const_array_init.arr : !cir.ptr<!cir.array<!s32i x 3>, lang_address_space(offload_constant)>
// CIR: cir.copy %[[GLOBAL]] to %[[ALLOCA]] {{.*}} : !cir.ptr<!cir.array<!s32i x 3>, lang_address_space(offload_constant)>, !cir.ptr<!cir.array<!s32i x 3>, lang_address_space(offload_private)>

// At -O0: fallback also uses cross-AS cir.copy (no addrspacecast).
// CIR-O0: cir.func {{.*}}@const_array_init
// CIR-O0: %[[ALLOCA:.*]] = cir.alloca !cir.array<!s32i x 3>, !cir.ptr<!cir.array<!s32i x 3>, lang_address_space(offload_private)>
// CIR-O0: %[[GLOBAL:.*]] = cir.get_global @__const.const_array_init.arr : !cir.ptr<!cir.array<!s32i x 3>, lang_address_space(offload_constant)>
// CIR-O0: cir.copy %[[GLOBAL]] to %[[ALLOCA]] {{.*}} : !cir.ptr<!cir.array<!s32i x 3>, lang_address_space(offload_constant)>, !cir.ptr<!cir.array<!s32i x 3>, lang_address_space(offload_private)>

// LLVM: define {{.*}}@const_array_init
// LLVM: alloca [3 x i32], {{.*}}addrspace(5)
// LLVM: call void @llvm.memcpy.p5.p4.i64(ptr addrspace(5){{.*}}, ptr addrspace(4){{.*}}, i64 12, i1 false)
