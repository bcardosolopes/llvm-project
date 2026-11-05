// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -target-feature +neon \
// RUN:    -fclangir -disable-O0-optnone \
// RUN:  -flax-vector-conversions=none -emit-cir -o %t.cir %s
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s

// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -target-feature +neon \
// RUN:    -fclangir -disable-O0-optnone \
// RUN:  -flax-vector-conversions=none -emit-llvm -fno-clangir-call-conv-lowering -o - %s \
// RUN: | opt -S -passes=mem2reg,simplifycfg -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// REQUIRES: aarch64-registered-target || arm-registered-target

// Test vld{2,3,4}_dup and vld{2,3,4}q_dup NEON intrinsics.
// These load one element from memory and duplicate it across all lanes.

#include <arm_neon.h>

// vld2_dup
int32x2x2_t test_vld2_dup_s32(int32_t const *ptr) {
    return vld2_dup_s32(ptr);
}

// CIR-LABEL: test_vld2_dup_s32
// CIR: cir.call_llvm_intrinsic "aarch64.neon.ld2r"

// LLVM-LABEL: @test_vld2_dup_s32
// LLVM: call { <2 x i32>, <2 x i32> } @llvm.aarch64.neon.ld2r.v2i32.p0(ptr %{{.*}})

// vld2q_dup
int32x4x2_t test_vld2q_dup_s32(int32_t const *ptr) {
    return vld2q_dup_s32(ptr);
}

// CIR-LABEL: test_vld2q_dup_s32
// CIR: cir.call_llvm_intrinsic "aarch64.neon.ld2r"

// LLVM-LABEL: @test_vld2q_dup_s32
// LLVM: call { <4 x i32>, <4 x i32> } @llvm.aarch64.neon.ld2r.v4i32.p0(ptr %{{.*}})

// vld3_dup
int32x2x3_t test_vld3_dup_s32(int32_t const *ptr) {
    return vld3_dup_s32(ptr);
}

// CIR-LABEL: test_vld3_dup_s32
// CIR: cir.call_llvm_intrinsic "aarch64.neon.ld3r"

// LLVM-LABEL: @test_vld3_dup_s32
// LLVM: call { <2 x i32>, <2 x i32>, <2 x i32> } @llvm.aarch64.neon.ld3r.v2i32.p0(ptr %{{.*}})

// vld3q_dup
int32x4x3_t test_vld3q_dup_s32(int32_t const *ptr) {
    return vld3q_dup_s32(ptr);
}

// CIR-LABEL: test_vld3q_dup_s32
// CIR: cir.call_llvm_intrinsic "aarch64.neon.ld3r"

// LLVM-LABEL: @test_vld3q_dup_s32
// LLVM: call { <4 x i32>, <4 x i32>, <4 x i32> } @llvm.aarch64.neon.ld3r.v4i32.p0(ptr %{{.*}})

// vld4_dup
int32x2x4_t test_vld4_dup_s32(int32_t const *ptr) {
    return vld4_dup_s32(ptr);
}

// CIR-LABEL: test_vld4_dup_s32
// CIR: cir.call_llvm_intrinsic "aarch64.neon.ld4r"

// LLVM-LABEL: @test_vld4_dup_s32
// LLVM: call { <2 x i32>, <2 x i32>, <2 x i32>, <2 x i32> } @llvm.aarch64.neon.ld4r.v2i32.p0(ptr %{{.*}})

// vld4q_dup
int32x4x4_t test_vld4q_dup_s32(int32_t const *ptr) {
    return vld4q_dup_s32(ptr);
}

// CIR-LABEL: test_vld4q_dup_s32
// CIR: cir.call_llvm_intrinsic "aarch64.neon.ld4r"

// LLVM-LABEL: @test_vld4q_dup_s32
// LLVM: call { <4 x i32>, <4 x i32>, <4 x i32>, <4 x i32> } @llvm.aarch64.neon.ld4r.v4i32.p0(ptr %{{.*}})
