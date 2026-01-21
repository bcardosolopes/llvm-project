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
#include "llvm/Support/ErrorHandling.h"

using namespace clang;
using namespace clang::CIRGen;
using namespace cir;

mlir::Value CIRGenFunction::emitNVPTXBuiltinExpr(unsigned builtinId,
                                                 const CallExpr *expr) {
  switch (builtinId) {
  case NVPTX::BI__nvvm_barrier_cluster_arrive:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               builder.getStringAttr("nvvm.barrier.cluster.arrive"),
               builder.getVoidTy())
        .getResult();
  case NVPTX::BI__nvvm_barrier_cluster_arrive_relaxed:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               builder.getStringAttr("nvvm.barrier.cluster.arrive.relaxed"),
               builder.getVoidTy())
        .getResult();
  case NVPTX::BI__nvvm_barrier_cluster_wait:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               builder.getStringAttr("nvvm.barrier.cluster.wait"),
               builder.getVoidTy())
        .getResult();
  case NVPTX::BI__nvvm_fence_sc_cluster:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               builder.getStringAttr("nvvm.fence.sc.cluster"),
               builder.getVoidTy(), mlir::ValueRange{})
        .getResult();
  case NVPTX::BI__nvvm_bar_sync:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               builder.getStringAttr("nvvm.barrier.cta.sync.aligned.all"),
               builder.getVoidTy(),
               mlir::ValueRange{emitScalarExpr(expr->getArg(0))})
        .getResult();
  case NVPTX::BI__syncthreads:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               builder.getStringAttr("nvvm.barrier.cta.sync.aligned.all"),
               builder.getVoidTy(),
               mlir::ValueRange{builder.getConstInt(getLoc(expr->getExprLoc()),
                                                    sInt32Ty, 0)})
        .getResult();
  case NVPTX::BI__nvvm_barrier_sync:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               builder.getStringAttr("nvvm.barrier.cta.sync.all"),
               builder.getVoidTy(),
               mlir::ValueRange{emitScalarExpr(expr->getArg(0))})
        .getResult();
  case NVPTX::BI__nvvm_barrier_sync_cnt:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               builder.getStringAttr("nvvm.barrier.cta.sync.count"),
               builder.getVoidTy(),
               mlir::ValueRange{emitScalarExpr(expr->getArg(0)),
                                emitScalarExpr(expr->getArg(1))})
        .getResult();
  case NVPTX::BI__nvvm_bar_warp_sync:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               builder.getStringAttr("nvvm.bar.warp.sync"), builder.getVoidTy(),
               mlir::ValueRange{emitScalarExpr(expr->getArg(0))})
        .getResult();
  case NVPTX::BI__nvvm_fmax_f: {
    mlir::Value arg0 = emitScalarExpr(expr->getArg(0));
    mlir::Value arg1 = emitScalarExpr(expr->getArg(1));
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               builder.getStringAttr("nvvm.fmax.f"), arg0.getType(),
               mlir::ValueRange{arg0, arg1})
        .getResult();
  }
  case NVPTX::BI__nvvm_fmin_f: {
    mlir::Value arg0 = emitScalarExpr(expr->getArg(0));
    mlir::Value arg1 = emitScalarExpr(expr->getArg(1));
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               builder.getStringAttr("nvvm.fmin.f"), arg0.getType(),
               mlir::ValueRange{arg0, arg1})
        .getResult();
  }
  case NVPTX::BI__nvvm_sqrt_rn_f: {
    mlir::Value arg0 = emitScalarExpr(expr->getArg(0));
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               builder.getStringAttr("nvvm.sqrt.rn.f"), arg0.getType(),
               mlir::ValueRange{arg0})
        .getResult();
  }
  case NVPTX::BI__nvvm_rcp_rn_f: {
    mlir::Value arg0 = emitScalarExpr(expr->getArg(0));
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               builder.getStringAttr("nvvm.rcp.rn.f"), arg0.getType(),
               mlir::ValueRange{arg0})
        .getResult();
  }
  case NVPTX::BI__nvvm_add_rn_f: {
    mlir::Value arg0 = emitScalarExpr(expr->getArg(0));
    mlir::Value arg1 = emitScalarExpr(expr->getArg(1));
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               builder.getStringAttr("nvvm.add.rn.f"), arg0.getType(),
               mlir::ValueRange{arg0, arg1})
        .getResult();
  }
  case NVPTX::BI__nvvm_fmax_d: {
    mlir::Value arg0 = emitScalarExpr(expr->getArg(0));
    mlir::Value arg1 = emitScalarExpr(expr->getArg(1));
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               builder.getStringAttr("nvvm.fmax.d"), arg0.getType(),
               mlir::ValueRange{arg0, arg1})
        .getResult();
  }
  case NVPTX::BI__nvvm_fmin_d: {
    mlir::Value arg0 = emitScalarExpr(expr->getArg(0));
    mlir::Value arg1 = emitScalarExpr(expr->getArg(1));
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               builder.getStringAttr("nvvm.fmin.d"), arg0.getType(),
               mlir::ValueRange{arg0, arg1})
        .getResult();
  }
  case NVPTX::BI__nvvm_sqrt_rn_d: {
    mlir::Value arg0 = emitScalarExpr(expr->getArg(0));
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               builder.getStringAttr("nvvm.sqrt.rn.d"), arg0.getType(),
               mlir::ValueRange{arg0})
        .getResult();
  }
  case NVPTX::BI__nvvm_rcp_rn_d: {
    mlir::Value arg0 = emitScalarExpr(expr->getArg(0));
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               builder.getStringAttr("nvvm.rcp.rn.d"), arg0.getType(),
               mlir::ValueRange{arg0})
        .getResult();
  }
  case NVPTX::BI__nvvm_mulhi_i: {
    mlir::Value arg0 = emitScalarExpr(expr->getArg(0));
    mlir::Value arg1 = emitScalarExpr(expr->getArg(1));
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               builder.getStringAttr("nvvm.mulhi.i"), arg0.getType(),
               mlir::ValueRange{arg0, arg1})
        .getResult();
  }
  case NVPTX::BI__nvvm_membar_cta:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               builder.getStringAttr("nvvm.membar.cta"), builder.getVoidTy())
        .getResult();
  case NVPTX::BI__nvvm_membar_gl:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               builder.getStringAttr("nvvm.membar.gl"), builder.getVoidTy())
        .getResult();
  case NVPTX::BI__nvvm_membar_sys:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               builder.getStringAttr("nvvm.membar.sys"), builder.getVoidTy())
        .getResult();
  // Special register reads: threadIdx, blockIdx, blockDim, gridDim
  case NVPTX::BI__nvvm_read_ptx_sreg_tid_x:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               builder.getStringAttr("nvvm.read.ptx.sreg.tid.x"), sInt32Ty)
        .getResult();
  case NVPTX::BI__nvvm_read_ptx_sreg_tid_y:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               builder.getStringAttr("nvvm.read.ptx.sreg.tid.y"), sInt32Ty)
        .getResult();
  case NVPTX::BI__nvvm_read_ptx_sreg_tid_z:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               builder.getStringAttr("nvvm.read.ptx.sreg.tid.z"), sInt32Ty)
        .getResult();
  case NVPTX::BI__nvvm_read_ptx_sreg_ctaid_x:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               builder.getStringAttr("nvvm.read.ptx.sreg.ctaid.x"), sInt32Ty)
        .getResult();
  case NVPTX::BI__nvvm_read_ptx_sreg_ctaid_y:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               builder.getStringAttr("nvvm.read.ptx.sreg.ctaid.y"), sInt32Ty)
        .getResult();
  case NVPTX::BI__nvvm_read_ptx_sreg_ctaid_z:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               builder.getStringAttr("nvvm.read.ptx.sreg.ctaid.z"), sInt32Ty)
        .getResult();
  case NVPTX::BI__nvvm_read_ptx_sreg_ntid_x:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               builder.getStringAttr("nvvm.read.ptx.sreg.ntid.x"), sInt32Ty)
        .getResult();
  case NVPTX::BI__nvvm_read_ptx_sreg_ntid_y:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               builder.getStringAttr("nvvm.read.ptx.sreg.ntid.y"), sInt32Ty)
        .getResult();
  case NVPTX::BI__nvvm_read_ptx_sreg_ntid_z:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               builder.getStringAttr("nvvm.read.ptx.sreg.ntid.z"), sInt32Ty)
        .getResult();
  case NVPTX::BI__nvvm_read_ptx_sreg_nctaid_x:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               builder.getStringAttr("nvvm.read.ptx.sreg.nctaid.x"), sInt32Ty)
        .getResult();
  case NVPTX::BI__nvvm_read_ptx_sreg_nctaid_y:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               builder.getStringAttr("nvvm.read.ptx.sreg.nctaid.y"), sInt32Ty)
        .getResult();
  case NVPTX::BI__nvvm_read_ptx_sreg_nctaid_z:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               builder.getStringAttr("nvvm.read.ptx.sreg.nctaid.z"), sInt32Ty)
        .getResult();
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
  auto vprintfFuncTy =
      FuncType::get({cir::PointerType::get(sInt8Ty), voidPtrTy}, sInt32Ty);
  auto vprintfFunc = cgm.createRuntimeFunction(vprintfFuncTy, "vprintf");
  auto formatString = args[0].getKnownRValue().getValue();
  return builder.createCallOp(loc, vprintfFunc, {formatString, packedData})
      .getResult();
}
