// RUN: %clang_cc1 -triple arm64-none-linux-gnu -target-feature +fullfp16 \
// RUN:    -fclangir -disable-O0-optnone \
// RUN:  -flax-vector-conversions=none -emit-cir -o %t.cir %s
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s

// RUN: %clang_cc1 -triple arm64-none-linux-gnu -target-feature +fullfp16 \
// RUN:    -fclangir -disable-O0-optnone \
// RUN:  -flax-vector-conversions=none -emit-llvm -fno-clangir-call-conv-lowering -o - %s \
// RUN: | opt -S -passes=mem2reg,simplifycfg -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// REQUIRES: aarch64-registered-target || arm-registered-target

// This test mimics clang/test/CodeGen/AArch64/v8.2a-fp16-intrinsics.c, which eventually
// CIR shall be able to support fully. Since this is going to take some time to converge,
// the unsupported/NYI code is commented out, so that we can incrementally improve this.
// The NYI filecheck used contains the LLVM output from OG codegen that should guide the
// correct result when implementing this into the CIR pipeline.

#include <arm_fp16.h>

// CIR-LABEL: vabsh_f16
// CIR: {{%.*}}  = cir.fabs {{%.*}} : !cir.f16
//
// LLVM-LABEL: test_vabsh_f16
// LLVM-SAME: (half{{( noundef)?}} [[a:%.]])
// LLVM:  [[ABS:%.*]] =  call half @llvm.fabs.f16(half [[a]])
// LLVM:  ret half [[ABS]]
float16_t test_vabsh_f16(float16_t a) {
  return vabsh_f16(a);
}

// CIR-LABEL: @test_vceqzh_f16(
// CIR: cir.cmp(eq, {{%.*}}, {{%.*}}) : !cir.f16, !cir.bool
// CIR: cir.select if {{%.*}} then {{%.*}} else {{%.*}} : (!cir.bool, !s16i, !s16i) -> !s16i
//
// LLVM-LABEL: test_vceqzh_f16
// LLVM-SAME: (half{{( noundef)?}} [[A:%.]])
// LLVM: [[CMP:%.*]] = fcmp oeq half [[A]], 0xH0000
// LLVM: [[SEL:%.*]] = select i1 [[CMP]], i16 -1, i16 0
// LLVM: ret i16 [[SEL]]
uint16_t test_vceqzh_f16(float16_t a) {
  return vceqzh_f16(a);
}

// CIR-LABEL: @test_vcgezh_f16(
// CIR: cir.cmp(ge, {{%.*}}, {{%.*}}) : !cir.f16, !cir.bool
// CIR: cir.select if {{%.*}} then {{%.*}} else {{%.*}} : (!cir.bool, !s16i, !s16i) -> !s16i
//
// LLVM-LABEL: test_vcgezh_f16
// LLVM-SAME: (half{{( noundef)?}} [[A:%.]])
// LLVM: [[CMP:%.*]] = fcmp oge half [[A]], 0xH0000
// LLVM: [[SEL:%.*]] = select i1 [[CMP]], i16 -1, i16 0
// LLVM: ret i16 [[SEL]]
uint16_t test_vcgezh_f16(float16_t a) {
  return vcgezh_f16(a);
}

// CIR-LABEL: @test_vcgtzh_f16(
// CIR: cir.cmp(gt, {{%.*}}, {{%.*}}) : !cir.f16, !cir.bool
// CIR: cir.select if {{%.*}} then {{%.*}} else {{%.*}} : (!cir.bool, !s16i, !s16i) -> !s16i
//
// LLVM-LABEL: test_vcgtzh_f16
// LLVM-SAME: (half{{( noundef)?}} [[A:%.]])
// LLVM: [[CMP:%.*]] = fcmp ogt half [[A]], 0xH0000
// LLVM: [[SEL:%.*]] = select i1 [[CMP]], i16 -1, i16 0
// LLVM: ret i16 [[SEL]]
uint16_t test_vcgtzh_f16(float16_t a) {
  return vcgtzh_f16(a);
}

// CIR-LABEL: @test_vclezh_f16(
// CIR: cir.cmp(le, {{%.*}}, {{%.*}}) : !cir.f16, !cir.bool
// CIR: cir.select if {{%.*}} then {{%.*}} else {{%.*}} : (!cir.bool, !s16i, !s16i) -> !s16i
//
// LLVM-LABEL: test_vclezh_f16
// LLVM-SAME: (half{{( noundef)?}} [[A:%.]])
// LLVM: [[CMP:%.*]] = fcmp ole half [[A]], 0xH0000
// LLVM: [[SEL:%.*]] = select i1 [[CMP]], i16 -1, i16 0
// LLVM: ret i16 [[SEL]]
uint16_t test_vclezh_f16(float16_t a) {
  return vclezh_f16(a);
}

// CIR-LABEL: @test_vcltzh_f16(
// CIR: cir.cmp(lt, {{%.*}}, {{%.*}}) : !cir.f16, !cir.bool
// CIR: cir.select if {{%.*}} then {{%.*}} else {{%.*}} : (!cir.bool, !s16i, !s16i) -> !s16i
//
// LLVM-LABEL: test_vcltzh_f16
// LLVM-SAME: (half{{( noundef)?}} [[A:%.]])
// LLVM: [[CMP:%.*]] = fcmp olt half [[A]], 0xH0000
// LLVM: [[SEL:%.*]] = select i1 [[CMP]], i16 -1, i16 0
// LLVM: ret i16 [[SEL]]
uint16_t test_vcltzh_f16(float16_t a) {
  return vcltzh_f16(a);
}

// LLVM-LABEL: test_vcvth_f16_s16
// LLVM: [[VCVT:%.*]] = sitofp i16 {{%.*}} to half
// LLVM: ret half [[VCVT]]
float16_t test_vcvth_f16_s16(int16_t a) {
  return vcvth_f16_s16(a);
}

// LLVM-LABEL: test_vcvth_f16_s32
// LLVM: [[VCVT:%.*]] = sitofp i32 {{%.*}} to half
// LLVM: ret half [[VCVT]]
float16_t test_vcvth_f16_s32(int32_t a) {
  return vcvth_f16_s32(a);
}

// LLVM-LABEL: test_vcvth_f16_s64
// LLVM: [[VCVT:%.*]] = sitofp i64 {{%.*}} to half
// LLVM: ret half [[VCVT]]
float16_t test_vcvth_f16_s64(int64_t a) {
  return vcvth_f16_s64(a);
}

// LLVM-LABEL: test_vcvth_f16_u16
// LLVM: [[VCVT:%.*]] = uitofp i16 {{%.*}} to half
// LLVM: ret half [[VCVT]]
float16_t test_vcvth_f16_u16(uint16_t a) {
  return vcvth_f16_u16(a);
}

// LLVM-LABEL: test_vcvth_f16_u32
// LLVM: [[VCVT:%.*]] = uitofp i32 {{%.*}} to half
// LLVM: ret half [[VCVT]]
float16_t test_vcvth_f16_u32(uint32_t a) {
  return vcvth_f16_u32(a);
}

// LLVM-LABEL: test_vcvth_f16_u64
// LLVM: [[VCVT:%.*]] = uitofp i64 {{%.*}} to half
// LLVM: ret half [[VCVT]]
float16_t test_vcvth_f16_u64(uint64_t a) {
  return vcvth_f16_u64(a);
}

// LLVM-LABEL: test_vcvth_s16_f16
// LLVM: [[VCVT:%.*]] = call i16 @llvm.aarch64.neon.fcvtzs.i16.f16(half {{%.*}})
// LLVM: ret i16 [[VCVT]]
int16_t test_vcvth_s16_f16(float16_t a) {
  return vcvth_s16_f16(a);
}

// LLVM-LABEL: test_vcvth_s32_f16
// LLVM: [[VCVT:%.*]] = call i32 @llvm.aarch64.neon.fcvtzs.i32.f16(half {{%.*}})
// LLVM: ret i32 [[VCVT]]
int32_t test_vcvth_s32_f16(float16_t a) {
  return vcvth_s32_f16(a);
}

// LLVM-LABEL: test_vcvth_s64_f16
// LLVM: [[VCVT:%.*]] = call i64 @llvm.aarch64.neon.fcvtzs.i64.f16(half {{%.*}})
// LLVM: ret i64 [[VCVT]]
int64_t test_vcvth_s64_f16(float16_t a) {
  return vcvth_s64_f16(a);
}

// LLVM-LABEL: test_vcvth_u16_f16
// LLVM: [[VCVT:%.*]] = call i16 @llvm.aarch64.neon.fcvtzu.i16.f16(half {{%.*}})
// LLVM: ret i16 [[VCVT]]
uint16_t test_vcvth_u16_f16(float16_t a) {
  return vcvth_u16_f16(a);
}

// LLVM-LABEL: test_vcvth_u32_f16
// LLVM: [[VCVT:%.*]] = call i32 @llvm.aarch64.neon.fcvtzu.i32.f16(half {{%.*}})
// LLVM: ret i32 [[VCVT]]
uint32_t test_vcvth_u32_f16(float16_t a) {
  return vcvth_u32_f16(a);
}

// LLVM-LABEL: test_vcvth_u64_f16
// LLVM: [[VCVT:%.*]] = call i64 @llvm.aarch64.neon.fcvtzu.i64.f16(half {{%.*}})
// LLVM: ret i64 [[VCVT]]
uint64_t test_vcvth_u64_f16(float16_t a) {
  return vcvth_u64_f16(a);
}

// LLVM-LABEL: test_vcvtah_s16_f16
// LLVM: [[FCVT:%.*]] = call i16 @llvm.aarch64.neon.fcvtas.i16.f16(half {{%.*}})
// LLVM: ret i16 [[FCVT]]
int16_t test_vcvtah_s16_f16(float16_t a) {
  return vcvtah_s16_f16(a);
}

// LLVM-LABEL: test_vcvtah_s32_f16
// LLVM: [[VCVT:%.*]] = call i32 @llvm.aarch64.neon.fcvtas.i32.f16(half {{%.*}})
// LLVM: ret i32 [[VCVT]]
int32_t test_vcvtah_s32_f16(float16_t a) {
  return vcvtah_s32_f16(a);
}

// LLVM-LABEL: test_vcvtah_s64_f16
// LLVM: [[VCVT:%.*]] = call i64 @llvm.aarch64.neon.fcvtas.i64.f16(half {{%.*}})
// LLVM: ret i64 [[VCVT]]
int64_t test_vcvtah_s64_f16(float16_t a) {
  return vcvtah_s64_f16(a);
}

// LLVM-LABEL: test_vcvtah_u16_f16
// LLVM: [[FCVT:%.*]] = call i16 @llvm.aarch64.neon.fcvtau.i16.f16(half {{%.*}})
// LLVM: ret i16 [[FCVT]]
uint16_t test_vcvtah_u16_f16(float16_t a) {
  return vcvtah_u16_f16(a);
}

// LLVM-LABEL: test_vcvtah_u32_f16
// LLVM: [[VCVT:%.*]] = call i32 @llvm.aarch64.neon.fcvtau.i32.f16(half {{%.*}})
// LLVM: ret i32 [[VCVT]]
uint32_t test_vcvtah_u32_f16(float16_t a) {
  return vcvtah_u32_f16(a);
}

// LLVM-LABEL: test_vcvtah_u64_f16
// LLVM: [[VCVT:%.*]] = call i64 @llvm.aarch64.neon.fcvtau.i64.f16(half {{%.*}})
// LLVM: ret i64 [[VCVT]]
uint64_t test_vcvtah_u64_f16(float16_t a) {
  return vcvtah_u64_f16(a);
}

// LLVM-LABEL: test_vcvtmh_s16_f16
// LLVM: [[FCVT:%.*]] = call i16 @llvm.aarch64.neon.fcvtms.i16.f16(half {{%.*}})
// LLVM: ret i16 [[FCVT]]
int16_t test_vcvtmh_s16_f16(float16_t a) {
  return vcvtmh_s16_f16(a);
}

// LLVM-LABEL: test_vcvtmh_s32_f16
// LLVM: [[VCVT:%.*]] = call i32 @llvm.aarch64.neon.fcvtms.i32.f16(half {{%.*}})
// LLVM: ret i32 [[VCVT]]
int32_t test_vcvtmh_s32_f16(float16_t a) {
  return vcvtmh_s32_f16(a);
}

// LLVM-LABEL: test_vcvtmh_s64_f16
// LLVM: [[VCVT:%.*]] = call i64 @llvm.aarch64.neon.fcvtms.i64.f16(half {{%.*}})
// LLVM: ret i64 [[VCVT]]
int64_t test_vcvtmh_s64_f16(float16_t a) {
  return vcvtmh_s64_f16(a);
}

// LLVM-LABEL: test_vcvtmh_u16_f16
// LLVM: [[FCVT:%.*]] = call i16 @llvm.aarch64.neon.fcvtmu.i16.f16(half {{%.*}})
// LLVM: ret i16 [[FCVT]]
uint16_t test_vcvtmh_u16_f16(float16_t a) {
  return vcvtmh_u16_f16(a);
}

// LLVM-LABEL: test_vcvtmh_u32_f16
// LLVM: [[VCVT:%.*]] = call i32 @llvm.aarch64.neon.fcvtmu.i32.f16(half {{%.*}})
// LLVM: ret i32 [[VCVT]]
uint32_t test_vcvtmh_u32_f16(float16_t a) {
  return vcvtmh_u32_f16(a);
}

// LLVM-LABEL: test_vcvtmh_u64_f16
// LLVM: [[VCVT:%.*]] = call i64 @llvm.aarch64.neon.fcvtmu.i64.f16(half {{%.*}})
// LLVM: ret i64 [[VCVT]]
uint64_t test_vcvtmh_u64_f16(float16_t a) {
  return vcvtmh_u64_f16(a);
}

// LLVM-LABEL: test_vcvtnh_s16_f16
// LLVM: [[FCVT:%.*]] = call i16 @llvm.aarch64.neon.fcvtns.i16.f16(half {{%.*}})
// LLVM: ret i16 [[FCVT]]
int16_t test_vcvtnh_s16_f16(float16_t a) {
  return vcvtnh_s16_f16(a);
}

// LLVM-LABEL: test_vcvtnh_s32_f16
// LLVM: [[VCVT:%.*]] = call i32 @llvm.aarch64.neon.fcvtns.i32.f16(half {{%.*}})
// LLVM: ret i32 [[VCVT]]
int32_t test_vcvtnh_s32_f16(float16_t a) {
  return vcvtnh_s32_f16(a);
}

// LLVM-LABEL: test_vcvtnh_s64_f16
// LLVM: [[VCVT:%.*]] = call i64 @llvm.aarch64.neon.fcvtns.i64.f16(half {{%.*}})
// LLVM: ret i64 [[VCVT]]
int64_t test_vcvtnh_s64_f16(float16_t a) {
  return vcvtnh_s64_f16(a);
}

// LLVM-LABEL: test_vcvtnh_u16_f16
// LLVM: [[FCVT:%.*]] = call i16 @llvm.aarch64.neon.fcvtnu.i16.f16(half {{%.*}})
// LLVM: ret i16 [[FCVT]]
uint16_t test_vcvtnh_u16_f16(float16_t a) {
  return vcvtnh_u16_f16(a);
}

// LLVM-LABEL: test_vcvtnh_u32_f16
// LLVM: [[VCVT:%.*]] = call i32 @llvm.aarch64.neon.fcvtnu.i32.f16(half {{%.*}})
// LLVM: ret i32 [[VCVT]]
uint32_t test_vcvtnh_u32_f16(float16_t a) {
  return vcvtnh_u32_f16(a);
}

// LLVM-LABEL: test_vcvtnh_u64_f16
// LLVM: [[VCVT:%.*]] = call i64 @llvm.aarch64.neon.fcvtnu.i64.f16(half {{%.*}})
// LLVM: ret i64 [[VCVT]]
uint64_t test_vcvtnh_u64_f16(float16_t a) {
  return vcvtnh_u64_f16(a);
}

// LLVM-LABEL: test_vcvtph_s16_f16
// LLVM: [[FCVT:%.*]] = call i16 @llvm.aarch64.neon.fcvtps.i16.f16(half {{%.*}})
// LLVM: ret i16 [[FCVT]]
int16_t test_vcvtph_s16_f16(float16_t a) {
  return vcvtph_s16_f16(a);
}

// LLVM-LABEL: test_vcvtph_s32_f16
// LLVM: [[VCVT:%.*]] = call i32 @llvm.aarch64.neon.fcvtps.i32.f16(half {{%.*}})
// LLVM: ret i32 [[VCVT]]
int32_t test_vcvtph_s32_f16(float16_t a) {
  return vcvtph_s32_f16(a);
}

// LLVM-LABEL: test_vcvtph_s64_f16
// LLVM: [[VCVT:%.*]] = call i64 @llvm.aarch64.neon.fcvtps.i64.f16(half {{%.*}})
// LLVM: ret i64 [[VCVT]]
int64_t test_vcvtph_s64_f16(float16_t a) {
  return vcvtph_s64_f16(a);
}

// LLVM-LABEL: test_vcvtph_u16_f16
// LLVM: [[FCVT:%.*]] = call i16 @llvm.aarch64.neon.fcvtpu.i16.f16(half {{%.*}})
// LLVM: ret i16 [[FCVT]]
uint16_t test_vcvtph_u16_f16(float16_t a) {
  return vcvtph_u16_f16(a);
}

// LLVM-LABEL: test_vcvtph_u32_f16
// LLVM: [[VCVT:%.*]] = call i32 @llvm.aarch64.neon.fcvtpu.i32.f16(half {{%.*}})
// LLVM: ret i32 [[VCVT]]
uint32_t test_vcvtph_u32_f16(float16_t a) {
  return vcvtph_u32_f16(a);
}

// LLVM-LABEL: test_vcvtph_u64_f16
// LLVM: [[VCVT:%.*]] = call i64 @llvm.aarch64.neon.fcvtpu.i64.f16(half {{%.*}})
// LLVM: ret i64 [[VCVT]]
uint64_t test_vcvtph_u64_f16(float16_t a) {
  return vcvtph_u64_f16(a);
}

// LLVM-LABEL: test_vnegh_f16
// LLVM: [[NEG:%.*]] = fneg half {{%.*}}
// LLVM: ret half [[NEG]]
float16_t test_vnegh_f16(float16_t a) {
  return vnegh_f16(a);
}

// LLVM-LABEL: test_vrecpeh_f16
// LLVM: [[VREC:%.*]] = call half @llvm.aarch64.neon.frecpe.f16(half {{%.*}})
// LLVM: ret half [[VREC]]
float16_t test_vrecpeh_f16(float16_t a) {
  return vrecpeh_f16(a);
}

// LLVM-LABEL: test_vrecpxh_f16
// LLVM: [[VREC:%.*]] = call half @llvm.aarch64.neon.frecpx.f16(half {{%.*}})
// LLVM: ret half [[VREC]]
float16_t test_vrecpxh_f16(float16_t a) {
  return vrecpxh_f16(a);
}

// LLVM-LABEL: test_vrndh_f16
// LLVM: [[RND:%.*]] = call half @llvm.trunc.f16(half {{%.*}})
// LLVM: ret half [[RND]]
float16_t test_vrndh_f16(float16_t a) {
  return vrndh_f16(a);
}

// LLVM-LABEL: test_vrndah_f16
// LLVM: [[RND:%.*]] = call half @llvm.round.f16(half {{%.*}})
// LLVM: ret half [[RND]]
float16_t test_vrndah_f16(float16_t a) {
  return vrndah_f16(a);
}

// LLVM-LABEL: test_vrndih_f16
// LLVM: [[RND:%.*]] = call half @llvm.nearbyint.f16(half {{%.*}})
// LLVM: ret half [[RND]]
float16_t test_vrndih_f16(float16_t a) {
  return vrndih_f16(a);
}

// LLVM-LABEL: test_vrndmh_f16
// LLVM: [[RND:%.*]] = call half @llvm.floor.f16(half {{%.*}})
// LLVM: ret half [[RND]]
float16_t test_vrndmh_f16(float16_t a) {
  return vrndmh_f16(a);
}

// LLVM-LABEL: test_vrndnh_f16
// LLVM: [[RND:%.*]] = call half @llvm.roundeven.f16(half {{%.*}})
// LLVM: ret half [[RND]]
float16_t test_vrndnh_f16(float16_t a) {
  return vrndnh_f16(a);
}

// LLVM-LABEL: test_vrndph_f16
// LLVM: [[RND:%.*]] = call half @llvm.ceil.f16(half {{%.*}})
// LLVM: ret half [[RND]]
float16_t test_vrndph_f16(float16_t a) {
  return vrndph_f16(a);
}

// LLVM-LABEL: test_vrndxh_f16
// LLVM: [[RND:%.*]] = call half @llvm.rint.f16(half {{%.*}})
// LLVM: ret half [[RND]]
float16_t test_vrndxh_f16(float16_t a) {
  return vrndxh_f16(a);
}

// LLVM-LABEL: test_vrsqrteh_f16
// LLVM: [[RND:%.*]] = call half @llvm.aarch64.neon.frsqrte.f16(half {{%.*}})
// LLVM: ret half [[RND]]
float16_t test_vrsqrteh_f16(float16_t a) {
  return vrsqrteh_f16(a);
}

// LLVM-LABEL: test_vsqrth_f16
// LLVM: [[SQR:%.*]] = call half @llvm.sqrt.f16(half {{%.*}})
// LLVM: ret half [[SQR]]
float16_t test_vsqrth_f16(float16_t a) {
  return vsqrth_f16(a);
}

// CIR-LABEL: vaddh_f16
// CIR: {{%.*}} = cir.binop(add, {{%.*}}, {{%.*}}) : !cir.f16
//
// LLVM-LABEL: test_vaddh_f16 
// LLVM-SAME: (half{{( noundef)?}} [[a:%.]], half{{( noundef)?}} [[b:%.]])
// LLVM:  [[ADD:%.*]] = fadd half [[a]], [[b]]
// LLVM:  ret half [[ADD]]
float16_t test_vaddh_f16(float16_t a, float16_t b) {
  return vaddh_f16(a, b);
}

// LLVM-LABEL: test_vabdh_f16
// LLVM: [[ABD:%.*]] = call half @llvm.aarch64.sisd.fabd.f16(half {{%.*}}, half {{%.*}})
// LLVM: ret half [[ABD]]
float16_t test_vabdh_f16(float16_t a, float16_t b) {
  return vabdh_f16(a, b);
}

// LLVM-LABEL: test_vcageh_f16
// LLVM: [[FACG:%.*]] = call i32 @llvm.aarch64.neon.facge.i32.f16(half {{%.*}}, half {{%.*}})
// LLVM: [[RET:%.*]] = trunc i32 [[FACG]] to i16
// LLVM: ret i16 [[RET]]
uint16_t test_vcageh_f16(float16_t a, float16_t b) {
  return vcageh_f16(a, b);
}

// LLVM-LABEL: test_vcagth_f16
// LLVM: [[FACG:%.*]] = call i32 @llvm.aarch64.neon.facgt.i32.f16(half {{%.*}}, half {{%.*}})
// LLVM: [[RET:%.*]] = trunc i32 [[FACG]] to i16
// LLVM: ret i16 [[RET]]
uint16_t test_vcagth_f16(float16_t a, float16_t b) {
  return vcagth_f16(a, b);
}

// LLVM-LABEL: test_vcaleh_f16
// LLVM: [[FACG:%.*]] = call i32 @llvm.aarch64.neon.facge.i32.f16(half {{%.*}}, half {{%.*}})
// LLVM: [[RET:%.*]] = trunc i32 [[FACG]] to i16
// LLVM: ret i16 [[RET]]
uint16_t test_vcaleh_f16(float16_t a, float16_t b) {
  return vcaleh_f16(a, b);
}

// LLVM-LABEL: test_vcalth_f16
// LLVM: [[FACG:%.*]] = call i32 @llvm.aarch64.neon.facgt.i32.f16(half {{%.*}}, half {{%.*}})
// LLVM: [[RET:%.*]] = trunc i32 [[FACG]] to i16
// LLVM: ret i16 [[RET]]
uint16_t test_vcalth_f16(float16_t a, float16_t b) {
  return vcalth_f16(a, b);
}

// LLVM-LABEL: test_vceqh_f16
// LLVM: [[CMP:%.*]] = fcmp oeq half {{%.*}}, {{%.*}}
// LLVM: [[SEL:%.*]] = select i1 [[CMP]], i16 -1, i16 0
// LLVM: ret i16 [[SEL]]
uint16_t test_vceqh_f16(float16_t a, float16_t b) {
  return vceqh_f16(a, b);
}

// LLVM-LABEL: test_vcgeh_f16
// LLVM: [[CMP:%.*]] = fcmp oge half {{%.*}}, {{%.*}}
// LLVM: [[SEL:%.*]] = select i1 [[CMP]], i16 -1, i16 0
// LLVM: ret i16 [[SEL]]
uint16_t test_vcgeh_f16(float16_t a, float16_t b) {
 return vcgeh_f16(a, b);
}

// LLVM-LABEL: test_vcgth_f16
// LLVM: [[CMP:%.*]] = fcmp ogt half {{%.*}}, {{%.*}}
// LLVM: [[SEL:%.*]] = select i1 [[CMP]], i16 -1, i16 0
// LLVM: ret i16 [[SEL]]
uint16_t test_vcgth_f16(float16_t a, float16_t b) {
  return vcgth_f16(a, b);
}

// LLVM-LABEL: test_vcleh_f16
// LLVM: [[CMP:%.*]] = fcmp ole half {{%.*}}, {{%.*}}
// LLVM: [[SEL:%.*]] = select i1 [[CMP]], i16 -1, i16 0
// LLVM: ret i16 [[SEL]]
uint16_t test_vcleh_f16(float16_t a, float16_t b) {
  return vcleh_f16(a, b);
}

// LLVM-LABEL: test_vclth_f16
// LLVM: [[CMP:%.*]] = fcmp olt half {{%.*}}, {{%.*}}
// LLVM: [[SEL:%.*]] = select i1 [[CMP]], i16 -1, i16 0
// LLVM: ret i16 [[SEL]]
uint16_t test_vclth_f16(float16_t a, float16_t b) {
  return vclth_f16(a, b);
}

// LLVM-LABEL: test_vcvth_n_f16_s16
// LLVM: [[SEXT:%.*]] = sext i16 {{%.*}} to i32
// LLVM: [[CVT:%.*]] = call half @llvm.aarch64.neon.vcvtfxs2fp.f16.i32(i32 [[SEXT]], i32 1)
// LLVM: ret half [[CVT]]
float16_t test_vcvth_n_f16_s16(int16_t a) {
  return vcvth_n_f16_s16(a, 1);
}

// LLVM-LABEL: test_vcvth_n_f16_s32
// LLVM: [[CVT:%.*]] = call half @llvm.aarch64.neon.vcvtfxs2fp.f16.i32(i32 {{%.*}}, i32 1)
// LLVM: ret half [[CVT]]
float16_t test_vcvth_n_f16_s32(int32_t a) {
  return vcvth_n_f16_s32(a, 1);
}

// LLVM-LABEL: test_vcvth_n_f16_s64
// LLVM: [[CVT:%.*]] = call half @llvm.aarch64.neon.vcvtfxs2fp.f16.i64(i64 {{%.*}}, i32 1)
// LLVM: ret half [[CVT]]
float16_t test_vcvth_n_f16_s64(int64_t a) {
  return vcvth_n_f16_s64(a, 1);
}

// LLVM-LABEL: test_vcvth_n_s16_f16
// LLVM: [[CVT:%.*]] = call i32 @llvm.aarch64.neon.vcvtfp2fxs.i32.f16(half {{%.*}}, i32 1)
// LLVM: [[RET:%.*]] = trunc i32 [[CVT]] to i16
// LLVM: ret i16 [[RET]]
int16_t test_vcvth_n_s16_f16(float16_t a) {
  return vcvth_n_s16_f16(a, 1);
}

// LLVM-LABEL: test_vcvth_n_s32_f16
// LLVM: [[CVT:%.*]] = call i32 @llvm.aarch64.neon.vcvtfp2fxs.i32.f16(half {{%.*}}, i32 1)
// LLVM: ret i32 [[CVT]]
int32_t test_vcvth_n_s32_f16(float16_t a) {
  return vcvth_n_s32_f16(a, 1);
}

// LLVM-LABEL: test_vcvth_n_s64_f16
// LLVM: [[CVT:%.*]] = call i64 @llvm.aarch64.neon.vcvtfp2fxs.i64.f16(half {{%.*}}, i32 1)
// LLVM: ret i64 [[CVT]]
int64_t test_vcvth_n_s64_f16(float16_t a) {
  return vcvth_n_s64_f16(a, 1);
}

// LLVM-LABEL: test_vcvth_n_f16_u16
// LLVM: [[ZEXT:%.*]] = zext i16 {{%.*}} to i32
// LLVM: [[CVT:%.*]] = call half @llvm.aarch64.neon.vcvtfxu2fp.f16.i32(i32 [[ZEXT]], i32 1)
// LLVM: ret half [[CVT]]
float16_t test_vcvth_n_f16_u16(int16_t a) {
  return vcvth_n_f16_u16(a, 1);
}

// LLVM-LABEL: test_vcvth_n_f16_u32
// LLVM: [[CVT:%.*]] = call half @llvm.aarch64.neon.vcvtfxu2fp.f16.i32(i32 {{%.*}}, i32 1)
// LLVM: ret half [[CVT]]
float16_t test_vcvth_n_f16_u32(int32_t a) {
  return vcvth_n_f16_u32(a, 1);
}

// LLVM-LABEL: test_vcvth_n_f16_u64
// LLVM: [[CVT:%.*]] = call half @llvm.aarch64.neon.vcvtfxu2fp.f16.i64(i64 {{%.*}}, i32 1)
// LLVM: ret half [[CVT]]
float16_t test_vcvth_n_f16_u64(int64_t a) {
  return vcvth_n_f16_u64(a, 1);
}

// LLVM-LABEL: test_vcvth_n_u16_f16
// LLVM: [[CVT:%.*]] = call i32 @llvm.aarch64.neon.vcvtfp2fxu.i32.f16(half {{%.*}}, i32 1)
// LLVM: [[RET:%.*]] = trunc i32 [[CVT]] to i16
// LLVM: ret i16 [[RET]]
int16_t test_vcvth_n_u16_f16(float16_t a) {
  return vcvth_n_u16_f16(a, 1);
}

// LLVM-LABEL: test_vcvth_n_u32_f16
// LLVM: [[CVT:%.*]] = call i32 @llvm.aarch64.neon.vcvtfp2fxu.i32.f16(half {{%.*}}, i32 1)
// LLVM: ret i32 [[CVT]]
int32_t test_vcvth_n_u32_f16(float16_t a) {
  return vcvth_n_u32_f16(a, 1);
}

// LLVM-LABEL: test_vcvth_n_u64_f16
// LLVM: [[CVT:%.*]] = call i64 @llvm.aarch64.neon.vcvtfp2fxu.i64.f16(half {{%.*}}, i32 1)
// LLVM: ret i64 [[CVT]]
int64_t test_vcvth_n_u64_f16(float16_t a) {
  return vcvth_n_u64_f16(a, 1);
}

// CIR-LABEL: vdivh_f16
// CIR: {{%.*}} = cir.binop(div, {{%.*}}, {{%.*}}) : !cir.f16
//
// LLVM-LABEL: test_vdivh_f16
// LLVM-SAME: (half{{( noundef)?}} [[a:%.]], half{{( noundef)?}} [[b:%.]])
// LLVM:  [[DIV:%.*]] = fdiv half [[a]], [[b]]
// LLVM:  ret half [[DIV]]
float16_t test_vdivh_f16(float16_t a, float16_t b) {
  return vdivh_f16(a, b);
}

// LLVM-LABEL: test_vmaxh_f16
// LLVM: [[MAX:%.*]] = call half @llvm.aarch64.neon.fmax.f16(half {{%.*}}, half {{%.*}})
// LLVM: ret half [[MAX]]
float16_t test_vmaxh_f16(float16_t a, float16_t b) {
  return vmaxh_f16(a, b);
}

// LLVM-LABEL: test_vmaxnmh_f16
// LLVM: [[MAX:%.*]] = call half @llvm.aarch64.neon.fmaxnm.f16(half {{%.*}}, half {{%.*}})
// LLVM: ret half [[MAX]]
float16_t test_vmaxnmh_f16(float16_t a, float16_t b) {
  return vmaxnmh_f16(a, b);
}

// LLVM-LABEL: test_vminh_f16
// LLVM: [[MIN:%.*]] = call half @llvm.aarch64.neon.fmin.f16(half {{%.*}}, half {{%.*}})
// LLVM: ret half [[MIN]]
float16_t test_vminh_f16(float16_t a, float16_t b) {
  return vminh_f16(a, b);
}

// LLVM-LABEL: test_vminnmh_f16
// LLVM: [[MIN:%.*]] = call half @llvm.aarch64.neon.fminnm.f16(half {{%.*}}, half {{%.*}})
// LLVM: ret half [[MIN]]
float16_t test_vminnmh_f16(float16_t a, float16_t b) {
  return vminnmh_f16(a, b);
}

// CIR-LABEL: vmulh_f16
// CIR: {{%.*}} = cir.binop(mul, {{%.*}}, {{%.*}}) : !cir.f16
//
// LLVM-LABEL: test_vmulh_f16
// LLVM-SAME: (half{{( noundef)?}} [[a:%.]], half{{( noundef)?}} [[b:%.]])
// LLVM:  [[MUL:%.*]] = fmul half [[a]], [[b]]
// LLVM:  ret half [[MUL]]
float16_t test_vmulh_f16(float16_t a, float16_t b) {
  return vmulh_f16(a, b);
}

// LLVM-LABEL: test_vmulxh_f16
// LLVM: [[MUL:%.*]] = call half @llvm.aarch64.neon.fmulx.f16(half {{%.*}}, half {{%.*}})
// LLVM: ret half [[MUL]]
float16_t test_vmulxh_f16(float16_t a, float16_t b) {
  return vmulxh_f16(a, b);
}

// LLVM-LABEL: test_vrecpsh_f16
// LLVM: [[RECPS:%.*]] = call half @llvm.aarch64.neon.frecps.f16(half {{%.*}}, half {{%.*}})
// LLVM: ret half [[RECPS]]
float16_t test_vrecpsh_f16(float16_t a, float16_t b) {
  return vrecpsh_f16(a, b);
}

// LLVM-LABEL: test_vrsqrtsh_f16
// LLVM: [[RSQRTS:%.*]] = call half @llvm.aarch64.neon.frsqrts.f16(half {{%.*}}, half {{%.*}})
// LLVM: ret half [[RSQRTS]]
float16_t test_vrsqrtsh_f16(float16_t a, float16_t b) {
  return vrsqrtsh_f16(a, b);
}

// CIR-LABEL: vsubh_f16
// CIR: {{%.*}} = cir.binop(sub, {{%.*}}, {{%.*}}) : !cir.f16
//
// LLVM-LABEL: test_vsubh_f16
// LLVM-SAME: (half{{( noundef)?}} [[a:%.]], half{{( noundef)?}} [[b:%.]])
// LLVM:  [[SUB:%.*]] = fsub half [[a]], [[b]]
// LLVM:  ret half [[SUB]]
float16_t test_vsubh_f16(float16_t a, float16_t b) {
  return vsubh_f16(a, b);
}

// LLVM-LABEL: test_vfmah_f16
// LLVM: [[FMA:%.*]] = call half @llvm.fma.f16(half {{%.*}}, half {{%.*}}, half {{%.*}})
// LLVM: ret half [[FMA]]
float16_t test_vfmah_f16(float16_t a, float16_t b, float16_t c) {
  return vfmah_f16(a, b, c);
}

// LLVM-LABEL: test_vfmsh_f16
// LLVM: [[NEG:%.*]] = fneg half {{%.*}}
// LLVM: [[FMA:%.*]] = call half @llvm.fma.f16(half [[NEG]], half {{%.*}}, half {{%.*}})
// LLVM: ret half [[FMA]]
float16_t test_vfmsh_f16(float16_t a, float16_t b, float16_t c) {
  return vfmsh_f16(a, b, c);
}
