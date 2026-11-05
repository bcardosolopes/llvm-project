// RUN: %clang_cc1 -Wno-error=int-conversion -triple amdgcn-unknown-unknown -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -Wno-error=int-conversion -triple amdgcn-unknown-unknown -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -Wno-error=int-conversion -triple amdgcn-unknown-unknown -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Verify that __builtin_frame_address preserves the alloca address space
// (addrspace 5 on AMDGPU) through the CIR pipeline.

// CIR-LABEL: @test_builtin_frame_address
// CIR: cir.frame_address({{%.*}}) : !cir.ptr<!u8i, lang_address_space(offload_private)>
// CIR: cir.cast bitcast {{%.*}} : !cir.ptr<!u8i, lang_address_space(offload_private)> -> !cir.ptr<!void, lang_address_space(offload_private)>

// LLVM-LABEL: @test_builtin_frame_address
// LLVM: call ptr addrspace(5) @llvm.frameaddress.p5(i32 0)
// OGCG-LABEL: @test_builtin_frame_address
// OGCG: call ptr addrspace(5) @llvm.frameaddress.p5(i32 0)
void test_builtin_frame_address(int *out) {
    *out = __builtin_frame_address(0);
}
