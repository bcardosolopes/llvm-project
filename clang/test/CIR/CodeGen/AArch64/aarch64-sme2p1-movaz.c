// REQUIRES: aarch64-registered-target

// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sme -target-feature +sme2p1 -disable-O0-optnone -Werror -Wall -emit-llvm -fclangir %s -o - | opt -S -passes=mem2reg,instcombine,tailcallelim | FileCheck %s -check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sme -target-feature +sme2p1 -disable-O0-optnone -Werror -Wall -emit-llvm %s -o - | opt -S -passes=mem2reg,instcombine,tailcallelim | FileCheck %s -check-prefix=OGCG

#include <arm_sme.h>

svint8x2_t test_svreadz_hor_za8_s8_x2(uint32_t slice)
    __arm_streaming __arm_inout("za") {
  return svreadz_hor_za8_s8_vg2(0, slice);
}

svuint16_t test_svreadz_hor_za16_u16(uint32_t slice)
    __arm_streaming __arm_inout("za") {
  return svreadz_hor_za16_u16(1, slice);
}

// CIR-LABEL: define{{.*}} @test_svreadz_hor_za8_s8_x2(
// CIR: [[TMP0:%.*]] = tail call { <vscale x 16 x i8>, <vscale x 16 x i8> } @llvm.aarch64.sme.readz.horiz.x2.nxv16i8(i32 0, i32 [[SLICE:%.*]])
// CIR: ret <{{.*}}>

// CIR-LABEL: define{{.*}} @test_svreadz_hor_za16_u16(
// CIR: [[TMP1:%.*]] = tail call <vscale x 8 x i16> @llvm.aarch64.sme.readz.horiz.nxv8i16(i32 1, i32 [[SLICE1:%.*]])
// CIR: ret <vscale x 8 x i16> [[TMP1]]

// OGCG-LABEL: define{{.*}} @test_svreadz_hor_za8_s8_x2(
// OGCG: [[TMP0:%.*]] = tail call { <vscale x 16 x i8>, <vscale x 16 x i8> } @llvm.aarch64.sme.readz.horiz.x2.nxv16i8(i32 0, i32 [[SLICE:%.*]])
// OGCG: ret { <vscale x 16 x i8>, <vscale x 16 x i8> } [[TMP0]]

// OGCG-LABEL: define{{.*}} @test_svreadz_hor_za16_u16(
// OGCG: [[TMP1:%.*]] = tail call <vscale x 8 x i16> @llvm.aarch64.sme.readz.horiz.nxv8i16(i32 1, i32 [[SLICE1:%.*]])
// OGCG: ret <vscale x 8 x i16> [[TMP1]]
