// Verify that CIR preserves the original LLVM data layout string faithfully.
// MLIR's DLTI round-trip is lossy (drops vector alignments, non-integral pointer
// address spaces, etc.), so CIR saves and restores the original string.

// RUN: %clang_cc1 -fclangir -triple amdgcn-amd-amdhsa -emit-llvm %s -o %t.cir.ll
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck %s --input-file=%t.cir.ll
// RUN: FileCheck %s --input-file=%t.ogcg.ll

// Verify the full AMDGPU data layout including vector alignments and ni:7:8:9.
// CHECK: target datalayout = "e-m:e-p:64:64-p1:64:64-p2:32:32-p3:32:32-p4:64:64-p5:32:32-p6:32:32-p7:160:256:256:32-p8:128:128:128:48-p9:192:256:256:32-i64:64-v16:16-v24:32-v32:32-v48:64-v96:128-v192:256-v256:256-v512:512-v1024:1024-v2048:2048-n32:64-S32-A5-G1-ni:7:8:9"

void f(void) {}
