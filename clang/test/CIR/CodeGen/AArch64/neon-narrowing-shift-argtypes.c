// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -target-feature +neon \
// RUN:   -fclangir -emit-cir -o %t.cir %s
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -target-feature +neon \
// RUN:   -fclangir -emit-llvm -o %t.ll %s
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -target-feature +neon \
// RUN:   -emit-llvm -o %t.ogcg.ll %s
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Verify scalar narrowing-shift intrinsics (i64->i32) pass the i64 argument
// directly without truncating it via a bitcast to i32.

#include <arm_neon.h>

// CIR-LABEL: test_vqrshrnd_n_s64
// CIR: cir.call_llvm_intrinsic "aarch64.neon.sqrshrn" {{.*}} : (!s64i, !s32i) -> !s32i
// LLVM-LABEL: test_vqrshrnd_n_s64
// LLVM: call i32 @llvm.aarch64.neon.sqrshrn.i32(i64 {{.*}}, i32 3)
// OGCG-LABEL: test_vqrshrnd_n_s64
// OGCG: call i32 @llvm.aarch64.neon.sqrshrn.i32(i64 {{.*}}, i32 3)
int32_t test_vqrshrnd_n_s64(int64_t a) {
  return vqrshrnd_n_s64(a, 3);
}

// CIR-LABEL: test_vqrshrnd_n_u64
// CIR: cir.call_llvm_intrinsic "aarch64.neon.uqrshrn" {{.*}} : (!u64i, !s32i) -> !u32i
// LLVM-LABEL: test_vqrshrnd_n_u64
// LLVM: call i32 @llvm.aarch64.neon.uqrshrn.i32(i64 {{.*}}, i32 3)
// OGCG-LABEL: test_vqrshrnd_n_u64
// OGCG: call i32 @llvm.aarch64.neon.uqrshrn.i32(i64 {{.*}}, i32 3)
uint32_t test_vqrshrnd_n_u64(uint64_t a) {
  return vqrshrnd_n_u64(a, 3);
}

// CIR-LABEL: test_vqrshrund_n_s64
// CIR: cir.call_llvm_intrinsic "aarch64.neon.sqrshrun" {{.*}} : (!s64i, !s32i) -> !u32i
// LLVM-LABEL: test_vqrshrund_n_s64
// LLVM: call i32 @llvm.aarch64.neon.sqrshrun.i32(i64 {{.*}}, i32 3)
// OGCG-LABEL: test_vqrshrund_n_s64
// OGCG: call i32 @llvm.aarch64.neon.sqrshrun.i32(i64 {{.*}}, i32 3)
uint32_t test_vqrshrund_n_s64(int64_t a) {
  return vqrshrund_n_s64(a, 3);
}

// CIR-LABEL: test_vqshrnd_n_s64
// CIR: cir.call_llvm_intrinsic "aarch64.neon.sqshrn" {{.*}} : (!s64i, !s32i) -> !s32i
// LLVM-LABEL: test_vqshrnd_n_s64
// LLVM: call i32 @llvm.aarch64.neon.sqshrn.i32(i64 {{.*}}, i32 3)
// OGCG-LABEL: test_vqshrnd_n_s64
// OGCG: call i32 @llvm.aarch64.neon.sqshrn.i32(i64 {{.*}}, i32 3)
int32_t test_vqshrnd_n_s64(int64_t a) {
  return vqshrnd_n_s64(a, 3);
}

// CIR-LABEL: test_vqshrnd_n_u64
// CIR: cir.call_llvm_intrinsic "aarch64.neon.uqshrn" {{.*}} : (!u64i, !s32i) -> !u32i
// LLVM-LABEL: test_vqshrnd_n_u64
// LLVM: call i32 @llvm.aarch64.neon.uqshrn.i32(i64 {{.*}}, i32 3)
// OGCG-LABEL: test_vqshrnd_n_u64
// OGCG: call i32 @llvm.aarch64.neon.uqshrn.i32(i64 {{.*}}, i32 3)
uint32_t test_vqshrnd_n_u64(uint64_t a) {
  return vqshrnd_n_u64(a, 3);
}

// CIR-LABEL: test_vqshrund_n_s64
// CIR: cir.call_llvm_intrinsic "aarch64.neon.sqshrun" {{.*}} : (!s64i, !s32i) -> !u32i
// LLVM-LABEL: test_vqshrund_n_s64
// LLVM: call i32 @llvm.aarch64.neon.sqshrun.i32(i64 {{.*}}, i32 3)
// OGCG-LABEL: test_vqshrund_n_s64
// OGCG: call i32 @llvm.aarch64.neon.sqshrun.i32(i64 {{.*}}, i32 3)
uint32_t test_vqshrund_n_s64(int64_t a) {
  return vqshrund_n_s64(a, 3);
}
