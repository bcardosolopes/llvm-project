// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx1100 -target-feature +wavefrontsize32 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx1100 -target-feature +wavefrontsize32 -O2 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx1100 -target-feature +wavefrontsize32 -O2 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// The RDNA3 (gfx1100/gfx1101/gfx1102) __builtin_amdgcn_wmma_* matrix
// multiply-accumulate builtins map to the llvm.amdgcn.wmma.* intrinsic family.
// Classic ref: clang/lib/CodeGen/TargetBuiltins/AMDGPU.cpp (wmma_* ->
// Intrinsic::amdgcn_wmma_*). ClangIR emits the same intrinsics via
// cir.call_llvm_intrinsic and lets the CIR->LLVM lowering re-derive the
// overloaded result/A-B fragment suffixes (e.g. .v8f32.v16f16) from the operand
// and result types. The `_w32` wave-mode suffix is not part of the intrinsic
// name. The LLVM/OGCG checks run at -O2 so the by-value fragment round-trips
// fold away, leaving only the intrinsic call.

typedef __fp16 half16 __attribute__((ext_vector_type(16)));
typedef float float8 __attribute__((ext_vector_type(8)));
typedef short short16 __attribute__((ext_vector_type(16)));
typedef int int8v __attribute__((ext_vector_type(8)));
typedef int int4v __attribute__((ext_vector_type(4)));
typedef int int2v __attribute__((ext_vector_type(2)));

// Half-precision inputs with an f32 accumulator.
float8 test_f32_f16(half16 a, half16 b, float8 c) {
  return __builtin_amdgcn_wmma_f32_16x16x16_f16_w32(a, b, c);
}
// CIR-LABEL: cir.func {{.*}}@test_f32_f16
// CIR: cir.call_llvm_intrinsic "amdgcn.wmma.f32.16x16x16.f16" {{.*}} : (!cir.vector<16 x !cir.f16>, !cir.vector<16 x !cir.f16>, !cir.vector<8 x !cir.float>) -> !cir.vector<8 x !cir.float>
// LLVM-LABEL: @test_f32_f16
// LLVM: call <8 x float> @llvm.amdgcn.wmma.f32.16x16x16.f16.v8f32.v16f16(<16 x half> %{{.*}}, <16 x half> %{{.*}}, <8 x float> %{{.*}})
// OGCG-LABEL: @test_f32_f16
// OGCG: call <8 x float> @llvm.amdgcn.wmma.f32.16x16x16.f16.v8f32.v16f16(<16 x half> %{{.*}}, <16 x half> %{{.*}}, <8 x float> %{{.*}})

// Bf16 inputs (packed as short) with an f32 accumulator.
float8 test_f32_bf16(short16 a, short16 b, float8 c) {
  return __builtin_amdgcn_wmma_f32_16x16x16_bf16_w32(a, b, c);
}
// CIR-LABEL: cir.func {{.*}}@test_f32_bf16
// CIR: cir.call_llvm_intrinsic "amdgcn.wmma.f32.16x16x16.bf16" {{.*}} -> !cir.vector<8 x !cir.float>
// LLVM-LABEL: @test_f32_bf16
// LLVM: call <8 x float> @llvm.amdgcn.wmma.f32.16x16x16.bf16.v8f32.v16i16(<16 x i16> %{{.*}}, <16 x i16> %{{.*}}, <8 x float> %{{.*}})
// OGCG-LABEL: @test_f32_bf16
// OGCG: call <8 x float> @llvm.amdgcn.wmma.f32.16x16x16.bf16.v8f32.v16i16(<16 x i16> %{{.*}}, <16 x i16> %{{.*}}, <8 x float> %{{.*}})

// Half-precision throughout; the trailing arg is the i1 opsel immediate.
half16 test_f16_f16(half16 a, half16 b, half16 c) {
  return __builtin_amdgcn_wmma_f16_16x16x16_f16_w32(a, b, c, 0);
}
// CIR-LABEL: cir.func {{.*}}@test_f16_f16
// CIR: cir.call_llvm_intrinsic "amdgcn.wmma.f16.16x16x16.f16" {{.*}}, %{{[0-9]+}} : (!cir.vector<16 x !cir.f16>, !cir.vector<16 x !cir.f16>, !cir.vector<16 x !cir.f16>, !cir.bool) -> !cir.vector<16 x !cir.f16>
// LLVM-LABEL: @test_f16_f16
// LLVM: call <16 x half> @llvm.amdgcn.wmma.f16.16x16x16.f16.v16f16.v16f16(<16 x half> %{{.*}}, <16 x half> %{{.*}}, <16 x half> %{{.*}}, i1 false)
// OGCG-LABEL: @test_f16_f16
// OGCG: call <16 x half> @llvm.amdgcn.wmma.f16.16x16x16.f16.v16f16.v16f16(<16 x half> %{{.*}}, <16 x half> %{{.*}}, <16 x half> %{{.*}}, i1 false)

// Bf16 throughout, with the i1 opsel immediate.
short16 test_bf16_bf16(short16 a, short16 b, short16 c) {
  return __builtin_amdgcn_wmma_bf16_16x16x16_bf16_w32(a, b, c, 0);
}
// CIR-LABEL: cir.func {{.*}}@test_bf16_bf16
// CIR: cir.call_llvm_intrinsic "amdgcn.wmma.bf16.16x16x16.bf16" {{.*}} -> !cir.vector<16 x !s16i>
// LLVM-LABEL: @test_bf16_bf16
// LLVM: call <16 x i16> @llvm.amdgcn.wmma.bf16.16x16x16.bf16.v16i16.v16i16(<16 x i16> %{{.*}}, <16 x i16> %{{.*}}, <16 x i16> %{{.*}}, i1 false)
// OGCG-LABEL: @test_bf16_bf16
// OGCG: call <16 x i16> @llvm.amdgcn.wmma.bf16.16x16x16.bf16.v16i16.v16i16(<16 x i16> %{{.*}}, <16 x i16> %{{.*}}, <16 x i16> %{{.*}}, i1 false)

// Signed 8-bit integer inputs with an i32 accumulator; the leading/trailing
// i1 immediates are the A/B sign flags and the clamp flag.
int8v test_i32_iu8(int4v a, int4v b, int8v c) {
  return __builtin_amdgcn_wmma_i32_16x16x16_iu8_w32(1, a, 1, b, c, 0);
}
// CIR-LABEL: cir.func {{.*}}@test_i32_iu8
// CIR: cir.call_llvm_intrinsic "amdgcn.wmma.i32.16x16x16.iu8" {{.*}} : (!cir.bool, !cir.vector<4 x !s32i>, !cir.bool, !cir.vector<4 x !s32i>, !cir.vector<8 x !s32i>, !cir.bool) -> !cir.vector<8 x !s32i>
// LLVM-LABEL: @test_i32_iu8
// LLVM: call <8 x i32> @llvm.amdgcn.wmma.i32.16x16x16.iu8.v8i32.v4i32(i1 true, <4 x i32> %{{.*}}, i1 true, <4 x i32> %{{.*}}, <8 x i32> %{{.*}}, i1 false)
// OGCG-LABEL: @test_i32_iu8
// OGCG: call <8 x i32> @llvm.amdgcn.wmma.i32.16x16x16.iu8.v8i32.v4i32(i1 true, <4 x i32> %{{.*}}, i1 true, <4 x i32> %{{.*}}, <8 x i32> %{{.*}}, i1 false)

// Signed 4-bit integer inputs (packed) with an i32 accumulator.
int8v test_i32_iu4(int2v a, int2v b, int8v c) {
  return __builtin_amdgcn_wmma_i32_16x16x16_iu4_w32(1, a, 1, b, c, 0);
}
// CIR-LABEL: cir.func {{.*}}@test_i32_iu4
// CIR: cir.call_llvm_intrinsic "amdgcn.wmma.i32.16x16x16.iu4" {{.*}} -> !cir.vector<8 x !s32i>
// LLVM-LABEL: @test_i32_iu4
// LLVM: call <8 x i32> @llvm.amdgcn.wmma.i32.16x16x16.iu4.v8i32.v2i32(i1 true, <2 x i32> %{{.*}}, i1 true, <2 x i32> %{{.*}}, <8 x i32> %{{.*}}, i1 false)
// OGCG-LABEL: @test_i32_iu4
// OGCG: call <8 x i32> @llvm.amdgcn.wmma.i32.16x16x16.iu4.v8i32.v2i32(i1 true, <2 x i32> %{{.*}}, i1 true, <2 x i32> %{{.*}}, <8 x i32> %{{.*}}, i1 false)

// Tied f16 variant maps to the distinct .tied intrinsic.
half16 test_f16_f16_tied(half16 a, half16 b, half16 c) {
  return __builtin_amdgcn_wmma_f16_16x16x16_f16_tied_w32(a, b, c, 0);
}
// CIR-LABEL: cir.func {{.*}}@test_f16_f16_tied
// CIR: cir.call_llvm_intrinsic "amdgcn.wmma.f16.16x16x16.f16.tied" {{.*}} -> !cir.vector<16 x !cir.f16>
// LLVM-LABEL: @test_f16_f16_tied
// LLVM: call <16 x half> @llvm.amdgcn.wmma.f16.16x16x16.f16.tied.v16f16.v16f16(<16 x half> %{{.*}}, <16 x half> %{{.*}}, <16 x half> %{{.*}}, i1 false)
// OGCG-LABEL: @test_f16_f16_tied
// OGCG: call <16 x half> @llvm.amdgcn.wmma.f16.16x16x16.f16.tied.v16f16.v16f16(<16 x half> %{{.*}}, <16 x half> %{{.*}}, <16 x half> %{{.*}}, i1 false)

// Tied bf16 variant maps to the distinct .tied intrinsic.
short16 test_bf16_bf16_tied(short16 a, short16 b, short16 c) {
  return __builtin_amdgcn_wmma_bf16_16x16x16_bf16_tied_w32(a, b, c, 0);
}
// CIR-LABEL: cir.func {{.*}}@test_bf16_bf16_tied
// CIR: cir.call_llvm_intrinsic "amdgcn.wmma.bf16.16x16x16.bf16.tied" {{.*}} -> !cir.vector<16 x !s16i>
// LLVM-LABEL: @test_bf16_bf16_tied
// LLVM: call <16 x i16> @llvm.amdgcn.wmma.bf16.16x16x16.bf16.tied.v16i16.v16i16(<16 x i16> %{{.*}}, <16 x i16> %{{.*}}, <16 x i16> %{{.*}}, i1 false)
// OGCG-LABEL: @test_bf16_bf16_tied
// OGCG: call <16 x i16> @llvm.amdgcn.wmma.bf16.16x16x16.bf16.tied.v16i16.v16i16(<16 x i16> %{{.*}}, <16 x i16> %{{.*}}, <16 x i16> %{{.*}}, i1 false)
