//===--- CIRGenAsm.cpp - Inline Assembly Support for CIR CodeGen ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains code to emit inline assembly.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/DiagnosticSema.h"

#include "CIRGenFunction.h"
#include "clang/CIR/MissingFeatures.h"

using namespace clang;
using namespace clang::CIRGen;
using namespace cir;

/// Look at AsmExpr; if it's a register-storage-class variable with an
/// asm("...") label, append that as a register constraint. Mirrors
/// clang/lib/CodeGen/CGStmt.cpp::AddVariableConstraints. The squash assumes
/// this is a member of clang::AsmStmt (a refactor that didn't land in 22.x);
/// keep it as a file-local helper here to match the pre-refactor API.
static std::string addVariableConstraints(
    const std::string &constraint, const Expr &asmExpr,
    const TargetInfo &target,
    llvm::function_ref<void(const Stmt *, StringRef)> errorUnsupported,
    bool earlyClobber, std::string *gccReg = nullptr) {
  const auto *asmDeclRef = dyn_cast<DeclRefExpr>(&asmExpr);
  if (!asmDeclRef)
    return constraint;
  const auto *variable = dyn_cast<VarDecl>(asmDeclRef->getDecl());
  if (!variable || variable->getStorageClass() != SC_Register)
    return constraint;
  AsmLabelAttr *attr = variable->getAttr<AsmLabelAttr>();
  if (!attr)
    return constraint;
  StringRef reg = attr->getLabel();
  assert(target.isValidGCCRegisterName(reg));
  TargetInfo::ConstraintInfo info(constraint, "");
  if (target.validateOutputConstraint(info) && !info.allowsRegister()) {
    errorUnsupported(nullptr, "__asm__");
    return constraint;
  }
  reg = target.getNormalizedGCCRegisterName(reg);
  if (gccReg)
    *gccReg = reg.str();
  return (earlyClobber ? "&{" : "{") + reg.str() + "}";
}

static AsmFlavor inferFlavor(const CIRGenModule &cgm, const AsmStmt &s) {
  AsmFlavor gnuAsmFlavor =
      cgm.getCodeGenOpts().getInlineAsmDialect() == CodeGenOptions::IAD_ATT
          ? AsmFlavor::x86_att
          : AsmFlavor::x86_intel;

  return isa<MSAsmStmt>(&s) ? AsmFlavor::x86_intel : gnuAsmFlavor;
}

static void collectClobbers(const CIRGenFunction &cgf, const AsmStmt &s,
                            std::string &constraints, bool &hasUnwindClobber,
                            bool &readOnly, bool &readNone) {

  hasUnwindClobber = false;
  const CIRGenModule &cgm = cgf.getCIRGenModule();

  // Clobbers
  for (unsigned i = 0, e = s.getNumClobbers(); i != e; i++) {
    std::string clobber = s.getClobber(i);
    if (clobber == "memory") {
      readOnly = readNone = false;
    } else if (clobber == "unwind") {
      hasUnwindClobber = true;
      continue;
    } else if (clobber != "cc") {
      clobber = cgf.getTarget().getNormalizedGCCRegisterName(clobber);
      if (cgm.getCodeGenOpts().StackClashProtector &&
          cgf.getTarget().isSPRegName(clobber))
        cgm.getDiags().Report(s.getAsmLoc(),
                              diag::warn_stack_clash_protection_inline_asm);
    }

    if (isa<MSAsmStmt>(&s)) {
      if (clobber == "eax" || clobber == "edx") {
        if (constraints.find("=&A") != std::string::npos)
          continue;
        std::string::size_type position1 =
            constraints.find("={" + clobber + "}");
        if (position1 != std::string::npos) {
          constraints.insert(position1 + 1, "&");
          continue;
        }
        std::string::size_type position2 = constraints.find("=A");
        if (position2 != std::string::npos) {
          constraints.insert(position2 + 1, "&");
          continue;
        }
      }
    }
    if (!constraints.empty())
      constraints += ',';

    constraints += "~{";
    constraints += clobber;
    constraints += '}';
  }

  // Add machine specific clobbers
  std::string_view machineClobbers = cgf.getTarget().getClobbers();
  if (!machineClobbers.empty()) {
    if (!constraints.empty())
      constraints += ',';
    constraints += machineClobbers;
  }
}

/// Build the source-location list used for LLVM !srcloc metadata on inline asm.
/// Mirrors OGCG's getAsmSrcLocInfo in CGStmt.cpp.
static mlir::ArrayAttr getAsmSrcLocAttr(CIRGenFunction &cgf, const AsmStmt &s) {
  SmallVector<int64_t, 8> locs;

  const StringLiteral *asmString = nullptr;
  if (const auto *gccAsmStmt = dyn_cast<GCCAsmStmt>(&s))
    asmString = dyn_cast<StringLiteral>(gccAsmStmt->getAsmStringExpr());

  if (!asmString) {
    locs.push_back(s.getAsmLoc().getRawEncoding());
    return cgf.getBuilder().getI64ArrayAttr(locs);
  }

  // Add the location of the first line.
  locs.push_back(asmString->getBeginLoc().getRawEncoding());

  // Add the location of each subsequent line start.
  StringRef asmText = asmString->getString();
  if (!asmText.empty()) {
    const SourceManager &sm = cgf.getContext().getSourceManager();
    const LangOptions &langOpts = cgf.getLangOpts();
    unsigned startToken = 0;
    unsigned byteOffset = 0;
    for (unsigned i = 0, e = asmText.size() - 1; i != e; ++i) {
      if (asmText[i] != '\n')
        continue;
      SourceLocation lineLoc =
          asmString->getLocationOfByte(i + 1, sm, langOpts, cgf.getTarget(),
                                       &startToken, &byteOffset);
      locs.push_back(lineLoc.getRawEncoding());
    }
  }

  return cgf.getBuilder().getI64ArrayAttr(locs);
}

static void
collectInOutConstraintInfos(const CIRGenFunction &cgf, const AsmStmt &s,
                            SmallVectorImpl<TargetInfo::ConstraintInfo> &out,
                            SmallVectorImpl<TargetInfo::ConstraintInfo> &in) {

  for (unsigned i = 0, e = s.getNumOutputs(); i != e; ++i) {
    StringRef name;
    if (const GCCAsmStmt *gas = dyn_cast<GCCAsmStmt>(&s))
      name = gas->getOutputName(i);
    TargetInfo::ConstraintInfo info(s.getOutputConstraint(i), name);
    // `validateOutputConstraint` modifies the `info` object by setting the
    // read/write, clobber, allows-register, and allows-memory process.
    bool isValid = cgf.getTarget().validateOutputConstraint(info);
    (void)isValid;
    assert(isValid && "Failed to parse output constraint");
    out.push_back(info);
  }

  for (unsigned i = 0, e = s.getNumInputs(); i != e; ++i) {
    StringRef name;
    if (const GCCAsmStmt *gas = dyn_cast<GCCAsmStmt>(&s))
      name = gas->getInputName(i);
    TargetInfo::ConstraintInfo info(s.getInputConstraint(i), name);
    // `validateInputConstraint` modifies the `info` object by setting the
    // read/write, clobber, allows-register, and allows-memory process.
    bool isValid = cgf.getTarget().validateInputConstraint(out, info);
    assert(isValid && "Failed to parse input constraint");
    (void)isValid;
    in.push_back(info);
  }
}

std::pair<mlir::Value, mlir::Type> CIRGenFunction::emitAsmInputLValue(
    const TargetInfo::ConstraintInfo &info, LValue inputValue,
    QualType inputType, std::string &constraintString, SourceLocation loc) {

  if (info.allowsRegister() || !info.allowsMemory()) {
    const CIRDataLayout &layout = cgm.getDataLayout();
    if (hasScalarEvaluationKind(inputType))
      return {emitLoadOfLValue(inputValue, loc).getValue(), mlir::Type()};

    mlir::Type ty = convertType(inputType);
    uint64_t size = layout.getTypeSizeInBits(ty);
    if ((size <= 64 && llvm::isPowerOf2_64(size)) ||
        getTargetHooks().isScalarizableAsmOperand(*this, ty)) {
      ty = cir::IntType::get(&getMLIRContext(), size, false);

      return {builder.createLoad(
                  getLoc(loc),
                  inputValue.getAddress().withElementType(builder, ty)),
              mlir::Type()};
    }
  }

  Address addr = inputValue.getAddress();
  constraintString += '*';
  return {addr.getPointer(), addr.getElementType()};
}

std::pair<mlir::Value, mlir::Type>
CIRGenFunction::emitAsmInput(const TargetInfo::ConstraintInfo &info,
                             const Expr *inputExpr,
                             std::string &constraintString) {
  mlir::Location loc = getLoc(inputExpr->getExprLoc());

  // If this can't be a register or memory, i.e., has to be a constant
  // (immediate or symbolic), try to emit it as such.
  if (!info.allowsRegister() && !info.allowsMemory()) {
    if (info.requiresImmediateConstant()) {
      Expr::EvalResult evalResult;
      inputExpr->EvaluateAsRValue(evalResult, getContext(), true);

      llvm::APSInt intResult;
      if (evalResult.Val.toIntegralConstant(intResult, inputExpr->getType(),
                                            getContext()))
        return {builder.getConstInt(loc, intResult), mlir::Type()};
    }

    Expr::EvalResult result;
    if (inputExpr->EvaluateAsInt(result, getContext()))
      return {builder.getConstInt(loc, result.Val.getInt()), mlir::Type()};
  }

  if (info.allowsRegister() || !info.allowsMemory())
    if (CIRGenFunction::hasScalarEvaluationKind(inputExpr->getType()))
      return {emitScalarExpr(inputExpr), mlir::Type()};
  if (inputExpr->getStmtClass() == Expr::CXXThisExprClass)
    return {emitScalarExpr(inputExpr), mlir::Type()};
  inputExpr = inputExpr->IgnoreParenNoopCasts(getContext());
  LValue dest = emitLValue(inputExpr);
  return emitAsmInputLValue(info, dest, inputExpr->getType(), constraintString,
                            inputExpr->getExprLoc());
}

static void emitAsmStores(CIRGenFunction &cgf, const AsmStmt &s,
                          const llvm::ArrayRef<mlir::Value> regResults,
                          const llvm::ArrayRef<mlir::Type> resultRegTypes,
                          const llvm::ArrayRef<mlir::Type> resultTruncRegTypes,
                          const llvm::ArrayRef<LValue> resultRegDests,
                          const llvm::ArrayRef<QualType> resultRegQualTys,
                          const llvm::BitVector &resultTypeRequiresCast,
                          const llvm::BitVector &resultRegIsFlagReg) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  CIRGenModule &cgm = cgf.cgm;
  const CIRDataLayout &layout = cgm.getDataLayout();
  mlir::MLIRContext *ctx = builder.getContext();

  assert(regResults.size() == resultRegTypes.size());
  assert(regResults.size() == resultTruncRegTypes.size());
  assert(regResults.size() == resultRegDests.size());

  // ResultRegDests can be also populated by addReturnRegisterOutputs() above,
  // in which case its size may grow.
  assert(resultTypeRequiresCast.size() <= resultRegDests.size());
  assert(resultRegIsFlagReg.size() <= resultRegDests.size());

  for (unsigned i = 0, e = regResults.size(); i != e; ++i) {
    mlir::Value tmp = regResults[i];
    mlir::Type truncTy = resultTruncRegTypes[i];

    // For flag register outputs (e.g., ={@cca}), the result is always 0 or 1.
    // Emit cir.assume(tmp < 2) to tell the optimizer the value is boolean.
    // Matches OGCG's llvm.assume emission at CGStmt.cpp:2739-2748.
    if (i < resultRegIsFlagReg.size() && resultRegIsFlagReg[i]) {
      auto intTy = mlir::dyn_cast<cir::IntType>(tmp.getType());
      if (intTy) {
        mlir::Location loc = cgf.getLoc(s.getAsmLoc());
        // Use unsigned comparison (ult) to match OGCG (ICMP_ULT).
        auto uintTy = cir::IntType::get(intTy.getContext(), intTy.getWidth(),
                                        /*isSigned=*/false);
        mlir::Value utmp = builder.createIntCast(tmp, uintTy);
        mlir::Value upperBound =
            builder.getConstantInt(loc, uintTy, 2);
        mlir::Value isBool = cir::CmpOp::create(
            builder, loc, builder.getBoolTy(), cir::CmpOpKind::lt, utmp,
            upperBound);
        cir::AssumeOp::create(builder, loc, isBool);
      }
    }

    // If the result type of the LLVM IR asm doesn't match the result type of
    // the expression, do the conversion.
    if (resultRegTypes[i] != truncTy) {

      // Truncate the integer result to the right size, note that TruncTy can be
      // a pointer.
      if (mlir::isa<cir::FPTypeInterface>(truncTy)) {
        tmp = builder.createFloatingCast(tmp, truncTy);
      } else if (isa<cir::PointerType>(truncTy) &&
                 isa<cir::IntType>(tmp.getType())) {
        uint64_t resSize = layout.getTypeSizeInBits(truncTy);
        tmp = builder.createIntCast(
            tmp, cir::IntType::get(ctx, (unsigned)resSize, false));
        tmp = builder.createIntToPtr(tmp, truncTy);
      } else if (isa<cir::PointerType>(tmp.getType()) &&
                 isa<cir::IntType>(truncTy)) {
        uint64_t tmpSize = layout.getTypeSizeInBits(tmp.getType());
        tmp = builder.createPtrToInt(
            tmp, cir::IntType::get(ctx, (unsigned)tmpSize, false));
        tmp = builder.createIntCast(tmp, truncTy);
      } else if (isa<cir::IntType>(tmp.getType()) &&
                 isa<cir::IntType>(truncTy)) {
        tmp = builder.createIntCast(tmp, truncTy);
      } else if (isa<cir::VectorType>(tmp.getType()) ||
                 isa<cir::VectorType>(truncTy)) {
        tmp = builder.createBitcast(tmp, truncTy);
      }
    }

    LValue dest = resultRegDests[i];
    // ResultTypeRequiresCast elements correspond to the first
    // ResultTypeRequiresCast.size() elements of RegResults.
    if ((i < resultTypeRequiresCast.size()) && resultTypeRequiresCast[i]) {
      unsigned size = cgf.getContext().getTypeSize(resultRegQualTys[i]);
      Address addr =
          dest.getAddress().withElementType(builder, resultRegTypes[i]);
      if (cgm.getTargetCIRGenInfo().isScalarizableAsmOperand(cgf, truncTy)) {
        builder.createStore(cgf.getLoc(s.getAsmLoc()), tmp, addr);
        continue;
      }

      QualType ty =
          cgf.getContext().getIntTypeForBitwidth(size, /*Signed=*/false);
      if (ty.isNull()) {
        const Expr *outExpr = s.getOutputExpr(i);
        cgm.getDiags().Report(outExpr->getExprLoc(),
                              diag::err_store_value_to_reg);
        return;
      }
      dest = cgf.makeAddrLValue(addr, ty);
    }

    cgf.emitStoreThroughLValue(RValue::get(tmp), dest);
  }
}

mlir::LogicalResult CIRGenFunction::emitAsmStmt(const AsmStmt &s) {
  ApplySourceAtomGroup atomGroup(*this);

  // Assemble the final asm string.
  std::string asmString = s.generateAsmString(getContext());
  SourceLocation srcLoc = s.getAsmLoc();
  mlir::Location loc = getLoc(srcLoc);
  mlir::ArrayAttr asmSrcLocAttr = getAsmSrcLocAttr(*this, s);

  // OGCG creates a RunCleanupsScope so temporaries from asm operand emission
  // are destroyed at statement end.
  // CIR emits operand-side effects/scopes directly and does not have a
  // separate RunCleanupsScope wrapper object for this statement.

  // Get all the output and input constraints together.
  SmallVector<TargetInfo::ConstraintInfo> outputConstraintInfos;
  SmallVector<TargetInfo::ConstraintInfo> inputConstraintInfos;
  collectInOutConstraintInfos(*this, s, outputConstraintInfos,
                              inputConstraintInfos);

  bool isGCCAsmGoto = false;
  // Owns the (possibly mangled) label names; getBlockAddressLabelName returns a
  // temporary std::string for GNU-local labels, so a StringRef vector would
  // dangle.
  SmallVector<std::string> gotoLabelNames;

  // Detect asm goto and collect label names + constraints.
  if (const auto *gs = dyn_cast<GCCAsmStmt>(&s)) {
    isGCCAsmGoto = gs->isAsmGoto();
  }

  std::string constraints;
  SmallVector<LValue> resultRegDests;
  SmallVector<QualType> resultRegQualTys;
  SmallVector<mlir::Type> resultRegTypes;
  SmallVector<mlir::Type> resultTruncRegTypes;
  SmallVector<mlir::Type> argTypes;
  SmallVector<mlir::Type> argElemTypes;
  SmallVector<mlir::Value> args;
  SmallVector<mlir::Value> outArgs;
  SmallVector<mlir::Value> inArgs;
  SmallVector<mlir::Value> inOutArgs;
  llvm::BitVector resultTypeRequiresCast;
  llvm::BitVector resultRegIsFlagReg;

  // Keep track of input constraints.
  std::string inOutConstraints;
  SmallVector<mlir::Type> inOutArgTypes;
  SmallVector<mlir::Type> inOutArgElemTypes;

  // Keep track of out constraints for tied input operand.
  SmallVector<std::string> outputConstraints;

  // Keep track of defined physregs.
  llvm::SmallSet<std::string, 8> physRegOutputs;

  // An inline asm can be marked readonly if it meets the following conditions:
  //  - it doesn't have any sideeffects
  //  - it doesn't clobber memory
  //  - it doesn't return a value by-reference
  // It can be marked readnone if it doesn't have any input memory constraints
  // in addition to meeting the conditions listed above.
  bool readOnly = true, readNone = true;

  std::string outputConstraint;
  for (unsigned i = 0, e = s.getNumOutputs(); i != e; ++i) {
    TargetInfo::ConstraintInfo &info = outputConstraintInfos[i];

    // Simplify the output constraint.
    outputConstraint = s.getOutputConstraint(i);
    outputConstraint = getTarget().simplifyConstraint(
        StringRef(outputConstraint).drop_front());

    const Expr *outExpr = s.getOutputExpr(i);
    outExpr = outExpr->IgnoreParenNoopCasts(getContext());

    std::string gccReg;
    outputConstraint = addVariableConstraints(
        outputConstraint, *outExpr, getTarget(),
        [&](const Stmt *unspStmt, StringRef msg) {
          cgm.errorUnsupported(unspStmt ? unspStmt : &s, msg);
        },
        info.earlyClobber(), &gccReg);

    // Give an error on multiple outputs to same physreg.
    if (!gccReg.empty() && !physRegOutputs.insert(gccReg).second)
      cgm.error(srcLoc, "multiple outputs to hard register: " + gccReg);

    outputConstraints.push_back(outputConstraint);
    LValue dest = emitLValue(outExpr);

    if (!constraints.empty())
      constraints += ',';

    // If this is a register output, then make the inline a sm return it
    // by-value.  If this is a memory result, return the value by-reference.
    QualType qty = outExpr->getType();
    const bool isScalarOrAggregate =
        hasScalarEvaluationKind(qty) || hasAggregateEvaluationKind(qty);
    if (!info.allowsMemory() && isScalarOrAggregate) {
      constraints += "=" + outputConstraint;
      resultRegQualTys.push_back(qty);
      resultRegDests.push_back(dest);

      bool isFlagReg = llvm::StringRef(outputConstraint).starts_with("{@cc");
      resultRegIsFlagReg.push_back(isFlagReg);

      mlir::Type ty = convertTypeForMem(qty);
      if (isFlagReg && qty->isBooleanType()) {
        unsigned storageBits = getContext().getTypeSize(qty);
        ty = cir::IntType::get(&getMLIRContext(), storageBits,
                               /*isSigned=*/false);
      }
      const bool requiresCast =
          info.allowsRegister() &&
          (cgm.getTargetCIRGenInfo().isScalarizableAsmOperand(*this, ty) ||
           isa<cir::RecordType, cir::ArrayType>(ty));

      resultTruncRegTypes.push_back(ty);
      resultTypeRequiresCast.push_back(requiresCast);

      if (requiresCast) {
        unsigned size = getContext().getTypeSize(qty);
        if (size == 0) {
          cgm.error(outExpr->getExprLoc(), "output size should not be zero");
          return mlir::failure();
        }

        ty = cir::IntType::get(&getMLIRContext(), size, false);
      }

      resultRegTypes.push_back(ty);
      // If this output is tied to an input, and if the input is larger, then
      // we need to set the actual result type of the inline asm node to be the
      // same as the input type.
      if (info.hasMatchingInput()) {
        unsigned inputNo;
        for (inputNo = 0; inputNo != s.getNumInputs(); ++inputNo) {
          TargetInfo::ConstraintInfo &input = inputConstraintInfos[inputNo];
          if (input.hasTiedOperand() && input.getTiedOperand() == i)
            break;
        }
        assert(inputNo != s.getNumInputs() && "Didn't find matching input!");

        QualType inputTy = s.getInputExpr(inputNo)->getType();
        QualType outputType = outExpr->getType();

        uint64_t inputSize = getContext().getTypeSize(inputTy);
        if (getContext().getTypeSize(outputType) < inputSize) {
          // Form the asm to return the value as a larger integer or fp type.
          resultRegTypes.back() = convertType(inputTy);
        }
      }

      if (mlir::Type adjTy = cgm.getTargetCIRGenInfo().adjustInlineAsmType(
              *this, outputConstraint, resultRegTypes.back()))
        resultRegTypes.back() = adjTy;
      else
        cgm.getDiags().Report(srcLoc, diag::err_asm_invalid_type_in_input)
            << outExpr->getType() << outputConstraint;

      // Update largest vector width for any vector types.
      // Matches OGCG CGStmt.cpp LargestVectorWidth tracking for outputs.
      if (auto vt = mlir::dyn_cast<cir::VectorType>(resultRegTypes.back()))
        if (!vt.getIsScalable())
          largestVectorWidth = std::max(
              (uint64_t)largestVectorWidth,
              cgm.getDataLayout().getTypeSizeInBits(vt)
                  .getKnownMinValue());
    } else {
      Address destAddr = dest.getAddress();

      // Matrix types in memory are represented by arrays, but accessed through
      // vector pointers, with the alignment specified on the access operation.
      // For inline assembly, update pointer arguments to use vector pointers.
      // Otherwise there will be a mis-match if the matrix is also an
      // input-argument which is represented as vector.
      if (isa<MatrixType>(outExpr->getType().getCanonicalType()))
        destAddr =
            destAddr.withElementType(builder, convertType(outExpr->getType()));

      argTypes.push_back(destAddr.getType());
      argElemTypes.push_back(destAddr.getElementType());
      outArgs.push_back(destAddr.getPointer());
      args.push_back(destAddr.getPointer());
      constraints += "=*";
      constraints += outputConstraint;
      readOnly = readNone = false;
    }

    if (info.isReadWrite()) {
      inOutConstraints += ',';
      const Expr *inputExpr = s.getOutputExpr(i);

      // argValue: mlir::Value, argElementType: mlir::Type.
      auto [argValue, argElementType] =
          emitAsmInputLValue(info, dest, inputExpr->getType(), inOutConstraints,
                             inputExpr->getExprLoc());

      if (mlir::Type adjTy = getTargetHooks().adjustInlineAsmType(
              *this, outputConstraint, argValue.getType()))
        argValue = builder.createBitcast(argValue, adjTy);

      // Update largest vector width for any vector types (tied output arg).
      if (auto vt = mlir::dyn_cast<cir::VectorType>(argValue.getType()))
        if (!vt.getIsScalable())
          largestVectorWidth = std::max(
              (uint64_t)largestVectorWidth,
              cgm.getDataLayout().getTypeSizeInBits(vt)
                  .getKnownMinValue());

      // Only tie earlyclobber physregs.
      if (info.allowsRegister() && (gccReg.empty() || info.earlyClobber()))
        inOutConstraints += llvm::utostr(i);
      else
        inOutConstraints += outputConstraint;

      inOutArgTypes.push_back(argValue.getType());
      inOutArgElemTypes.push_back(argElementType);
      inOutArgs.push_back(argValue);
    }

  } // iterate over output operands

  for (unsigned i = 0, e = s.getNumInputs(); i != e; ++i) {
    TargetInfo::ConstraintInfo &info = inputConstraintInfos[i];
    const Expr *inputExpr = s.getInputExpr(i);

    if (info.allowsMemory())
      readNone = false;

    if (!constraints.empty())
      constraints += ',';

    std::string inputConstraint(s.getInputConstraint(i));
    inputConstraint =
        getTarget().simplifyConstraint(inputConstraint, &outputConstraintInfos);

    inputConstraint = addVariableConstraints(
        inputConstraint, *inputExpr->IgnoreParenNoopCasts(getContext()),
        getTarget(),
        [&](const Stmt *unspStmt, StringRef msg) {
          cgm.errorUnsupported(unspStmt ? unspStmt : &s, msg);
        },
        /*EarlyClobber=*/false);

    std::string replaceConstraint(inputConstraint);
    // argValue: mlir::Value, argElementType: mlir::Type.
    auto [argValue, argElemType] = emitAsmInput(info, inputExpr, constraints);

    // If this input argument is tied to a larger output result, extend the
    // input to be the same size as the output.  The LLVM backend wants to see
    // the input and output of a matching constraint be the same size.  Note
    // that GCC does not define what the top bits are here.  We use zext because
    // that is usually cheaper, but LLVM IR should really get an anyext someday.
    if (info.hasTiedOperand()) {
      unsigned output = info.getTiedOperand();
      QualType outputType = s.getOutputExpr(output)->getType();
      QualType inputTy = inputExpr->getType();

      if (getContext().getTypeSize(outputType) >
          getContext().getTypeSize(inputTy)) {
        auto zeroExtendInteger = [&](mlir::Value value) -> mlir::Value {
          auto intTy = mlir::dyn_cast<cir::IntType>(value.getType());
          if (!intTy || intTy.isUnsigned())
            return value;
          return builder.createIntCast(
              value, cir::IntType::get(&getMLIRContext(), intTy.getWidth(),
                                       /*isSigned=*/false));
        };

        // Use ptrtoint as appropriate so that we can do our extension.
        if (isa<cir::PointerType>(argValue.getType()))
          argValue = builder.createPtrToInt(argValue, uIntPtrTy);
        else
          argValue = zeroExtendInteger(argValue);
        mlir::Type outputTy = convertType(outputType);
        if (isa<cir::IntType>(outputTy))
          argValue = builder.createIntCast(argValue, outputTy);
        else if (isa<cir::PointerType>(outputTy))
          argValue = builder.createIntCast(argValue, uIntPtrTy);
        else if (isa<cir::FPTypeInterface>(outputTy))
          argValue = builder.createFloatingCast(argValue, outputTy);
      }

      // Deal with the tied operands' constraint code in adjustInlineAsmType.
      replaceConstraint = outputConstraints[output];
    }

    if (mlir::Type adjTy = getTargetHooks().adjustInlineAsmType(
            *this, replaceConstraint, argValue.getType()))
      argValue = builder.createBitcast(argValue, adjTy);
    else
      cgm.getDiags().Report(s.getAsmLoc(), diag::err_asm_invalid_type_in_input)
          << inputExpr->getType() << inputConstraint;

    // Update largest vector width for any vector types (input operand).
    if (auto vt = mlir::dyn_cast<cir::VectorType>(argValue.getType()))
      if (!vt.getIsScalable())
        largestVectorWidth = std::max(
            (uint64_t)largestVectorWidth,
            cgm.getDataLayout().getTypeSizeInBits(vt)
                .getKnownMinValue());

    argTypes.push_back(argValue.getType());
    argElemTypes.push_back(argElemType);
    inArgs.push_back(argValue);
    args.push_back(argValue);
    constraints += inputConstraint;
  } // iterate over input operands

  // Append the "input" part of inout constraints.
  for (unsigned i = 0, e = inOutArgs.size(); i != e; ++i) {
    args.push_back(inOutArgs[i]);
    argTypes.push_back(inOutArgTypes[i]);
    argElemTypes.push_back(inOutArgElemTypes[i]);
  }
  constraints += inOutConstraints;

  // For asm goto, add label constraints (!i) and collect label names.
  // Labels must come before clobbers in the constraint string.
  if (isGCCAsmGoto) {
    const auto *gs = cast<GCCAsmStmt>(&s);
    for (const auto *e : gs->labels()) {
      // Use the same name mangling as emitLabel / cir.goto
      // (getBlockAddressLabelName) so GNU-local labels (`__label__ x`) match the
      // cir.label op name (`x$<id>`) when GotoSolver resolves successors.
      gotoLabelNames.push_back(cgm.getBlockAddressLabelName(*e->getLabel()));
      if (!constraints.empty())
        constraints += ',';
      constraints += "!i";
    }
  }

  bool hasUnwindClobber = false;
  collectClobbers(*this, s, constraints, hasUnwindClobber, readOnly, readNone);

  assert(!(hasUnwindClobber && isGCCAsmGoto) &&
         "unwind clobber can't be used with asm goto");

  std::array<mlir::ValueRange, 3> operands = {outArgs, inArgs, inOutArgs};

  mlir::Type resultType;

  if (resultRegTypes.size() == 1)
    resultType = resultRegTypes[0];
  else if (resultRegTypes.size() > 1)
    resultType = builder.getAnonRecordTy(resultRegTypes, /*packed=*/false,
                                         /*padded=*/false);

  bool hasSideEffect = s.isVolatile() || s.getNumOutputs() == 0;

  // For Microsoft-style asm blobs, conservatively mirror OGCG's SawAsmBlock
  // behavior so implicit-return handling does not assume the return register is
  // untouched after inline asm.
  if (isa<MSAsmStmt>(&s))
    sawAsmBlock = true;

  std::vector<mlir::Value> regResults;

  if (isGCCAsmGoto) {
    // Emit cir.asm.goto — a terminator with goto label references.

    llvm::SmallVector<mlir::Attribute> operandAttrs;
    int i = 0;
    for (auto typ : argElemTypes) {
      if (typ) {
        auto op = args[i];
        assert(mlir::isa<cir::PointerType>(op.getType()) &&
               "pointer type expected");
        assert(cast<cir::PointerType>(op.getType()).getPointee() == typ &&
               "element type differs from pointee type!");
        operandAttrs.push_back(mlir::UnitAttr::get(&getMLIRContext()));
      } else {
        operandAttrs.push_back(
            mlir::DictionaryAttr::get(&getMLIRContext()));
      }
      i++;
    }

    // Convert label names to ArrayAttr.
    SmallVector<mlir::Attribute> labelAttrs;
    for (const auto &name : gotoLabelNames)
      labelAttrs.push_back(cgm.getStringAttrCached(name));

    auto asmGotoOp = cir::InlineAsmGotoOp::create(
        builder, getLoc(s.getAsmLoc()), resultType, operands, asmString,
        constraints, hasSideEffect, inferFlavor(cgm, s),
        builder.getArrayAttr(operandAttrs),
        builder.getArrayAttr(labelAttrs),
        /*targets=*/mlir::BlockRange{});

    // Attach readnone/readonly memory effects. OGCG sets these in
    // UpdateAsmCallInst (CGStmt.cpp:2659-2665) for asm goto as well.
    if (!hasSideEffect) {
      if (readNone)
        asmGotoOp->setAttr("cir.asm_readnone",
                           mlir::UnitAttr::get(&getMLIRContext()));
      else if (readOnly)
        asmGotoOp->setAttr("cir.asm_readonly",
                           mlir::UnitAttr::get(&getMLIRContext()));
    }

    // Propagate [[clang::nomerge]] to asm goto.
    // Matches OGCG UpdateAsmCallInst (CGStmt.cpp:2657-2658).
    if (inNoMergeAttributedStmt)
      asmGotoOp->setAttr("cir.nomerge",
                          mlir::UnitAttr::get(&getMLIRContext()));

    // Forward backend diagnostic locations as !srcloc metadata.
    asmGotoOp->setAttr("cir.asm_srcloc", asmSrcLocAttr);

    // Mark asm goto as convergent in CUDA/OpenCL modes.
    // Matches OGCG UpdateAsmCallInst (CGStmt.cpp:2696-2701).
    if (!inNoConvergentAttributedStmt &&
        getLangOpts().assumeFunctionsAreConvergent()) {
      auto convgt = cir::ConvergentAttr::get(&getMLIRContext());
      asmGotoOp->setAttr(convgt.getMnemonic(), convgt);
    }

    // asm.goto is a terminator; create a new block for fallthrough code.
    // Insert it directly after the asm.goto block so that GotoSolver's
    // getNextNode() finds the correct fallthrough block. Without this,
    // the indirectGotoBlock (created by &&label) may sit between the
    // asm.goto and its fallthrough, causing GotoSolver to wire the wrong
    // block as the fallthrough successor.
    mlir::Block *asmBlock = builder.getBlock();
    builder.createBlock(asmBlock->getParent(),
                        std::next(mlir::Region::iterator(asmBlock)));

    // Extract register output results and emit stores in the fallthrough
    // block, matching the non-goto pattern. OGCG duplicates these stores in
    // split blocks for indirect destinations; we handle that at LLVM lowering
    // time in convertInlineAsmGotoOp.
    mlir::Value result;
    if (asmGotoOp.getNumResults())
      result = asmGotoOp.getResult(0);

    if (resultRegTypes.size() == 1) {
      regResults.push_back(result);
    } else if (resultRegTypes.size() > 1) {
      CharUnits alignment = CharUnits::One();
      mlir::Value dest =
          emitAlloca("__asm_result", resultType, loc, alignment, false);
      Address addr = Address(dest, alignment);
      builder.createStore(loc, result, addr);

      for (unsigned i = 0, e = resultRegTypes.size(); i != e; ++i) {
        cir::PointerType typ = builder.getPointerTo(resultRegTypes[i]);
        cir::GetMemberOp ptr =
            builder.createGetMember(loc, typ, dest, "", i);
        cir::LoadOp tmp = builder.createLoad(loc, Address(ptr, alignment));
        regResults.push_back(tmp);
      }
    }
  } else {
    // Build operand_attrs — mark pointer operands that have element types.
    // This mirrors the asm goto path (lines 598-612 above).
    llvm::SmallVector<mlir::Attribute> operandAttrs;
    int i = 0;
    for (auto typ : argElemTypes) {
      if (typ) {
        auto op = args[i];
        assert(mlir::isa<cir::PointerType>(op.getType()) &&
               "pointer type expected");
        operandAttrs.push_back(mlir::UnitAttr::get(&getMLIRContext()));
      } else {
        operandAttrs.push_back(
            mlir::DictionaryAttr::get(&getMLIRContext()));
      }
      i++;
    }

    cir::InlineAsmOp ia = cir::InlineAsmOp::create(
        builder, getLoc(s.getAsmLoc()), resultType, operands, asmString,
        constraints, hasSideEffect, inferFlavor(cgm, s),
        builder.getArrayAttr(operandAttrs));

    // OGCG attaches !srcloc metadata for backend asm diagnostics.
    ia->setAttr("cir.asm_srcloc", asmSrcLocAttr);

    // Propagate [[clang::nomerge]] to the asm call.
    // Matches OGCG UpdateAsmCallInst (CGStmt.cpp:2657-2658).
    if (inNoMergeAttributedStmt)
      ia->setAttr("cir.nomerge", mlir::UnitAttr::get(&getMLIRContext()));

    // Conservatively mark all inline asm as convergent in CUDA/OpenCL modes,
    // unless inside [[clang::noconvergent]].
    // Matches OGCG UpdateAsmCallInst (CGStmt.cpp:2696-2701).
    if (!inNoConvergentAttributedStmt &&
        getLangOpts().assumeFunctionsAreConvergent()) {
      auto convgt = cir::ConvergentAttr::get(&getMLIRContext());
      ia->setAttr(convgt.getMnemonic(), convgt);
    }
    // OGCG passes operand bundles for Windows SEH/funclet exception handling.
    // CIR currently rejects funclet-pad EH personalities earlier in EH setup.

    if (hasUnwindClobber) {
      assert(!cir::MissingFeatures::asmUnwindClobber());
    } else {
      // Attach readnone/readonly memory effects. OGCG sets these in
      // UpdateAsmCallInst (CGStmt.cpp:2659-2665) — if the asm has no side
      // effects and no memory operands, it gets memory(none); if it has
      // memory inputs but no memory outputs/clobber, it gets memory(read).
      if (!hasSideEffect) {
        if (readNone)
          ia->setAttr("cir.asm_readnone",
                      mlir::UnitAttr::get(&getMLIRContext()));
        else if (readOnly)
          ia->setAttr("cir.asm_readonly",
                      mlir::UnitAttr::get(&getMLIRContext()));
      }

      mlir::Value result;
      if (ia.getNumResults())
        result = ia.getResult(0);

      llvm::SmallVector<mlir::Attribute> operandAttrs;

      int i = 0;
      for (auto typ : argElemTypes) {
        if (typ) {
          auto op = args[i];
          assert(mlir::isa<cir::PointerType>(op.getType()) &&
                 "pointer type expected");
          assert(cast<cir::PointerType>(op.getType()).getPointee() == typ &&
                 "element type differs from pointee type!");

          operandAttrs.push_back(mlir::UnitAttr::get(&getMLIRContext()));
        } else {
          // We need to add an attribute for every arg since later, during
          // the lowering to LLVM IR the attributes will be assigned to the
          // CallInsn argument by index, i.e. we can't skip null type here.
          // The placeholder is an empty dictionary and not a null attribute:
          // an ArrayAttr holding a null cannot be printed or serialized.
          operandAttrs.push_back(
              mlir::DictionaryAttr::get(&getMLIRContext()));
        }
        i++;
      }
      assert(args.size() == operandAttrs.size() &&
             "The number of attributes is not even with the number of operands");

      ia.setOperandAttrsAttr(builder.getArrayAttr(operandAttrs));

      if (resultRegTypes.size() == 1) {
        regResults.push_back(result);
      } else if (resultRegTypes.size() > 1) {
        CharUnits alignment = CharUnits::One();
        mlir::Value dest =
            emitAlloca("__asm_result", resultType, loc, alignment, false);
        Address addr = Address(dest, alignment);
        builder.createStore(loc, result, addr);

        for (unsigned i = 0, e = resultRegTypes.size(); i != e; ++i) {
          cir::PointerType typ = builder.getPointerTo(resultRegTypes[i]);
          cir::GetMemberOp ptr =
              builder.createGetMember(loc, typ, dest, "", i);
          cir::LoadOp tmp = builder.createLoad(loc, Address(ptr, alignment));
          regResults.push_back(tmp);
        }
      }
    }
  }

  // Emit stores for register output results. For asm goto, the stores go in
  // the fallthrough block; OGCG also stores in indirect destinations via split
  // blocks, which we handle at LLVM lowering time.
  emitAsmStores(*this, s, regResults, resultRegTypes, resultTruncRegTypes,
                resultRegDests, resultRegQualTys, resultTypeRequiresCast,
                resultRegIsFlagReg);

  return mlir::success();
}
