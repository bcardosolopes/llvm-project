// REQUIRES: aarch64-registered-target

// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -target-feature +v8.1a \
// RUN:   -emit-llvm -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -target-feature +v8.1a \
// RUN:   -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

#include <arm_neon.h>

// --- vqabs: Saturating absolute value (Vectorize1ArgType) ---

// CIR-LABEL: @test_vqabsb_s8
// CIR: insertelement <8 x i8>
// CIR: call <8 x i8> @llvm.aarch64.neon.sqabs.v8i8(<8 x i8>
// CIR: extractelement <8 x i8>
// OGCG-LABEL: @test_vqabsb_s8
// OGCG: call <8 x i8> @llvm.aarch64.neon.sqabs.v8i8(<8 x i8>
int8_t test_vqabsb_s8(int8_t a) { return vqabsb_s8(a); }

// CIR-LABEL: @test_vqabsh_s16
// CIR: insertelement <4 x i16>
// CIR: call <4 x i16> @llvm.aarch64.neon.sqabs.v4i16(<4 x i16>
// CIR: extractelement <4 x i16>
// OGCG-LABEL: @test_vqabsh_s16
// OGCG: call <4 x i16> @llvm.aarch64.neon.sqabs.v4i16(<4 x i16>
int16_t test_vqabsh_s16(int16_t a) { return vqabsh_s16(a); }

// --- vqneg: Saturating negate (Vectorize1ArgType) ---

// CIR-LABEL: @test_vqnegb_s8
// CIR: call <8 x i8> @llvm.aarch64.neon.sqneg.v8i8(<8 x i8>
// OGCG-LABEL: @test_vqnegb_s8
// OGCG: call <8 x i8> @llvm.aarch64.neon.sqneg.v8i8(<8 x i8>
int8_t test_vqnegb_s8(int8_t a) { return vqnegb_s8(a); }

// CIR-LABEL: @test_vqnegh_s16
// CIR: call <4 x i16> @llvm.aarch64.neon.sqneg.v4i16(<4 x i16>
// OGCG-LABEL: @test_vqnegh_s16
// OGCG: call <4 x i16> @llvm.aarch64.neon.sqneg.v4i16(<4 x i16>
int16_t test_vqnegh_s16(int16_t a) { return vqnegh_s16(a); }

// --- vqadd: Saturating add (i8 and i16) ---

// CIR-LABEL: @test_vqaddb_s8
// CIR: call <8 x i8> @llvm.aarch64.neon.sqadd.v8i8(<8 x i8>
// OGCG-LABEL: @test_vqaddb_s8
// OGCG: call <8 x i8> @llvm.aarch64.neon.sqadd.v8i8(<8 x i8>
int8_t test_vqaddb_s8(int8_t a, int8_t b) { return vqaddb_s8(a, b); }

// CIR-LABEL: @test_vqaddb_u8
// CIR: call <8 x i8> @llvm.aarch64.neon.uqadd.v8i8(<8 x i8>
// OGCG-LABEL: @test_vqaddb_u8
// OGCG: call <8 x i8> @llvm.aarch64.neon.uqadd.v8i8(<8 x i8>
uint8_t test_vqaddb_u8(uint8_t a, uint8_t b) { return vqaddb_u8(a, b); }

// CIR-LABEL: @test_vqaddh_s16
// CIR: call <4 x i16> @llvm.aarch64.neon.sqadd.v4i16(<4 x i16>
// OGCG-LABEL: @test_vqaddh_s16
// OGCG: call <4 x i16> @llvm.aarch64.neon.sqadd.v4i16(<4 x i16>
int16_t test_vqaddh_s16(int16_t a, int16_t b) { return vqaddh_s16(a, b); }

// CIR-LABEL: @test_vqaddh_u16
// CIR: call <4 x i16> @llvm.aarch64.neon.uqadd.v4i16(<4 x i16>
// OGCG-LABEL: @test_vqaddh_u16
// OGCG: call <4 x i16> @llvm.aarch64.neon.uqadd.v4i16(<4 x i16>
uint16_t test_vqaddh_u16(uint16_t a, uint16_t b) { return vqaddh_u16(a, b); }

// --- vqsub: Saturating subtract (i8 and i16) ---

// CIR-LABEL: @test_vqsubb_s8
// CIR: call <8 x i8> @llvm.aarch64.neon.sqsub.v8i8(<8 x i8>
// OGCG-LABEL: @test_vqsubb_s8
// OGCG: call <8 x i8> @llvm.aarch64.neon.sqsub.v8i8(<8 x i8>
int8_t test_vqsubb_s8(int8_t a, int8_t b) { return vqsubb_s8(a, b); }

// CIR-LABEL: @test_vqsubb_u8
// CIR: call <8 x i8> @llvm.aarch64.neon.uqsub.v8i8(<8 x i8>
// OGCG-LABEL: @test_vqsubb_u8
// OGCG: call <8 x i8> @llvm.aarch64.neon.uqsub.v8i8(<8 x i8>
uint8_t test_vqsubb_u8(uint8_t a, uint8_t b) { return vqsubb_u8(a, b); }

// CIR-LABEL: @test_vqsubh_s16
// CIR: call <4 x i16> @llvm.aarch64.neon.sqsub.v4i16(<4 x i16>
// OGCG-LABEL: @test_vqsubh_s16
// OGCG: call <4 x i16> @llvm.aarch64.neon.sqsub.v4i16(<4 x i16>
int16_t test_vqsubh_s16(int16_t a, int16_t b) { return vqsubh_s16(a, b); }

// CIR-LABEL: @test_vqsubh_u16
// CIR: call <4 x i16> @llvm.aarch64.neon.uqsub.v4i16(<4 x i16>
// OGCG-LABEL: @test_vqsubh_u16
// OGCG: call <4 x i16> @llvm.aarch64.neon.uqsub.v4i16(<4 x i16>
uint16_t test_vqsubh_u16(uint16_t a, uint16_t b) { return vqsubh_u16(a, b); }

// --- vqdmulh: Saturating doubling multiply high (i16) ---

// CIR-LABEL: @test_vqdmulhh_s16
// CIR: call <4 x i16> @llvm.aarch64.neon.sqdmulh.v4i16(<4 x i16>
// OGCG-LABEL: @test_vqdmulhh_s16
// OGCG: call <4 x i16> @llvm.aarch64.neon.sqdmulh.v4i16(<4 x i16>
int16_t test_vqdmulhh_s16(int16_t a, int16_t b) { return vqdmulhh_s16(a, b); }

// --- vqdmull: Saturating doubling multiply long (i16 -> i32) ---

// CIR-LABEL: @test_vqdmullh_s16
// CIR: call <4 x i32> @llvm.aarch64.neon.sqdmull.v4i32(<4 x i16>
// OGCG-LABEL: @test_vqdmullh_s16
// OGCG: call <4 x i32> @llvm.aarch64.neon.sqdmull.v4i32(<4 x i16>
int32_t test_vqdmullh_s16(int16_t a, int16_t b) { return vqdmullh_s16(a, b); }

// --- vqrdmlah/sh: Saturating rounding doubling multiply accumulate/subtract (i16) ---

// CIR-LABEL: @test_vqrdmlahh_s16
// CIR: call <4 x i16> @llvm.aarch64.neon.sqrdmlah.v4i16(<4 x i16>
// OGCG-LABEL: @test_vqrdmlahh_s16
// OGCG: call <4 x i16> @llvm.aarch64.neon.sqrdmlah.v4i16(<4 x i16>
int16_t test_vqrdmlahh_s16(int16_t a, int16_t b, int16_t c) { return vqrdmlahh_s16(a, b, c); }

// CIR-LABEL: @test_vqrdmlshh_s16
// CIR: call <4 x i16> @llvm.aarch64.neon.sqrdmlsh.v4i16(<4 x i16>
// OGCG-LABEL: @test_vqrdmlshh_s16
// OGCG: call <4 x i16> @llvm.aarch64.neon.sqrdmlsh.v4i16(<4 x i16>
int16_t test_vqrdmlshh_s16(int16_t a, int16_t b, int16_t c) { return vqrdmlshh_s16(a, b, c); }

// --- vqrdmulh: Saturating rounding doubling multiply high (i16) ---

// CIR-LABEL: @test_vqrdmulhh_s16
// CIR: call <4 x i16> @llvm.aarch64.neon.sqrdmulh.v4i16(<4 x i16>
// OGCG-LABEL: @test_vqrdmulhh_s16
// OGCG: call <4 x i16> @llvm.aarch64.neon.sqrdmulh.v4i16(<4 x i16>
int16_t test_vqrdmulhh_s16(int16_t a, int16_t b) { return vqrdmulhh_s16(a, b); }

// --- vqrshl: Saturating rounding shift left (i8 and i16) ---

// CIR-LABEL: @test_vqrshlb_s8
// CIR: call <8 x i8> @llvm.aarch64.neon.sqrshl.v8i8(<8 x i8>
// OGCG-LABEL: @test_vqrshlb_s8
// OGCG: call <8 x i8> @llvm.aarch64.neon.sqrshl.v8i8(<8 x i8>
int8_t test_vqrshlb_s8(int8_t a, int8_t b) { return vqrshlb_s8(a, b); }

// CIR-LABEL: @test_vqrshlb_u8
// CIR: call <8 x i8> @llvm.aarch64.neon.uqrshl.v8i8(<8 x i8>
// OGCG-LABEL: @test_vqrshlb_u8
// OGCG: call <8 x i8> @llvm.aarch64.neon.uqrshl.v8i8(<8 x i8>
uint8_t test_vqrshlb_u8(uint8_t a, uint8_t b) { return vqrshlb_u8(a, b); }

// CIR-LABEL: @test_vqrshlh_s16
// CIR: call <4 x i16> @llvm.aarch64.neon.sqrshl.v4i16(<4 x i16>
// OGCG-LABEL: @test_vqrshlh_s16
// OGCG: call <4 x i16> @llvm.aarch64.neon.sqrshl.v4i16(<4 x i16>
int16_t test_vqrshlh_s16(int16_t a, int16_t b) { return vqrshlh_s16(a, b); }

// CIR-LABEL: @test_vqrshlh_u16
// CIR: call <4 x i16> @llvm.aarch64.neon.uqrshl.v4i16(<4 x i16>
// OGCG-LABEL: @test_vqrshlh_u16
// OGCG: call <4 x i16> @llvm.aarch64.neon.uqrshl.v4i16(<4 x i16>
uint16_t test_vqrshlh_u16(uint16_t a, uint16_t b) { return vqrshlh_u16(a, b); }

// --- vqshl: Saturating shift left (register, i8 and i16) ---

// CIR-LABEL: @test_vqshlb_s8
// CIR: call <8 x i8> @llvm.aarch64.neon.sqshl.v8i8(<8 x i8>
// OGCG-LABEL: @test_vqshlb_s8
// OGCG: call <8 x i8> @llvm.aarch64.neon.sqshl.v8i8(<8 x i8>
int8_t test_vqshlb_s8(int8_t a, int8_t b) { return vqshlb_s8(a, b); }

// CIR-LABEL: @test_vqshlb_u8
// CIR: call <8 x i8> @llvm.aarch64.neon.uqshl.v8i8(<8 x i8>
// OGCG-LABEL: @test_vqshlb_u8
// OGCG: call <8 x i8> @llvm.aarch64.neon.uqshl.v8i8(<8 x i8>
uint8_t test_vqshlb_u8(uint8_t a, uint8_t b) { return vqshlb_u8(a, b); }

// CIR-LABEL: @test_vqshlh_s16
// CIR: call <4 x i16> @llvm.aarch64.neon.sqshl.v4i16(<4 x i16>
// OGCG-LABEL: @test_vqshlh_s16
// OGCG: call <4 x i16> @llvm.aarch64.neon.sqshl.v4i16(<4 x i16>
int16_t test_vqshlh_s16(int16_t a, int16_t b) { return vqshlh_s16(a, b); }

// CIR-LABEL: @test_vqshlh_u16
// CIR: call <4 x i16> @llvm.aarch64.neon.uqshl.v4i16(<4 x i16>
// OGCG-LABEL: @test_vqshlh_u16
// OGCG: call <4 x i16> @llvm.aarch64.neon.uqshl.v4i16(<4 x i16>
uint16_t test_vqshlh_u16(uint16_t a, uint16_t b) { return vqshlh_u16(a, b); }

// --- vqmovn: Saturating narrowing move ---

// CIR-LABEL: @test_vqmovnh_s16
// CIR: call <8 x i8> @llvm.aarch64.neon.sqxtn.v8i8(<8 x i16>
// OGCG-LABEL: @test_vqmovnh_s16
// OGCG: call <8 x i8> @llvm.aarch64.neon.sqxtn.v8i8(<8 x i16>
int8_t test_vqmovnh_s16(int16_t a) { return vqmovnh_s16(a); }

// CIR-LABEL: @test_vqmovnh_u16
// CIR: call <8 x i8> @llvm.aarch64.neon.uqxtn.v8i8(<8 x i16>
// OGCG-LABEL: @test_vqmovnh_u16
// OGCG: call <8 x i8> @llvm.aarch64.neon.uqxtn.v8i8(<8 x i16>
uint8_t test_vqmovnh_u16(uint16_t a) { return vqmovnh_u16(a); }

// CIR-LABEL: @test_vqmovns_u32
// CIR: call <4 x i16> @llvm.aarch64.neon.uqxtn.v4i16(<4 x i32>
// OGCG-LABEL: @test_vqmovns_u32
// OGCG: call <4 x i16> @llvm.aarch64.neon.uqxtn.v4i16(<4 x i32>
uint16_t test_vqmovns_u32(uint32_t a) { return vqmovns_u32(a); }

// --- vqmovun: Saturating unsigned narrowing from signed ---

// CIR-LABEL: @test_vqmovunh_s16
// CIR: call <8 x i8> @llvm.aarch64.neon.sqxtun.v8i8(<8 x i16>
// OGCG-LABEL: @test_vqmovunh_s16
// OGCG: call <8 x i8> @llvm.aarch64.neon.sqxtun.v8i8(<8 x i16>
uint8_t test_vqmovunh_s16(int16_t a) { return vqmovunh_s16(a); }

// CIR-LABEL: @test_vqmovuns_s32
// CIR: call <4 x i16> @llvm.aarch64.neon.sqxtun.v4i16(<4 x i32>
// OGCG-LABEL: @test_vqmovuns_s32
// OGCG: call <4 x i16> @llvm.aarch64.neon.sqxtun.v4i16(<4 x i32>
uint16_t test_vqmovuns_s32(int32_t a) { return vqmovuns_s32(a); }

// --- vsqadd/vuqadd: Unsigned/signed accumulate of signed/unsigned ---

// CIR-LABEL: @test_vsqaddb_u8
// CIR: call <8 x i8> @llvm.aarch64.neon.usqadd.v8i8(<8 x i8>
// OGCG-LABEL: @test_vsqaddb_u8
// OGCG: call <8 x i8> @llvm.aarch64.neon.usqadd.v8i8(<8 x i8>
uint8_t test_vsqaddb_u8(uint8_t a, uint8_t b) { return vsqaddb_u8(a, b); }

// CIR-LABEL: @test_vsqaddh_u16
// CIR: call <4 x i16> @llvm.aarch64.neon.usqadd.v4i16(<4 x i16>
// OGCG-LABEL: @test_vsqaddh_u16
// OGCG: call <4 x i16> @llvm.aarch64.neon.usqadd.v4i16(<4 x i16>
uint16_t test_vsqaddh_u16(uint16_t a, uint16_t b) { return vsqaddh_u16(a, b); }

// CIR-LABEL: @test_vuqaddb_s8
// CIR: call <8 x i8> @llvm.aarch64.neon.suqadd.v8i8(<8 x i8>
// OGCG-LABEL: @test_vuqaddb_s8
// OGCG: call <8 x i8> @llvm.aarch64.neon.suqadd.v8i8(<8 x i8>
int8_t test_vuqaddb_s8(int8_t a, int8_t b) { return vuqaddb_s8(a, b); }

// CIR-LABEL: @test_vuqaddh_s16
// CIR: call <4 x i16> @llvm.aarch64.neon.suqadd.v4i16(<4 x i16>
// OGCG-LABEL: @test_vuqaddh_s16
// OGCG: call <4 x i16> @llvm.aarch64.neon.suqadd.v4i16(<4 x i16>
int16_t test_vuqaddh_s16(int16_t a, int16_t b) { return vuqaddh_s16(a, b); }

// --- vqshl_n: Saturating shift left (immediate, i8 and i16) ---

// CIR-LABEL: @test_vqshlb_n_s8
// CIR: call <8 x i8> @llvm.aarch64.neon.sqshl.v8i8(<8 x i8> %{{.*}}, <8 x i8> <i8 3,
// OGCG-LABEL: @test_vqshlb_n_s8
// OGCG: call <8 x i8> @llvm.aarch64.neon.sqshl.v8i8(<8 x i8> %{{.*}}, <8 x i8> <i8 3,
int8_t test_vqshlb_n_s8(int8_t a) { return vqshlb_n_s8(a, 3); }

// CIR-LABEL: @test_vqshlb_n_u8
// CIR: call <8 x i8> @llvm.aarch64.neon.uqshl.v8i8(<8 x i8> %{{.*}}, <8 x i8> <i8 3,
// OGCG-LABEL: @test_vqshlb_n_u8
// OGCG: call <8 x i8> @llvm.aarch64.neon.uqshl.v8i8(<8 x i8> %{{.*}}, <8 x i8> <i8 3,
uint8_t test_vqshlb_n_u8(uint8_t a) { return vqshlb_n_u8(a, 3); }

// CIR-LABEL: @test_vqshlh_n_s16
// CIR: call <4 x i16> @llvm.aarch64.neon.sqshl.v4i16(<4 x i16> %{{.*}}, <4 x i16> <i16 3,
// OGCG-LABEL: @test_vqshlh_n_s16
// OGCG: call <4 x i16> @llvm.aarch64.neon.sqshl.v4i16(<4 x i16> %{{.*}}, <4 x i16> <i16 3,
int16_t test_vqshlh_n_s16(int16_t a) { return vqshlh_n_s16(a, 3); }

// CIR-LABEL: @test_vqshlh_n_u16
// CIR: call <4 x i16> @llvm.aarch64.neon.uqshl.v4i16(<4 x i16> %{{.*}}, <4 x i16> <i16 3,
// OGCG-LABEL: @test_vqshlh_n_u16
// OGCG: call <4 x i16> @llvm.aarch64.neon.uqshl.v4i16(<4 x i16> %{{.*}}, <4 x i16> <i16 3,
uint16_t test_vqshlh_n_u16(uint16_t a) { return vqshlh_n_u16(a, 3); }

// --- vqshlu_n: Saturating unsigned shift left from signed (i8 and i16) ---

// CIR-LABEL: @test_vqshlub_n_s8
// CIR: call <8 x i8> @llvm.aarch64.neon.sqshlu.v8i8(<8 x i8> %{{.*}}, <8 x i8> <i8 3,
// OGCG-LABEL: @test_vqshlub_n_s8
// OGCG: call <8 x i8> @llvm.aarch64.neon.sqshlu.v8i8(<8 x i8> %{{.*}}, <8 x i8> <i8 3,
uint8_t test_vqshlub_n_s8(int8_t a) { return vqshlub_n_s8(a, 3); }

// CIR-LABEL: @test_vqshluh_n_s16
// CIR: call <4 x i16> @llvm.aarch64.neon.sqshlu.v4i16(<4 x i16> %{{.*}}, <4 x i16> <i16 3,
// OGCG-LABEL: @test_vqshluh_n_s16
// OGCG: call <4 x i16> @llvm.aarch64.neon.sqshlu.v4i16(<4 x i16> %{{.*}}, <4 x i16> <i16 3,
uint16_t test_vqshluh_n_s16(int16_t a) { return vqshluh_n_s16(a, 3); }

// --- vqshrn_n: Saturating narrowing right shift ---

// CIR-LABEL: @test_vqshrnh_n_s16
// CIR: call <8 x i8> @llvm.aarch64.neon.sqshrn.v8i8(<8 x i16> %{{.*}}, i32 3)
// OGCG-LABEL: @test_vqshrnh_n_s16
// OGCG: call <8 x i8> @llvm.aarch64.neon.sqshrn.v8i8(<8 x i16> %{{.*}}, i32 3)
int8_t test_vqshrnh_n_s16(int16_t a) { return vqshrnh_n_s16(a, 3); }

// CIR-LABEL: @test_vqshrnh_n_u16
// CIR: call <8 x i8> @llvm.aarch64.neon.uqshrn.v8i8(<8 x i16> %{{.*}}, i32 3)
// OGCG-LABEL: @test_vqshrnh_n_u16
// OGCG: call <8 x i8> @llvm.aarch64.neon.uqshrn.v8i8(<8 x i16> %{{.*}}, i32 3)
uint8_t test_vqshrnh_n_u16(uint16_t a) { return vqshrnh_n_u16(a, 3); }

// CIR-LABEL: @test_vqshrns_n_s32
// CIR: call <4 x i16> @llvm.aarch64.neon.sqshrn.v4i16(<4 x i32> %{{.*}}, i32 3)
// OGCG-LABEL: @test_vqshrns_n_s32
// OGCG: call <4 x i16> @llvm.aarch64.neon.sqshrn.v4i16(<4 x i32> %{{.*}}, i32 3)
int16_t test_vqshrns_n_s32(int32_t a) { return vqshrns_n_s32(a, 3); }

// CIR-LABEL: @test_vqshrns_n_u32
// CIR: call <4 x i16> @llvm.aarch64.neon.uqshrn.v4i16(<4 x i32> %{{.*}}, i32 3)
// OGCG-LABEL: @test_vqshrns_n_u32
// OGCG: call <4 x i16> @llvm.aarch64.neon.uqshrn.v4i16(<4 x i32> %{{.*}}, i32 3)
uint16_t test_vqshrns_n_u32(uint32_t a) { return vqshrns_n_u32(a, 3); }

// --- vqshrun_n: Saturating unsigned narrowing right shift from signed ---

// CIR-LABEL: @test_vqshrunh_n_s16
// CIR: call <8 x i8> @llvm.aarch64.neon.sqshrun.v8i8(<8 x i16> %{{.*}}, i32 3)
// OGCG-LABEL: @test_vqshrunh_n_s16
// OGCG: call <8 x i8> @llvm.aarch64.neon.sqshrun.v8i8(<8 x i16> %{{.*}}, i32 3)
uint8_t test_vqshrunh_n_s16(int16_t a) { return vqshrunh_n_s16(a, 3); }

// CIR-LABEL: @test_vqshruns_n_s32
// CIR: call <4 x i16> @llvm.aarch64.neon.sqshrun.v4i16(<4 x i32> %{{.*}}, i32 3)
// OGCG-LABEL: @test_vqshruns_n_s32
// OGCG: call <4 x i16> @llvm.aarch64.neon.sqshrun.v4i16(<4 x i32> %{{.*}}, i32 3)
uint16_t test_vqshruns_n_s32(int32_t a) { return vqshruns_n_s32(a, 3); }

// --- vqrshrn_n: Saturating rounding narrowing right shift ---

// CIR-LABEL: @test_vqrshrnh_n_s16
// CIR: call <8 x i8> @llvm.aarch64.neon.sqrshrn.v8i8(<8 x i16> %{{.*}}, i32 3)
// OGCG-LABEL: @test_vqrshrnh_n_s16
// OGCG: call <8 x i8> @llvm.aarch64.neon.sqrshrn.v8i8(<8 x i16> %{{.*}}, i32 3)
int8_t test_vqrshrnh_n_s16(int16_t a) { return vqrshrnh_n_s16(a, 3); }

// CIR-LABEL: @test_vqrshrnh_n_u16
// CIR: call <8 x i8> @llvm.aarch64.neon.uqrshrn.v8i8(<8 x i16> %{{.*}}, i32 3)
// OGCG-LABEL: @test_vqrshrnh_n_u16
// OGCG: call <8 x i8> @llvm.aarch64.neon.uqrshrn.v8i8(<8 x i16> %{{.*}}, i32 3)
uint8_t test_vqrshrnh_n_u16(uint16_t a) { return vqrshrnh_n_u16(a, 3); }

// CIR-LABEL: @test_vqrshrns_n_s32
// CIR: call <4 x i16> @llvm.aarch64.neon.sqrshrn.v4i16(<4 x i32> %{{.*}}, i32 3)
// OGCG-LABEL: @test_vqrshrns_n_s32
// OGCG: call <4 x i16> @llvm.aarch64.neon.sqrshrn.v4i16(<4 x i32> %{{.*}}, i32 3)
int16_t test_vqrshrns_n_s32(int32_t a) { return vqrshrns_n_s32(a, 3); }

// CIR-LABEL: @test_vqrshrns_n_u32
// CIR: call <4 x i16> @llvm.aarch64.neon.uqrshrn.v4i16(<4 x i32> %{{.*}}, i32 3)
// OGCG-LABEL: @test_vqrshrns_n_u32
// OGCG: call <4 x i16> @llvm.aarch64.neon.uqrshrn.v4i16(<4 x i32> %{{.*}}, i32 3)
uint16_t test_vqrshrns_n_u32(uint32_t a) { return vqrshrns_n_u32(a, 3); }

// --- vqrshrun_n: Saturating rounding unsigned narrowing right shift from signed ---

// CIR-LABEL: @test_vqrshrunh_n_s16
// CIR: call <8 x i8> @llvm.aarch64.neon.sqrshrun.v8i8(<8 x i16> %{{.*}}, i32 3)
// OGCG-LABEL: @test_vqrshrunh_n_s16
// OGCG: call <8 x i8> @llvm.aarch64.neon.sqrshrun.v8i8(<8 x i16> %{{.*}}, i32 3)
uint8_t test_vqrshrunh_n_s16(int16_t a) { return vqrshrunh_n_s16(a, 3); }

// CIR-LABEL: @test_vqrshruns_n_s32
// CIR: call <4 x i16> @llvm.aarch64.neon.sqrshrun.v4i16(<4 x i32> %{{.*}}, i32 3)
// OGCG-LABEL: @test_vqrshruns_n_s32
// OGCG: call <4 x i16> @llvm.aarch64.neon.sqrshrun.v4i16(<4 x i32> %{{.*}}, i32 3)
uint16_t test_vqrshruns_n_s32(int32_t a) { return vqrshruns_n_s32(a, 3); }

// --- vsli/vsri: Shift left/right and insert (i64 -> <1 x i64>) ---

// CIR-LABEL: @test_vslid_n_s64
// CIR: call <1 x i64> @llvm.aarch64.neon.vsli.v1i64(<1 x i64> %{{.*}}, <1 x i64> %{{.*}}, i32 3)
// OGCG-LABEL: @test_vslid_n_s64
// OGCG: call <1 x i64> @llvm.aarch64.neon.vsli.v1i64(<1 x i64> %{{.*}}, <1 x i64> %{{.*}}, i32 3)
int64_t test_vslid_n_s64(int64_t a, int64_t b) { return vslid_n_s64(a, b, 3); }

// CIR-LABEL: @test_vslid_n_u64
// CIR: call <1 x i64> @llvm.aarch64.neon.vsli.v1i64(<1 x i64> %{{.*}}, <1 x i64> %{{.*}}, i32 3)
// OGCG-LABEL: @test_vslid_n_u64
// OGCG: call <1 x i64> @llvm.aarch64.neon.vsli.v1i64(<1 x i64> %{{.*}}, <1 x i64> %{{.*}}, i32 3)
uint64_t test_vslid_n_u64(uint64_t a, uint64_t b) { return vslid_n_u64(a, b, 3); }

// CIR-LABEL: @test_vsrid_n_s64
// CIR: call <1 x i64> @llvm.aarch64.neon.vsri.v1i64(<1 x i64> %{{.*}}, <1 x i64> %{{.*}}, i32 3)
// OGCG-LABEL: @test_vsrid_n_s64
// OGCG: call <1 x i64> @llvm.aarch64.neon.vsri.v1i64(<1 x i64> %{{.*}}, <1 x i64> %{{.*}}, i32 3)
int64_t test_vsrid_n_s64(int64_t a, int64_t b) { return vsrid_n_s64(a, b, 3); }

// CIR-LABEL: @test_vsrid_n_u64
// CIR: call <1 x i64> @llvm.aarch64.neon.vsri.v1i64(<1 x i64> %{{.*}}, <1 x i64> %{{.*}}, i32 3)
// OGCG-LABEL: @test_vsrid_n_u64
// OGCG: call <1 x i64> @llvm.aarch64.neon.vsri.v1i64(<1 x i64> %{{.*}}, <1 x i64> %{{.*}}, i32 3)
uint64_t test_vsrid_n_u64(uint64_t a, uint64_t b) { return vsrid_n_u64(a, b, 3); }
