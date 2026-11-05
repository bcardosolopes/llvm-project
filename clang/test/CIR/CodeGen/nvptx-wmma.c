// RUN: %clang_cc1 -triple nvptx64-nvidia-cuda -target-feature +sm_80 -target-feature +ptx80 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple nvptx64-nvidia-cuda -target-feature +sm_80 -target-feature +ptx80 -O2 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple nvptx64-nvidia-cuda -target-feature +sm_80 -target-feature +ptx80 -O2 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// The __hmma_*/__imma_*/__dmma_*/__mma_* WMMA/MMA tensor-core builtins map to
// the llvm.nvvm.wmma.*/llvm.nvvm.mma.* intrinsic family. Classic ref:
// clang/lib/CodeGen/TargetBuiltins/NVPTX.cpp (getNVPTXMmaLdstInfo /
// getNVPTXMmaInfo). ClangIR emits the same intrinsics via cir.call_llvm_intrinsic
// and lets the CIR->LLVM lowering re-derive the pointer overload suffix (.p0)
// from the operand types. The LLVM/OGCG checks run at -O2 so the fragment-array
// round-trips fold away, leaving only the intrinsic calls.

// Half-precision inputs with an f32 accumulator (m16n16k16, row-major).
void hmma_f32(const int *a, const int *b, const float *c, float *d) {
  int fa[8], fb[8]; float fc[8], fd[8];
  __hmma_m16n16k16_ld_a(fa, a, 16, 0);
  __hmma_m16n16k16_ld_b(fb, b, 16, 0);
  __hmma_m16n16k16_ld_c_f32(fc, c, 16, 0);
  __hmma_m16n16k16_mma_f32f32(fd, fa, fb, fc, 0, 0);
  __hmma_m16n16k16_st_c_f32(d, fd, 16, 0);
}

// CIR-LABEL: @hmma_f32
// CIR: cir.call_llvm_intrinsic "nvvm.wmma.m16n16k16.load.a.row.stride.f16" {{.*}} -> !rec_anon_struct
// CIR: cir.call_llvm_intrinsic "nvvm.wmma.m16n16k16.load.b.row.stride.f16" {{.*}} -> !rec_anon_struct
// CIR: cir.call_llvm_intrinsic "nvvm.wmma.m16n16k16.load.c.row.stride.f32"
// CIR: cir.call_llvm_intrinsic "nvvm.wmma.m16n16k16.mma.row.row.f32.f32" {{.*}} !cir.vector<2 x !cir.f16>{{.*}} !cir.float
// CIR: cir.call_llvm_intrinsic "nvvm.wmma.m16n16k16.store.d.row.stride.f32" {{.*}} -> !void
// LLVM-LABEL: @hmma_f32
// LLVM-DAG: call {{.*}} @llvm.nvvm.wmma.m16n16k16.load.a.row.stride.f16.p0(
// LLVM-DAG: call {{.*}} @llvm.nvvm.wmma.m16n16k16.load.b.row.stride.f16.p0(
// LLVM-DAG: call {{.*}} @llvm.nvvm.wmma.m16n16k16.load.c.row.stride.f32.p0(
// LLVM-DAG: call {{.*}} @llvm.nvvm.wmma.m16n16k16.mma.row.row.f32.f32(
// LLVM-DAG: call void @llvm.nvvm.wmma.m16n16k16.store.d.row.stride.f32.p0(
// OGCG-LABEL: @hmma_f32
// OGCG-DAG: call {{.*}} @llvm.nvvm.wmma.m16n16k16.load.a.row.stride.f16.p0(
// OGCG-DAG: call {{.*}} @llvm.nvvm.wmma.m16n16k16.load.b.row.stride.f16.p0(
// OGCG-DAG: call {{.*}} @llvm.nvvm.wmma.m16n16k16.load.c.row.stride.f32.p0(
// OGCG-DAG: call {{.*}} @llvm.nvvm.wmma.m16n16k16.mma.row.row.f32.f32(
// OGCG-DAG: call void @llvm.nvvm.wmma.m16n16k16.store.d.row.stride.f32.p0(

// Signed 8-bit integer inputs, col-major load, i32 accumulator.
void imma_s8(const int *a, const int *b, const int *c, int *d) {
  int fa[8], fb[8], fc[8], fd[8];
  __imma_m16n16k16_ld_a_s8(fa, a, 16, 1);
  __imma_m16n16k16_mma_s8(fd, fa, fb, fc, 0, 0);
  __imma_m16n16k16_st_c_i32(d, fd, 16, 0);
}

// CIR-LABEL: @imma_s8
// CIR: cir.call_llvm_intrinsic "nvvm.wmma.m16n16k16.load.a.col.stride.s8"
// CIR: cir.call_llvm_intrinsic "nvvm.wmma.m16n16k16.mma.row.row.s8" {{.*}} !s32i
// CIR: cir.call_llvm_intrinsic "nvvm.wmma.m16n16k16.store.d.row.stride.s32" {{.*}} -> !void
// LLVM-LABEL: @imma_s8
// LLVM-DAG: call {{.*}} @llvm.nvvm.wmma.m16n16k16.load.a.col.stride.s8.p0(
// LLVM-DAG: call {{.*}} @llvm.nvvm.wmma.m16n16k16.mma.row.row.s8(
// LLVM-DAG: call void @llvm.nvvm.wmma.m16n16k16.store.d.row.stride.s32.p0(
// OGCG-LABEL: @imma_s8
// OGCG-DAG: call {{.*}} @llvm.nvvm.wmma.m16n16k16.load.a.col.stride.s8.p0(
// OGCG-DAG: call {{.*}} @llvm.nvvm.wmma.m16n16k16.mma.row.row.s8(
// OGCG-DAG: call void @llvm.nvvm.wmma.m16n16k16.store.d.row.stride.s32.p0(

// Double-precision (m8n8k4). The A fragment load returns a scalar (not an
// aggregate), exercising the NumResults == 1 path.
void dmma_f64(const double *a, double *d) {
  double fa[1], fb[1], fc[2], fd[2];
  __dmma_m8n8k4_ld_a(fa, a, 4, 0);
  __dmma_m8n8k4_mma_f64(fd, fa, fb, fc, 0, 0);
  __dmma_m8n8k4_st_c_f64(d, fd, 4, 0);
}

// CIR-LABEL: @dmma_f64
// CIR: cir.call_llvm_intrinsic "nvvm.wmma.m8n8k4.load.a.row.stride.f64" {{.*}} -> !cir.double
// CIR: cir.call_llvm_intrinsic "nvvm.wmma.m8n8k4.mma.row.row.f64" {{.*}} !cir.double
// CIR: cir.call_llvm_intrinsic "nvvm.wmma.m8n8k4.store.d.row.stride.f64" {{.*}} -> !void
// LLVM-LABEL: @dmma_f64
// LLVM-DAG: call {{.*}} @llvm.nvvm.wmma.m8n8k4.load.a.row.stride.f64.p0(
// LLVM-DAG: call {{.*}} @llvm.nvvm.wmma.m8n8k4.mma.row.row.f64(
// LLVM-DAG: call void @llvm.nvvm.wmma.m8n8k4.store.d.row.stride.f64.p0(
// OGCG-LABEL: @dmma_f64
// OGCG-DAG: call {{.*}} @llvm.nvvm.wmma.m8n8k4.load.a.row.stride.f64.p0(
// OGCG-DAG: call {{.*}} @llvm.nvvm.wmma.m8n8k4.mma.row.row.f64(
// OGCG-DAG: call void @llvm.nvvm.wmma.m8n8k4.store.d.row.stride.f64.p0(
