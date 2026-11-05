// REQUIRES: aarch64-registered-target
//
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sme -target-feature +sme2 -target-feature +sme-i16i64 -target-feature +sme-f64f64 -emit-llvm -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sme -target-feature +sme2 -target-feature +sme-i16i64 -target-feature +sme-f64f64 -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Verify that ClangIR lowers the AArch64 SME2 multi-vector (x2/x4) builtins to
// the same @llvm.aarch64.sme.* multi-vector intrinsics as classic CodeGen,
// with the same operand unpacking (extractvalue per svintNxM component), the
// same overload (data vector type), and the same operand-group swap for the
// commutative svsudot/svsumla forms (classic swapCommutativeSMEOperands).
// Tested at the raw __builtin_sme_* level to avoid the generated arm_sme.h
// header dependency. The multi-vector struct operands are unpacked by
// getAArch64SVEProcessedOperands (shared with the SVE tuple builtins).

typedef __SVInt8_t svint8_t;
typedef __SVUint8_t svuint8_t;
typedef __SVInt32_t svint32_t;
typedef __SVInt64_t svint64_t;
typedef __SVFloat32_t svfloat32_t;
typedef __clang_svint8x2_t svint8x2_t;
typedef __clang_svint8x4_t svint8x4_t;
typedef __clang_svuint8x2_t svuint8x2_t;
typedef __clang_svuint8x4_t svuint8x4_t;
typedef __clang_svint32x2_t svint32x2_t;
typedef __clang_svint32x4_t svint32x4_t;
typedef __clang_svint64x2_t svint64x2_t;
typedef __clang_svfloat32x2_t svfloat32x2_t;
typedef __clang_svfloat32x4_t svfloat32x4_t;

// Multi-vector ZA add, x2: the svint32x2_t operand is unpacked into its two
// component vectors and passed to the vg1x2 intrinsic overloaded on nxv4i32.
void test_add_za32_vg1x2(unsigned s, svint32x2_t zn)
    __arm_streaming __arm_inout("za") {
  __builtin_sme_svadd_za32_s32_vg1x2(s, zn);
}
// CIR-LABEL: @test_add_za32_vg1x2
// CIR: %[[A0:.*]] = extractvalue { <vscale x 4 x i32>, <vscale x 4 x i32> } %{{.*}}, 0
// CIR: %[[A1:.*]] = extractvalue { <vscale x 4 x i32>, <vscale x 4 x i32> } %{{.*}}, 1
// CIR: call void @llvm.aarch64.sme.add.za32.vg1x2.nxv4i32(i32 %{{.*}}, <vscale x 4 x i32> %[[A0]], <vscale x 4 x i32> %[[A1]])
// OGCG-LABEL: @test_add_za32_vg1x2
// OGCG: call void @llvm.aarch64.sme.add.za32.vg1x2.nxv4i32(i32 %{{.*}}, <vscale x 4 x i32> %{{.*}}, <vscale x 4 x i32> %{{.*}})

// Multi-vector ZA add, x4.
void test_add_za32_vg1x4(unsigned s, svint32x4_t zn)
    __arm_streaming __arm_inout("za") {
  __builtin_sme_svadd_za32_s32_vg1x4(s, zn);
}
// CIR-LABEL: @test_add_za32_vg1x4
// CIR: call void @llvm.aarch64.sme.add.za32.vg1x4.nxv4i32(
// OGCG-LABEL: @test_add_za32_vg1x4
// OGCG: call void @llvm.aarch64.sme.add.za32.vg1x4.nxv4i32(

// Multi-vector ZA add, x2, 64-bit (sme-i16i64).
void test_add_za64_vg1x2(unsigned s, svint64x2_t zn)
    __arm_streaming __arm_inout("za") {
  __builtin_sme_svadd_za64_s64_vg1x2(s, zn);
}
// CIR-LABEL: @test_add_za64_vg1x2
// CIR: call void @llvm.aarch64.sme.add.za64.vg1x2.nxv2i64(
// OGCG-LABEL: @test_add_za64_vg1x2
// OGCG: call void @llvm.aarch64.sme.add.za64.vg1x2.nxv2i64(

// Multi-vector ZA sub, x2.
void test_sub_za32_vg1x2(unsigned s, svint32x2_t zn)
    __arm_streaming __arm_inout("za") {
  __builtin_sme_svsub_za32_s32_vg1x2(s, zn);
}
// CIR-LABEL: @test_sub_za32_vg1x2
// CIR: call void @llvm.aarch64.sme.sub.za32.vg1x2.nxv4i32(
// OGCG-LABEL: @test_sub_za32_vg1x2
// OGCG: call void @llvm.aarch64.sme.sub.za32.vg1x2.nxv4i32(

// Multi-vector integer dot, x2: both x2 operands unpacked; no swap.
void test_dot_za32_vg1x2(unsigned s, svint8x2_t zn, svint8x2_t zm)
    __arm_streaming __arm_inout("za") {
  __builtin_sme_svdot_za32_s8_vg1x2(s, zn, zm);
}
// CIR-LABEL: @test_dot_za32_vg1x2
// CIR: call void @llvm.aarch64.sme.sdot.za32.vg1x2.nxv16i8(
// OGCG-LABEL: @test_dot_za32_vg1x2
// OGCG: call void @llvm.aarch64.sme.sdot.za32.vg1x2.nxv16i8(

// Multi-vector FP MLA, x2.
void test_fmla_za32_vg1x2(unsigned s, svfloat32x2_t zn, svfloat32x2_t zm)
    __arm_streaming __arm_inout("za") {
  __builtin_sme_svmla_za32_f32_vg1x2(s, zn, zm);
}
// CIR-LABEL: @test_fmla_za32_vg1x2
// CIR: call void @llvm.aarch64.sme.fmla.vg1x2.nxv4f32(
// OGCG-LABEL: @test_fmla_za32_vg1x2
// OGCG: call void @llvm.aarch64.sme.fmla.vg1x2.nxv4f32(

// Multi-vector FP MLS, x2.
void test_fmls_za32_vg1x2(unsigned s, svfloat32x2_t zn, svfloat32x2_t zm)
    __arm_streaming __arm_inout("za") {
  __builtin_sme_svmls_za32_f32_vg1x2(s, zn, zm);
}
// CIR-LABEL: @test_fmls_za32_vg1x2
// CIR: call void @llvm.aarch64.sme.fmls.vg1x2.nxv4f32(
// OGCG-LABEL: @test_fmls_za32_vg1x2
// OGCG: call void @llvm.aarch64.sme.fmls.vg1x2.nxv4f32(

// Commutative svsudot, x2 (MultiVec=2): the two operand groups (zn, zm) are
// swapped so zm's two component vectors precede zn's, matching classic
// swapCommutativeSMEOperands + the usdot.za32.vg1x2 intrinsic.
void test_sudot_za32_vg1x2(unsigned s, svint8x2_t zn, svuint8x2_t zm)
    __arm_streaming __arm_inout("za") {
  __builtin_sme_svsudot_za32_s8_vg1x2(s, zn, zm);
}
// CIR-LABEL: @test_sudot_za32_vg1x2
// CIR: %[[ZN0:.*]] = extractvalue { <vscale x 16 x i8>, <vscale x 16 x i8> } %{{.*}}, 0
// CIR: %[[ZN1:.*]] = extractvalue { <vscale x 16 x i8>, <vscale x 16 x i8> } %{{.*}}, 1
// CIR: %[[ZM0:.*]] = extractvalue { <vscale x 16 x i8>, <vscale x 16 x i8> } %{{.*}}, 0
// CIR: %[[ZM1:.*]] = extractvalue { <vscale x 16 x i8>, <vscale x 16 x i8> } %{{.*}}, 1
// CIR: call void @llvm.aarch64.sme.usdot.za32.vg1x2.nxv16i8(i32 %{{.*}}, <vscale x 16 x i8> %[[ZM0]], <vscale x 16 x i8> %[[ZM1]], <vscale x 16 x i8> %[[ZN0]], <vscale x 16 x i8> %[[ZN1]])
// OGCG-LABEL: @test_sudot_za32_vg1x2
// OGCG: call void @llvm.aarch64.sme.usdot.za32.vg1x2.nxv16i8(

// Commutative svsumla, x1 (MultiVec=1): single-vector operands swapped
// (zm before zn), matching classic swapCommutativeSMEOperands + usmla.
void test_sumla_za32_vg4x1(unsigned s, svint8_t zn, svuint8_t zm)
    __arm_streaming __arm_inout("za") {
  __builtin_sme_svsumla_za32_s8_vg4x1(s, zn, zm);
}
// CIR-LABEL: @test_sumla_za32_vg4x1
// CIR: call void @llvm.aarch64.sme.usmla.za32.single.vg4x1.nxv16i8(
// OGCG-LABEL: @test_sumla_za32_vg4x1
// OGCG: call void @llvm.aarch64.sme.usmla.za32.single.vg4x1.nxv16i8(

// Multi-vector ZA read, x2: the intrinsic returns the tuple aggregate.
svint32x2_t test_read_za32_vg1x2(unsigned s) __arm_streaming __arm_in("za") {
  return __builtin_sme_svread_za32_s32_vg1x2(s);
}
// CIR-LABEL: @test_read_za32_vg1x2
// CIR: call { <vscale x 4 x i32>, <vscale x 4 x i32> } @llvm.aarch64.sme.read.vg1x2.nxv4i32(i32
// OGCG-LABEL: @test_read_za32_vg1x2
// OGCG: call { <vscale x 4 x i32>, <vscale x 4 x i32> } @llvm.aarch64.sme.read.vg1x2.nxv4i32(i32

// Multi-vector ZA write, x2: the x2 operand is unpacked into two component
// vectors passed to the write intrinsic.
void test_write_za32_vg1x2(unsigned s, svint32x2_t zn)
    __arm_streaming __arm_inout("za") {
  __builtin_sme_svwrite_za32_s32_vg1x2(s, zn);
}
// CIR-LABEL: @test_write_za32_vg1x2
// CIR: call void @llvm.aarch64.sme.write.vg1x2.nxv4i32(i32 %{{.*}}, <vscale x 4 x i32> %{{.*}}, <vscale x 4 x i32> %{{.*}})
// OGCG-LABEL: @test_write_za32_vg1x2
// OGCG: call void @llvm.aarch64.sme.write.vg1x2.nxv4i32(i32 %{{.*}}, <vscale x 4 x i32> %{{.*}}, <vscale x 4 x i32> %{{.*}})
