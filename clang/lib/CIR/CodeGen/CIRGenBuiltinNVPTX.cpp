//===---- CIRGenBuiltinNVPTX.cpp - Emit CIR for NVPTX builtins ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This contains code to emit NVPTX Builtin calls as CIR.
//
//===----------------------------------------------------------------------===//

#include "CIRGenCXXABI.h"
#include "CIRGenCall.h"
#include "CIRGenFunction.h"
#include "CIRGenModule.h"
#include "TargetInfo.h"
#include "clang/CIR/MissingFeatures.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Value.h"
#include "mlir/IR/ValueRange.h"
#include "clang/AST/GlobalDecl.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/TargetBuiltins.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/IR/CIRTypes.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsNVPTX.h"
#include "llvm/Support/ErrorHandling.h"

#include <array>

using namespace clang;
using namespace clang::CIRGen;
using namespace cir;

// Emits a generic-address-space atomicrmw for the __nvvm_atom_<op>_gen_*
// builtins. Mirrors classic NVPTX.cpp MakeBinaryAtomicValue: seq_cst, natural
// pointee alignment, result is the old value. Works for integer ops and (for
// add) floating-point.
static mlir::Value emitNVPTXAtomicRMW(CIRGenFunction &cgf, const CallExpr *expr,
                                      cir::AtomicFetchKind kind) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  Address destAddr = cgf.emitPointerWithAlignment(expr->getArg(0));
  mlir::Value val = cgf.emitScalarExpr(expr->getArg(1));
  auto rmw = cir::AtomicFetchOp::create(
      builder, cgf.getLoc(expr->getSourceRange()), destAddr.emitRawPointer(),
      val, kind, cir::MemOrder::SequentiallyConsistent,
      builder.getI64IntegerAttr(destAddr.getAlignment().getAsAlign().value()),
      /*is_volatile=*/false, /*fetch_first=*/true, /*sync_scope=*/nullptr);
  return rmw->getResult(0);
}

// Emits a generic-address-space atomic exchange for __nvvm_atom_xchg_gen_*.
// Mirrors classic NVPTX.cpp MakeBinaryAtomicValue(Xchg): seq_cst, natural
// alignment, result is the old value.
static mlir::Value emitNVPTXAtomicXchg(CIRGenFunction &cgf,
                                       const CallExpr *expr) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  Address destAddr = cgf.emitPointerWithAlignment(expr->getArg(0));
  mlir::Value val = cgf.emitScalarExpr(expr->getArg(1));
  auto xchg = cir::AtomicXchgOp::create(
      builder, cgf.getLoc(expr->getSourceRange()), destAddr.emitRawPointer(),
      val, cir::MemOrder::SequentiallyConsistent,
      builder.getI64IntegerAttr(destAddr.getAlignment().getAsAlign().value()),
      /*is_volatile=*/false, /*sync_scope=*/nullptr);
  return xchg->getResult(0);
}

// Emits a generic-address-space cmpxchg for the __nvvm_atom_cas_gen_* builtins.
// Mirrors classic NVPTX.cpp MakeAtomicCmpXchgValue(ReturnBool=false): seq_cst
// on both success and failure, natural alignment, result is the old value.
static mlir::Value emitNVPTXAtomicCmpXchg(CIRGenFunction &cgf,
                                          const CallExpr *expr) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  Address destAddr = cgf.emitPointerWithAlignment(expr->getArg(0));
  mlir::Value expected = cgf.emitScalarExpr(expr->getArg(1));
  mlir::Value desired = cgf.emitScalarExpr(expr->getArg(2));
  auto seqCst = cir::MemOrderAttr::get(&cgf.getMLIRContext(),
                                       cir::MemOrder::SequentiallyConsistent);
  auto op = cir::AtomicCmpXchgOp::create(
      builder, cgf.getLoc(expr->getSourceRange()), expected.getType(),
      builder.getBoolTy(), destAddr.emitRawPointer(), expected, desired, seqCst,
      seqCst,
      builder.getI64IntegerAttr(destAddr.getAlignment().getAsAlign().value()),
      /*weak=*/nullptr, /*is_volatile=*/nullptr, /*sync_scope=*/nullptr);
  return op.getResult(0);
}

// Emits a read-only global load for the scalar __nvvm_ldg_* builtins. Mirrors
// classic NVPTX.cpp MakeLdg: address-space-cast the generic pointer argument to
// the NVPTX global address space (target addrspace(1)), then an aligned load
// (natural pointee alignment) carrying !invariant.load. Classic ref:
// TargetBuiltins/NVPTX.cpp ldg_* -> MakeLdg.
static mlir::Value emitNVPTXLdg(CIRGenFunction &cgf, const CallExpr *expr) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Location loc = cgf.getLoc(expr->getSourceRange());
  mlir::Value ptr = cgf.emitScalarExpr(expr->getArg(0));
  QualType argType = expr->getArg(0)->getType();
  clang::CharUnits align =
      cgf.cgm.getNaturalTypeAlignment(argType->getPointeeType(), nullptr,
                                      nullptr, /*forPointeeType=*/false);
  mlir::Type elemTy = cgf.convertTypeForMem(argType->getPointeeType());

  // Use target addrspace(1) for NVPTX ADDRESS_SPACE_GLOBAL, matching classic
  // MakeLdg's Builder.getPtrTy(1).
  auto globalAS = cir::TargetAddressSpaceAttr::get(&cgf.getMLIRContext(), 1);
  cir::PointerType globalPtrTy = builder.getPointerTo(elemTy, globalAS);
  mlir::Value casted = builder.createAddrSpaceCast(loc, ptr, globalPtrTy);

  cir::LoadOp load = builder.createAlignedLoad(loc, elemTy, casted,
                                               align.getAsAlign());
  load->setAttr("cir.invariant_load",
                mlir::UnitAttr::get(builder.getContext()));
  return load.getResult();
}

//===----------------------------------------------------------------------===//
// WMMA / MMA tensor-core builtins
//
// The __hmma_*/__bmma_*/__imma_*/__dmma_*/__mma_* builtins map to the
// llvm.nvvm.wmma.*/llvm.nvvm.mma.* intrinsic family. This mirrors classic
// CodeGen's table-driven handling in TargetBuiltins/NVPTX.cpp
// (NVPTXMmaLdstInfo/NVPTXMmaInfo + getNVPTXMmaLdstInfo/getNVPTXMmaInfo): each
// builtin selects an llvm::Intrinsic::ID and a description of how to marshal
// fragment operands. The tables below reuse the exact same LLVM Intrinsic IDs;
// the intrinsic's dotted name is recovered via llvm::Intrinsic::getBaseName and
// the overload suffixes (e.g. `.p0`) are computed by the CIR->LLVM lowering of
// cir.call_llvm_intrinsic from the operand/result types.
//===----------------------------------------------------------------------===//

namespace {
// Maps an MMA load/store builtin to its intrinsic variant. Mirrors classic
// NVPTXMmaLdstInfo.
struct NVPTXMmaLdstInfo {
  unsigned NumResults; // Number of elements to load/store.
  // Intrinsic IDs for row/col variants. 0 if a layout is unsupported.
  unsigned IID_col;
  unsigned IID_row;
};

#define MMA_INTR(geom_op_type, layout)                                          \
  llvm::Intrinsic::nvvm_wmma_##geom_op_type##_##layout##_stride
#define MMA_LDST(n, geom_op_type)                                              \
  { n, MMA_INTR(geom_op_type, col), MMA_INTR(geom_op_type, row) }

static NVPTXMmaLdstInfo getNVPTXMmaLdstInfo(unsigned builtinId) {
  switch (builtinId) {
  // FP MMA loads
  case NVPTX::BI__hmma_m16n16k16_ld_a:
    return MMA_LDST(8, m16n16k16_load_a_f16);
  case NVPTX::BI__hmma_m16n16k16_ld_b:
    return MMA_LDST(8, m16n16k16_load_b_f16);
  case NVPTX::BI__hmma_m16n16k16_ld_c_f16:
    return MMA_LDST(4, m16n16k16_load_c_f16);
  case NVPTX::BI__hmma_m16n16k16_ld_c_f32:
    return MMA_LDST(8, m16n16k16_load_c_f32);
  case NVPTX::BI__hmma_m32n8k16_ld_a:
    return MMA_LDST(8, m32n8k16_load_a_f16);
  case NVPTX::BI__hmma_m32n8k16_ld_b:
    return MMA_LDST(8, m32n8k16_load_b_f16);
  case NVPTX::BI__hmma_m32n8k16_ld_c_f16:
    return MMA_LDST(4, m32n8k16_load_c_f16);
  case NVPTX::BI__hmma_m32n8k16_ld_c_f32:
    return MMA_LDST(8, m32n8k16_load_c_f32);
  case NVPTX::BI__hmma_m8n32k16_ld_a:
    return MMA_LDST(8, m8n32k16_load_a_f16);
  case NVPTX::BI__hmma_m8n32k16_ld_b:
    return MMA_LDST(8, m8n32k16_load_b_f16);
  case NVPTX::BI__hmma_m8n32k16_ld_c_f16:
    return MMA_LDST(4, m8n32k16_load_c_f16);
  case NVPTX::BI__hmma_m8n32k16_ld_c_f32:
    return MMA_LDST(8, m8n32k16_load_c_f32);

  // Integer MMA loads
  case NVPTX::BI__imma_m16n16k16_ld_a_s8:
    return MMA_LDST(2, m16n16k16_load_a_s8);
  case NVPTX::BI__imma_m16n16k16_ld_a_u8:
    return MMA_LDST(2, m16n16k16_load_a_u8);
  case NVPTX::BI__imma_m16n16k16_ld_b_s8:
    return MMA_LDST(2, m16n16k16_load_b_s8);
  case NVPTX::BI__imma_m16n16k16_ld_b_u8:
    return MMA_LDST(2, m16n16k16_load_b_u8);
  case NVPTX::BI__imma_m16n16k16_ld_c:
    return MMA_LDST(8, m16n16k16_load_c_s32);
  case NVPTX::BI__imma_m32n8k16_ld_a_s8:
    return MMA_LDST(4, m32n8k16_load_a_s8);
  case NVPTX::BI__imma_m32n8k16_ld_a_u8:
    return MMA_LDST(4, m32n8k16_load_a_u8);
  case NVPTX::BI__imma_m32n8k16_ld_b_s8:
    return MMA_LDST(1, m32n8k16_load_b_s8);
  case NVPTX::BI__imma_m32n8k16_ld_b_u8:
    return MMA_LDST(1, m32n8k16_load_b_u8);
  case NVPTX::BI__imma_m32n8k16_ld_c:
    return MMA_LDST(8, m32n8k16_load_c_s32);
  case NVPTX::BI__imma_m8n32k16_ld_a_s8:
    return MMA_LDST(1, m8n32k16_load_a_s8);
  case NVPTX::BI__imma_m8n32k16_ld_a_u8:
    return MMA_LDST(1, m8n32k16_load_a_u8);
  case NVPTX::BI__imma_m8n32k16_ld_b_s8:
    return MMA_LDST(4, m8n32k16_load_b_s8);
  case NVPTX::BI__imma_m8n32k16_ld_b_u8:
    return MMA_LDST(4, m8n32k16_load_b_u8);
  case NVPTX::BI__imma_m8n32k16_ld_c:
    return MMA_LDST(8, m8n32k16_load_c_s32);

  // Sub-integer MMA loads.
  // Only row/col layout is supported by A/B fragments.
  case NVPTX::BI__imma_m8n8k32_ld_a_s4:
    return {1, 0, MMA_INTR(m8n8k32_load_a_s4, row)};
  case NVPTX::BI__imma_m8n8k32_ld_a_u4:
    return {1, 0, MMA_INTR(m8n8k32_load_a_u4, row)};
  case NVPTX::BI__imma_m8n8k32_ld_b_s4:
    return {1, MMA_INTR(m8n8k32_load_b_s4, col), 0};
  case NVPTX::BI__imma_m8n8k32_ld_b_u4:
    return {1, MMA_INTR(m8n8k32_load_b_u4, col), 0};
  case NVPTX::BI__imma_m8n8k32_ld_c:
    return MMA_LDST(2, m8n8k32_load_c_s32);
  case NVPTX::BI__bmma_m8n8k128_ld_a_b1:
    return {1, 0, MMA_INTR(m8n8k128_load_a_b1, row)};
  case NVPTX::BI__bmma_m8n8k128_ld_b_b1:
    return {1, MMA_INTR(m8n8k128_load_b_b1, col), 0};
  case NVPTX::BI__bmma_m8n8k128_ld_c:
    return MMA_LDST(2, m8n8k128_load_c_s32);

  // Double MMA loads
  case NVPTX::BI__dmma_m8n8k4_ld_a:
    return MMA_LDST(1, m8n8k4_load_a_f64);
  case NVPTX::BI__dmma_m8n8k4_ld_b:
    return MMA_LDST(1, m8n8k4_load_b_f64);
  case NVPTX::BI__dmma_m8n8k4_ld_c:
    return MMA_LDST(2, m8n8k4_load_c_f64);

  // Alternate float MMA loads
  case NVPTX::BI__mma_bf16_m16n16k16_ld_a:
    return MMA_LDST(4, m16n16k16_load_a_bf16);
  case NVPTX::BI__mma_bf16_m16n16k16_ld_b:
    return MMA_LDST(4, m16n16k16_load_b_bf16);
  case NVPTX::BI__mma_bf16_m8n32k16_ld_a:
    return MMA_LDST(2, m8n32k16_load_a_bf16);
  case NVPTX::BI__mma_bf16_m8n32k16_ld_b:
    return MMA_LDST(8, m8n32k16_load_b_bf16);
  case NVPTX::BI__mma_bf16_m32n8k16_ld_a:
    return MMA_LDST(8, m32n8k16_load_a_bf16);
  case NVPTX::BI__mma_bf16_m32n8k16_ld_b:
    return MMA_LDST(2, m32n8k16_load_b_bf16);
  case NVPTX::BI__mma_tf32_m16n16k8_ld_a:
    return MMA_LDST(4, m16n16k8_load_a_tf32);
  case NVPTX::BI__mma_tf32_m16n16k8_ld_b:
    return MMA_LDST(4, m16n16k8_load_b_tf32);
  case NVPTX::BI__mma_tf32_m16n16k8_ld_c:
    return MMA_LDST(8, m16n16k8_load_c_f32);

  // NOTE: NVCC uses fragment C (not D) for both loads and stores, so store
  // builtins named `st_c_*` map to the `store_d_*` intrinsics.
  // FP MMA stores.
  case NVPTX::BI__hmma_m16n16k16_st_c_f16:
    return MMA_LDST(4, m16n16k16_store_d_f16);
  case NVPTX::BI__hmma_m16n16k16_st_c_f32:
    return MMA_LDST(8, m16n16k16_store_d_f32);
  case NVPTX::BI__hmma_m32n8k16_st_c_f16:
    return MMA_LDST(4, m32n8k16_store_d_f16);
  case NVPTX::BI__hmma_m32n8k16_st_c_f32:
    return MMA_LDST(8, m32n8k16_store_d_f32);
  case NVPTX::BI__hmma_m8n32k16_st_c_f16:
    return MMA_LDST(4, m8n32k16_store_d_f16);
  case NVPTX::BI__hmma_m8n32k16_st_c_f32:
    return MMA_LDST(8, m8n32k16_store_d_f32);

  // Integer and sub-integer MMA stores use LLVM's i32 in the name.
  case NVPTX::BI__imma_m16n16k16_st_c_i32:
    return MMA_LDST(8, m16n16k16_store_d_s32);
  case NVPTX::BI__imma_m32n8k16_st_c_i32:
    return MMA_LDST(8, m32n8k16_store_d_s32);
  case NVPTX::BI__imma_m8n32k16_st_c_i32:
    return MMA_LDST(8, m8n32k16_store_d_s32);
  case NVPTX::BI__imma_m8n8k32_st_c_i32:
    return MMA_LDST(2, m8n8k32_store_d_s32);
  case NVPTX::BI__bmma_m8n8k128_st_c_i32:
    return MMA_LDST(2, m8n8k128_store_d_s32);

  // Double MMA store
  case NVPTX::BI__dmma_m8n8k4_st_c_f64:
    return MMA_LDST(2, m8n8k4_store_d_f64);

  // Alternate float MMA store
  case NVPTX::BI__mma_m16n16k8_st_c_f32:
    return MMA_LDST(8, m16n16k8_store_d_f32);

  default:
    llvm_unreachable("Unknown MMA builtin");
  }
}
#undef MMA_LDST
#undef MMA_INTR

// Maps an MMA compute builtin to its intrinsic variants. Mirrors classic
// NVPTXMmaInfo.
struct NVPTXMmaInfo {
  unsigned NumEltsA;
  unsigned NumEltsB;
  unsigned NumEltsC;
  unsigned NumEltsD;

  // Variants ordered by layout-A/layout-B/satf ('row' before 'col').
  std::array<unsigned, 8> Variants;

  unsigned getMMAIntrinsic(int Layout, bool Satf) {
    unsigned Index = Layout + 4 * Satf;
    if (Index >= Variants.size())
      return 0;
    return Variants[Index];
  }
};

static NVPTXMmaInfo getNVPTXMmaInfo(unsigned builtinId) {
  // clang-format off
#define MMA_VARIANTS(geom, type)                                          \
      llvm::Intrinsic::nvvm_wmma_##geom##_mma_row_row_##type,             \
      llvm::Intrinsic::nvvm_wmma_##geom##_mma_row_col_##type,             \
      llvm::Intrinsic::nvvm_wmma_##geom##_mma_col_row_##type,             \
      llvm::Intrinsic::nvvm_wmma_##geom##_mma_col_col_##type
#define MMA_SATF_VARIANTS(geom, type)                                     \
      MMA_VARIANTS(geom, type),                                           \
      llvm::Intrinsic::nvvm_wmma_##geom##_mma_row_row_##type##_satfinite, \
      llvm::Intrinsic::nvvm_wmma_##geom##_mma_row_col_##type##_satfinite, \
      llvm::Intrinsic::nvvm_wmma_##geom##_mma_col_row_##type##_satfinite, \
      llvm::Intrinsic::nvvm_wmma_##geom##_mma_col_col_##type##_satfinite
// Sub-integer MMA only supports row.col layout.
#define MMA_VARIANTS_I4(geom, type) \
      0, \
      llvm::Intrinsic::nvvm_wmma_##geom##_mma_row_col_##type,             \
      0, \
      0, \
      0, \
      llvm::Intrinsic::nvvm_wmma_##geom##_mma_row_col_##type##_satfinite, \
      0, \
      0
// b1 MMA does not support .satfinite.
#define MMA_VARIANTS_B1_XOR(geom, type) \
      0, \
      llvm::Intrinsic::nvvm_wmma_##geom##_mma_xor_popc_row_col_##type,    \
      0, \
      0, \
      0, \
      0, \
      0, \
      0
#define MMA_VARIANTS_B1_AND(geom, type) \
      0, \
      llvm::Intrinsic::nvvm_wmma_##geom##_mma_and_popc_row_col_##type,    \
      0, \
      0, \
      0, \
      0, \
      0, \
      0
  // clang-format on
  switch (builtinId) {
  // FP MMA. 'type' uses D_C notation; NumEltsN are ordered A,B,C,D.
  case NVPTX::BI__hmma_m16n16k16_mma_f16f16:
    return {8, 8, 4, 4, {{MMA_SATF_VARIANTS(m16n16k16, f16_f16)}}};
  case NVPTX::BI__hmma_m16n16k16_mma_f32f16:
    return {8, 8, 4, 8, {{MMA_SATF_VARIANTS(m16n16k16, f32_f16)}}};
  case NVPTX::BI__hmma_m16n16k16_mma_f16f32:
    return {8, 8, 8, 4, {{MMA_SATF_VARIANTS(m16n16k16, f16_f32)}}};
  case NVPTX::BI__hmma_m16n16k16_mma_f32f32:
    return {8, 8, 8, 8, {{MMA_SATF_VARIANTS(m16n16k16, f32_f32)}}};
  case NVPTX::BI__hmma_m32n8k16_mma_f16f16:
    return {8, 8, 4, 4, {{MMA_SATF_VARIANTS(m32n8k16, f16_f16)}}};
  case NVPTX::BI__hmma_m32n8k16_mma_f32f16:
    return {8, 8, 4, 8, {{MMA_SATF_VARIANTS(m32n8k16, f32_f16)}}};
  case NVPTX::BI__hmma_m32n8k16_mma_f16f32:
    return {8, 8, 8, 4, {{MMA_SATF_VARIANTS(m32n8k16, f16_f32)}}};
  case NVPTX::BI__hmma_m32n8k16_mma_f32f32:
    return {8, 8, 8, 8, {{MMA_SATF_VARIANTS(m32n8k16, f32_f32)}}};
  case NVPTX::BI__hmma_m8n32k16_mma_f16f16:
    return {8, 8, 4, 4, {{MMA_SATF_VARIANTS(m8n32k16, f16_f16)}}};
  case NVPTX::BI__hmma_m8n32k16_mma_f32f16:
    return {8, 8, 4, 8, {{MMA_SATF_VARIANTS(m8n32k16, f32_f16)}}};
  case NVPTX::BI__hmma_m8n32k16_mma_f16f32:
    return {8, 8, 8, 4, {{MMA_SATF_VARIANTS(m8n32k16, f16_f32)}}};
  case NVPTX::BI__hmma_m8n32k16_mma_f32f32:
    return {8, 8, 8, 8, {{MMA_SATF_VARIANTS(m8n32k16, f32_f32)}}};

  // Integer MMA
  case NVPTX::BI__imma_m16n16k16_mma_s8:
    return {2, 2, 8, 8, {{MMA_SATF_VARIANTS(m16n16k16, s8)}}};
  case NVPTX::BI__imma_m16n16k16_mma_u8:
    return {2, 2, 8, 8, {{MMA_SATF_VARIANTS(m16n16k16, u8)}}};
  case NVPTX::BI__imma_m32n8k16_mma_s8:
    return {4, 1, 8, 8, {{MMA_SATF_VARIANTS(m32n8k16, s8)}}};
  case NVPTX::BI__imma_m32n8k16_mma_u8:
    return {4, 1, 8, 8, {{MMA_SATF_VARIANTS(m32n8k16, u8)}}};
  case NVPTX::BI__imma_m8n32k16_mma_s8:
    return {1, 4, 8, 8, {{MMA_SATF_VARIANTS(m8n32k16, s8)}}};
  case NVPTX::BI__imma_m8n32k16_mma_u8:
    return {1, 4, 8, 8, {{MMA_SATF_VARIANTS(m8n32k16, u8)}}};

  // Sub-integer MMA
  case NVPTX::BI__imma_m8n8k32_mma_s4:
    return {1, 1, 2, 2, {{MMA_VARIANTS_I4(m8n8k32, s4)}}};
  case NVPTX::BI__imma_m8n8k32_mma_u4:
    return {1, 1, 2, 2, {{MMA_VARIANTS_I4(m8n8k32, u4)}}};
  case NVPTX::BI__bmma_m8n8k128_mma_xor_popc_b1:
    return {1, 1, 2, 2, {{MMA_VARIANTS_B1_XOR(m8n8k128, b1)}}};
  case NVPTX::BI__bmma_m8n8k128_mma_and_popc_b1:
    return {1, 1, 2, 2, {{MMA_VARIANTS_B1_AND(m8n8k128, b1)}}};

  // Double MMA
  case NVPTX::BI__dmma_m8n8k4_mma_f64:
    return {1, 1, 2, 2, {{MMA_VARIANTS(m8n8k4, f64)}}};

  // Alternate FP MMA
  case NVPTX::BI__mma_bf16_m16n16k16_mma_f32:
    return {4, 4, 8, 8, {{MMA_VARIANTS(m16n16k16, bf16)}}};
  case NVPTX::BI__mma_bf16_m8n32k16_mma_f32:
    return {2, 8, 8, 8, {{MMA_VARIANTS(m8n32k16, bf16)}}};
  case NVPTX::BI__mma_bf16_m32n8k16_mma_f32:
    return {8, 2, 8, 8, {{MMA_VARIANTS(m32n8k16, bf16)}}};
  case NVPTX::BI__mma_tf32_m16n16k8_mma_f32:
    return {4, 4, 8, 8, {{MMA_VARIANTS(m16n16k8, tf32)}}};
  default:
    llvm_unreachable("Unexpected builtin ID.");
  }
#undef MMA_VARIANTS
#undef MMA_SATF_VARIANTS
#undef MMA_VARIANTS_I4
#undef MMA_VARIANTS_B1_AND
#undef MMA_VARIANTS_B1_XOR
}
} // namespace

// The dotted intrinsic name (without the leading "llvm.") for a given ID.
// cir.call_llvm_intrinsic re-derives overload suffixes from operand types.
static llvm::StringRef getNVPTXIntrinsicName(unsigned iid) {
  return llvm::Intrinsic::getBaseName(iid).drop_front(sizeof("llvm.") - 1);
}

// Element type of a load/store fragment. The struct/scalar element type is
// keyed off the intrinsic base-name type suffix, matching the LLVM intrinsic's
// return/param element type: `.f16` -> <2 x half>, `.f32` -> float,
// `.f64` -> double, and all integer-ish fragments (s8/u8/s4/u4/b1/s32/bf16/
// tf32) -> i32.
static mlir::Type getNVPTXMmaFragEltType(CIRGenFunction &cgf, unsigned iid) {
  llvm::StringRef suffix =
      llvm::Intrinsic::getBaseName(iid).rsplit('.').second;
  if (suffix == "f16")
    return cir::VectorType::get(cgf.fP16Ty, 2);
  if (suffix == "f32")
    return cgf.floatTy;
  if (suffix == "f64")
    return cgf.doubleTy;
  return cgf.sInt32Ty;
}

// A/B (input), C, and D fragment element types for an MMA compute builtin.
static void getNVPTXMmaEltTypes(CIRGenFunction &cgf, unsigned builtinId,
                                mlir::Type &abTy, mlir::Type &cTy,
                                mlir::Type &dTy) {
  mlir::Type half2 = cir::VectorType::get(cgf.fP16Ty, 2);
  mlir::Type f32 = cgf.floatTy;
  mlir::Type f64 = cgf.doubleTy;
  mlir::Type i32 = cgf.sInt32Ty;
  switch (builtinId) {
  // hmma: A/B are <2 x half>; C/D depend on the D<C> type suffix.
  case NVPTX::BI__hmma_m16n16k16_mma_f16f16:
  case NVPTX::BI__hmma_m32n8k16_mma_f16f16:
  case NVPTX::BI__hmma_m8n32k16_mma_f16f16:
    abTy = half2, cTy = half2, dTy = half2;
    return;
  case NVPTX::BI__hmma_m16n16k16_mma_f32f16:
  case NVPTX::BI__hmma_m32n8k16_mma_f32f16:
  case NVPTX::BI__hmma_m8n32k16_mma_f32f16:
    abTy = half2, cTy = half2, dTy = f32;
    return;
  case NVPTX::BI__hmma_m16n16k16_mma_f16f32:
  case NVPTX::BI__hmma_m32n8k16_mma_f16f32:
  case NVPTX::BI__hmma_m8n32k16_mma_f16f32:
    abTy = half2, cTy = f32, dTy = half2;
    return;
  case NVPTX::BI__hmma_m16n16k16_mma_f32f32:
  case NVPTX::BI__hmma_m32n8k16_mma_f32f32:
  case NVPTX::BI__hmma_m8n32k16_mma_f32f32:
    abTy = half2, cTy = f32, dTy = f32;
    return;
  // dmma: all double.
  case NVPTX::BI__dmma_m8n8k4_mma_f64:
    abTy = f64, cTy = f64, dTy = f64;
    return;
  // Alternate FP: A/B packed as i32, C/D f32.
  case NVPTX::BI__mma_bf16_m16n16k16_mma_f32:
  case NVPTX::BI__mma_bf16_m8n32k16_mma_f32:
  case NVPTX::BI__mma_bf16_m32n8k16_mma_f32:
  case NVPTX::BI__mma_tf32_m16n16k8_mma_f32:
    abTy = i32, cTy = f32, dTy = f32;
    return;
  // Integer / sub-integer / b1: all i32.
  default:
    abTy = i32, cTy = i32, dTy = i32;
    return;
  }
}

// Emits an MMA fragment load builtin. Mirrors classic NVPTX.cpp MMA-load path:
// call the load intrinsic, then store each returned element into the fragment
// array pointed to by arg0.
static mlir::Value emitNVPTXWmmaLd(CIRGenFunction &cgf, const CallExpr *expr,
                                   unsigned builtinId) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Location loc = cgf.getLoc(expr->getSourceRange());
  Address dst = cgf.emitPointerWithAlignment(expr->getArg(0));
  mlir::Value src = cgf.emitScalarExpr(expr->getArg(1));
  mlir::Value ldm = cgf.emitScalarExpr(expr->getArg(2));
  std::optional<llvm::APSInt> isColMajorArg =
      expr->getArg(3)->getIntegerConstantExpr(cgf.getContext());
  if (!isColMajorArg)
    return nullptr;
  bool isColMajor = isColMajorArg->getSExtValue();
  NVPTXMmaLdstInfo ii = getNVPTXMmaLdstInfo(builtinId);
  unsigned iid = isColMajor ? ii.IID_col : ii.IID_row;
  if (iid == 0)
    return nullptr;

  mlir::Type eltTy = getNVPTXMmaFragEltType(cgf, iid);
  mlir::StringAttr name =
      cgf.cgm.getStringAttrCached(getNVPTXIntrinsicName(iid));
  clang::CharUnits align = clang::CharUnits::fromQuantity(4);
  mlir::Type dstElem = dst.getElementType();

  // A single-result load returns a scalar directly (no aggregate); the
  // fragment array element type already matches, so store it verbatim.
  if (ii.NumResults == 1) {
    mlir::Value result =
        cir::LLVMIntrinsicCallOp::create(builder, loc, name, eltTy,
                                         mlir::ValueRange{src, ldm})
            .getResult();
    builder.createAlignedStore(loc, result, dst.emitRawPointer(), align);
    return result;
  }

  llvm::SmallVector<mlir::Type, 8> members(ii.NumResults, eltTy);
  auto resTy = cir::RecordType::get(&cgf.getMLIRContext(), members,
                                    /*packed=*/false, /*padded=*/false,
                                    cir::RecordType::Struct);
  mlir::Value result =
      cir::LLVMIntrinsicCallOp::create(builder, loc, name, resTy,
                                       mlir::ValueRange{src, ldm})
          .getResult();
  for (unsigned i = 0; i < ii.NumResults; ++i) {
    mlir::Value elt =
        cir::ExtractMemberOp::create(builder, loc, result, i).getResult();
    if (elt.getType() != dstElem)
      elt = builder.createBitcast(loc, elt, dstElem);
    mlir::Value idx = builder.getConstInt(loc, cgf.sInt32Ty, i);
    mlir::Value gep = builder.createPtrStride(loc, dst.emitRawPointer(), idx);
    builder.createAlignedStore(loc, elt, gep, align);
  }
  return result;
}

// Emits an MMA fragment store builtin. Mirrors classic NVPTX.cpp MMA-store
// path: load each element from the fragment array (arg1) and pass them to the
// store intrinsic writing to arg0.
static mlir::Value emitNVPTXWmmaSt(CIRGenFunction &cgf, const CallExpr *expr,
                                   unsigned builtinId) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Location loc = cgf.getLoc(expr->getSourceRange());
  mlir::Value dst = cgf.emitScalarExpr(expr->getArg(0));
  Address src = cgf.emitPointerWithAlignment(expr->getArg(1));
  mlir::Value ldm = cgf.emitScalarExpr(expr->getArg(2));
  std::optional<llvm::APSInt> isColMajorArg =
      expr->getArg(3)->getIntegerConstantExpr(cgf.getContext());
  if (!isColMajorArg)
    return nullptr;
  bool isColMajor = isColMajorArg->getSExtValue();
  NVPTXMmaLdstInfo ii = getNVPTXMmaLdstInfo(builtinId);
  unsigned iid = isColMajor ? ii.IID_col : ii.IID_row;
  if (iid == 0)
    return nullptr;

  mlir::Type eltTy = getNVPTXMmaFragEltType(cgf, iid);
  mlir::StringAttr name =
      cgf.cgm.getStringAttrCached(getNVPTXIntrinsicName(iid));
  clang::CharUnits align = clang::CharUnits::fromQuantity(4);
  mlir::Type srcElem = src.getElementType();

  llvm::SmallVector<mlir::Value, 12> values = {dst};
  for (unsigned i = 0; i < ii.NumResults; ++i) {
    mlir::Value idx = builder.getConstInt(loc, cgf.sInt32Ty, i);
    mlir::Value gep = builder.createPtrStride(loc, src.emitRawPointer(), idx);
    mlir::Value v = builder.createAlignedLoad(loc, srcElem, gep, align);
    if (v.getType() != eltTy)
      v = builder.createBitcast(loc, v, eltTy);
    values.push_back(v);
  }
  values.push_back(ldm);
  return cir::LLVMIntrinsicCallOp::create(builder, loc, name,
                                          builder.getVoidTy(), values)
      .getResult();
}

// Emits an MMA compute builtin. Mirrors classic NVPTX.cpp MMA path: load A/B/C
// fragment arrays, call the mma intrinsic, and store the D fragment.
static mlir::Value emitNVPTXWmmaMma(CIRGenFunction &cgf, const CallExpr *expr,
                                    unsigned builtinId) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Location loc = cgf.getLoc(expr->getSourceRange());
  Address dst = cgf.emitPointerWithAlignment(expr->getArg(0));
  Address srcA = cgf.emitPointerWithAlignment(expr->getArg(1));
  Address srcB = cgf.emitPointerWithAlignment(expr->getArg(2));
  Address srcC = cgf.emitPointerWithAlignment(expr->getArg(3));
  std::optional<llvm::APSInt> layoutArg =
      expr->getArg(4)->getIntegerConstantExpr(cgf.getContext());
  if (!layoutArg)
    return nullptr;
  int layout = layoutArg->getSExtValue();
  if (layout < 0 || layout > 3)
    return nullptr;
  llvm::APSInt satfArg;
  if (builtinId == NVPTX::BI__bmma_m8n8k128_mma_xor_popc_b1 ||
      builtinId == NVPTX::BI__bmma_m8n8k128_mma_and_popc_b1)
    satfArg = 0; // .b1 has no satf argument.
  else if (std::optional<llvm::APSInt> optSatfArg =
               expr->getArg(5)->getIntegerConstantExpr(cgf.getContext()))
    satfArg = *optSatfArg;
  else
    return nullptr;
  bool satf = satfArg.getSExtValue();
  NVPTXMmaInfo mi = getNVPTXMmaInfo(builtinId);
  unsigned iid = mi.getMMAIntrinsic(layout, satf);
  if (iid == 0) // Unsupported combination of layout/satf.
    return nullptr;

  mlir::Type abTy, cTy, dTy;
  getNVPTXMmaEltTypes(cgf, builtinId, abTy, cTy, dTy);
  clang::CharUnits align = clang::CharUnits::fromQuantity(4);

  llvm::SmallVector<mlir::Value, 24> values;
  auto loadFragment = [&](Address src, unsigned n, mlir::Type eltTy) {
    mlir::Type srcElem = src.getElementType();
    for (unsigned i = 0; i < n; ++i) {
      mlir::Value idx = builder.getConstInt(loc, cgf.sInt32Ty, i);
      mlir::Value gep = builder.createPtrStride(loc, src.emitRawPointer(), idx);
      mlir::Value v = builder.createAlignedLoad(loc, srcElem, gep, align);
      if (v.getType() != eltTy)
        v = builder.createBitcast(loc, v, eltTy);
      values.push_back(v);
    }
  };
  loadFragment(srcA, mi.NumEltsA, abTy);
  loadFragment(srcB, mi.NumEltsB, abTy);
  loadFragment(srcC, mi.NumEltsC, cTy);

  llvm::SmallVector<mlir::Type, 8> members(mi.NumEltsD, dTy);
  auto resTy = cir::RecordType::get(&cgf.getMLIRContext(), members,
                                    /*packed=*/false, /*padded=*/false,
                                    cir::RecordType::Struct);
  mlir::StringAttr name =
      cgf.cgm.getStringAttrCached(getNVPTXIntrinsicName(iid));
  mlir::Value result =
      cir::LLVMIntrinsicCallOp::create(builder, loc, name, resTy, values)
          .getResult();

  mlir::Type dstElem = dst.getElementType();
  for (unsigned i = 0; i < mi.NumEltsD; ++i) {
    mlir::Value elt =
        cir::ExtractMemberOp::create(builder, loc, result, i).getResult();
    if (elt.getType() != dstElem)
      elt = builder.createBitcast(loc, elt, dstElem);
    mlir::Value idx = builder.getConstInt(loc, cgf.sInt32Ty, i);
    mlir::Value gep = builder.createPtrStride(loc, dst.emitRawPointer(), idx);
    builder.createAlignedStore(loc, elt, gep, align);
  }
  return result;
}

mlir::Value CIRGenFunction::emitNVPTXBuiltinExpr(unsigned builtinId,
                                                 const CallExpr *expr) {
  switch (builtinId) {
  // Generic-address-space integer atomics -> cir.atomic.fetch (seq_cst).
  // Classic ref: TargetBuiltins/NVPTX.cpp atom_<op>_gen_* -> MakeBinaryAtomicValue.
  case NVPTX::BI__nvvm_atom_add_gen_i:
  case NVPTX::BI__nvvm_atom_add_gen_l:
  case NVPTX::BI__nvvm_atom_add_gen_ll:
    return emitNVPTXAtomicRMW(*this, expr, cir::AtomicFetchKind::Add);
  case NVPTX::BI__nvvm_atom_sub_gen_i:
  case NVPTX::BI__nvvm_atom_sub_gen_l:
  case NVPTX::BI__nvvm_atom_sub_gen_ll:
    return emitNVPTXAtomicRMW(*this, expr, cir::AtomicFetchKind::Sub);
  case NVPTX::BI__nvvm_atom_and_gen_i:
  case NVPTX::BI__nvvm_atom_and_gen_l:
  case NVPTX::BI__nvvm_atom_and_gen_ll:
    return emitNVPTXAtomicRMW(*this, expr, cir::AtomicFetchKind::And);
  case NVPTX::BI__nvvm_atom_or_gen_i:
  case NVPTX::BI__nvvm_atom_or_gen_l:
  case NVPTX::BI__nvvm_atom_or_gen_ll:
    return emitNVPTXAtomicRMW(*this, expr, cir::AtomicFetchKind::Or);
  case NVPTX::BI__nvvm_atom_xor_gen_i:
  case NVPTX::BI__nvvm_atom_xor_gen_l:
  case NVPTX::BI__nvvm_atom_xor_gen_ll:
    return emitNVPTXAtomicRMW(*this, expr, cir::AtomicFetchKind::Xor);
  case NVPTX::BI__nvvm_atom_xchg_gen_i:
  case NVPTX::BI__nvvm_atom_xchg_gen_l:
  case NVPTX::BI__nvvm_atom_xchg_gen_ll:
    return emitNVPTXAtomicXchg(*this, expr);
  case NVPTX::BI__nvvm_atom_max_gen_i:
  case NVPTX::BI__nvvm_atom_max_gen_l:
  case NVPTX::BI__nvvm_atom_max_gen_ll:
    return emitNVPTXAtomicRMW(*this, expr, cir::AtomicFetchKind::Max);
  case NVPTX::BI__nvvm_atom_max_gen_ui:
  case NVPTX::BI__nvvm_atom_max_gen_ul:
  case NVPTX::BI__nvvm_atom_max_gen_ull:
    return emitNVPTXAtomicRMW(*this, expr, cir::AtomicFetchKind::UMax);
  case NVPTX::BI__nvvm_atom_min_gen_i:
  case NVPTX::BI__nvvm_atom_min_gen_l:
  case NVPTX::BI__nvvm_atom_min_gen_ll:
    return emitNVPTXAtomicRMW(*this, expr, cir::AtomicFetchKind::Min);
  case NVPTX::BI__nvvm_atom_min_gen_ui:
  case NVPTX::BI__nvvm_atom_min_gen_ul:
  case NVPTX::BI__nvvm_atom_min_gen_ull:
    return emitNVPTXAtomicRMW(*this, expr, cir::AtomicFetchKind::UMin);

  // Generic-address-space floating-point atomic add -> cir.atomic.fetch fadd.
  // Classic ref: TargetBuiltins/NVPTX.cpp atom_add_gen_f/d.
  case NVPTX::BI__nvvm_atom_add_gen_f:
  case NVPTX::BI__nvvm_atom_add_gen_d:
    return emitNVPTXAtomicRMW(*this, expr, cir::AtomicFetchKind::Add);

  // Generic-address-space compare-and-swap -> cir.atomic.cmpxchg (seq_cst).
  // Classic ref: TargetBuiltins/NVPTX.cpp atom_cas_gen_* -> MakeAtomicCmpXchgValue.
  case NVPTX::BI__nvvm_atom_cas_gen_us:
  case NVPTX::BI__nvvm_atom_cas_gen_i:
  case NVPTX::BI__nvvm_atom_cas_gen_l:
  case NVPTX::BI__nvvm_atom_cas_gen_ll:
    return emitNVPTXAtomicCmpXchg(*this, expr);

  // __nvvm_ex2_approx_f/_d -> llvm.nvvm.ex2.approx.f32/f64 (overloaded on the
  // operand type). Classic ref: TargetBuiltins/NVPTX.cpp ex2_approx_f/d.
  case NVPTX::BI__nvvm_ex2_approx_f:
  case NVPTX::BI__nvvm_ex2_approx_d: {
    mlir::Value arg0 = emitScalarExpr(expr->getArg(0));
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("nvvm.ex2.approx"), arg0.getType(),
               mlir::ValueRange{arg0})
        .getResult();
  }

  // Read-only global loads -> addrspacecast to global + load with
  // !invariant.load. Classic ref: TargetBuiltins/NVPTX.cpp ldg_* -> MakeLdg.
  // Both scalar and vector (__nvvm_ldg_*{2,4}) overloads go through the same
  // path: MakeLdg is type-agnostic, deriving the pointee element type via
  // ConvertTypeForMem and the alignment via getNaturalPointeeTypeAlignment, so
  // the vector pointee (e.g. <2 x i32> align 8, <4 x i32> align 16) falls out
  // of the natural-alignment computation with no special casing.
  case NVPTX::BI__nvvm_ldg_c:
  case NVPTX::BI__nvvm_ldg_sc:
  case NVPTX::BI__nvvm_ldg_c2:
  case NVPTX::BI__nvvm_ldg_sc2:
  case NVPTX::BI__nvvm_ldg_c4:
  case NVPTX::BI__nvvm_ldg_sc4:
  case NVPTX::BI__nvvm_ldg_s:
  case NVPTX::BI__nvvm_ldg_s2:
  case NVPTX::BI__nvvm_ldg_s4:
  case NVPTX::BI__nvvm_ldg_i:
  case NVPTX::BI__nvvm_ldg_i2:
  case NVPTX::BI__nvvm_ldg_i4:
  case NVPTX::BI__nvvm_ldg_l:
  case NVPTX::BI__nvvm_ldg_l2:
  case NVPTX::BI__nvvm_ldg_ll:
  case NVPTX::BI__nvvm_ldg_ll2:
  case NVPTX::BI__nvvm_ldg_uc:
  case NVPTX::BI__nvvm_ldg_uc2:
  case NVPTX::BI__nvvm_ldg_uc4:
  case NVPTX::BI__nvvm_ldg_us:
  case NVPTX::BI__nvvm_ldg_us2:
  case NVPTX::BI__nvvm_ldg_us4:
  case NVPTX::BI__nvvm_ldg_ui:
  case NVPTX::BI__nvvm_ldg_ui2:
  case NVPTX::BI__nvvm_ldg_ui4:
  case NVPTX::BI__nvvm_ldg_ul:
  case NVPTX::BI__nvvm_ldg_ul2:
  case NVPTX::BI__nvvm_ldg_ull:
  case NVPTX::BI__nvvm_ldg_ull2:
  case NVPTX::BI__nvvm_ldg_f:
  case NVPTX::BI__nvvm_ldg_f2:
  case NVPTX::BI__nvvm_ldg_f4:
  case NVPTX::BI__nvvm_ldg_d:
  case NVPTX::BI__nvvm_ldg_d2:
    return emitNVPTXLdg(*this, expr);

  case NVPTX::BI__nvvm_barrier_cluster_arrive:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("nvvm.barrier.cluster.arrive"),
               builder.getVoidTy())
        .getResult();
  case NVPTX::BI__nvvm_barrier_cluster_arrive_relaxed:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("nvvm.barrier.cluster.arrive.relaxed"),
               builder.getVoidTy())
        .getResult();
  case NVPTX::BI__nvvm_barrier_cluster_wait:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("nvvm.barrier.cluster.wait"),
               builder.getVoidTy())
        .getResult();
  case NVPTX::BI__nvvm_fence_sc_cluster:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("nvvm.fence.sc.cluster"),
               builder.getVoidTy(), mlir::ValueRange{})
        .getResult();
  case NVPTX::BI__nvvm_bar_sync:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("nvvm.barrier.cta.sync.aligned.all"),
               builder.getVoidTy(),
               mlir::ValueRange{emitScalarExpr(expr->getArg(0))})
        .getResult();
  case NVPTX::BI__syncthreads:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("nvvm.barrier.cta.sync.aligned.all"),
               builder.getVoidTy(),
               mlir::ValueRange{builder.getConstInt(getLoc(expr->getExprLoc()),
                                                    sInt32Ty, 0)})
        .getResult();
  case NVPTX::BI__nvvm_barrier_sync:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("nvvm.barrier.cta.sync.all"),
               builder.getVoidTy(),
               mlir::ValueRange{emitScalarExpr(expr->getArg(0))})
        .getResult();
  case NVPTX::BI__nvvm_barrier_sync_cnt:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("nvvm.barrier.cta.sync.count"),
               builder.getVoidTy(),
               mlir::ValueRange{emitScalarExpr(expr->getArg(0)),
                                emitScalarExpr(expr->getArg(1))})
        .getResult();
  case NVPTX::BI__nvvm_bar_warp_sync:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("nvvm.bar.warp.sync"),
               builder.getVoidTy(),
               mlir::ValueRange{emitScalarExpr(expr->getArg(0))})
        .getResult();
  case NVPTX::BI__nvvm_fmax_f: {
    mlir::Value arg0 = emitScalarExpr(expr->getArg(0));
    mlir::Value arg1 = emitScalarExpr(expr->getArg(1));
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("nvvm.fmax.f"), arg0.getType(),
               mlir::ValueRange{arg0, arg1})
        .getResult();
  }
  case NVPTX::BI__nvvm_fmin_f: {
    mlir::Value arg0 = emitScalarExpr(expr->getArg(0));
    mlir::Value arg1 = emitScalarExpr(expr->getArg(1));
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("nvvm.fmin.f"), arg0.getType(),
               mlir::ValueRange{arg0, arg1})
        .getResult();
  }
  case NVPTX::BI__nvvm_sqrt_rn_f: {
    mlir::Value arg0 = emitScalarExpr(expr->getArg(0));
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("nvvm.sqrt.rn.f"), arg0.getType(),
               mlir::ValueRange{arg0})
        .getResult();
  }
  case NVPTX::BI__nvvm_rcp_rn_f: {
    mlir::Value arg0 = emitScalarExpr(expr->getArg(0));
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("nvvm.rcp.rn.f"), arg0.getType(),
               mlir::ValueRange{arg0})
        .getResult();
  }
  case NVPTX::BI__nvvm_add_rn_f: {
    mlir::Value arg0 = emitScalarExpr(expr->getArg(0));
    mlir::Value arg1 = emitScalarExpr(expr->getArg(1));
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("nvvm.add.rn.f"), arg0.getType(),
               mlir::ValueRange{arg0, arg1})
        .getResult();
  }
  case NVPTX::BI__nvvm_fmax_d: {
    mlir::Value arg0 = emitScalarExpr(expr->getArg(0));
    mlir::Value arg1 = emitScalarExpr(expr->getArg(1));
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("nvvm.fmax.d"), arg0.getType(),
               mlir::ValueRange{arg0, arg1})
        .getResult();
  }
  case NVPTX::BI__nvvm_fmin_d: {
    mlir::Value arg0 = emitScalarExpr(expr->getArg(0));
    mlir::Value arg1 = emitScalarExpr(expr->getArg(1));
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("nvvm.fmin.d"), arg0.getType(),
               mlir::ValueRange{arg0, arg1})
        .getResult();
  }
  case NVPTX::BI__nvvm_sqrt_rn_d: {
    mlir::Value arg0 = emitScalarExpr(expr->getArg(0));
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("nvvm.sqrt.rn.d"), arg0.getType(),
               mlir::ValueRange{arg0})
        .getResult();
  }
  case NVPTX::BI__nvvm_rcp_rn_d: {
    mlir::Value arg0 = emitScalarExpr(expr->getArg(0));
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("nvvm.rcp.rn.d"), arg0.getType(),
               mlir::ValueRange{arg0})
        .getResult();
  }
  case NVPTX::BI__nvvm_mulhi_i: {
    mlir::Value arg0 = emitScalarExpr(expr->getArg(0));
    mlir::Value arg1 = emitScalarExpr(expr->getArg(1));
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("nvvm.mulhi.i"), arg0.getType(),
               mlir::ValueRange{arg0, arg1})
        .getResult();
  }
  case NVPTX::BI__nvvm_membar_cta:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("nvvm.membar.cta"), builder.getVoidTy())
        .getResult();
  case NVPTX::BI__nvvm_membar_gl:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("nvvm.membar.gl"), builder.getVoidTy())
        .getResult();
  case NVPTX::BI__nvvm_membar_sys:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("nvvm.membar.sys"), builder.getVoidTy())
        .getResult();
  // Special register reads: threadIdx, blockIdx, blockDim, gridDim
  case NVPTX::BI__nvvm_read_ptx_sreg_tid_x:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("nvvm.read.ptx.sreg.tid.x"), sInt32Ty)
        .getResult();
  case NVPTX::BI__nvvm_read_ptx_sreg_tid_y:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("nvvm.read.ptx.sreg.tid.y"), sInt32Ty)
        .getResult();
  case NVPTX::BI__nvvm_read_ptx_sreg_tid_z:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("nvvm.read.ptx.sreg.tid.z"), sInt32Ty)
        .getResult();
  case NVPTX::BI__nvvm_read_ptx_sreg_ctaid_x:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("nvvm.read.ptx.sreg.ctaid.x"), sInt32Ty)
        .getResult();
  case NVPTX::BI__nvvm_read_ptx_sreg_ctaid_y:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("nvvm.read.ptx.sreg.ctaid.y"), sInt32Ty)
        .getResult();
  case NVPTX::BI__nvvm_read_ptx_sreg_ctaid_z:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("nvvm.read.ptx.sreg.ctaid.z"), sInt32Ty)
        .getResult();
  case NVPTX::BI__nvvm_read_ptx_sreg_ntid_x:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("nvvm.read.ptx.sreg.ntid.x"), sInt32Ty)
        .getResult();
  case NVPTX::BI__nvvm_read_ptx_sreg_ntid_y:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("nvvm.read.ptx.sreg.ntid.y"), sInt32Ty)
        .getResult();
  case NVPTX::BI__nvvm_read_ptx_sreg_ntid_z:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("nvvm.read.ptx.sreg.ntid.z"), sInt32Ty)
        .getResult();
  case NVPTX::BI__nvvm_read_ptx_sreg_nctaid_x:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("nvvm.read.ptx.sreg.nctaid.x"), sInt32Ty)
        .getResult();
  case NVPTX::BI__nvvm_read_ptx_sreg_nctaid_y:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("nvvm.read.ptx.sreg.nctaid.y"), sInt32Ty)
        .getResult();
  case NVPTX::BI__nvvm_read_ptx_sreg_nctaid_z:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("nvvm.read.ptx.sreg.nctaid.z"), sInt32Ty)
        .getResult();

  // WMMA/MMA tensor-core fragment loads -> llvm.nvvm.wmma.*.load.*. Classic
  // ref: TargetBuiltins/NVPTX.cpp MMA-load path.
  case NVPTX::BI__hmma_m16n16k16_ld_a:
  case NVPTX::BI__hmma_m16n16k16_ld_b:
  case NVPTX::BI__hmma_m16n16k16_ld_c_f16:
  case NVPTX::BI__hmma_m16n16k16_ld_c_f32:
  case NVPTX::BI__hmma_m32n8k16_ld_a:
  case NVPTX::BI__hmma_m32n8k16_ld_b:
  case NVPTX::BI__hmma_m32n8k16_ld_c_f16:
  case NVPTX::BI__hmma_m32n8k16_ld_c_f32:
  case NVPTX::BI__hmma_m8n32k16_ld_a:
  case NVPTX::BI__hmma_m8n32k16_ld_b:
  case NVPTX::BI__hmma_m8n32k16_ld_c_f16:
  case NVPTX::BI__hmma_m8n32k16_ld_c_f32:
  case NVPTX::BI__imma_m16n16k16_ld_a_s8:
  case NVPTX::BI__imma_m16n16k16_ld_a_u8:
  case NVPTX::BI__imma_m16n16k16_ld_b_s8:
  case NVPTX::BI__imma_m16n16k16_ld_b_u8:
  case NVPTX::BI__imma_m16n16k16_ld_c:
  case NVPTX::BI__imma_m32n8k16_ld_a_s8:
  case NVPTX::BI__imma_m32n8k16_ld_a_u8:
  case NVPTX::BI__imma_m32n8k16_ld_b_s8:
  case NVPTX::BI__imma_m32n8k16_ld_b_u8:
  case NVPTX::BI__imma_m32n8k16_ld_c:
  case NVPTX::BI__imma_m8n32k16_ld_a_s8:
  case NVPTX::BI__imma_m8n32k16_ld_a_u8:
  case NVPTX::BI__imma_m8n32k16_ld_b_s8:
  case NVPTX::BI__imma_m8n32k16_ld_b_u8:
  case NVPTX::BI__imma_m8n32k16_ld_c:
  case NVPTX::BI__imma_m8n8k32_ld_a_s4:
  case NVPTX::BI__imma_m8n8k32_ld_a_u4:
  case NVPTX::BI__imma_m8n8k32_ld_b_s4:
  case NVPTX::BI__imma_m8n8k32_ld_b_u4:
  case NVPTX::BI__imma_m8n8k32_ld_c:
  case NVPTX::BI__bmma_m8n8k128_ld_a_b1:
  case NVPTX::BI__bmma_m8n8k128_ld_b_b1:
  case NVPTX::BI__bmma_m8n8k128_ld_c:
  case NVPTX::BI__dmma_m8n8k4_ld_a:
  case NVPTX::BI__dmma_m8n8k4_ld_b:
  case NVPTX::BI__dmma_m8n8k4_ld_c:
  case NVPTX::BI__mma_bf16_m16n16k16_ld_a:
  case NVPTX::BI__mma_bf16_m16n16k16_ld_b:
  case NVPTX::BI__mma_bf16_m8n32k16_ld_a:
  case NVPTX::BI__mma_bf16_m8n32k16_ld_b:
  case NVPTX::BI__mma_bf16_m32n8k16_ld_a:
  case NVPTX::BI__mma_bf16_m32n8k16_ld_b:
  case NVPTX::BI__mma_tf32_m16n16k8_ld_a:
  case NVPTX::BI__mma_tf32_m16n16k8_ld_b:
  case NVPTX::BI__mma_tf32_m16n16k8_ld_c:
    return emitNVPTXWmmaLd(*this, expr, builtinId);

  // WMMA/MMA tensor-core fragment stores -> llvm.nvvm.wmma.*.store.*. Classic
  // ref: TargetBuiltins/NVPTX.cpp MMA-store path.
  case NVPTX::BI__hmma_m16n16k16_st_c_f16:
  case NVPTX::BI__hmma_m16n16k16_st_c_f32:
  case NVPTX::BI__hmma_m32n8k16_st_c_f16:
  case NVPTX::BI__hmma_m32n8k16_st_c_f32:
  case NVPTX::BI__hmma_m8n32k16_st_c_f16:
  case NVPTX::BI__hmma_m8n32k16_st_c_f32:
  case NVPTX::BI__imma_m16n16k16_st_c_i32:
  case NVPTX::BI__imma_m32n8k16_st_c_i32:
  case NVPTX::BI__imma_m8n32k16_st_c_i32:
  case NVPTX::BI__imma_m8n8k32_st_c_i32:
  case NVPTX::BI__bmma_m8n8k128_st_c_i32:
  case NVPTX::BI__dmma_m8n8k4_st_c_f64:
  case NVPTX::BI__mma_m16n16k8_st_c_f32:
    return emitNVPTXWmmaSt(*this, expr, builtinId);

  // WMMA/MMA tensor-core compute -> llvm.nvvm.wmma.*.mma.*. Classic ref:
  // TargetBuiltins/NVPTX.cpp MMA path.
  case NVPTX::BI__hmma_m16n16k16_mma_f16f16:
  case NVPTX::BI__hmma_m16n16k16_mma_f32f16:
  case NVPTX::BI__hmma_m16n16k16_mma_f32f32:
  case NVPTX::BI__hmma_m16n16k16_mma_f16f32:
  case NVPTX::BI__hmma_m32n8k16_mma_f16f16:
  case NVPTX::BI__hmma_m32n8k16_mma_f32f16:
  case NVPTX::BI__hmma_m32n8k16_mma_f32f32:
  case NVPTX::BI__hmma_m32n8k16_mma_f16f32:
  case NVPTX::BI__hmma_m8n32k16_mma_f16f16:
  case NVPTX::BI__hmma_m8n32k16_mma_f32f16:
  case NVPTX::BI__hmma_m8n32k16_mma_f32f32:
  case NVPTX::BI__hmma_m8n32k16_mma_f16f32:
  case NVPTX::BI__imma_m16n16k16_mma_s8:
  case NVPTX::BI__imma_m16n16k16_mma_u8:
  case NVPTX::BI__imma_m32n8k16_mma_s8:
  case NVPTX::BI__imma_m32n8k16_mma_u8:
  case NVPTX::BI__imma_m8n32k16_mma_s8:
  case NVPTX::BI__imma_m8n32k16_mma_u8:
  case NVPTX::BI__imma_m8n8k32_mma_s4:
  case NVPTX::BI__imma_m8n8k32_mma_u4:
  case NVPTX::BI__bmma_m8n8k128_mma_xor_popc_b1:
  case NVPTX::BI__bmma_m8n8k128_mma_and_popc_b1:
  case NVPTX::BI__dmma_m8n8k4_mma_f64:
  case NVPTX::BI__mma_bf16_m16n16k16_mma_f32:
  case NVPTX::BI__mma_bf16_m8n32k16_mma_f32:
  case NVPTX::BI__mma_bf16_m32n8k16_mma_f32:
  case NVPTX::BI__mma_tf32_m16n16k8_mma_f32:
    return emitNVPTXWmmaMma(*this, expr, builtinId);

  default:
    return nullptr;
  }
}

// vprintf takes two args: A format string, and a pointer to a buffer containing
// the varargs.
//
// For example, the call
//
//   printf("format string", arg1, arg2, arg3);
//
// is converted into something resembling
//
//   struct Tmp {
//     Arg1 a1;
//     Arg2 a2;
//     Arg3 a3;
//   };
//   char* buf = alloca(sizeof(Tmp));
//   *(Tmp*)buf = {a1, a2, a3};
//   vprintf("format string", buf);
//
// `buf` is aligned to the max of {alignof(Arg1), ...}.  Furthermore, each of
// the args is itself aligned to its preferred alignment.
//
// Note that by the time this function runs, the arguments have already
// undergone the standard C vararg promotion (short -> int, float -> double
// etc). In this function we pack the arguments into the buffer described above.
static mlir::Value packArgsIntoNVPTXFormatBuffer(CIRGenFunction &cgf,
                                                 const CallArgList &args,
                                                 mlir::Location loc) {
  const CIRDataLayout &dataLayout = cgf.cgm.getDataLayout();
  CIRGenBuilderTy &builder = cgf.getBuilder();

  if (args.size() <= 1)
    // If there are no arguments other than the format string,
    // pass a nullptr to vprintf.
    return builder.getNullPtr(cgf.voidPtrTy, loc);

  llvm::SmallVector<mlir::Type, 8> argTypes;
  for (const auto &arg : llvm::drop_begin(args))
    argTypes.push_back(arg.getKnownRValue().getValue().getType());

  // We can directly store the arguments into a record, and the alignment
  // would automatically be correct. That's because vprintf does not
  // accept aggregates.
  mlir::Type allocaTy =
      cir::RecordType::get(&cgf.getMLIRContext(), argTypes, /*packed=*/false,
                           /*padded=*/false, cir::RecordType::Struct);
  // TODO(cir): Use getPrefTypeAlign here to match OG codegen behavior.
  // CIR's data layout currently doesn't differentiate struct preferred alignment
  // from ABI alignment (LLVM defaults StructPrefAlignment to 8, giving align 8
  // for { i32 } structs on nvptx64). Once CIR's data layout is fixed, this
  // should produce align 8 matching the incubator and OG codegen.
  auto allocaAlign = clang::CharUnits::fromQuantity(
      dataLayout.getPrefTypeAlign(allocaTy).value());
  Address allocaAddr =
      cgf.createTempAlloca(allocaTy, allocaAlign, loc, "printf_args");
  mlir::Value alloca = allocaAddr.getPointer();

  for (auto [i, arg] : llvm::enumerate(llvm::drop_begin(args))) {
    mlir::Value member =
        builder.createGetMember(loc, cir::PointerType::get(argTypes[i]), alloca,
                                /*name=*/"", /*index=*/i);
    auto preferredAlign = clang::CharUnits::fromQuantity(
        dataLayout.getPrefTypeAlign(argTypes[i]).value());
    builder.createAlignedStore(loc, arg.getKnownRValue().getValue(), member,
                               preferredAlign);
  }

  return builder.createBitcast(alloca, cgf.voidPtrTy);
}

mlir::Value
CIRGenFunction::emitNVPTXDevicePrintfCallExpr(const CallExpr *expr) {
  assert(cgm.getTriple().isNVPTX());
  CallArgList args;
  emitCallArgs(args,
               expr->getDirectCallee()->getType()->getAs<FunctionProtoType>(),
               expr->arguments(), expr->getDirectCallee());

  mlir::Location loc = getLoc(expr->getBeginLoc());

  // Except the format string, no non-scalar arguments are allowed for
  // device-side printf.
  bool hasNonScalar =
      llvm::any_of(llvm::drop_begin(args), [&](const CallArg &a) {
        return !a.getKnownRValue().isScalar();
      });
  if (hasNonScalar) {
    cgm.errorUnsupported(expr, "non-scalar args to printf");
    return builder.getConstInt(loc, sInt32Ty, 0);
  }

  mlir::Value packedData = packArgsIntoNVPTXFormatBuffer(*this, args, loc);

  // int vprintf(char *format, void *packedData);
  //
  // dontDefer=true keeps this a pure external declaration, mirroring classic
  // CGGPUBuiltin.cpp GetVprintfDeclaration's raw llvm::Function::Create. If a
  // libc header (glibc <stdio.h>) supplies a __gnu_inline `vprintf` whose body
  // is `vfprintf(stdout, fmt, ap)`, the default deferred path would move that
  // definition from deferredDecls into deferredDeclsToEmit (CIRGenModule.cpp
  // getOrCreateCIRFunction) and emit an available_externally `@vprintf` body at
  // -O>=1. The inliner would then interpose that body, turning the device call
  // into `@vfprintf`, a host stdio symbol absent from the CUDA device runtime.
  auto vprintfFuncTy =
      FuncType::get({cir::PointerType::get(sInt8Ty), voidPtrTy}, sInt32Ty);
  auto vprintfFunc = cgm.createRuntimeFunction(vprintfFuncTy, "vprintf",
                                               /*extraAttrs=*/{},
                                               /*isLocal=*/false,
                                               /*assumeConvergent=*/false,
                                               /*dontDefer=*/true);
  auto formatString = args[0].getKnownRValue().getValue();
  return builder.createCallOp(loc, vprintfFunc, {formatString, packedData})
      .getResult();
}
