//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This contains code to emit Expr nodes as CIR code.
//
//===----------------------------------------------------------------------===//

#include "Address.h"
#include "CIRGenCUDARuntime.h"
#include "CIRGenCXXABI.h"
#include "CIRGenCleanup.h"
#include "CIRGenConstantEmitter.h"
#include "CIRGenFunction.h"
#include "CIRGenSanitizer.h"
#include "CIRGenModule.h"
#include "CIRGenValue.h"
#include "mlir/Dialect/OpenMP/OpenMPDialect.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/IR/Value.h"
#include "clang/AST/Attr.h"
#include "clang/AST/CharUnits.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/InferAlloc.h"
#include "clang/AST/GlobalDecl.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Basic/AddressSpaces.h"
#include "clang/Basic/Linkage.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/CIR/Dialect/IR/CIRAttrs.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/IR/CIRTypes.h"
#include "clang/Basic/NoSanitizeList.h"
#include "clang/CIR/MissingFeatures.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/xxhash.h"
#include <optional>

using namespace clang;
using namespace clang::CIRGen;

namespace {

static Address maybeConvertMatrixAddress(Address addr, QualType matrixTy,
                                         CIRGenFunction &cgf) {
  mlir::Type vectorTy = cgf.convertType(matrixTy);
  if (addr.getElementType() == vectorTy)
    return addr;
  return addr.withElementType(cgf.getBuilder(), vectorTy);
}

constexpr uint32_t kBlockFieldIsBlock = 0x07;
constexpr uint32_t kBlockFieldIsByref = 0x08;
constexpr uint32_t kBlockFieldIsObject = 0x03;
constexpr uint32_t kBlockFieldIsWeak = 0x10;
constexpr uint32_t kBlockHasExtendedLayout = 1u << 31;

class DirectBlockFieldFlags {
  uint32_t flags;

public:
  DirectBlockFieldFlags() : flags(0) {}
  explicit DirectBlockFieldFlags(uint32_t flags) : flags(flags) {}

  uint32_t getBitMask() const { return flags; }
  bool operator==(DirectBlockFieldFlags other) const {
    return flags == other.flags;
  }
};

enum class DirectBlockCaptureEntityKind {
  None,
  CXXRecord,
  AddressDiscriminatedPointerAuth,
  ARCWeak,
  ARCStrong,
  NonTrivialCStruct,
  BlockObject,
};

enum class DirectBlockCaptureStrKind {
  CopyHelper,
  DisposeHelper,
};

struct DirectBlockHelperCaptureInfo {
  const BlockDecl::Capture *capture;
  QualType captureType;
  PointerAuthQualifier pointerAuth;
  CharUnits offset;
  DirectBlockCaptureEntityKind copyKind;
  DirectBlockFieldFlags copyFlags;
  DirectBlockCaptureEntityKind disposeKind;
  DirectBlockFieldFlags disposeFlags;

  bool isConstantOrTrivial() const {
    return copyKind == DirectBlockCaptureEntityKind::None &&
           disposeKind == DirectBlockCaptureEntityKind::None;
  }
};

enum class BlockLayoutOpcode : unsigned char {
  Operator = 0,
  NonObjectBytes = 1,
  NonObjectWords = 2,
  Strong = 3,
  Byref = 4,
  Weak = 5,
  Unretained = 6,
};

struct BlockLayoutEntry {
  BlockLayoutOpcode opcode;
  CharUnits bytePos;
  CharUnits size;

  bool operator<(const BlockLayoutEntry &other) const {
    return bytePos < other.bytePos;
  }
};

static bool isSupportedObjCStrongDirectBlockCapture(QualType captureTy) {
  return captureTy.getObjCLifetime() == Qualifiers::OCL_Strong &&
         captureTy->isObjCRetainableType() && !captureTy->isBlockPointerType();
}

static bool isSupportedObjCStrongBlockPointerDirectBlockCapture(
    QualType captureTy) {
  return captureTy->isBlockPointerType() &&
         captureTy.isNonTrivialToPrimitiveCopy() == QualType::PCK_ARCStrong;
}

static bool isSupportedObjCUnretainedDirectBlockCapture(QualType captureTy) {
  return captureTy.getObjCLifetime() == Qualifiers::OCL_ExplicitNone &&
         captureTy->isObjCRetainableType() && !captureTy->isBlockPointerType();
}

static bool isSupportedObjCWeakDirectBlockCapture(QualType captureTy) {
  return captureTy.getObjCLifetime() == Qualifiers::OCL_Weak &&
         captureTy->isObjCRetainableType() && !captureTy->isBlockPointerType();
}

static BlockLayoutOpcode
getDirectBlockLayoutOpcode(const BlockDecl::Capture &capture,
                           QualType captureTy,
                           const DirectBlockHelperCaptureInfo &captureInfo) {
  if (capture.isEscapingByref())
    return BlockLayoutOpcode::Byref;

  if (captureInfo.copyKind == DirectBlockCaptureEntityKind::ARCWeak ||
      captureInfo.disposeKind == DirectBlockCaptureEntityKind::ARCWeak)
    return BlockLayoutOpcode::Weak;

  if (captureInfo.copyKind == DirectBlockCaptureEntityKind::ARCStrong ||
      captureInfo.disposeKind == DirectBlockCaptureEntityKind::ARCStrong)
    return BlockLayoutOpcode::Strong;

  if (captureInfo.copyKind == DirectBlockCaptureEntityKind::BlockObject ||
      captureInfo.disposeKind == DirectBlockCaptureEntityKind::BlockObject)
    return BlockLayoutOpcode::Strong;

  if (isSupportedObjCUnretainedDirectBlockCapture(captureTy))
    return BlockLayoutOpcode::Unretained;

  return BlockLayoutOpcode::NonObjectBytes;
}

static bool directBlockDestructorCanThrow(QualType captureTy) {
  if (const auto *record = captureTy->getAsCXXRecordDecl())
    if (const CXXDestructorDecl *dtor = record->getDestructor())
      return dtor->getType()->castAs<FunctionProtoType>()->canThrow();
  return false;
}

static DirectBlockFieldFlags
getDirectBlockFieldFlagsForObjCObjectPointer(const BlockDecl::Capture &capture,
                                             QualType captureTy) {
  uint32_t flags =
      captureTy->isBlockPointerType() ? kBlockFieldIsBlock : kBlockFieldIsObject;
  if (capture.isEscapingByref() && captureTy.isObjCGCWeak())
    flags |= kBlockFieldIsWeak;
  return DirectBlockFieldFlags(flags);
}

static std::pair<DirectBlockCaptureEntityKind, DirectBlockFieldFlags>
computeCopyInfoForDirectBlockCapture(const BlockDecl::Capture &capture,
                                     QualType captureTy,
                                     const LangOptions &langOpts) {
  if (capture.getCopyExpr()) {
    assert(!capture.isByRef());
    return {DirectBlockCaptureEntityKind::CXXRecord,
            DirectBlockFieldFlags()};
  }

  if (capture.isEscapingByref()) {
    uint32_t flags = kBlockFieldIsByref;
    if (captureTy.isObjCGCWeak())
      flags |= kBlockFieldIsWeak;
    return {DirectBlockCaptureEntityKind::BlockObject,
            DirectBlockFieldFlags(flags)};
  }

  if (captureTy.hasAddressDiscriminatedPointerAuth())
    return {DirectBlockCaptureEntityKind::AddressDiscriminatedPointerAuth,
            DirectBlockFieldFlags()};

  DirectBlockFieldFlags flags =
      getDirectBlockFieldFlagsForObjCObjectPointer(capture, captureTy);
  bool isBlockPointer = captureTy->isBlockPointerType();

  switch (captureTy.isNonTrivialToPrimitiveCopy()) {
  case QualType::PCK_ARCWeak:
    return {DirectBlockCaptureEntityKind::ARCWeak, flags};
  case QualType::PCK_ARCStrong:
    return {!isBlockPointer ? DirectBlockCaptureEntityKind::ARCStrong
                            : DirectBlockCaptureEntityKind::BlockObject,
            flags};
  case QualType::PCK_PtrAuth:
    return {DirectBlockCaptureEntityKind::AddressDiscriminatedPointerAuth,
            DirectBlockFieldFlags()};
  case QualType::PCK_Trivial:
  case QualType::PCK_VolatileTrivial:
    if (!captureTy->isObjCRetainableType())
      return {DirectBlockCaptureEntityKind::None, DirectBlockFieldFlags()};
    if (captureTy->isObjCInertUnsafeUnretainedType())
      return {DirectBlockCaptureEntityKind::None, DirectBlockFieldFlags()};
    if (!captureTy.getObjCLifetime() && !langOpts.ObjCAutoRefCount)
      return {DirectBlockCaptureEntityKind::BlockObject, flags};
    return {DirectBlockCaptureEntityKind::None, DirectBlockFieldFlags()};
  case QualType::PCK_Struct:
    // A non-trivial C struct captured by value (e.g. an ObjC-ARC struct with a
    // __strong/__weak member): the block copy helper must copy-construct it
    // (mirrors OGCG BlockCaptureEntityKind::NonTrivialCStruct); dropping it to
    // None emits a bitwise memcpy that fails to retain the captured members.
    return {DirectBlockCaptureEntityKind::NonTrivialCStruct,
            DirectBlockFieldFlags()};
  }

  llvm_unreachable("after exhaustive PrimitiveCopyKind switch");
}

static std::pair<DirectBlockCaptureEntityKind, DirectBlockFieldFlags>
computeDestroyInfoForDirectBlockCapture(const BlockDecl::Capture &capture,
                                        QualType captureTy,
                                        const LangOptions &langOpts) {
  if (capture.isEscapingByref()) {
    uint32_t flags = kBlockFieldIsByref;
    if (captureTy.isObjCGCWeak())
      flags |= kBlockFieldIsWeak;
    return {DirectBlockCaptureEntityKind::BlockObject,
            DirectBlockFieldFlags(flags)};
  }

  switch (captureTy.isDestructedType()) {
  case QualType::DK_cxx_destructor:
    return {DirectBlockCaptureEntityKind::CXXRecord, DirectBlockFieldFlags()};
  case QualType::DK_objc_strong_lifetime:
    return {DirectBlockCaptureEntityKind::ARCStrong,
            getDirectBlockFieldFlagsForObjCObjectPointer(capture, captureTy)};
  case QualType::DK_objc_weak_lifetime:
    return {DirectBlockCaptureEntityKind::ARCWeak,
            getDirectBlockFieldFlagsForObjCObjectPointer(capture, captureTy)};
  case QualType::DK_none:
    if (captureTy->isObjCRetainableType() &&
        !captureTy.getQualifiers().hasObjCLifetime() &&
        !langOpts.ObjCAutoRefCount &&
        !captureTy->isObjCInertUnsafeUnretainedType()) {
      return {DirectBlockCaptureEntityKind::BlockObject,
              getDirectBlockFieldFlagsForObjCObjectPointer(capture, captureTy)};
    }
    return {DirectBlockCaptureEntityKind::None, DirectBlockFieldFlags()};
  case QualType::DK_nontrivial_c_struct:
    // Mirror of the copy side: the block dispose helper must destroy the
    // captured non-trivial C struct (release its __strong/__weak members)
    // instead of leaking them (OGCG BlockCaptureEntityKind::NonTrivialCStruct).
    return {DirectBlockCaptureEntityKind::NonTrivialCStruct,
            DirectBlockFieldFlags()};
  }

  llvm_unreachable("after exhaustive DestructionKind switch");
}

static std::string
getDirectBlockCaptureStr(const DirectBlockHelperCaptureInfo &captureInfo,
                         DirectBlockCaptureStrKind strKind,
                         CIRGenModule &cgm) {
  std::string str;
  ASTContext &ctx = cgm.getASTContext();

  DirectBlockCaptureEntityKind kind =
      strKind == DirectBlockCaptureStrKind::DisposeHelper
          ? captureInfo.disposeKind
          : captureInfo.copyKind;
  DirectBlockFieldFlags flags =
      strKind == DirectBlockCaptureStrKind::DisposeHelper
          ? captureInfo.disposeFlags
          : captureInfo.copyFlags;

  switch (kind) {
  case DirectBlockCaptureEntityKind::CXXRecord: {
    str += "c";
    SmallString<256> typeStr;
    llvm::raw_svector_ostream out(typeStr);
    cgm.getCXXABI().getMangleContext().mangleCanonicalTypeName(
        captureInfo.captureType, out);
    str += std::to_string(typeStr.size()) + typeStr.c_str();
    break;
  }
  case DirectBlockCaptureEntityKind::ARCWeak:
    str += "w";
    break;
  case DirectBlockCaptureEntityKind::ARCStrong:
    str += "s";
    break;
  case DirectBlockCaptureEntityKind::NonTrivialCStruct: {
    // Deterministic, content-based suffix so identical captures produce the
    // same linkonce_odr helper name across TUs. (OGCG encodes the non-trivial
    // copy/dtor helper-function string here; a mangled type name is equally
    // ODR-stable within ClangIR and keeps distinct struct captures distinct.)
    str += "n";
    SmallString<256> typeStr;
    llvm::raw_svector_ostream out(typeStr);
    cgm.getCXXABI().getMangleContext().mangleCanonicalTypeName(
        captureInfo.captureType, out);
    str += std::to_string(typeStr.size()) + typeStr.c_str();
    break;
  }
  case DirectBlockCaptureEntityKind::AddressDiscriminatedPointerAuth: {
    PointerAuthQualifier pointerAuth = captureInfo.pointerAuth;
    assert(pointerAuth && pointerAuth.isAddressDiscriminated());
    str += "p" + std::to_string(pointerAuth.getKey()) + "d" +
           std::to_string(pointerAuth.getExtraDiscriminator());
    break;
  }
  case DirectBlockCaptureEntityKind::BlockObject: {
    const VarDecl *var = captureInfo.capture->getVariable();
    unsigned fieldFlags = flags.getBitMask();
    if (fieldFlags & kBlockFieldIsByref) {
      str += "r";
      if (fieldFlags & kBlockFieldIsWeak)
        str += "w";
      else {
        if (strKind != DirectBlockCaptureStrKind::DisposeHelper &&
            ctx.getBlockVarCopyInit(var).canThrow())
          str += "c";
        if (strKind != DirectBlockCaptureStrKind::CopyHelper &&
            directBlockDestructorCanThrow(captureInfo.captureType))
          str += "d";
      }
    } else {
      assert((fieldFlags & kBlockFieldIsObject) && "unexpected flag value");
      str += fieldFlags == kBlockFieldIsBlock ? "b" : "o";
    }
    break;
  }
  case DirectBlockCaptureEntityKind::None:
    break;
  }

  return str;
}

static std::string getDirectBlockCopyDestroyHelperFuncName(
    llvm::ArrayRef<DirectBlockHelperCaptureInfo> captures,
    CharUnits blockAlignment, DirectBlockCaptureStrKind strKind,
    CIRGenModule &cgm) {
  assert((strKind == DirectBlockCaptureStrKind::CopyHelper ||
          strKind == DirectBlockCaptureStrKind::DisposeHelper) &&
         "unexpected DirectBlockCaptureStrKind");

  std::string name = strKind == DirectBlockCaptureStrKind::CopyHelper
                         ? "__copy_helper_block_"
                         : "__destroy_helper_block_";
  if (cgm.getLangOpts().Exceptions)
    name += "e";
  if (cgm.getCodeGenOpts().ObjCAutoRefCountExceptions)
    name += "a";
  name += std::to_string(blockAlignment.getQuantity()) + "_";

  for (const DirectBlockHelperCaptureInfo &captureInfo : captures) {
    if (captureInfo.isConstantOrTrivial())
      continue;
    name += std::to_string(captureInfo.offset.getQuantity());
    name += getDirectBlockCaptureStr(captureInfo, strKind, cgm);
  }

  return name;
}

static const CIRGenFunctionInfo &
getDirectBlockHelperFunctionInfo(CIRGenModule &cgm, bool isCopy) {
  ASTContext &ctx = cgm.getASTContext();
  SmallVector<CanQualType, 2> argTypes;
  argTypes.push_back(ctx.getCanonicalParamType(ctx.VoidPtrTy));
  if (isCopy)
    argTypes.push_back(ctx.getCanonicalParamType(ctx.VoidPtrTy));
  return cgm.getTypes().arrangeCIRFunctionInfo(
      CanQualType::CreateUnsafe(ctx.getCanonicalType(ctx.VoidTy)),
      FnInfoOpts::None, argTypes, RequiredArgs::All, FunctionType::ExtInfo());
}

static void setDirectBlockHelperAttributesVisibility(
    bool capturesNonExternalType, cir::FuncOp fn,
    const CIRGenFunctionInfo &info, CIRGenModule &cgm) {
  if (capturesNonExternalType) {
    cgm.setInternalFunctionAttributes(GlobalDecl{}, fn, info);
  } else {
    cgm.setCIRFunctionAttributes(GlobalDecl{}, info, fn, /*isThunk=*/false);
    cgm.getTargetCIRGenInfo().setTargetAttributes(nullptr, fn, cgm);
    cgm.setCIRFunctionAttributesForDefinition(GlobalDecl{}, fn);
    fn.setLinkage(cir::GlobalLinkageKind::LinkOnceODRLinkage);
    fn.setGlobalVisibility(cir::VisibilityKind::Hidden);
    mlir::SymbolTable::setSymbolVisibility(
        fn, CIRGenModule::getMLIRVisibilityFromCIRLinkage(fn.getLinkage()));
    if (cgm.supportsCOMDAT())
      fn.setComdat(true);

    mlir::NamedAttrList attrs;
    if (auto existingAttrs = fn.getExtraAttrs())
      attrs = mlir::NamedAttrList{existingAttrs->getElements().getValue()};
    attrs.set("unnamed_addr", mlir::UnitAttr::get(&cgm.getMLIRContext()));
    fn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
        attrs.getDictionary(&cgm.getMLIRContext())));
  }

  if (cgm.getTarget().getTriple().isX86()) {
    mlir::NamedAttrList attrs;
    if (auto existingAttrs = fn.getExtraAttrs())
      attrs = mlir::NamedAttrList{existingAttrs->getElements().getValue()};
    attrs.set("min-legal-vector-width", cgm.getStringAttrCached("0"));
    fn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
        attrs.getDictionary(&cgm.getMLIRContext())));
  }
}

static uint64_t
getObjCInlineBlockLayoutInstruction(SmallVectorImpl<unsigned char> &layout) {
  unsigned size = layout.size();
  uint64_t result = 0;

  if (size >= 4)
    return 0;

  uint64_t strongWordCount = 0;
  uint64_t byrefWordCount = 0;
  uint64_t weakWordCount = 0;
  unsigned char inst = 0;
  BlockLayoutOpcode opcode = BlockLayoutOpcode::Operator;

  switch (size) {
  case 3:
    inst = layout[0];
    opcode = static_cast<BlockLayoutOpcode>(inst >> 4);
    if (opcode == BlockLayoutOpcode::Strong)
      strongWordCount = (inst & 0xF) + 1;
    else
      return 0;
    inst = layout[1];
    opcode = static_cast<BlockLayoutOpcode>(inst >> 4);
    if (opcode == BlockLayoutOpcode::Byref)
      byrefWordCount = (inst & 0xF) + 1;
    else
      return 0;
    inst = layout[2];
    opcode = static_cast<BlockLayoutOpcode>(inst >> 4);
    if (opcode == BlockLayoutOpcode::Weak)
      weakWordCount = (inst & 0xF) + 1;
    else
      return 0;
    break;

  case 2:
    inst = layout[0];
    opcode = static_cast<BlockLayoutOpcode>(inst >> 4);
    if (opcode == BlockLayoutOpcode::Strong) {
      strongWordCount = (inst & 0xF) + 1;
      inst = layout[1];
      opcode = static_cast<BlockLayoutOpcode>(inst >> 4);
      if (opcode == BlockLayoutOpcode::Byref)
        byrefWordCount = (inst & 0xF) + 1;
      else if (opcode == BlockLayoutOpcode::Weak)
        weakWordCount = (inst & 0xF) + 1;
      else
        return 0;
    } else if (opcode == BlockLayoutOpcode::Byref) {
      byrefWordCount = (inst & 0xF) + 1;
      inst = layout[1];
      opcode = static_cast<BlockLayoutOpcode>(inst >> 4);
      if (opcode == BlockLayoutOpcode::Weak)
        weakWordCount = (inst & 0xF) + 1;
      else
        return 0;
    } else {
      return 0;
    }
    break;

  case 1:
    inst = layout[0];
    opcode = static_cast<BlockLayoutOpcode>(inst >> 4);
    if (opcode == BlockLayoutOpcode::Strong)
      strongWordCount = (inst & 0xF) + 1;
    else if (opcode == BlockLayoutOpcode::Byref)
      byrefWordCount = (inst & 0xF) + 1;
    else if (opcode == BlockLayoutOpcode::Weak)
      weakWordCount = (inst & 0xF) + 1;
    else
      return 0;
    break;

  default:
    return 0;
  }

  if (strongWordCount == 16 || byrefWordCount == 16 || weakWordCount == 16)
    return 0;

  unsigned count = (strongWordCount != 0) + (byrefWordCount != 0) +
                   (weakWordCount != 0);
  if (size != count)
    return 0;

  if (strongWordCount)
    result = strongWordCount;
  result <<= 4;
  if (byrefWordCount)
    result += byrefWordCount;
  result <<= 4;
  if (weakWordCount)
    result += weakWordCount;
  return result;
}

static CIRGenRuntimePointerAuthInfo
getBlockHeaderPointerAuthInfo(CIRGenFunction &cgf,
                              const PointerAuthSchema &schema,
                              Address storageAddress, GlobalDecl decl,
                              QualType ty) {
  if (!schema || !cgf.cgm.shouldSignPointer(schema))
    return {};

  CIRGenRuntimePointerAuthInfo info{
      schema.getAuthenticationMode(), schema.isIsaPointer(),
      schema.authenticatesNullValues(), schema.getKey(), mlir::Value{}};

  mlir::Location loc = storageAddress.getPointer().getLoc();
  uint64_t otherDiscriminator =
      cgf.cgm.getPointerAuthOtherDiscriminator(schema, decl, ty);
  if (otherDiscriminator) {
    info.discriminator =
        cgf.getBuilder().getConstantInt(loc, cgf.cgm.uIntPtrTy,
                                        otherDiscriminator);
  }

  if (schema.isAddressDiscriminated()) {
    mlir::Value storagePtr = storageAddress.emitRawPointer();
    if (info.discriminator)
      info.discriminator =
          cgf.emitPointerAuthBlendDiscriminator(storagePtr, info.discriminator);
    else
      info.discriminator = cgf.getBuilder().createPtrToInt(storagePtr,
                                                           cgf.cgm.uIntPtrTy);
  }

  return info;
}

static std::string getBlockMangledName(CIRGenModule &cgm, GlobalDecl gd,
                                       const BlockDecl *blockDecl) {
  MangleContext &mangleCtx = cgm.getCXXABI().getMangleContext();
  const Decl *decl = gd.getDecl();

  SmallString<256> buffer;
  llvm::raw_svector_ostream out(buffer);
  if (!decl) {
    mangleCtx.mangleBlock(blockDecl->getDeclContext(), blockDecl, out);
  } else if (const auto *ctor = dyn_cast<CXXConstructorDecl>(decl)) {
    mangleCtx.mangleCtorBlock(ctor, gd.getCtorType(), blockDecl, out);
  } else if (const auto *dtor = dyn_cast<CXXDestructorDecl>(decl)) {
    mangleCtx.mangleDtorBlock(dtor, gd.getDtorType(), blockDecl, out);
  } else {
    mangleCtx.mangleBlock(cast<DeclContext>(decl), blockDecl, out);
  }
  return std::string(out.str());
}

} // namespace
using namespace cir;

//===----------------------------------------------------------------------===//
// counted_by / __builtin_dynamic_object_size support
//===----------------------------------------------------------------------===//

namespace {

static std::string
createCFIIcallTypeIdString(CIRGenFunction &cgf, QualType fnType) {
  bool generalizePointers =
      cgf.cgm.getCodeGenOpts().SanitizeCfiICallGeneralizePointers;
  fnType = generalizeFunctionTypeForCfiIcall(cgf.getContext(), fnType,
                                             generalizePointers);
  std::string typeId = cgf.cgm.createStableMetadataIdentifierKeyForType(fnType);
  if (generalizePointers)
    typeId += ".generalized";
  return typeId;
}

static void maybeAttachCFIIcallAttrs(CIRGenFunction &cgf,
                                     cir::CIRCallOpInterface call,
                                     QualType fnType, SourceLocation loc) {
  if (!cgf.sanOpts.has(SanitizerKind::CFIICall))
    return;

  bool generalizePointers =
      cgf.cgm.getCodeGenOpts().SanitizeCfiICallGeneralizePointers;
  QualType generalizedFnType = generalizeFunctionTypeForCfiIcall(
      cgf.getContext(), fnType, generalizePointers);
  std::string typeId = createCFIIcallTypeIdString(cgf, fnType);
  call->setAttr("cir.cfi.icall.typeid", cgf.cgm.getStringAttrCached(typeId));
  if (!isExternallyVisible(generalizedFnType->getLinkage()))
    call->setAttr("cir.cfi.icall.typetest_kind",
                  cgf.cgm.getStringAttrCached("hidden"));

  StringRef handlerKind = "abort";
  if (cgf.cgm.getCodeGenOpts().SanitizeTrap.has(SanitizerKind::CFIICall))
    handlerKind = "trap";
  else if (cgf.cgm.getCodeGenOpts().SanitizeRecover.has(
               SanitizerKind::CFIICall))
    handlerKind = "recover";

  cgf.emitSanitizerStatReportStub(cgf.getLoc(loc), CFISanitizerStatKind::ICall,
                                  call.getOperation());
  call->setAttr("cir.cfi.icall.kind", cgf.cgm.getStringAttrCached(handlerKind));
  call->setAttr("cir.nosanitize",
                mlir::UnitAttr::get(&cgf.getMLIRContext()));
  setUBSanSourceLocationAttrs(cgf, call, loc);
  setUBSanTypeNameAttr(cgf, call, fnType);
  setUBSanTypeDescriptorAttrs(cgf, call, fnType);
  call->setDiscardableAttr("cir.ubsan.type_check_kind",
                           cgf.getBuilder().getI8IntegerAttr(4));
  if (cgf.cgm.getCodeGenOpts().SanitizeMinimalRuntime)
    call->setAttr("cir.cfi.icall.minimal_runtime",
                  mlir::UnitAttr::get(&cgf.getMLIRContext()));
  if (cgf.cgm.getCodeGenOpts().SanitizeHandlerPreserveAllRegs)
    call->setAttr("cir.cfi.icall.preserve_all_regs",
                  mlir::UnitAttr::get(&cgf.getMLIRContext()));
}

static bool isWrappedCXXThis(const Expr *obj) {
  const Expr *base = obj;
  while (!isa<CXXThisExpr>(base)) {
    if (isa<CXXDynamicCastExpr>(base))
      return false;

    if (const auto *ce = dyn_cast<CastExpr>(base)) {
      base = ce->getSubExpr();
    } else if (const auto *pe = dyn_cast<ParenExpr>(base)) {
      base = pe->getSubExpr();
    } else if (const auto *uo = dyn_cast<UnaryOperator>(base)) {
      if (uo->getOpcode() == UO_Extension)
        base = uo->getSubExpr();
      else
        return false;
    } else {
      return false;
    }
  }
  return true;
}

static cir::CallOp emitTypeMismatchStubCall(
    CIRGenFunction &cgf, mlir::Location loc, SourceLocation sourceLoc,
    mlir::Value ptrAsInt, QualType ty, unsigned log2Align,
    CIRGenFunction::TypeCheckKind tck, StringRef handlerKind) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  auto stubFnTy =
      cir::FuncType::get({ptrAsInt.getType()}, builder.getVoidTy());
  auto stubFn = cgf.cgm.createRuntimeFunction(
      stubFnTy, "__cir_ubsan_type_mismatch_handler", /*attrs=*/{},
      /*isLocal=*/false, /*assumeConvergent=*/false);

  auto call = builder.createCallOp(loc, stubFn, {ptrAsInt});
  setUBSanSourceLocationAttrs(cgf, call, sourceLoc);
  setUBSanTypeNameAttr(cgf, call, ty);
  setUBSanTypeDescriptorAttrs(cgf, call, ty);
  call->setDiscardableAttr("cir.ubsan.log2_alignment",
                           builder.getI8IntegerAttr(log2Align));
  call->setDiscardableAttr("cir.ubsan.type_check_kind",
                           builder.getI8IntegerAttr(static_cast<uint8_t>(tck)));
  call->setDiscardableAttr("cir.ubsan.handler_kind",
                           builder.getStringAttr(handlerKind));
  call->setDiscardableAttr("cir.ubsan.handler_name",
                           builder.getStringAttr("type_mismatch"));
  call->setDiscardableAttr("cir.nosanitize",
                           mlir::UnitAttr::get(builder.getContext()));
  return call;
}

/// Cleanup that emits a cir.lifetime.end marker (same as CIRGenDecl.cpp).
struct CallLifetimeEnd final : EHScopeStack::Cleanup {
  Address addr;
  CallLifetimeEnd(Address addr) : addr(addr) {}
  void emit(CIRGenFunction &cgf, Flags flags) override {
    auto &builder = cgf.getBuilder();
    mlir::OpBuilder::InsertionGuard guard(builder);
    mlir::Block *block = builder.getInsertionBlock();
    if (block && block->mightHaveTerminator() && block->getTerminator())
      builder.setInsertionPoint(block->getTerminator());
    builder.createLifetimeEnd(addr.getPointer().getLoc(),
                              addr.getPointer());
  }
};

/// EH-only cleanup for the block copy helper: disposes a capture that has
/// already been copied into the destination block when a later capture's
/// copy-constructor throws during _Block_copy. Mirrors classic CGBlocks
/// pushCaptureCleanup(ForCopyHelper). Only C++ record captures can throw during
/// copy, so these cleanups run only on that unwind path and are deactivated on
/// normal completion (EH-only), leaving the successfully-copied captures intact.
struct DirectBlockCaptureCopyCleanup final : EHScopeStack::Cleanup {
  DirectBlockCaptureEntityKind kind;
  Address field;
  clang::QualType captureFieldTy;
  unsigned disposeFlags;
  cir::FuncOp blockObjectDispose;
  mlir::Location loc;
  DirectBlockCaptureCopyCleanup(DirectBlockCaptureEntityKind kind, Address field,
                                clang::QualType captureFieldTy,
                                unsigned disposeFlags,
                                cir::FuncOp blockObjectDispose,
                                mlir::Location loc)
      : kind(kind), field(field), captureFieldTy(captureFieldTy),
        disposeFlags(disposeFlags), blockObjectDispose(blockObjectDispose),
        loc(loc) {}
  void emit(CIRGenFunction &cgf, Flags) override {
    switch (kind) {
    case DirectBlockCaptureEntityKind::CXXRecord:
      cgf.emitDestroy(field, captureFieldTy,
                      cgf.getDestroyer(clang::QualType::DK_cxx_destructor),
                      /*useEHCleanupForArray=*/false);
      break;
    case DirectBlockCaptureEntityKind::NonTrivialCStruct:
      cgf.emitDestroy(field, captureFieldTy,
                      cgf.getDestroyer(clang::QualType::DK_nontrivial_c_struct),
                      /*useEHCleanupForArray=*/false);
      break;
    case DirectBlockCaptureEntityKind::ARCStrong:
      cgf.emitARCDestroyStrong(field, loc, /*isImprecise=*/true);
      break;
    case DirectBlockCaptureEntityKind::ARCWeak:
      cgf.emitARCDestroyWeak(field);
      break;
    case DirectBlockCaptureEntityKind::BlockObject: {
      CIRGenBuilderTy &builder = cgf.getBuilder();
      mlir::Type voidPtrTy = builder.getVoidPtrTy();
      mlir::Value dstPtr = field.getPointer();
      if (dstPtr.getType() != voidPtrTy)
        dstPtr = builder.createBitcast(loc, dstPtr, voidPtrTy);
      cgf.emitRuntimeCall(loc, blockObjectDispose,
                          {dstPtr, builder.getConstInt(
                                       loc, builder.getSInt32Ty(), disposeFlags)});
      break;
    }
    default:
      break;
    }
  }
};

static bool containsAnyVarDecl(const Stmt *s) {
  if (!s)
    return false;

  if (const auto *catchStmt = dyn_cast<CXXCatchStmt>(s))
    if (catchStmt->getExceptionDecl())
      return true;

  if (const auto *ifStmt = dyn_cast<IfStmt>(s))
    if (ifStmt->getConditionVariable())
      return true;

  if (const auto *switchStmt = dyn_cast<SwitchStmt>(s))
    if (switchStmt->getConditionVariable())
      return true;

  if (const auto *whileStmt = dyn_cast<WhileStmt>(s))
    if (whileStmt->getConditionVariable())
      return true;

  if (const auto *forStmt = dyn_cast<ForStmt>(s))
    if (forStmt->getConditionVariable())
      return true;

  if (const auto *forRangeStmt = dyn_cast<CXXForRangeStmt>(s))
    if (forRangeStmt->getLoopVariable())
      return true;
  if (const auto *declStmt = dyn_cast<DeclStmt>(s))
    for (const Decl *decl : declStmt->decls())
      if (isa<VarDecl>(decl))
        return true;

  for (const Stmt *child : s->children())
    if (containsAnyVarDecl(child))
      return true;

  return false;
}

static bool isObjCClassLayoutKnownStatically(const ObjCInterfaceDecl *id) {
  assert(id && "expected Objective-C interface");

  for (; id; id = id->getSuperClass()) {
    if (id->getIdentifier() && id->getIdentifier()->getName() == "NSObject")
      return true;
    if (!id->getImplementation())
      return false;
  }

  return true;
}

static bool usesGNUstep2ObjCRuntime(const CIRGenFunction &cgf) {
  const ObjCRuntime runtime = cgf.getContext().getLangOpts().ObjCRuntime;
  return runtime.getKind() == ObjCRuntime::GNUstep &&
         runtime.getVersion() >= VersionTuple(2);
}

static bool usesGNUObjCRuntime(const CIRGenFunction &cgf) {
  const ObjCRuntime runtime = cgf.getContext().getLangOpts().ObjCRuntime;
  return runtime.getKind() == ObjCRuntime::GCC ||
         runtime.getKind() == ObjCRuntime::GNUstep;
}

static bool usesLegacyGNUObjCRuntime(const CIRGenFunction &cgf) {
  const ObjCRuntime runtime = cgf.getContext().getLangOpts().ObjCRuntime;
  return runtime.getKind() == ObjCRuntime::GCC ||
         (runtime.getKind() == ObjCRuntime::GNUstep &&
          runtime.getVersion() < VersionTuple(2));
}

static bool usesNonFragileGNUObjCRuntime(const CIRGenFunction &cgf) {
  return usesGNUObjCRuntime(cgf) &&
         cgf.getContext().getLangOpts().ObjCRuntime.isNonFragile();
}

static std::string mangleGNUObjCTypeEncodingForSymbol(const CIRGenFunction &cgf,
                                                      QualType ty) {
  std::string typeEncoding;
  cgf.getContext().getObjCEncodingForType(ty, typeEncoding);
  if (cgf.getContext().getTargetInfo().getTriple().isOSBinFormatELF())
    llvm::replace(typeEncoding, '@', '\1');
  if (cgf.getContext().getTargetInfo().getTriple().isOSWindows())
    llvm::replace(typeEncoding, '=', '\2');
  return typeEncoding;
}

static cir::GlobalOp getOrCreateObjCIvarOffsetVariable(CIRGenFunction &cgf,
                                                       mlir::Location loc,
                                                       const ObjCInterfaceDecl *id,
                                                       const ObjCIvarDecl *ivar) {
  const ObjCInterfaceDecl *container = ivar->getContainingInterface();
  SmallString<64> name;
  if (usesGNUstep2ObjCRuntime(cgf)) {
    name += "__objc_ivar_offset_";
    name += container->getName();
    name += ".";
    name += ivar->getName();
    name += ".";
    name += mangleGNUObjCTypeEncodingForSymbol(cgf, ivar->getType());
  } else if (usesGNUObjCRuntime(cgf)) {
    name += "__objc_ivar_offset_";
    name += container->getName();
    name += ".";
    name += ivar->getName();
  } else {
    name += "OBJC_IVAR_$_";
    name += container->getObjCRuntimeNameAsString();
    name += ".";
    name += ivar->getName();
  }

  if (auto *existing = cgf.cgm.getGlobalValue(name))
    return mlir::cast<cir::GlobalOp>(existing);

  mlir::Type offsetTy;
  if (usesGNUstep2ObjCRuntime(cgf)) {
    offsetTy = cgf.getBuilder().getUInt32Ty();
  } else if (usesLegacyGNUObjCRuntime(cgf)) {
    offsetTy = cgf.getBuilder().getPointerTo(cgf.getBuilder().getUInt32Ty());
  } else {
    // Match Darwin non-fragile ObjC metadata emission. Share the canonical
    // offset-var type with the class-metadata path (getObjCDarwinClassGlobal)
    // so the definition and this declaration/get_global agree on width and
    // signedness.
    offsetTy = cgf.cgm.getObjCDarwinIvarOffsetVarType();
  }
  const bool isCOFF = cgf.getTarget().getTriple().isOSBinFormatCOFF();
  const bool isGNUstep2DLLImport =
      usesGNUstep2ObjCRuntime(cgf) && isCOFF && container->hasAttr<DLLImportAttr>();
  uint64_t offsetValue = 0;
  bool isConstant = isObjCClassLayoutKnownStatically(id);
  if (usesGNUstep2ObjCRuntime(cgf)) {
    isConstant =
        !isGNUstep2DLLImport && isObjCClassLayoutKnownStatically(container);
    if (isConstant) {
      uint64_t baseOffset =
          cgf.getContext().lookupFieldBitOffset(container, ivar) /
          cgf.getContext().getCharWidth();
      uint64_t superInstanceSize = 0;
      if (const ObjCInterfaceDecl *superClassDecl = container->getSuperClass())
        superInstanceSize =
            cgf.getContext().getASTObjCInterfaceLayout(superClassDecl)
                .getSize()
                .getQuantity();
      offsetValue = baseOffset - superInstanceSize;
    }
  } else if (isConstant) {
    offsetValue =
        cgf.getContext().lookupFieldBitOffset(id, ivar) /
        cgf.getContext().getCharWidth();
  }

  CharUnits ivarOffsetAlign =
      cgf.getTarget().getTriple().getArch() == llvm::Triple::aarch64
          ? cgf.getContext().getTypeAlignInChars(cgf.getContext().IntTy)
          : cgf.getContext().getTypeAlignInChars(cgf.getContext().LongTy);
  CharUnits gnustep2OffsetAlign =
      cgf.getContext().getTypeSizeInChars(cgf.getContext().IntTy);

  cir::GlobalOp gv = CIRGenModule::createGlobalOp(cgf.cgm, loc, name, offsetTy,
                                                  /*isConstant=*/false);
  gv.setLinkage(cir::GlobalLinkageKind::ExternalLinkage);
  if (usesGNUstep2ObjCRuntime(cgf)) {
    const bool isPrivateOrPackage =
        ivar->getAccessControl() == ObjCIvarDecl::Private ||
        ivar->getAccessControl() == ObjCIvarDecl::Package;
    const bool isHidden =
        isPrivateOrPackage || container->getVisibility() == HiddenVisibility;
    if (isHidden) {
      gv.setGlobalVisibility(cir::VisibilityKind::Hidden);
      mlir::SymbolTable::setSymbolVisibility(gv, cgf.cgm.getMLIRVisibility(gv));
    } else {
      gv.setGlobalVisibility(cir::VisibilityKind::Default);
      mlir::SymbolTable::setSymbolVisibility(
          gv, gv.isDeclaration() ? cgf.cgm.getMLIRVisibility(gv)
                                 : mlir::SymbolTable::Visibility::Public);
    }
    if (!isGNUstep2DLLImport)
      gv.setAlignmentAttr(cgf.cgm.getSize(gnustep2OffsetAlign));
    if (isCOFF) {
      gv->removeAttr("cir.dll_storage_class");
      gv->removeAttr("dso_local");
      if (isGNUstep2DLLImport) {
        gv->setAttr("cir.dll_storage_class",
                    cgf.cgm.getStringAttrCached("dllimport"));
      } else if (container->hasAttr<DLLExportAttr>() && !isHidden) {
        gv->setAttr("cir.dll_storage_class",
                    cgf.cgm.getStringAttrCached("dllexport"));
        cgf.cgm.setDSOLocal(static_cast<mlir::Operation *>(gv));
      }
    }
    if (!isGNUstep2DLLImport && isConstant)
      gv.setInitialValueAttr(cir::IntAttr::get(offsetTy, offsetValue));
  } else if (usesLegacyGNUObjCRuntime(cgf)) {
    mlir::SymbolTable::setSymbolVisibility(gv,
                                           mlir::SymbolTable::Visibility::Public);
    gv.setAlignmentAttr(cgf.cgm.getSize(cgf.getPointerAlign()));
  } else {
    const bool isDLLImport = isCOFF && container->hasAttr<DLLImportAttr>();
    mlir::SymbolTable::setSymbolVisibility(
        gv, isDLLImport ? cgf.cgm.getMLIRVisibility(gv)
                        : mlir::SymbolTable::Visibility::Public);
    if (!isDLLImport)
      gv.setAlignmentAttr(cgf.cgm.getSize(ivarOffsetAlign));
    if (isCOFF) {
      const bool isPrivateOrPackage =
          ivar->getAccessControl() == ObjCIvarDecl::Private ||
          ivar->getAccessControl() == ObjCIvarDecl::Package;
      if (isDLLImport) {
        gv->setAttr("cir.dll_storage_class",
                    cgf.cgm.getStringAttrCached("dllimport"));
      } else if (container->hasAttr<DLLExportAttr>() && !isPrivateOrPackage) {
        gv->setAttr("cir.dll_storage_class",
                    cgf.cgm.getStringAttrCached("dllexport"));
      }
    }
    if (!isDLLImport) {
      if (!isCOFF)
        gv.setSectionAttr(cgf.cgm.getStringAttrCached("__DATA, __objc_ivar"));
      gv.setInitialValueAttr(cir::IntAttr::get(offsetTy, offsetValue));
    }
  }
  return gv;
}

static std::optional<std::pair<llvm::APInt, bool>>
getConstantIntValueAndSignedness(mlir::Value value) {
  if (auto constant = value.getDefiningOp<cir::ConstantOp>()) {
    if (auto intAttr = constant.getValueAttr<cir::IntAttr>()) {
      if (auto intTy = mlir::dyn_cast<cir::IntType>(value.getType()))
        return std::pair{intAttr.getValue(), intTy.isSigned()};
    }
  }

  if (auto cast = value.getDefiningOp<cir::CastOp>()) {
    if (cast.getKind() == cir::CastKind::integral)
      return getConstantIntValueAndSignedness(cast.getSrc());
  }

  return std::nullopt;
}

struct PreserveAccessIndexTypedefDebugInfo {
  std::string name;
  llvm::SmallVector<std::string, 2> btfDeclTags;
  std::string filePath;
  unsigned line = 0;
  bool isConstQualified = false;
  bool isVolatileQualified = false;
  bool isRestrictQualified = false;
};

struct PreserveAccessIndexPointerLevelDebugInfo {
  llvm::SmallVector<std::string, 2> btfTypeTags;
  bool pointeeConstQualified = false;
  bool pointeeVolatileQualified = false;
  bool pointeeRestrictQualified = false;
};

static llvm::SmallVector<std::string, 2>
collectPreserveAccessIndexBTFDeclTagStrings(const Decl *decl) {
  llvm::SmallVector<std::string, 2> tags;
  if (!decl)
    return tags;
  for (const auto *tag : decl->specific_attrs<BTFDeclTagAttr>())
    tags.push_back(tag->getBTFDeclTag().str());
  return tags;
}

static llvm::SmallVector<std::string, 2>
collectPreserveAccessIndexBTFTypeTagStringsFromQualType(QualType qt) {
  llvm::SmallVector<std::string, 2> tags;
  if (qt.isNull())
    return tags;

  if (const auto *atomicTy = qt->getAs<AtomicType>())
    qt = atomicTy->getValueType();

  while (!qt.isNull()) {
    if (const auto *parenTy = qt->getAs<ParenType>()) {
      qt = parenTy->getInnerType();
      continue;
    }
    if (const auto *btfTy =
            llvm::dyn_cast<BTFTagAttributedType>(qt.getTypePtr())) {
      llvm::StringRef tag = btfTy->getAttr()->getBTFTypeTag();
      if (!tag.empty())
        tags.insert(tags.begin(), tag.str());
      qt = btfTy->getWrappedType();
      continue;
    }
    if (const auto *attrTy = qt->getAs<AttributedType>()) {
      qt = attrTy->getModifiedType();
      continue;
    }
    break;
  }

  return tags;
}

static void collectPreserveAccessIndexPointerDebugInfoFromQualType(
    QualType qt,
    llvm::SmallVectorImpl<PreserveAccessIndexPointerLevelDebugInfo> &levels) {
  if (qt.isNull())
    return;

  llvm::SmallVector<std::string, 2> currentPointerTags =
      collectPreserveAccessIndexBTFTypeTagStringsFromQualType(qt);

  while (!qt.isNull()) {
    if (const auto *parenTy = qt->getAs<ParenType>()) {
      qt = parenTy->getInnerType();
      continue;
    }
    if (const auto *attrTy = qt->getAs<AttributedType>()) {
      qt = attrTy->getModifiedType();
      continue;
    }
    break;
  }

  const auto *ptrTy = qt->getAs<clang::PointerType>();
  if (!ptrTy)
    return;

  QualType pointee = ptrTy->getPointeeType();
  PreserveAccessIndexPointerLevelDebugInfo info;
  info.pointeeConstQualified = pointee.isConstQualified();
  info.pointeeVolatileQualified = pointee.isVolatileQualified();
  info.pointeeRestrictQualified = pointee.isRestrictQualified();
  info.btfTypeTags = std::move(currentPointerTags);
  if (info.btfTypeTags.empty())
    info.btfTypeTags =
        collectPreserveAccessIndexBTFTypeTagStringsFromQualType(pointee);
  levels.push_back(std::move(info));
  collectPreserveAccessIndexPointerDebugInfoFromQualType(
      pointee.getUnqualifiedType(), levels);
}

static void populatePreserveAccessIndexLocationDebugInfo(
    ASTContext &ctx, SourceLocation loc,
    PreserveAccessIndexTypedefDebugInfo &info) {
  const SourceManager &sm = ctx.getSourceManager();
  if (!loc.isValid())
    return;

  SourceLocation presumedLoc = sm.getExpansionLoc(loc);
  PresumedLoc ploc = sm.getPresumedLoc(presumedLoc);
  if (ploc.isValid()) {
    info.filePath = ploc.getFilename();
    info.line = ploc.getLine();
    return;
  }

  SourceLocation spellingLoc = sm.getSpellingLoc(loc);
  if (!spellingLoc.isValid())
    return;
  if (const FileEntry *fe = sm.getFileEntryForID(sm.getFileID(spellingLoc)))
    info.filePath = fe->tryGetRealPathName().str();
  info.line = sm.getSpellingLineNumber(spellingLoc);
}

static std::optional<PreserveAccessIndexTypedefDebugInfo>
getPreserveAccessIndexTypedefDebugInfoFromQualType(ASTContext &ctx,
                                                   QualType qt) {
  if (qt.isNull())
    return std::nullopt;

  qt = qt.getLocalUnqualifiedType();
  while (!qt.isNull()) {
    if (const auto *parenTy = qt->getAs<ParenType>()) {
      qt = parenTy->getInnerType();
      continue;
    }
    if (const auto *btfTy =
            llvm::dyn_cast<BTFTagAttributedType>(qt.getTypePtr())) {
      qt = btfTy->getWrappedType();
      continue;
    }
    if (const auto *attrTy = qt->getAs<AttributedType>()) {
      qt = attrTy->getModifiedType();
      continue;
    }
    break;
  }

  const TypedefNameDecl *typedefDecl = nullptr;
  QualType underlying;
  llvm::SmallVector<std::string, 2> btfDeclTags;

  if (const auto *tt = qt->getAs<TypedefType>()) {
    typedefDecl = tt->getDecl();
    underlying = typedefDecl->getUnderlyingType();
    btfDeclTags = collectPreserveAccessIndexBTFDeclTagStrings(typedefDecl);
  }

  if (!typedefDecl)
    return std::nullopt;

  PreserveAccessIndexTypedefDebugInfo info;
  info.name = typedefDecl->getNameAsString();
  info.btfDeclTags = std::move(btfDeclTags);
  info.isConstQualified = underlying.isConstQualified();
  info.isVolatileQualified = underlying.isVolatileQualified();
  info.isRestrictQualified = underlying.isRestrictQualified();
  populatePreserveAccessIndexLocationDebugInfo(ctx, typedefDecl->getLocation(),
                                               info);
  return info;
}

static constexpr llvm::StringLiteral PreserveAccessIndexSourceTypeAttrName =
    "cir.preserve_access_index.source_type";

static void setPreserveAccessIndexSourceTypeAttrImpl(CIRGenFunction &cgf,
                                                     mlir::Operation *op,
                                                     QualType sourceType) {
  if (sourceType.isNull())
    return;

  op->setAttr(PreserveAccessIndexSourceTypeAttrName,
              mlir::TypeAttr::get(cgf.convertType(sourceType)));

  auto setUnitAttr = [&](llvm::StringRef name, bool enabled) {
    if (!enabled)
      return;
    op->setAttr(name, mlir::UnitAttr::get(&cgf.getMLIRContext()));
  };

  setUnitAttr("cir.debug.qual_const", sourceType.isLocalConstQualified());
  setUnitAttr("cir.debug.qual_volatile", sourceType.isLocalVolatileQualified());
  setUnitAttr("cir.debug.qual_restrict", sourceType.isLocalRestrictQualified());

  if (auto typedefDbg = getPreserveAccessIndexTypedefDebugInfoFromQualType(
          cgf.getContext(), sourceType)) {
    op->setAttr("cir.debug.typedef_name",
                cgf.getCIRGenModule().getStringAttrCached(typedefDbg->name));
    if (!typedefDbg->btfDeclTags.empty()) {
      llvm::SmallVector<mlir::Attribute> typedefTags;
      typedefTags.reserve(typedefDbg->btfDeclTags.size());
      for (llvm::StringRef tag : typedefDbg->btfDeclTags)
        typedefTags.push_back(cgf.getCIRGenModule().getStringAttrCached(tag));
      op->setAttr("cir.debug.typedef_btf_decl_tag",
                  cgf.getBuilder().getArrayAttr(typedefTags));
    }
    if (!typedefDbg->filePath.empty())
      op->setAttr("cir.debug.typedef_file",
                  cgf.getCIRGenModule().getStringAttrCached(
                      typedefDbg->filePath));
    if (typedefDbg->line)
      op->setAttr("cir.debug.typedef_line",
                  cgf.getBuilder().getI32IntegerAttr(typedefDbg->line));
    setUnitAttr("cir.debug.typedef_const", typedefDbg->isConstQualified);
    setUnitAttr("cir.debug.typedef_volatile", typedefDbg->isVolatileQualified);
    setUnitAttr("cir.debug.typedef_restrict", typedefDbg->isRestrictQualified);
  }

  llvm::SmallVector<PreserveAccessIndexPointerLevelDebugInfo, 4>
      pointerDebugInfo;
  collectPreserveAccessIndexPointerDebugInfoFromQualType(sourceType,
                                                         pointerDebugInfo);
  if (pointerDebugInfo.empty())
    return;

  llvm::SmallVector<mlir::Attribute> btfTagLevels;
  llvm::SmallVector<mlir::Attribute> pointeeConstLevels;
  llvm::SmallVector<mlir::Attribute> pointeeVolatileLevels;
  llvm::SmallVector<mlir::Attribute> pointeeRestrictLevels;
  btfTagLevels.reserve(pointerDebugInfo.size());
  pointeeConstLevels.reserve(pointerDebugInfo.size());
  pointeeVolatileLevels.reserve(pointerDebugInfo.size());
  pointeeRestrictLevels.reserve(pointerDebugInfo.size());
  for (const PreserveAccessIndexPointerLevelDebugInfo &info :
       pointerDebugInfo) {
    llvm::SmallVector<mlir::Attribute> btfTags;
    btfTags.reserve(info.btfTypeTags.size());
    for (llvm::StringRef tag : info.btfTypeTags)
      btfTags.push_back(cgf.getCIRGenModule().getStringAttrCached(tag));
    btfTagLevels.push_back(cgf.getBuilder().getArrayAttr(btfTags));
    pointeeConstLevels.push_back(
        cgf.getBuilder().getBoolAttr(info.pointeeConstQualified));
    pointeeVolatileLevels.push_back(
        cgf.getBuilder().getBoolAttr(info.pointeeVolatileQualified));
    pointeeRestrictLevels.push_back(
        cgf.getBuilder().getBoolAttr(info.pointeeRestrictQualified));
  }
  op->setAttr("cir.debug.pointer_chain_btf_type_tags",
              cgf.getBuilder().getArrayAttr(btfTagLevels));
  op->setAttr("cir.debug.pointer_chain_pointee_const",
              cgf.getBuilder().getArrayAttr(pointeeConstLevels));
  op->setAttr("cir.debug.pointer_chain_pointee_volatile",
              cgf.getBuilder().getArrayAttr(pointeeVolatileLevels));
  op->setAttr("cir.debug.pointer_chain_pointee_restrict",
              cgf.getBuilder().getArrayAttr(pointeeRestrictLevels));
}

} // namespace

void clang::CIRGen::setPreserveAccessIndexSourceTypeAttr(
    CIRGenFunction &cgf, mlir::Operation *op, QualType sourceType) {
  setPreserveAccessIndexSourceTypeAttrImpl(cgf, op, sourceType);
}

static bool emitScalarRangeCheck(CIRGenFunction &cgf, mlir::Value value,
                                 QualType ty, SourceLocation loc);

mlir::Value CIRGenFunction::emitNonNullRValueCheck(RValue rv, QualType type) {
  mlir::Value value = rv.getValue();
  auto loc = value.getLoc();
  auto zero = builder.getNullValue(value.getType(), loc);
  auto cmp =
      cir::CmpOp::create(builder, loc, cir::CmpOpKind::ne, value, zero);
  markNoSanitize(cmp);
  return cmp;
}

void CIRGenFunction::emitNonNullArgCheck(Address addr, QualType argType,
                                         SourceLocation argLoc,
                                         AbstractCallee callee,
                                         unsigned parmNum) {
  emitNonNullArgCheck(RValue::get(addr.getPointer()), argType, argLoc, callee,
                      parmNum);
}

bool CIRGenFunction::isNullPointerAllowed(TypeCheckKind TCK) {
  return TCK == TCK_DowncastPointer || TCK == TCK_Upcast ||
         TCK == TCK_UpcastToVirtualBase || TCK == TCK_DynamicOperation;
}

static std::optional<CharUnits>
findKnownBaseAlignment(CIRGenModule &cgm, mlir::Value ptr) {
  if (auto allocaOp = ptr.getDefiningOp<cir::AllocaOp>())
    if (auto alignAttr = allocaOp.getAlignmentAttr())
      return CharUnits::fromQuantity(alignAttr.getInt());

  if (auto getGlobalOp = ptr.getDefiningOp<cir::GetGlobalOp>())
    if (auto globalOp = mlir::dyn_cast_or_null<cir::GlobalOp>(
            cgm.getGlobalValue(getGlobalOp.getName())))
      if (auto alignment = globalOp.getAlignment())
        return CharUnits::fromQuantity(*alignment);

  // Array elements preserve the array's alignment because element sizes are a
  // multiple of their required alignment.
  if (auto getElementOp = ptr.getDefiningOp<cir::GetElementOp>())
    return findKnownBaseAlignment(cgm, getElementOp.getBase());

  // Union member addresses preserve the base object's alignment because all
  // members share the same storage. Struct/class members can reduce effective
  // alignment, so do not recurse through them here.
  if (auto getMemberOp = ptr.getDefiningOp<cir::GetMemberOp>())
    if (auto recordTy =
            mlir::dyn_cast<cir::RecordType>(getMemberOp.getAddrTy().getPointee());
        recordTy && recordTy.getKind() == cir::RecordType::Union)
      return findKnownBaseAlignment(cgm, getMemberOp.getAddr());

  if (auto castOp = ptr.getDefiningOp<cir::CastOp>())
    if (castOp.getKind() == cir::CastKind::address_space)
      return findKnownBaseAlignment(cgm, castOp.getSrc());

  return std::nullopt;
}

bool CIRGenFunction::isVptrCheckRequired(TypeCheckKind TCK, QualType Ty) {
  CXXRecordDecl *RD = Ty->getAsCXXRecordDecl();
  return (RD && RD->hasDefinition() && RD->isDynamicClass()) &&
         (TCK == TCK_MemberAccess || TCK == TCK_MemberCall ||
          TCK == TCK_DowncastPointer || TCK == TCK_DowncastReference ||
          TCK == TCK_UpcastToVirtualBase || TCK == TCK_DynamicOperation);
}

bool CIRGenFunction::sanitizePerformTypeCheck() const {
  return sanOpts.has(SanitizerKind::Null) ||
         sanOpts.has(SanitizerKind::Alignment) ||
         sanOpts.has(SanitizerKind::ObjectSize) ||
         sanOpts.has(SanitizerKind::Vptr);
}

std::pair<mlir::Value, mlir::Value>
CIRGenFunction::emitCheckedByteOffset(mlir::Location loc, mlir::Value index,
                                      uint64_t elementSize) {
  CIRGenBuilderTy &builder = getBuilder();
  unsigned intPtrWidth = cgm.getDataLayout().getPointerTypeSizeInBits(
      builder.getPointerTo(builder.getVoidTy()));
  auto intPtrTy = builder.getSIntNTy(intPtrWidth);

  mlir::Value offset = index;
  if (offset.getType() != intPtrTy)
    offset = builder.createIntCast(offset, intPtrTy);
  if (auto *op = offset.getDefiningOp())
    markNoSanitize(op);

  mlir::Value noOverflow = builder.getFalse(loc);
  markNoSanitize(noOverflow.getDefiningOp());

  if (auto constantIndex = getConstantIntValueAndSignedness(index)) {
    llvm::APInt indexValue = constantIndex->second
                                 ? constantIndex->first.sextOrTrunc(intPtrWidth)
                                 : constantIndex->first.zextOrTrunc(intPtrWidth);
    llvm::APInt offsetValue(intPtrWidth, 0, /*isSigned=*/true);
    bool overflow = false;
    if (elementSize != 1) {
      llvm::APInt scale(intPtrWidth, elementSize, /*isSigned=*/false);
      offsetValue = scale.smul_ov(indexValue, overflow);
    } else {
      offsetValue = indexValue;
    }
    return {builder.getConstAPInt(loc, intPtrTy, offsetValue),
            builder.getBool(overflow, loc)};
  }

  if (elementSize == 1)
    return {offset, noOverflow};

  mlir::Value scale = builder.getConstInt(loc, intPtrTy, elementSize);
  auto mul = cir::BinOpOverflowOp::create(builder, loc, intPtrTy,
                                          cir::BinOpOverflowKind::Mul, scale,
                                          offset);
  markNoSanitize(mul);
  return {mul.getResult(), mul.getOverflow()};
}

void CIRGenFunction::emitPointerOverflowCheck(mlir::Location loc,
                                              mlir::Value basePtr,
                                              mlir::Value totalOffset,
                                              mlir::Value offsetOverflows,
                                              bool signedIndices,
                                              bool isSubtraction,
                                              SourceLocation sourceLoc) {
  if (getLangOpts().PointerOverflowDefined ||
      !sanOpts.has(SanitizerKind::PointerOverflow))
    return;

  auto ptrTy = mlir::dyn_cast<cir::PointerType>(basePtr.getType());
  if (!ptrTy || ptrTy.getAddrSpace())
    return;

  if (auto cst = totalOffset.getDefiningOp<cir::ConstantOp>()) {
    if (auto intAttr = cst.getValueAttr<cir::IntAttr>())
      if (intAttr.getValue().isZero())
        return;
  }

  CIRGenBuilderTy &builder = getBuilder();
  auto intPtrTy = mlir::cast<cir::IntType>(
      cgm.getDataLayout().getIntPtrType(basePtr.getType()));
  auto signedOffsetTy =
      mlir::cast<cir::IntType>(totalOffset.getType());
  mlir::Value zero = builder.getConstInt(loc, intPtrTy, 0);
  mlir::Value signedZero = builder.getConstInt(loc, signedOffsetTy, 0);

  mlir::Value baseAsInt = builder.createPtrToInt(basePtr, intPtrTy);
  markNoSanitize(baseAsInt.getDefiningOp());
  mlir::Value offsetAsInt = totalOffset;
  if (offsetAsInt.getType() != intPtrTy)
    offsetAsInt = builder.createIntCast(offsetAsInt, intPtrTy);
  if (auto *op = offsetAsInt.getDefiningOp())
    markNoSanitize(op);
  mlir::Value computedGEP = builder.createAdd(loc, baseAsInt, offsetAsInt);
  markNoSanitize(computedGEP.getDefiningOp());

  llvm::SmallVector<mlir::Value, 2> checks;
  if (!cgm.getCodeGenOpts().NullPointerIsValid) {
    mlir::Value baseIsNotNull =
        builder.createCompare(loc, cir::CmpOpKind::ne, basePtr,
                              builder.getNullPtr(basePtr.getType(), loc));
    mlir::Value resultIsNotNull =
        builder.createCompare(loc, cir::CmpOpKind::ne, computedGEP, zero);
    markNoSanitize(baseIsNotNull.getDefiningOp());
    markNoSanitize(resultIsNotNull.getDefiningOp());
    mlir::Value validNullness =
        builder.createCompare(loc, cir::CmpOpKind::eq, baseIsNotNull,
                              resultIsNotNull);
    markNoSanitize(validNullness.getDefiningOp());
    checks.push_back(validNullness);
  }

  mlir::Value noOffsetOverflow = builder.createNot(offsetOverflows);
  markNoSanitize(noOffsetOverflow.getDefiningOp());

  mlir::Value validGEP = nullptr;
  if (signedIndices) {
    mlir::Value posOrZeroValid =
        builder.createCompare(loc, cir::CmpOpKind::ge, computedGEP, baseAsInt);
    mlir::Value posOrZeroOffset =
        builder.createCompare(loc, cir::CmpOpKind::ge, totalOffset, signedZero);
    mlir::Value negValid =
        builder.createCompare(loc, cir::CmpOpKind::lt, computedGEP, baseAsInt);
    markNoSanitize(posOrZeroValid.getDefiningOp());
    markNoSanitize(posOrZeroOffset.getDefiningOp());
    markNoSanitize(negValid.getDefiningOp());
    validGEP =
        builder.createSelect(loc, posOrZeroOffset, posOrZeroValid, negValid);
    if (auto *op = validGEP.getDefiningOp())
      markNoSanitize(op);
  } else if (!isSubtraction) {
    validGEP =
        builder.createCompare(loc, cir::CmpOpKind::ge, computedGEP, baseAsInt);
    markNoSanitize(validGEP.getDefiningOp());
  } else {
    validGEP =
        builder.createCompare(loc, cir::CmpOpKind::le, computedGEP, baseAsInt);
    markNoSanitize(validGEP.getDefiningOp());
  }
  validGEP = builder.createAnd(loc, validGEP, noOffsetOverflow);
  markNoSanitize(validGEP.getDefiningOp());
  checks.push_back(validGEP);

  mlir::Value allValid = checks.front();
  for (mlir::Value check : llvm::drop_begin(checks)) {
    allValid = builder.createAnd(loc, allValid, check);
    markNoSanitize(allValid.getDefiningOp());
  }

  auto ifOp = cir::IfOp::create(
      builder, loc, allValid, /*withElseRegion=*/true,
      [&](mlir::OpBuilder &, mlir::Location ifLoc) {
        builder.createYield(ifLoc);
      },
      [&](mlir::OpBuilder &, mlir::Location ifLoc) {
        if (cgm.getCodeGenOpts().SanitizeTrap.has(
                SanitizerKind::PointerOverflow)) {
          auto trap = cir::TrapOp::create(builder, ifLoc);
          trap->setAttr(
              "cir.ubsantrap_handler",
              mlir::IntegerAttr::get(
                  mlir::IntegerType::get(builder.getContext(), 8), 19));
          markNoSanitize(trap);
          return;
        }

        auto stubFnTy =
            cir::FuncType::get({intPtrTy, intPtrTy}, builder.getVoidTy());
        auto stubFn = cgm.createRuntimeFunction(
            stubFnTy, "__cir_ubsan_pointer_overflow_check", /*attrs=*/{},
            /*isLocal=*/false, /*assumeConvergent=*/false);
        auto call =
            builder.createCallOp(ifLoc, stubFn, {baseAsInt, computedGEP});
        call->setDiscardableAttr("cir.ubsan.handler_name",
                                 builder.getStringAttr("pointer_overflow"));

        StringRef handlerKind = "abort";
        if (cgm.getCodeGenOpts().SanitizeRecover.has(
                SanitizerKind::PointerOverflow))
          handlerKind = "recover";
        call->setDiscardableAttr("cir.ubsan.handler_kind",
                                 builder.getStringAttr(handlerKind));
        call->setDiscardableAttr("cir.nosanitize",
                                 mlir::UnitAttr::get(builder.getContext()));

        PresumedLoc presumedLoc =
            getContext().getSourceManager().getPresumedLoc(sourceLoc);
        if (presumedLoc.isValid()) {
          call->setDiscardableAttr("cir.ubsan.source_file",
                                   builder.getStringAttr(
                                       presumedLoc.getFilename()));
          call->setDiscardableAttr("cir.ubsan.source_line",
                                   builder.getI32IntegerAttr(
                                       presumedLoc.getLine()));
          call->setDiscardableAttr("cir.ubsan.source_column",
                                   builder.getI32IntegerAttr(
                                       presumedLoc.getColumn()));
        }

        if (handlerKind == "abort") {
          auto unreachable = cir::UnreachableOp::create(builder, ifLoc);
          markNoSanitize(unreachable);
        } else {
          auto yield = cir::YieldOp::create(builder, ifLoc);
          markNoSanitize(yield);
        }
      });
  setSanitizerBranchAttrs(builder, ifOp);
}

void CIRGenFunction::emitTypeCheck(TypeCheckKind TCK, SourceLocation Loc,
                                   mlir::Value Ptr, QualType Ty,
                                   CharUnits Alignment,
                                   SanitizerSet SkippedChecks,
                                   mlir::Value ArraySize) {
  if (!sanitizePerformTypeCheck())
    return;

  // Don't check pointers outside the default address space.
  if (auto ptrTy = mlir::dyn_cast<cir::PointerType>(Ptr.getType()))
    if (ptrTy.getAddrSpace())
      return;

  // Don't check pointers to volatile data.
  if (Ty.isVolatileQualified())
    return;

  mlir::Location loc = getLoc(Loc);

  // Determine whether this pointer is guaranteed non-null (e.g., alloca).
  bool isGuaranteedNonNull =
      SkippedChecks.has(SanitizerKind::Null) || findAllocaOp(Ptr);

  bool allowNullPointers = isNullPointerAllowed(TCK);
  auto intPtrTy = mlir::cast<cir::IntType>(convertType(getContext().getUIntPtrType()));

  // Pointer casts and similar operations may legitimately operate on null
  // pointers. Match OGCG by skipping the remaining checks entirely on null,
  // while still performing the full type/objectsize/alignment/vptr checks on
  // the non-null path.
  if (allowNullPointers && !isGuaranteedNonNull) {
    auto nullPtr = builder.getNullPtr(Ptr.getType(), loc);
    auto isNotNull =
        cir::CmpOp::create(builder, loc, cir::CmpOpKind::ne, Ptr, nullPtr);
    markNoSanitize(isNotNull);

    SanitizerSet nonNullPathSkippedChecks = SkippedChecks;
    nonNullPathSkippedChecks.set(SanitizerKind::Null, true);

    auto ifOp = cir::IfOp::create(
        builder, loc, isNotNull, /*withElseRegion=*/true,
        [&](mlir::OpBuilder &, mlir::Location) {
          emitTypeCheck(TCK, Loc, Ptr, Ty, Alignment, nonNullPathSkippedChecks,
                        ArraySize);
          builder.createYield(loc);
        },
        [&](mlir::OpBuilder &, mlir::Location) { builder.createYield(loc); });
    // Match OGCG: when null pointers are allowed, the outer null-skip wrapper
    // is an unweighted control-flow split. Only the inner sanitizer branches
    // (handler-vs-continue or vptr cache-hit-vs-miss) carry profile weights.
    markNoSanitize(ifOp);
    return;
  }

  SmallVector<std::pair<mlir::Value, SanitizerKind::SanitizerOrdinal>, 3>
      checks;
  auto addCheck = [&](mlir::Value check,
                      SanitizerKind::SanitizerOrdinal ordinal) {
    if (auto *op = check.getDefiningOp())
      markNoSanitize(op);
    check = emitGuardedUBSanCheck(loc, check, ordinal);
    checks.emplace_back(check, ordinal);
  };
  auto combineChecks = [&](mlir::Value lhs, mlir::Value rhs) -> mlir::Value {
    if (!lhs)
      return rhs;
    if (!rhs)
      return lhs;
    mlir::Value result = builder.createAnd(loc, lhs, rhs);
    if (auto *op = result.getDefiningOp())
      markNoSanitize(op);
    return result;
  };

  if (sanOpts.has(SanitizerKind::Null) &&
      !SkippedChecks.has(SanitizerKind::Null) && !isGuaranteedNonNull) {
    auto nullPtr = builder.getNullPtr(Ptr.getType(), loc);
    auto isNotNull =
        cir::CmpOp::create(builder, loc, cir::CmpOpKind::ne, Ptr, nullPtr);
    markNoSanitize(isNotNull);
    addCheck(isNotNull, SanitizerKind::SO_Null);
  }

  if (sanOpts.has(SanitizerKind::ObjectSize) &&
      !SkippedChecks.has(SanitizerKind::ObjectSize) &&
      !Ty->isIncompleteType()) {
    uint64_t tySize = cgm.getMinimumObjectSize(Ty).getQuantity();
    mlir::Value size = builder.getConstInt(loc, intPtrTy, tySize);
    if (ArraySize) {
      bool zeroSizedArray = false;
      bool singleElementArray = false;
      if (auto arraySizeCst = ArraySize.getDefiningOp<cir::ConstantOp>())
        if (auto intAttr = arraySizeCst.getValueAttr<cir::IntAttr>()) {
          zeroSizedArray = intAttr.getValue().isZero();
          singleElementArray = intAttr.getValue().isOne();
        }

      mlir::Value checkedArraySize = ArraySize;
      if (checkedArraySize.getType() != intPtrTy)
        checkedArraySize = builder.createIntCast(checkedArraySize, intPtrTy);
      if (auto *op = checkedArraySize.getDefiningOp())
        markNoSanitize(op);

      if (zeroSizedArray) {
        size = builder.getConstInt(loc, intPtrTy, 0);
      } else if (!singleElementArray) {
        size =
            builder.createMul(loc, size, checkedArraySize,
                              cir::OverflowBehavior::NoUnsignedWrap);
        if (auto *op = size.getDefiningOp())
          markNoSanitize(op);
      }
    }

    bool sizeIsZero = false;
    if (auto sizeCst = size.getDefiningOp<cir::ConstantOp>())
      if (auto intAttr = sizeCst.getValueAttr<cir::IntAttr>())
        sizeIsZero = intAttr.getValue().isZero();

    if (!sizeIsZero) {
      mlir::Value objSize = cir::ObjSizeOp::create(
          builder, loc, intPtrTy, Ptr, /*min=*/false, /*nullUnknown=*/false,
          /*dynamic=*/false);
      markNoSanitize(objSize.getDefiningOp());
      mlir::Value largeEnough =
          builder.createCompare(loc, cir::CmpOpKind::ge, objSize, size);
      addCheck(largeEnough, SanitizerKind::SO_ObjectSize);
    }
  }

  CharUnits alignValue = Alignment;
  if (sanOpts.has(SanitizerKind::Alignment) &&
      !SkippedChecks.has(SanitizerKind::Alignment)) {
    if (!Ty->isIncompleteType() && alignValue.isZero())
      alignValue = cgm.getNaturalTypeAlignment(Ty, nullptr, nullptr,
                                               /*forPointeeType=*/true);

    bool needsAlignCheck = !alignValue.isZero() && alignValue > CharUnits::One();
    if (needsAlignCheck)
      if (auto allocaOp = findAllocaOp(Ptr))
        if (auto alignAttr = allocaOp.getAlignmentAttr())
          needsAlignCheck = alignAttr.getInt() < alignValue.getQuantity();
    if (needsAlignCheck)
      if (auto knownBaseAlignment = findKnownBaseAlignment(cgm, Ptr))
        needsAlignCheck = *knownBaseAlignment < alignValue;

    if (needsAlignCheck) {
      mlir::Value ptrAsInt =
          builder.createPtrToInt(Ptr, intPtrTy);
      markNoSanitize(ptrAsInt.getDefiningOp());
      mlir::Value mask = builder.getConstInt(loc, intPtrTy,
                                             alignValue.getQuantity() - 1);
      mlir::Value masked = builder.createAnd(loc, ptrAsInt, mask);
      if (auto *op = masked.getDefiningOp())
        markNoSanitize(op);
      mlir::Value zero = builder.getConstInt(loc, intPtrTy, 0);
      mlir::Value aligned =
          builder.createCompare(loc, cir::CmpOpKind::eq, masked, zero);
      bool isAlwaysAligned = false;
      if (auto alignedConst = aligned.getDefiningOp<cir::ConstantOp>())
        if (auto alignedAttr = alignedConst.getValueAttr<cir::BoolAttr>())
          isAlwaysAligned = alignedAttr.getValue();
      if (!isAlwaysAligned)
        addCheck(aligned, SanitizerKind::SO_Alignment);
    }
  }

  if (!checks.empty()) {
    auto isTrap = [&](SanitizerKind::SanitizerOrdinal ordinal) {
      return cgm.getCodeGenOpts().SanitizeTrap.has(ordinal);
    };
    auto isRecover = [&](SanitizerKind::SanitizerOrdinal ordinal) {
      return !isTrap(ordinal) &&
             cgm.getCodeGenOpts().SanitizeRecover.has(ordinal);
    };

    mlir::Value trapPass, fatalPass, recoverPass;
    for (auto [check, ordinal] : checks) {
      if (isTrap(ordinal))
        trapPass = combineChecks(trapPass, check);
      else if (isRecover(ordinal))
        recoverPass = combineChecks(recoverPass, check);
      else
        fatalPass = combineChecks(fatalPass, check);
    }

    // Use alignValue only if alignment checking is enabled; otherwise
    // use 1 as the default log2 alignment (matching OGCG's
    // AlignVal ? Log2(*AlignVal) : 1 where AlignVal is only set when
    // SanOpts.has(Alignment)).
    bool alignCheckEnabled = sanOpts.has(SanitizerKind::Alignment) &&
                             !SkippedChecks.has(SanitizerKind::Alignment);
    unsigned log2Align = (alignCheckEnabled && alignValue.getQuantity())
                             ? llvm::Log2_64(alignValue.getQuantity())
                             : 1;

    if (trapPass) {
      auto trapIf = cir::IfOp::create(
          builder, loc, trapPass, /*withElseRegion=*/true,
          [&](mlir::OpBuilder &, mlir::Location ifLoc) {
            builder.createYield(ifLoc);
          },
          [&](mlir::OpBuilder &, mlir::Location ifLoc) {
            auto trap = cir::TrapOp::create(builder, ifLoc);
            trap->setAttr(
                "cir.ubsantrap_handler",
                mlir::IntegerAttr::get(
                    mlir::IntegerType::get(builder.getContext(), 8), 22));
            markNoSanitize(trap);
          });
      setSanitizerBranchAttrs(builder, trapIf);
    }

    if (fatalPass || recoverPass) {
      mlir::Value ptrAsInt = builder.createPtrToInt(Ptr, intPtrTy);
      markNoSanitize(ptrAsInt.getDefiningOp());

      mlir::Value jointPass = combineChecks(fatalPass, recoverPass);
      auto handlerIf = cir::IfOp::create(
          builder, loc, jointPass, /*withElseRegion=*/true,
          [&](mlir::OpBuilder &, mlir::Location ifLoc) {
            builder.createYield(ifLoc);
          },
          [&](mlir::OpBuilder &, mlir::Location ifLoc) {
            if (fatalPass && recoverPass) {
              auto innerIf = cir::IfOp::create(
                  builder, ifLoc, fatalPass, /*withElseRegion=*/true,
                  [&](mlir::OpBuilder &, mlir::Location innerLoc) {
                    emitTypeMismatchStubCall(*this, innerLoc, Loc, ptrAsInt, Ty,
                                             log2Align, TCK, "recover");
                    builder.createYield(innerLoc);
                  },
                  [&](mlir::OpBuilder &, mlir::Location innerLoc) {
                    emitTypeMismatchStubCall(*this, innerLoc, Loc, ptrAsInt, Ty,
                                             log2Align, TCK, "abort");
                    auto unreachable =
                        cir::UnreachableOp::create(builder, innerLoc);
                    markNoSanitize(unreachable);
                  });
              setSanitizerBranchAttrs(builder, innerIf);
              return;
            }

            StringRef handlerKind = fatalPass ? "abort" : "recover";
            emitTypeMismatchStubCall(*this, ifLoc, Loc, ptrAsInt, Ty,
                                     log2Align, TCK, handlerKind);
            if (handlerKind == "abort") {
              auto unreachable = cir::UnreachableOp::create(builder, ifLoc);
              markNoSanitize(unreachable);
            } else {
              builder.createYield(ifLoc);
            }
          });
      setSanitizerBranchAttrs(builder, handlerIf);
    }
  }

  // --- Vptr check ---
  // If the pointer is non-null and vptr checks are enabled, emit the
  // vptr type check. The check must be guarded by a null check because
  // loading the vtable pointer from a null pointer would crash.
  if (sanOpts.has(SanitizerKind::Vptr) &&
      !SkippedChecks.has(SanitizerKind::Vptr) &&
      isVptrCheckRequired(TCK, Ty)) {

    // Ensure the pointer is non-null before loading the vtable pointer.
    // If there's no compile-time guarantee, emit a null check and skip
    // the vptr check for null pointers (matching OGCG CGExpr.cpp:888-898).
    auto emitVptrCheckBody = [&]() {
    // Compute mangled RTTI name for the type hash and no-sanitize-list check.
    SmallString<64> mangledName;
    llvm::raw_svector_ostream out(mangledName);
    cgm.getCXXABI().getMangleContext().mangleCXXRTTI(Ty.getUnqualifiedType(),
                                                     out);

    // Check if this type is in the no_sanitize list.
    if (!getContext().getNoSanitizeList().containsType(SanitizerKind::Vptr,
                                                       out.str())) {
      auto intPtrTy = convertType(getContext().getUIntPtrType());
      auto ptrAsInt = cir::CastOp::create(builder, loc, intPtrTy,
                                          cir::CastKind::ptr_to_int, Ptr);
      markNoSanitize(ptrAsInt);

      // Keep the concrete RTTI symbol live through lowering. The deferred
      // LLVM rewrite uses this operand to populate the expected typeinfo in the
      // final UBSan descriptor, including cases where the RTTI is otherwise
      // referenced only by sanitizer metadata.
      auto rttiAttr =
          mlir::cast<mlir::TypedAttr>(
              cgm.getAddrOfRTTIDescriptor(loc, Ty.getUnqualifiedType()));
      auto rtti = builder.getConstant(loc, rttiAttr);

      // Get or create the vptr check stub function.
      auto stubFnTy =
          cir::FuncType::get({intPtrTy, rtti.getType()}, builder.getVoidTy());
      auto stubFn = cgm.createRuntimeFunction(
          stubFnTy, "__cir_ubsan_vptr_check", /*attrs=*/{},
          /*isLocal=*/false, /*assumeConvergent=*/false);

      auto call = builder.createCallOp(loc, stubFn, {ptrAsInt, rtti});

      // Attach UBSan metadata.
      PresumedLoc PLoc = getContext().getSourceManager().getPresumedLoc(Loc);
      if (PLoc.isValid()) {
        call->setDiscardableAttr("cir.ubsan.source_file",
                                 builder.getStringAttr(PLoc.getFilename()));
        call->setDiscardableAttr("cir.ubsan.source_line",
                                 builder.getI32IntegerAttr(PLoc.getLine()));
        call->setDiscardableAttr("cir.ubsan.source_column",
                                 builder.getI32IntegerAttr(PLoc.getColumn()));
      }
      SmallString<32> typeName;
      cgm.getDiags().ConvertArgToString(
          DiagnosticsEngine::ak_qualtype, (intptr_t)Ty.getAsOpaquePtr(),
          StringRef(), StringRef(), {}, typeName, {});
      call->setDiscardableAttr("cir.ubsan.type_name",
                               builder.getStringAttr(typeName));
      call->setDiscardableAttr(
          "cir.ubsan.type_check_kind",
          builder.getI8IntegerAttr(static_cast<uint8_t>(TCK)));
      call->setDiscardableAttr(
          "cir.ubsan.type_hash",
          builder.getI64IntegerAttr(
              static_cast<int64_t>(llvm::xxh3_64bits(out.str()))));
      call->setDiscardableAttr("cir.ubsan.rtti_name",
                               builder.getStringAttr(out.str()));
      // Determine handler kind.
      StringRef handlerKind = "abort";
      if (cgm.getCodeGenOpts().SanitizeTrap.has(SanitizerKind::Vptr))
        handlerKind = "trap";
      else if (cgm.getCodeGenOpts().SanitizeRecover.has(SanitizerKind::Vptr))
        handlerKind = "recover";
      call->setDiscardableAttr("cir.ubsan.handler_kind",
                               builder.getStringAttr(handlerKind));
      call->setDiscardableAttr("cir.nosanitize",
                               mlir::UnitAttr::get(builder.getContext()));
      call->setDiscardableAttr(
          "cir.ubsan.handler_name",
          builder.getStringAttr("dynamic_type_cache_miss"));
    }
    }; // end emitVptrCheckBody lambda

    if (!isGuaranteedNonNull) {
      // Emit null check: skip vptr check for null pointers.
      auto nullPtr = builder.getNullPtr(Ptr.getType(), loc);
      auto isNotNull =
          cir::CmpOp::create(builder, loc, cir::CmpOpKind::ne, Ptr, nullPtr);
      markNoSanitize(isNotNull);

      auto ifOp = cir::IfOp::create(
          builder, loc, isNotNull, /*withElseRegion=*/false,
          [&](mlir::OpBuilder &, mlir::Location) {
            emitVptrCheckBody();
            builder.createYield(loc);
          });
      // Match OGCG: the outer null guard only carries !nosanitize.
      markNoSanitize(ifOp);
    } else {
      emitVptrCheckBody();
    }
  }
}

namespace {

/// \p StructAccessBase returns the base \p Expr of a field access. It returns
/// either a \p DeclRefExpr, representing the base pointer to the struct, i.e.:
///
///     p in p->a.b.c
///
/// or a \p MemberExpr, if the \p MemberExpr has the \p RecordDecl we're
/// looking for:
///
///     struct s {
///       struct s *ptr;
///       int count;
///       char array[] __attribute__((counted_by(count)));
///     };
///
/// If we have an expression like \p p->ptr->array[index], we want the
/// \p MemberExpr for \p p->ptr instead of \p p.
class StructAccessBase
    : public ConstStmtVisitor<StructAccessBase, const Expr *> {
  const RecordDecl *ExpectedRD;

  bool isExpectedRecordDecl(const Expr *E) const {
    QualType Ty = E->getType();
    if (Ty->isPointerType())
      Ty = Ty->getPointeeType();
    return ExpectedRD == Ty->getAsRecordDecl();
  }

public:
  StructAccessBase(const RecordDecl *ExpectedRD) : ExpectedRD(ExpectedRD) {}

  const Expr *Visit(const Expr *E) {
    return ConstStmtVisitor<StructAccessBase, const Expr *>::Visit(E);
  }

  const Expr *VisitStmt(const Stmt *S) { return nullptr; }

  const Expr *VisitDeclRefExpr(const DeclRefExpr *E) {
    return isExpectedRecordDecl(E) ? E : nullptr;
  }
  const Expr *VisitMemberExpr(const MemberExpr *E) {
    if (isExpectedRecordDecl(E) && E->isArrow())
      return E;
    const Expr *Res = Visit(E->getBase());
    return !Res && isExpectedRecordDecl(E) ? E : Res;
  }
  const Expr *VisitCompoundLiteralExpr(const CompoundLiteralExpr *E) {
    return isExpectedRecordDecl(E) ? E : nullptr;
  }
  const Expr *VisitCallExpr(const CallExpr *E) {
    return isExpectedRecordDecl(E) ? E : nullptr;
  }

  const Expr *VisitArraySubscriptExpr(const ArraySubscriptExpr *E) {
    if (isExpectedRecordDecl(E))
      return E;
    return Visit(E->getBase());
  }
  const Expr *VisitCastExpr(const CastExpr *E) {
    if (E->getCastKind() == CK_LValueToRValue)
      return isExpectedRecordDecl(E) ? E : nullptr;
    return Visit(E->getSubExpr());
  }
  const Expr *VisitParenExpr(const ParenExpr *E) {
    return Visit(E->getSubExpr());
  }
  const Expr *VisitUnaryAddrOf(const UnaryOperator *E) {
    return Visit(E->getSubExpr());
  }
  const Expr *VisitUnaryDeref(const UnaryOperator *E) {
    return Visit(E->getSubExpr());
  }
};

} // end anonymous namespace

mlir::ArrayAttr CIRGenFunction::buildAllocToken(QualType allocType) const {
  auto allocToken =
      infer_alloc::getAllocTokenMetadata(allocType, getContext());
  if (!allocToken)
    return {};

  return mlir::ArrayAttr::get(
      &getMLIRContext(),
      {cgm.getStringAttrCached(allocToken->TypeName),
       mlir::BoolAttr::get(&getMLIRContext(), allocToken->ContainsPointer)});
}

mlir::ArrayAttr CIRGenFunction::buildAllocToken(const CallExpr *e,
                                                bool allowUnknown) const {
  QualType allocType =
      infer_alloc::inferPossibleType(e, getContext(), CurCast);
  if (allocType.isNull())
    return allowUnknown ? mlir::ArrayAttr::get(&getMLIRContext(), {}) : mlir::ArrayAttr();

  if (auto payload = buildAllocToken(allocType))
    return payload;

  return allowUnknown ? mlir::ArrayAttr::get(&getMLIRContext(), {}) : mlir::ArrayAttr();
}

void CIRGenFunction::maybeAttachAllocToken(cir::CIRCallOpInterface callOp,
                                           const CallExpr *e) {
  if (!callOp)
    return;
  if (auto payload = buildAllocToken(e))
    callOp->setAttr("cir.alloc_token", payload);
}

void CIRGenFunction::maybeAttachAllocToken(cir::CIRCallOpInterface callOp,
                                           QualType allocType) {
  if (!callOp)
    return;
  if (auto payload = buildAllocToken(allocType))
    callOp->setAttr("cir.alloc_token", payload);
}

mlir::Value CIRGenFunction::getAddrOfCountedByField(
    const Expr *base, const FieldDecl *countDecl) {
  const RecordDecl *rd = countDecl->getParent()->getOuterLexicalRecordContext();

  // Find the base struct expr (i.e. p in p->a.b.c.d).
  const Expr *structBase = StructAccessBase(rd).Visit(base);
  if (!structBase || structBase->HasSideEffects(getContext()))
    return {};

  Address baseAddr = Address::invalid();
  if (structBase->getType()->isPointerType()) {
    LValueBaseInfo baseInfo;
    TBAAAccessInfo tbaaInfo;
    baseAddr = emitPointerWithAlignment(structBase, &baseInfo, &tbaaInfo);
  } else if (structBase->isLValue()) {
    LValue lv = emitLValue(structBase);
    baseAddr = lv.getAddress();
  } else {
    return {};
  }

  QualType recordTy = getContext().getCanonicalTagType(rd);
  LValue currentLV = makeAddrLValue(baseAddr, recordTy);

  // If the count field is a direct member of the outer record, access it
  // directly. Otherwise, walk through anonymous struct intermediaries
  // using the IndirectFieldDecl chain.
  if (countDecl->getParent() == rd) {
    LValue countLV = emitLValueForField(currentLV, countDecl);
    return countLV.getPointer();
  }

  // The count field is in a nested anonymous struct. Find the
  // IndirectFieldDecl in the outer record that chains to it.
  for (const auto *decl : rd->decls()) {
    if (const auto *ifd = dyn_cast<IndirectFieldDecl>(decl)) {
      if (ifd->getAnonField() == countDecl) {
        // Walk the chain: each entry is a FieldDecl for an anonymous struct
        // member or the final target field.
        for (const auto *nd : ifd->chain()) {
          const auto *chainField = cast<FieldDecl>(nd);
          currentLV = emitLValueForField(currentLV, chainField);
        }
        return currentLV.getPointer();
      }
    }
  }

  // Fallback: direct access (should not reach here for valid counted_by).
  LValue countLV = emitLValueForField(currentLV, countDecl);
  return countLV.getPointer();
}

mlir::Value CIRGenFunction::emitLoadOfCountedByField(
    const Expr *base, const FieldDecl *famDecl, const FieldDecl *countDecl) {
  mlir::Value addr = getAddrOfCountedByField(base, countDecl);
  if (!addr)
    return {};

  // Build an LValue from the address and load the scalar.
  auto ptrTy = mlir::cast<cir::PointerType>(addr.getType());
  QualType countQualTy = countDecl->getType();
  CharUnits alignment = getContext().getDeclAlign(countDecl);
  LValue countLV = makeAddrLValue(
      Address(addr, ptrTy.getPointee(), alignment), countQualTy);
  return emitLoadOfScalar(countLV, countDecl->getLocation());
}

/// Get the address of a zero-sized field within a record. The resulting address
/// doesn't necessarily have the right type.
Address CIRGenFunction::emitAddrOfFieldStorage(Address base,
                                               const FieldDecl *field,
                                               llvm::StringRef fieldName,
                                               unsigned fieldIndex) {
  if (isEmptyFieldForLayout(getContext(), field)) {
    // Empty fields for layout purposes are not part of the CIR record type.
    // Compute the address from the ASTRecordLayout field offset, mirroring
    // OGCG's emitAddrOfZeroSizeField.
    mlir::Location loc = getLoc(field->getLocation());
    CharUnits offset = getContext().toCharUnitsFromBits(
        getContext().getFieldOffset(field));
    if (offset.isZero())
      return Address(
          builder.createBitcast(
              loc, base.getPointer(),
              cir::PointerType::get(convertType(field->getType()))),
          base.getAlignment());
    // Non-zero offset for a zero-sized field is uncommon but possible.
    // Cast to i8*, stride by offset, cast to field type pointer.
    auto i8Ty = builder.getUIntNTy(8);
    auto i8PtrTy = cir::PointerType::get(i8Ty);
    mlir::Value baseI8 = builder.createBitcast(loc, base.getPointer(), i8PtrTy);
    mlir::Value offsetVal = builder.getConstInt(
        loc, mlir::cast<cir::IntType>(builder.getSIntNTy(64)),
        offset.getQuantity());
    mlir::Value result = builder.createPtrStride(loc, baseI8, offsetVal);
    result = builder.createBitcast(
        loc, result,
        cir::PointerType::get(convertType(field->getType())));
    return Address(result, CharUnits::One());
  }

  mlir::Location loc = getLoc(field->getLocation());

  // Always use the member type from the record type to ensure consistency
  // with what the layout builder stored. After a type cache clear (triggered
  // by skippedLayout when completing a record type), convertType may return
  // a different type for function pointer fields (real vs placeholder), which
  // would cause GetMemberOp verification to fail with "member type mismatch".
  // For potentially overlapping fields (i.e. [[no_unique_address]]), this is
  // also necessary because the layout stores the base subobject type, not the
  // complete type.
  auto recordPtrTy = mlir::cast<cir::PointerType>(base.getPointer().getType());
  auto recordTy = mlir::cast<cir::RecordType>(recordPtrTy.getPointee());
  if (LLVM_UNLIKELY(fieldIndex >= recordTy.getMembers().size())) {
    // Some anonymous-union/indirect-member initialization paths can see a
    // stale/aliased field index for the current record type. Avoid crashing on
    // out-of-range indexing and compute the field storage address directly from
    // the AST field offset, matching the zero-sized-field fallback style.
    CharUnits offset = getContext().toCharUnitsFromBits(
        getContext().getFieldOffset(field));
    auto i8Ty = builder.getUIntNTy(8);
    auto i8PtrTy = builder.getPointerTo(i8Ty, recordPtrTy.getAddrSpace());
    mlir::Value baseI8 = builder.createBitcast(loc, base.getPointer(), i8PtrTy);
    mlir::Value result = baseI8;
    if (!offset.isZero()) {
      mlir::Value offsetVal = builder.getConstInt(
          loc, mlir::cast<cir::IntType>(builder.getSIntNTy(64)),
          offset.getQuantity());
      result = builder.createPtrStride(loc, baseI8, offsetVal);
    }
    result = builder.createBitcast(
        loc, result,
        builder.getPointerTo(convertType(field->getType()),
                             recordPtrTy.getAddrSpace()));
    return Address(result, base.getAlignment().alignmentAtOffset(offset));
  }
  mlir::Type fieldType = recordTy.getMembers()[fieldIndex];
  auto fieldPtr = builder.getPointerTo(fieldType, recordPtrTy.getAddrSpace());
  // For most cases fieldName is the same as field->getName() but for lambdas,
  // which do not currently carry the name, so it can be passed down from the
  // CaptureStmt.
  cir::GetMemberOp memberAddr = builder.createGetMember(
      loc, fieldPtr, base.getPointer(), fieldName, fieldIndex);

  // Compute alignment from the AST field offset directly. This avoids
  // depending on record-field index maps in cases with indirect/anonymous
  // member initialization where CIR layout indexes may not match the current
  // record type view.
  CharUnits offset = getContext().toCharUnitsFromBits(
      getContext().getFieldOffset(field));
  return Address(memberAddr, base.getAlignment().alignmentAtOffset(offset));
}

/// Given an expression of pointer type, try to
/// derive a more accurate bound on the alignment of the pointer.
Address CIRGenFunction::emitPointerWithAlignment(const Expr *expr,
                                                 LValueBaseInfo *baseInfo,
                                                 TBAAAccessInfo *tbaaInfo,
                                                 bool isKnownNonNull) {
  // OGCG has no assertion here — the expression type may be an array that
  // will decay to a pointer through a CK_ArrayToPointerDecay cast.
  expr = expr->IgnoreParens();

  // Casts:
  if (auto const *ce = dyn_cast<CastExpr>(expr)) {
    if (const auto *ece = dyn_cast<ExplicitCastExpr>(ce))
      cgm.emitExplicitCastExprType(ece, this);

    switch (ce->getCastKind()) {
    // Non-converting casts (but not C's implicit conversion from void*).
    case CK_BitCast:
    case CK_NoOp:
    case CK_AddressSpaceConversion: {
      if (const auto *ptrTy =
              ce->getSubExpr()->getType()->getAs<PointerType>()) {
        if (ptrTy->getPointeeType()->isVoidType())
          break;

        LValueBaseInfo innerBaseInfo;
        TBAAAccessInfo innerTBAAInfo;
        Address addr =
            emitPointerWithAlignment(ce->getSubExpr(), &innerBaseInfo,
                                     &innerTBAAInfo, isKnownNonNull);
        if (baseInfo)
          *baseInfo = innerBaseInfo;
        if (tbaaInfo)
          *tbaaInfo = innerTBAAInfo;

        if (isa<ExplicitCastExpr>(ce)) {
          LValueBaseInfo targetTypeBaseInfo;
          TBAAAccessInfo targetTypeTBAAInfo;

          const QualType pointeeType = expr->getType()->getPointeeType();
          const CharUnits align = cgm.getNaturalTypeAlignment(
              pointeeType, &targetTypeBaseInfo, &targetTypeTBAAInfo);
          if (tbaaInfo)
            *tbaaInfo =
                cgm.mergeTBAAInfoForCast(*tbaaInfo, targetTypeTBAAInfo);

          // If the source l-value is opaque, honor the alignment of the
          // casted-to type.
          if (innerBaseInfo.getAlignmentSource() != AlignmentSource::Decl) {
            if (baseInfo)
              baseInfo->mergeForCast(targetTypeBaseInfo);
            addr = Address(addr.getPointer(), addr.getElementType(), align);
          }
        }

        if (sanOpts.has(SanitizerKind::CFIUnrelatedCast) &&
            ce->getCastKind() == CK_BitCast) {
          if (const auto *ptrTy = expr->getType()->getAs<PointerType>())
            emitVTablePtrCheckForCast(
                ptrTy->getPointeeType(), addr, /*mayBeNull=*/true,
                CFICastCheckKind::UnrelatedCast, ce->getBeginLoc());
        }

        const mlir::Type eltTy =
            convertTypeForMem(expr->getType()->getPointeeType());
        addr = getBuilder().createElementBitCast(getLoc(expr->getSourceRange()),
                                                 addr, eltTy);
        if (ce->getCastKind() == CK_AddressSpaceConversion) {
          addr = getBuilder().createAddrSpaceCast(
              addr, convertType(expr->getType()), eltTy);
        }

        // OGCG calls authPointerToPointerCast here for pointer authentication
        // re-signing when casting between pointer types.
        if (ce->getSubExpr()->getType()->isPointerType() &&
            expr->getType()->isPointerType()) {
          mlir::Value authedPtr = authPointerToPointerCast(
              addr.getPointer(), ce->getSubExpr()->getType(), expr->getType());
          addr = addr.withPointer(authedPtr);
        }

        return addr;
      }
      break;
    }

    case CK_ArrayToPointerDecay:
      return emitArrayToPointerDecay(ce->getSubExpr(), baseInfo, tbaaInfo);

    case CK_UncheckedDerivedToBase:
    case CK_DerivedToBase: {
      // TODO: Support accesses to members of base classes in TBAA. For now,
      // conservatively reset TBAA to the base class type.
      if (tbaaInfo)
        *tbaaInfo = cgm.getTBAAAccessInfo(expr->getType());
      Address addr = emitPointerWithAlignment(
          ce->getSubExpr(), baseInfo, nullptr,
          isKnownNonNull ||
              ce->getCastKind() == CK_UncheckedDerivedToBase);
      const CXXRecordDecl *derived =
          ce->getSubExpr()->getType()->getPointeeCXXRecordDecl();
      return getAddressOfBaseClass(addr, derived, ce->path(),
                                   shouldNullCheckClassCastValue(ce),
                                   ce->getExprLoc());
    }

    case CK_AnyPointerToBlockPointerCast:
    case CK_BaseToDerived:
    case CK_BaseToDerivedMemberPointer:
    case CK_BlockPointerToObjCPointerCast:
    case CK_BuiltinFnToFnPtr:
    case CK_CPointerToObjCPointerCast:
    case CK_ConstructorConversion:
    case CK_DerivedToBaseMemberPointer:
    case CK_Dynamic:
    case CK_FunctionToPointerDecay:
    case CK_IntegralToPointer:
    case CK_LValueToRValue:
    case CK_LValueToRValueBitCast:
    case CK_NullToMemberPointer:
    case CK_NullToPointer:
    case CK_ReinterpretMemberPointer:
    case CK_UserDefinedConversion:
      // Common pointer conversions, nothing to do here.
      // TODO: Is there any reason to treat base-to-derived conversions
      // specially?
      break;

    // Atomic-to-non-atomic and non-atomic-to-atomic casts can appear on
    // pointer types (e.g. `int *_Atomic`). They are no-ops for pointer
    // alignment purposes, matching OGCG's default fallthrough behavior.
    case CK_AtomicToNonAtomic:
    case CK_NonAtomicToAtomic:
      break;

    case CK_ARCConsumeObject:
    case CK_ARCExtendBlockObject:
    case CK_ARCProduceObject:
    case CK_ARCReclaimReturnedObject:
    case CK_BooleanToSignedIntegral:
    case CK_CopyAndAutoreleaseBlockObject:
    case CK_Dependent:
    case CK_FixedPointCast:
    case CK_FixedPointToBoolean:
    case CK_FixedPointToFloating:
    case CK_FixedPointToIntegral:
    case CK_FloatingCast:
    case CK_FloatingComplexCast:
    case CK_FloatingComplexToBoolean:
    case CK_FloatingComplexToIntegralComplex:
    case CK_FloatingComplexToReal:
    case CK_FloatingRealToComplex:
    case CK_FloatingToBoolean:
    case CK_FloatingToFixedPoint:
    case CK_FloatingToIntegral:
    case CK_HLSLAggregateSplatCast:
    case CK_HLSLArrayRValue:
    case CK_HLSLElementwiseCast:
    case CK_HLSLVectorTruncation:
    case CK_HLSLMatrixTruncation:
    case CK_IntToOCLSampler:
    case CK_IntegralCast:
    case CK_IntegralComplexCast:
    case CK_IntegralComplexToBoolean:
    case CK_IntegralComplexToFloatingComplex:
    case CK_IntegralComplexToReal:
    case CK_IntegralRealToComplex:
    case CK_IntegralToBoolean:
    case CK_IntegralToFixedPoint:
    case CK_IntegralToFloating:
    case CK_LValueBitCast:
    case CK_MatrixCast:
    case CK_MemberPointerToBoolean:
    case CK_ObjCObjectLValueCast:
    case CK_PointerToBoolean:
    case CK_PointerToIntegral:
    case CK_ToUnion:
    case CK_ToVoid:
    case CK_VectorSplat:
    case CK_ZeroToOCLOpaqueType:
      llvm_unreachable("unexpected cast for emitPointerWithAlignment");
    }
  }

  // Unary &
  if (const UnaryOperator *uo = dyn_cast<UnaryOperator>(expr)) {
    // TODO(cir): maybe we should use cir.unary for pointers here instead.
    if (uo->getOpcode() == UO_AddrOf) {
      LValue lv = emitLValue(uo->getSubExpr());
      if (baseInfo)
        *baseInfo = lv.getBaseInfo();
      if (tbaaInfo)
        *tbaaInfo = lv.getTBAAInfo();
      Address addr = lv.getAddress();
      if (isKnownNonNull && !addr.isKnownNonNull())
        addr.setKnownNonNull();
      return addr;
    }
  }

  // std::addressof and variants.
  if (auto const *call = dyn_cast<CallExpr>(expr)) {
    switch (call->getBuiltinCallee()) {
    default:
      break;
    case Builtin::BIaddressof:
    case Builtin::BI__addressof:
    case Builtin::BI__builtin_addressof: {
      LValue lv = emitLValue(call->getArg(0));
      if (baseInfo)
        *baseInfo = lv.getBaseInfo();
      if (tbaaInfo)
        *tbaaInfo = lv.getTBAAInfo();
      Address addr = lv.getAddress();
      if (isKnownNonNull && !addr.isKnownNonNull())
        addr.setKnownNonNull();
      return addr;
    }
    }
  }

  // Handle pointer arithmetic (ptr + offset) to propagate alignment.
  // When dereferencing the result of pointer arithmetic (e.g.,
  // *(int*)((char*)p + 1)), the alignment must account for the offset.
  // Without this, CIR would use the base pointer's alignment (e.g., 4)
  // even though the offset makes it only 1-byte aligned.
  //
  // Each operand is emitted exactly once, matching OGCG's
  // emitPointerArithmetic (CGExpr.cpp:1398-1434). The previous
  // implementation called emitScalarExpr on the full expression, which
  // re-emitted the pointer operand and caused double evaluation of
  // side effects.
  if (const auto *bo = dyn_cast<BinaryOperator>(expr)) {
    if (bo->isAdditiveOp()) {
      Expr *pointerOperand = bo->getLHS();
      Expr *indexOperand = bo->getRHS();
      bool isSubtraction = bo->getOpcode() == BO_Sub;
      bool swapped = false;

      if (!isSubtraction && !pointerOperand->getType()->isAnyPointerType()) {
        std::swap(pointerOperand, indexOperand);
        swapped = true;
      }

      // Emit operands in the correct evaluation order.
      // C++17 requires left-to-right evaluation of operands. When swapped
      // (int + ptr), the index was originally the LHS, so emit it first.
      LValueBaseInfo innerBaseInfo;
      TBAAAccessInfo innerTBAAInfo;
      Address baseAddr = Address::invalid();
      mlir::Value index;
      if (swapped) {
        index = emitScalarExpr(indexOperand);
        baseAddr = emitPointerWithAlignment(pointerOperand, &innerBaseInfo,
                                            &innerTBAAInfo);
      } else {
        baseAddr = emitPointerWithAlignment(pointerOperand, &innerBaseInfo,
                                            &innerTBAAInfo);
        index = emitScalarExpr(indexOperand);
      }

      if (baseInfo)
        *baseInfo = innerBaseInfo;
      if (tbaaInfo)
        *tbaaInfo = innerTBAAInfo;

      // Compute alignment at the offset.
      QualType pointeeTy = bo->getType()->getPointeeType();
      CharUnits eltSize = getContext().getTypeSizeInChars(pointeeTy);
      CharUnits align = baseAddr.getAlignment();

      // If the index is a constant, compute exact alignment at offset.
      Expr::EvalResult result;
      if (indexOperand->EvaluateAsInt(result, getContext())) {
        llvm::APSInt idxVal = result.Val.getInt();
        CharUnits offset =
            CharUnits::fromQuantity(idxVal.getExtValue() *
                                    eltSize.getQuantity());
        align = align.alignmentAtOffset(offset);
      } else {
        // Non-constant index: use worst-case alignment for any element.
        align = align.alignmentOfArrayElement(eltSize);
      }

      // Build the pointer arithmetic (PtrStrideOp) from the
      // already-emitted operands. Determine GEP no-wrap flags matching
      // the scalar emitter's emitPointerArithmetic.
      mlir::Location loc = getLoc(bo->getExprLoc());
      bool signedIndices = mlir::cast<cir::IntType>(index.getType()).isSigned();
      cir::GEPNoWrapFlags nwFlags = cir::GEPNoWrapFlags::none;
      if (!getLangOpts().PointerOverflowDefined) {
        nwFlags = cir::GEPNoWrapFlags::inbounds;
        if (!signedIndices && !isSubtraction)
          nwFlags = nwFlags | cir::GEPNoWrapFlags::nuw;
      }

      // Negate index for subtraction.
      if (isSubtraction) {
        index = builder.createNeg(index);
      }

      // Match OGCG's EmitPointerWithAlignment -> EmitPointerArithmetic flow:
      // pointer arithmetic used only to form an lvalue still needs the same
      // array-bounds sanitizer check as the scalar path.
      if (sanOpts.has(SanitizerKind::ArrayBounds)) {
        mlir::Value checkedIndex = index;
        if (auto intTy = mlir::dyn_cast<cir::IntType>(checkedIndex.getType()))
          if (!intTy.isFundamental()) {
            checkedIndex = builder.createIntCast(checkedIndex, ptrDiffTy);
            if (auto *op = checkedIndex.getDefiningOp())
              markNoSanitize(op);
          }
        emitBoundsCheck(bo, pointerOperand, checkedIndex,
                        indexOperand->getType(), /*accessed=*/false);
      }

      // For a pointer to a variably-modified (VLA) array, the array element
      // type is erased in CIR (e.g. int(*)[m] becomes !cir.ptr<int>), so the
      // PtrStrideOp below would stride by the inner element size. Scale the
      // index by the VLA element count so it strides by a whole VLA element,
      // matching the scalar emitPointerArithmetic path.
      if (const VariableArrayType *vla =
              getContext().getAsVariableArrayType(pointeeTy)) {
        mlir::Value numElements = getVLASize(vla).numElts;
        if (index.getType() != numElements.getType())
          index = builder.createIntCast(index, numElements.getType());
        cir::OverflowBehavior ob = getLangOpts().PointerOverflowDefined
                                       ? cir::OverflowBehavior::None
                                       : cir::OverflowBehavior::NoSignedWrap;
        index = builder.createMul(loc, index, numElements, ob);
        nwFlags = getLangOpts().PointerOverflowDefined
                      ? cir::GEPNoWrapFlags::none
                      : cir::GEPNoWrapFlags::inbounds;
      }

      mlir::Value ptr = baseAddr.getPointer();
      mlir::Value resultPtr = cir::PtrStrideOp::create(
          builder, loc, ptr.getType(), ptr, index, nwFlags);
      // Use the pointer's own pointee type as element type to avoid
      // mismatches with _BitInt storage types.
      auto resPtrTy = mlir::cast<cir::PointerType>(resultPtr.getType());
      return Address(resultPtr, resPtrTy.getPointee(), align, isKnownNonNull);
    }
  }

  // If the expression type is an array or dependent type, emit it as an lvalue
  // and take its address. Arrays need array-to-pointer decay. Dependent types
  // (e.g., unresolved TemplateSpecializationType) can't go through emitScalarExpr.
  if (expr->getType()->isArrayType() ||
      expr->getType()->isDependentType()) {
    LValue lv = emitLValue(expr);
    if (baseInfo)
      *baseInfo = lv.getBaseInfo();
    if (tbaaInfo)
      *tbaaInfo = lv.getTBAAInfo();
    return lv.getAddress();
  }

  // Otherwise, use the alignment of the type.
  Address addr = makeNaturalAddressForPointer(
      emitScalarExpr(expr), expr->getType()->getPointeeType(), CharUnits(),
      /*forPointeeType=*/true, baseInfo, tbaaInfo);
  if (isKnownNonNull && !addr.isKnownNonNull())
    addr.setKnownNonNull();
  return addr;
}

void CIRGenFunction::emitStoreThroughLValue(RValue src, LValue dst,
                                            bool isInit) {
  if (!dst.isSimple()) {
    if (dst.isVectorElt()) {
      // Read/modify/write the vector, inserting the new element
      const mlir::Location loc = dst.getVectorPointer().getLoc();
      const mlir::Value vector =
          builder.createLoad(loc, dst.getVectorAddress(),
                             dst.isVolatileQualified());
      // OGCG zero-extends narrow source values to match vector element width.
      // In CIR, the AST-level ImplicitCastExpr already converts the source
      // to the vector element type, and VecInsertOp enforces type matching,
      // so no explicit zero-extension is needed here.
      const mlir::Value newVector = cir::VecInsertOp::create(
          builder, loc, vector, src.getValue(), dst.getVectorIdx());
      builder.createStore(loc, newVector, dst.getVectorAddress(),
                          dst.isVolatileQualified());
      return;
    }

    if (dst.isExtVectorElt())
      return emitStoreThroughExtVectorComponentLValue(src, dst);

    if (dst.isGlobalReg())
      return emitStoreThroughGlobalRegLValue(src, dst);

    if (dst.isMatrixElt()) {
      const mlir::Location loc =
          currSrcLoc ? *currSrcLoc : builder.getUnknownLoc();
      const mlir::Value matrix =
          builder.createLoad(loc, dst.getMatrixAddress(),
                             dst.isVolatileQualified());
      const mlir::Value newMatrix = cir::VecInsertOp::create(
          builder, loc, matrix, src.getValue(), dst.getMatrixIdx());
      auto storeOp =
          builder.createStore(loc, newMatrix, dst.getMatrixAddress(),
                              dst.isVolatileQualified(), dst.isNontemporal());
      addOpToCurrentSourceAtom(storeOp, newMatrix);
      cgm.decorateOperationWithTBAA(storeOp, dst.getTBAAInfo());
      return;
    }

    assert(dst.isBitField() && "Unknown LValue type");
    emitStoreThroughBitfieldLValue(src, dst);
    return;
  }

  // CIR has no ObjC-specific load/store mode at this layer.

  assert(src.isScalar() && "Can't emit an aggregate store with this method");
  if (PointerAuthQualifier pointerAuth =
          dst.getQuals().getPointerAuth().withoutKeyNone()) {
    src = RValue::get(emitPointerAuthQualify(pointerAuth, src.getValue(),
                                             dst.getType(), dst.getAddress(),
                                             /*isKnownNonNull=*/false));
    dst.getQuals().removePointerAuth();
  }
  emitStoreOfScalar(src.getValue(), dst, isInit);
}

void CIRGenFunction::emitStoreThroughExtVectorComponentLValue(RValue src,
                                                              LValue dst) {
  mlir::Location loc = dst.getExtVectorPointer().getLoc();

  // HLSL allows storing to scalar values through ExtVector component LValues.
  // To support this we need to handle the case where the destination address is
  // a scalar.
  Address dstAddr = dst.getExtVectorAddress();
  if (!mlir::isa<cir::VectorType>(dstAddr.getElementType())) {
    cgm.errorNYI(loc, "emitStoreThroughExtVectorComponentLValue: HLSL");
    return;
  }

  // This access turns into a read/modify/write of the vector.  Load the input
  // value now.
  mlir::Value vec = builder.createLoad(loc, dstAddr, dst.isVolatileQualified());
  const mlir::ArrayAttr elts = dst.getExtVectorElts();

  mlir::Value srcVal = src.getValue();

  if (const clang::VectorType *vTy =
          dst.getType()->getAs<clang::VectorType>()) {
    unsigned numSrcElts = vTy->getNumElements();
    unsigned numDstElts = cast<cir::VectorType>(vec.getType()).getSize();
    if (numDstElts == numSrcElts) {
      // Use shuffle vector if the src and destination are the same number of
      // elements and restore the vector mask since it is on the side it will be
      // stored.
      SmallVector<int64_t, 4> mask(numDstElts);
      for (unsigned i = 0; i != numSrcElts; ++i)
        mask[getAccessedFieldNo(i, elts)] = i;

      vec = builder.createVecShuffle(loc, srcVal, mask);
    } else if (numDstElts > numSrcElts) {
      // Extended the source vector to the same length and then shuffle it
      // into the destination.
      // FIXME: since we're shuffling with undef, can we just use the indices
      //        into that?  This could be simpler.
      SmallVector<int64_t, 4> extMask;
      for (unsigned i = 0; i != numSrcElts; ++i)
        extMask.push_back(i);
      extMask.resize(numDstElts, -1);
      mlir::Value extSrcVal = builder.createVecShuffle(loc, srcVal, extMask);
      // build identity
      SmallVector<int64_t, 4> mask;
      for (unsigned i = 0; i != numDstElts; ++i)
        mask.push_back(i);

      // When the vector size is odd and .odd or .hi is used, the last element
      // of the Elts constant array will be one past the size of the vector.
      // Ignore the last element here, if it is greater than the mask size.
      if ((unsigned)getAccessedFieldNo(numSrcElts - 1, elts) == mask.size())
        numSrcElts--;

      // modify when what gets shuffled in
      for (unsigned i = 0; i != numSrcElts; ++i)
        mask[getAccessedFieldNo(i, elts)] = i + numDstElts;
      vec = builder.createVecShuffle(loc, vec, extSrcVal, mask);
    } else {
      // We should never shorten the vector
      llvm_unreachable("unexpected shorten vector length");
    }
  } else {
    // If the Src is a scalar (not a vector), and the target is a vector it must
    // be updating one element.
    unsigned inIdx = getAccessedFieldNo(0, elts);
    auto elt = builder.getSInt64(inIdx, loc);

    vec = cir::VecInsertOp::create(builder, loc, vec, srcVal, elt);
  }

  builder.createStore(loc, vec, dst.getExtVectorAddress(),
                      dst.isVolatileQualified());
}

/// Load of global named registers are always calls to read_register.
RValue CIRGenFunction::emitLoadOfGlobalRegLValue(LValue lv) {
  assert((lv.getType()->isIntegerType() || lv.getType()->isPointerType()) &&
         "Bad type for register variable");

  mlir::Type intTy = convertType(lv.getType());
  mlir::Type regTy = intTy;

  // For pointer types, use an integer type matching the pointer width, then
  // convert back to pointer with inttoptr (matching OGCG).
  if (lv.getType()->isPointerType()) {
    unsigned ptrWidth = getContext().getTargetInfo().getPointerWidth(
        lv.getType()->getPointeeType().getAddressSpace());
    regTy = cir::IntType::get(&cgm.getMLIRContext(), ptrWidth,
                              /*isSigned=*/false);
  }

  mlir::Value result = cir::ReadRegisterOp::create(
      builder, *currSrcLoc, regTy, lv.getGlobalReg());

  if (lv.getType()->isPointerType()) {
    result = builder.createIntToPtr(result, convertType(lv.getType()));
  }

  return RValue::get(result);
}

/// Store to global named registers are always calls to write_register.
void CIRGenFunction::emitStoreThroughGlobalRegLValue(RValue src, LValue dst) {
  assert((dst.getType()->isIntegerType() || dst.getType()->isPointerType()) &&
         "Bad type for register variable");

  mlir::Value value = src.getValue();

  // For pointer types, convert to integer with ptrtoint (matching OGCG).
  if (dst.getType()->isPointerType()) {
    unsigned ptrWidth = getContext().getTargetInfo().getPointerWidth(
        dst.getType()->getPointeeType().getAddressSpace());
    mlir::Type intTy = cir::IntType::get(&cgm.getMLIRContext(), ptrWidth,
                                         /*isSigned=*/false);
    value = builder.createPtrToInt(value, intTy);
  }

  cir::WriteRegisterOp::create(builder, *currSrcLoc, dst.getGlobalReg(),
                               value);
}

/// Check if a variable may need destruction (mirrors CGCXXABI::mayNeedDestruction).
static LValue emitGlobalVarDeclLValue(CIRGenFunction &cgf, const Expr *e,
                                      const VarDecl *vd) {
  QualType t = e->getType();

  // If it's thread_local with dynamic initialization that needs a wrapper,
  // emit a call to its wrapper function (_ZTW*) instead of a direct access.
  // The wrapper ensures __tls_init runs on first access per-thread (lazy
  // initialization). Variables with constant initialization and no destructor
  // can be accessed directly without the wrapper.
  if (vd->getTLSKind() == VarDecl::TLS_Dynamic &&
      usesThreadWrapperFunction(vd, cgf.getContext())) {
    // Ensure the underlying GlobalOp exists so that:
    // 1. The variable is added to cxxThreadLocals (triggers wrapper generation
    //    in emitCXXThreadLocalInitFuncs)
    // 2. The wrapper body can reference the GlobalOp via cir.get_global
    // OGCG does this implicitly via GetOrCreateLLVMGlobal in other code paths,
    // but CIR's wrapper path bypasses getAddrOfGlobalVar.
    mlir::Type varTy = cgf.convertTypeForMem(vd->getType());
    cgf.cgm.getOrCreateCIRGlobal(vd, varTy, NotForDefinition);
    auto &mangleCtx = cast<ItaniumMangleContext>(
        cgf.cgm.getCXXABI().getMangleContext());
    SmallString<256> wrapperName;
    {
      llvm::raw_svector_ostream out(wrapperName);
      mangleCtx.mangleItaniumThreadLocalWrapper(vd, out);
    }

    // Get or create a declaration for the wrapper function. The wrapper body
    // is created later in CIRGenItaniumCXXABI::emitCXXThreadLocalInitFuncs.
    mlir::Location loc = cgf.getLoc(e->getSourceRange());
    QualType wrapperResultQT =
        vd->getType()->isReferenceType() ? vd->getType()->getPointeeType() : vd->getType();
    mlir::Type wrapperResultTy = cgf.convertTypeForMem(wrapperResultQT);
    auto ptrTy = cgf.getBuilder().getPointerTo(wrapperResultTy);
    auto wrapperFnTy = cir::FuncType::get({}, ptrTy);

    auto *wrapperOp = cgf.cgm.getGlobalValue(wrapperName);
    cir::FuncOp wrapperFn;
    if (wrapperOp) {
      wrapperFn = cast<cir::FuncOp>(wrapperOp);
    } else {
      // Create a forward declaration with external linkage. The body and
      // proper linkage (weak_odr, hidden, comdat) are set later in
      // CIRGenItaniumCXXABI::emitCXXThreadLocalInitFuncs. For extern TLS
      // variables with no definition in this TU, the declaration stays
      // external, which is correct.
      wrapperFn = cgf.cgm.createCIRFunction(
          loc, wrapperName, wrapperFnTy, /*FD=*/nullptr);
      // On Darwin, TLS_Dynamic wrappers use cxx_fast_tlscc. Set it on the
      // forward declaration so the call below uses the correct CC. The body
      // emission in emitCXXThreadLocalInitFuncs will also set it.
      if (cgf.cgm.getTarget().getTriple().isOSDarwin()) {
        wrapperFn.setCallingConv(cir::CallingConv::CXXFastTLS);
        mlir::NamedAttrList attrs;
        if (auto existingExtraAttrs = wrapperFn.getExtraAttrs())
          attrs = mlir::NamedAttrList{
              existingExtraAttrs->getElements().getValue()};
        auto noThrow =
            cir::NoThrowAttr::get(&cgf.cgm.getMLIRContext());
        attrs.set(noThrow.getMnemonic(), noThrow);
        wrapperFn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
            attrs.getDictionary(&cgf.cgm.getMLIRContext())));
      }
    }

    // Inherit the calling convention from the wrapper function — on Darwin
    // this will be cxx_fast_tlscc.
    auto callOp = cgf.getBuilder().createCallOp(
        loc, wrapperFn, {}, wrapperFn.getCallingConv());
    mlir::Value addr = callOp.getResult();

    CharUnits alignment = cgf.getContext().getDeclAlign(vd);
    LValue lv;
    if (vd->getType()->isReferenceType())
      // _ZTW* wrappers for thread_local references already return the
      // referenced object's address, not the address of the TLS reference
      // storage. Loading through the wrapper result would add a spurious extra
      // indirection versus OGCG.
      lv = cgf.makeNaturalAlignAddrLValue(addr, wrapperResultQT);
    else
      lv = cgf.makeAddrLValue(Address(addr, wrapperResultTy, alignment), t,
                              AlignmentSource::Decl);
    return lv;
  }

  // Match OGCG's device-side declare-target handling: plain globals still use
  // the normal address path, while `link` and `to`/`enter`+USM use a
  // ref-pointer helper.
  mlir::Value v;
  if (cgf.getLangOpts().OpenMPIsTargetDevice) {
    if (mlir::Value refPtr = cgf.cgm.getAddrOfOpenMPDeclareTargetVar(vd)) {
      Address refPtrAddr(refPtr, cgf.getPointerAlign());
      v = cgf.getBuilder()
              .createLoad(cgf.getLoc(e->getSourceRange()), refPtrAddr)
              .getResult();
    }
  }

  // Traditional LLVM codegen handles thread local separately, CIR handles
  // as part of getAddrOfGlobalVar.
  if (!v) {
    v = cgf.cgm.getAddrOfGlobalVar(vd);
  }

  mlir::Type realVarTy = cgf.convertTypeForMem(vd->getType());
  auto vPtrTy = mlir::cast<cir::PointerType>(v.getType());
  cir::PointerType realPtrTy =
      cgf.getBuilder().getPointerTo(realVarTy, vPtrTy.getAddrSpace());
  if (realPtrTy != v.getType())
    v = cgf.getBuilder().createBitcast(v.getLoc(), v, realPtrTy);

  CharUnits alignment = cgf.getContext().getDeclAlign(vd);
  Address addr(v, realVarTy, alignment);
  if (cgf.getLangOpts().OpenMP && !cgf.getLangOpts().OpenMPSimd &&
      vd->hasAttr<OMPThreadPrivateDeclAttr>()) {
    if (cgf.getLangOpts().OpenMPUseTLS &&
        cgf.getContext().getTargetInfo().isTLSSupported())
      return cgf.makeAddrLValue(addr, t, AlignmentSource::Decl);

    mlir::Value threadprivateAddr = mlir::omp::ThreadprivateOp::create(
        cgf.getBuilder(), cgf.getLoc(e->getExprLoc()), v.getType(), v);
    return cgf.makeAddrLValue(addr.withPointer(threadprivateAddr), t,
                              AlignmentSource::Decl);
  }

  LValue lv;
  if (vd->getType()->isReferenceType())
    lv = cgf.emitLoadOfReferenceLValue(addr, cgf.getLoc(e->getSourceRange()),
                                       vd->getType(), AlignmentSource::Decl);
  else
    lv = cgf.makeAddrLValue(addr, t, AlignmentSource::Decl);
  // OGCG calls setObjCGCLValueClass() here for ObjC GC write barrier
  // classification. ObjC GC was deprecated in 2012 and is not supported
  // by CIR.
  return lv;
}

void CIRGenFunction::emitStoreOfScalar(mlir::Value value, Address addr,
                                       bool isVolatile, QualType ty,
                                       LValueBaseInfo baseInfo,
                                       TBAAAccessInfo tbaaInfo, bool isInit,
                                       bool isNontemporal) {
  // Traditional LLVM codegen handles thread local separately, CIR handles
  // as part of getAddrOfGlobalVar (GetGlobalOp).

  if (const auto *clangVecTy = ty->getAs<clang::VectorType>()) {
    // Boolean vectors use `iN` as storage type. The conversion is handled
    // entirely by emitToMemory — skip the optimal vector widening path.
    if (clangVecTy->isPackedVectorBoolType(getContext())) {
      // Fall through to emitToMemory below.
    } else if (clangVecTy->isExtVectorBoolType()) {
      cgm.errorNYI(addr.getPointer().getLoc(),
                   "emitStoreOfScalar ExtVectorBoolType");
      return;
    } else {
      // Handles vectors of sizes that are likely to be expanded to a larger
      // size to optimize performance.
      const mlir::Type elementType = addr.getElementType();
      const auto vecTy = cast<cir::VectorType>(elementType);

      // TODO(CIR): Use `ABIInfo::getOptimalVectorMemoryType` once upstreamed
      assert(!cir::MissingFeatures::cirgenABIInfo());
      if (vecTy.getSize() == 3 && !getLangOpts().PreserveVec3Type) {
        auto newVecTy = cir::VectorType::get(vecTy.getElementType(), 4);
        unsigned srcElts = vecTy.getSize();
        unsigned dstElts = newVecTy.getSize();
        if (dstElts <= srcElts) {
          cgm.errorNYI(
              "vector shrink in emitStoreOfScalar"); // Shrink the vector
        }

        // Build mask for extending vector with undef elements.
        // Example: char3 -> char4 => mask = [0, 1, 2, -1]
        llvm::SmallVector<int64_t, 16> mask(srcElts);
        std::iota(mask.begin(), mask.end(), 0);
        // Handle vector widening by filling remaining lanes with undef (-1)
        mask.resize(dstElts, -1);

        value = builder.createVecShuffle(*currSrcLoc, value, mask);
        addr = addr.withElementType(builder, newVecTy);
      }
    }
  }

  value = emitToMemory(value, ty);

  // If emitToMemory changed the value type (e.g. packed vector<bool> → int),
  // update the address element type to match.
  if (value.getType() != addr.getElementType()) {
    // A pointer value whose only mismatch with the destination slot is its
    // address space must be address-space cast to the slot's space (matching
    // OGCG, which relies on a CK_AddressSpaceConversion inserting the cast on
    // the value). Reshaping the slot instead would keep a pointer in the
    // source space stored through the destination's space, silently discarding
    // the address-space change (e.g. a CUDA __shared__ / addrspace(3) global
    // referenced through a generic pointer would lose its addrspacecast).
    auto valuePtrTy = mlir::dyn_cast<cir::PointerType>(value.getType());
    auto slotPtrTy = mlir::dyn_cast<cir::PointerType>(addr.getElementType());
    if (valuePtrTy && slotPtrTy &&
        valuePtrTy.getPointee() == slotPtrTy.getPointee() &&
        valuePtrTy.getAddrSpace() != slotPtrTy.getAddrSpace())
      value = builder.createAddrSpaceCast(value, slotPtrTy);
    else
      addr = addr.withElementType(builder, value.getType());
  }

  LValue atomicLValue = LValue::makeAddr(addr, ty, baseInfo);
  atomicLValue.setTBAAInfo(tbaaInfo);
  if (ty->isAtomicType() ||
      (!isInit && isLValueSuitableForInlineAtomic(atomicLValue))) {
    emitAtomicStore(RValue::get(value), atomicLValue, isInit);
    return;
  }

  // Update the alloca with more info on initialization.
  assert(addr.getPointer() && "expected pointer to exist");
  auto srcAlloca = addr.getDefiningOp<cir::AllocaOp>();
  // Look through address space cast (e.g. for CUDA/HIP/OpenCL).
  if (!srcAlloca) {
    if (auto castOp = addr.getDefiningOp<cir::CastOp>()) {
      if (castOp.getKind() == cir::CastKind::address_space)
        srcAlloca = castOp.getSrc().getDefiningOp<cir::AllocaOp>();
    }
  }
  if (currVarDecl && srcAlloca) {
    const VarDecl *vd = currVarDecl;
    assert(vd && "VarDecl expected");
    if (vd->hasInit())
      srcAlloca.setInitAttr(mlir::UnitAttr::get(&getMLIRContext()));
  }

  assert(currSrcLoc && "must pass in source location");
  auto storeOp =
      builder.createStore(*currSrcLoc, value, addr, isVolatile, isNontemporal);
  addOpToCurrentSourceAtom(storeOp, value);

  cgm.decorateOperationWithTBAA(storeOp, tbaaInfo);
}

// TODO: Replace this with a proper TargetInfo function call.
/// Helper method to check if the underlying ABI is AAPCS
static bool isAAPCS(const TargetInfo &targetInfo) {
  return targetInfo.getABI().starts_with("aapcs");
}

mlir::Value CIRGenFunction::emitStoreThroughBitfieldLValue(RValue src,
                                                           LValue dst) {

  const CIRGenBitFieldInfo &info = dst.getBitFieldInfo();
  mlir::Type resLTy = convertTypeForMem(dst.getType());
  Address ptr = dst.getBitFieldAddress();

  bool useVolatile = cgm.getCodeGenOpts().AAPCSBitfieldWidth &&
                     dst.isVolatileQualified() &&
                     info.volatileStorageSize != 0 && isAAPCS(cgm.getTarget());

  mlir::Value dstAddr = dst.getAddress().getPointer();

  // SetBitfieldOp requires IntType for both value and result. If the type
  // is BoolType (e.g. bool bitfield), use an unsigned int type for the
  // operation, then cast the result back to bool. This mirrors the load
  // path's handling in emitLoadOfBitfieldLValue.
  mlir::Value srcVal = src.getValue();
  bool isBoolTy = mlir::isa<cir::BoolType>(resLTy);
  mlir::Type setBitfieldTy = resLTy;
  if (isBoolTy) {
    mlir::Type intTy = cir::IntType::get(resLTy.getContext(), 8, false);
    srcVal = builder.createCast(dstAddr.getLoc(), cir::CastKind::bool_to_int,
                                srcVal, intTy);
    setBitfieldTy = intTy;
  }

  // OGCG performs an extra volatile load when ForceAAPCSBitfieldLoad is set
  // for 32-bit ARM (AAPCS) targets (CGExpr.cpp:2954-2956). CIR targets
  // arm64 (AArch64/AAPCS64), which does not have this behavior.

  mlir::Location opLoc = currSrcLoc ? *currSrcLoc : dstAddr.getLoc();
  mlir::Value result =
      builder.createSetBitfield(opLoc, setBitfieldTy, ptr, ptr.getElementType(),
                                srcVal, info, dst.isVolatileQualified(),
                                useVolatile);
  if (auto setBitfieldOp = result.getDefiningOp())
    addOpToCurrentSourceAtom(setBitfieldOp);

  if (isBoolTy)
    result = builder.createCast(dstAddr.getLoc(), cir::CastKind::int_to_bool,
                                result, cir::BoolType::get(resLTy.getContext()));

  return result;
}

RValue CIRGenFunction::emitLoadOfBitfieldLValue(LValue lv, SourceLocation loc) {
  const CIRGenBitFieldInfo &info = lv.getBitFieldInfo();

  // Get the output type.
  mlir::Type resLTy = convertType(lv.getType());
  Address ptr = lv.getBitFieldAddress();

  bool useVolatile = cgm.getCodeGenOpts().AAPCSBitfieldWidth &&
                     lv.isVolatileQualified() &&
                     info.volatileStorageSize != 0 && isAAPCS(cgm.getTarget());

  // GetBitfieldOp requires an IntType result. If the field type is BoolType
  // (e.g. bool bitfield), use an unsigned int type for the operation, then
  // cast the result to bool afterwards.
  bool isBoolTy = mlir::isa<cir::BoolType>(resLTy);
  mlir::Type getBitfieldTy =
      isBoolTy ? cir::IntType::get(resLTy.getContext(), 8, false) : resLTy;

  mlir::Value field = builder.createGetBitfield(getLoc(loc), getBitfieldTy, ptr,
                                                ptr.getElementType(), info,
                                                lv.isVolatile(), useVolatile);

  if (!(isBoolTy && info.size == 1))
    (void)emitScalarRangeCheck(*this, field, lv.getType(), loc);

  if (isBoolTy)
    field = builder.createCast(getLoc(loc), cir::CastKind::int_to_bool, field,
                               cir::BoolType::get(resLTy.getContext()));

  return RValue::get(field);
}

Address CIRGenFunction::getAddrOfBitFieldStorage(LValue base,
                                                 const FieldDecl *field,
                                                 mlir::Type fieldType,
                                                 unsigned index) {
  mlir::Location loc = getLoc(field->getLocation());
  auto basePtrTy = mlir::cast<cir::PointerType>(base.getPointer().getType());
  cir::PointerType fieldPtr =
      getBuilder().getPointerTo(fieldType, basePtrTy.getAddrSpace());
  auto rec = cast<cir::RecordType>(base.getAddress().getElementType());
  cir::GetMemberOp sea = getBuilder().createGetMember(
      loc, fieldPtr, base.getPointer(), field->getName(),
      rec.isUnion() ? field->getFieldIndex() : index);
  CharUnits offset = CharUnits::fromQuantity(
      rec.getElementOffset(cgm.getDataLayout().layout, index));
  return Address(sea, base.getAlignment().alignmentAtOffset(offset));
}

LValue CIRGenFunction::emitLValueForBitField(LValue base,
                                             const FieldDecl *field) {
  LValueBaseInfo baseInfo = base.getBaseInfo();
  const CIRGenRecordLayout &layout =
      cgm.getTypes().getCIRGenRecordLayout(field->getParent());
  const CIRGenBitFieldInfo &info = layout.getBitFieldInfo(field);

  // OGCG wraps bitfield addresses with BPF preserve.access.index intrinsics
  // when inside a preserved access index region. BPF is not a supported CIR
  // target.

  bool useVolatile = isAAPCS(cgm.getTarget()) &&
                     cgm.getCodeGenOpts().AAPCSBitfieldWidth &&
                     info.volatileStorageSize != 0 &&
                     field->getType()
                         .withCVRQualifiers(base.getVRQualifiers())
                         .isVolatileQualified();

  mlir::Location loc = getLoc(field->getLocation());
  Address addr = Address::invalid();

  if (useVolatile) {
    // For volatile AAPCS bitfields, compute an element-offset address from the
    // struct base instead of using the struct field GEP. The volatile access
    // unit may differ from the CIR struct field layout.
    // Matches OGCG CGExpr.cpp: Addr.withElementType(FieldIntTy) then
    // CreateConstInBoundsGEP(Addr, VolatileOffset).
    addr = base.getAddress();
    auto volatileTy = cir::IntType::get(builder.getContext(),
                                        info.volatileStorageSize, info.isSigned);
    // Bitcast to the volatile element type first.
    addr = builder.createElementBitCast(loc, addr, volatileTy);
    // volatileStorageOffset is in units of the volatile element type, not bytes.
    unsigned volatileOffset = info.volatileStorageOffset.getQuantity();
    if (volatileOffset) {
      auto indexTy =
          cir::IntType::get(builder.getContext(), 64, /*isSigned=*/false);
      auto offsetVal = builder.getConstant(
          loc, cir::IntAttr::get(indexTy, volatileOffset));
      auto offsetPtr =
          cir::PtrStrideOp::create(builder, loc, addr.getPointer().getType(),
                                   addr.getPointer(), offsetVal,
                                   cir::GEPNoWrapFlags::inbounds);
      CharUnits eltSize =
          CharUnits::fromQuantity(info.volatileStorageSize / 8);
      addr = Address(offsetPtr, volatileTy,
                     addr.getAlignment().alignmentAtOffset(
                         eltSize * volatileOffset));
    }
  } else {
    unsigned idx = layout.getCIRFieldNo(field);
    addr = getAddrOfBitFieldStorage(base, field, info.storageType, idx);
    if (addr.getElementType() != info.storageType)
      addr = builder.createElementBitCast(loc, addr, info.storageType);
  }

  QualType fieldType =
      field->getType().withCVRQualifiers(base.getVRQualifiers());
  // TODO(cir): Support TBAA for bit fields. OGCG also has this as a TODO.
  LValueBaseInfo fieldBaseInfo(baseInfo.getAlignmentSource());
  return LValue::makeBitfield(addr, info, fieldType, fieldBaseInfo,
                              TBAAAccessInfo());
}

/// Returns true if the given type, or any of its base classes or fields,
/// contains a vtable pointer.
static bool hasAnyVptr(const QualType type, const ASTContext &context) {
  const auto *rd = type.getTypePtr()->getAsCXXRecordDecl();
  if (!rd)
    return false;

  if (rd->isDynamicClass())
    return true;

  for (const auto &base : rd->bases())
    if (hasAnyVptr(base.getType(), context))
      return true;

  for (const FieldDecl *field : rd->fields())
    if (hasAnyVptr(field->getType(), context))
      return true;

  return false;
}

/// Get the field index in debug info. Unnamed bitfields are skipped.
static unsigned getDebugInfoFIndex(const RecordDecl *rec, unsigned fieldIndex) {
  unsigned i = 0;
  unsigned skipped = 0;

  for (const auto *field : rec->getDefinition()->fields()) {
    if (i == fieldIndex)
      break;
    if (field->isUnnamedBitField())
      skipped++;
    i++;
  }

  return fieldIndex - skipped;
}

static SourceLocation getPreserveAccessDebugLoc(CIRGenFunction &cgf,
                                                SourceLocation loc) {
  if (!loc.isValid() ||
      cgf.getCIRGenModule().getCodeGenOpts().DebugInfoMacroExpansionLoc)
    return loc;
  return cgf.getContext().getSourceManager().getFileLoc(loc);
}

static Address emitPreserveStructAccess(CIRGenFunction &cgf, LValue base,
                                        Address addr, const FieldDecl *field,
                                        unsigned fieldIndex,
                                        SourceLocation accessLoc) {
  SourceLocation preserveLoc =
      getPreserveAccessDebugLoc(cgf, accessLoc.isValid() ? accessLoc
                                                         : field->getLocation());
  mlir::Location loc = cgf.getLoc(preserveLoc);
  auto basePtrTy = mlir::cast<cir::PointerType>(addr.getPointer().getType());
  auto recordTy = mlir::cast<cir::RecordType>(basePtrTy.getPointee());
  mlir::Type fieldStorageTy = recordTy.getMembers()[fieldIndex];
  auto fieldPtrTy =
      cgf.getBuilder().getPointerTo(fieldStorageTy, basePtrTy.getAddrSpace());
  auto i32Ty = cgf.getBuilder().getSIntNTy(32);
  mlir::Value gepIndex =
      cgf.getBuilder().getConstInt(loc, i32Ty, fieldIndex);
  mlir::Value debugIndex = cgf.getBuilder().getConstInt(
      loc, i32Ty,
      getDebugInfoFIndex(field->getParent(), field->getFieldIndex()));
  cir::LLVMIntrinsicCallOp preserve = cir::LLVMIntrinsicCallOp::create(
      cgf.getBuilder(), loc,
      cgf.getCIRGenModule().getStringAttrCached(
          "preserve.struct.access.index"),
      fieldPtrTy, mlir::ValueRange{addr.getPointer(), gepIndex, debugIndex});
  setPreserveAccessIndexSourceTypeAttr(cgf, preserve, base.getType());

  CharUnits offset = cgf.getContext().toCharUnitsFromBits(
      cgf.getContext().getFieldOffset(field));
  return Address(preserve.getResult(), fieldStorageTy,
                 addr.getAlignment().alignmentAtOffset(offset));
}

static Address emitPreserveUnionAccess(CIRGenFunction &cgf, LValue base,
                                       Address addr, const FieldDecl *field,
                                       SourceLocation accessLoc) {
  SourceLocation preserveLoc =
      getPreserveAccessDebugLoc(cgf, accessLoc.isValid() ? accessLoc
                                                         : field->getLocation());
  mlir::Location loc = cgf.getLoc(preserveLoc);
  auto i32Ty = cgf.getBuilder().getSIntNTy(32);
  mlir::Value debugIndex = cgf.getBuilder().getConstInt(
      loc, i32Ty,
      getDebugInfoFIndex(field->getParent(), field->getFieldIndex()));
  cir::LLVMIntrinsicCallOp preserve = cir::LLVMIntrinsicCallOp::create(
      cgf.getBuilder(), loc,
      cgf.getCIRGenModule().getStringAttrCached(
          "preserve.union.access.index"),
      addr.getPointer().getType(),
      mlir::ValueRange{addr.getPointer(), debugIndex});
  setPreserveAccessIndexSourceTypeAttr(cgf, preserve, base.getType());
  return Address(preserve.getResult(), addr.getElementType(),
                 addr.getAlignment());
}

LValue CIRGenFunction::emitLValueForField(LValue base, const FieldDecl *field,
                                          SourceLocation accessLoc) {
  LValueBaseInfo baseInfo = base.getBaseInfo();

  // pointerOverflowDefined: When -fwrapv-pointer is active, GetMemberOp GEPs
  // must not use inbounds/nuw. This is handled at the module level via the
  // cir.pointer_overflow_defined attribute, checked in GetMemberOp lowering.

  if (field->isBitField())
    return emitLValueForBitField(base, field);

  QualType fieldType = field->getType();
  const RecordDecl *rec = field->getParent();
  AlignmentSource baseAlignSource = baseInfo.getAlignmentSource();
  LValueBaseInfo fieldBaseInfo(getFieldAlignmentSource(baseAlignSource));
  TBAAAccessInfo fieldTBAAInfo;
  if (base.getTBAAInfo().isMayAlias() || rec->hasAttr<MayAliasAttr>() ||
      fieldType->isVectorType()) {
    fieldTBAAInfo = TBAAAccessInfo::getMayAliasInfo();
  } else if (rec->isUnion()) {
    // TODO: Support TBAA for unions.
    fieldTBAAInfo = TBAAAccessInfo::getMayAliasInfo();
  } else {
    // If no base type been assigned for the base access, then try to generate
    // one for this base lvalue.
    fieldTBAAInfo = base.getTBAAInfo();
    if (!fieldTBAAInfo.baseType) {
      fieldTBAAInfo.baseType = cgm.getTBAABaseTypeInfo(base.getType());
      assert(!fieldTBAAInfo.offset &&
             "Nonzero offset for an access with no base type!");
    }

    // Adjust offset to be relative to the base type.
    const clang::ASTRecordLayout &layout =
        getContext().getASTRecordLayout(field->getParent());
    unsigned charWidth = getContext().getCharWidth();
    if (fieldTBAAInfo.baseType)
      fieldTBAAInfo.offset +=
          layout.getFieldOffset(field->getFieldIndex()) / charWidth;

    // Update the final access type and size.
    fieldTBAAInfo.accessType = cgm.getTBAATypeInfo(fieldType);
    fieldTBAAInfo.size =
        getContext().getTypeSizeInChars(fieldType).getQuantity();
  }
  // When TBAA is disabled (O0, relaxed-aliasing), we should not emit
  // field-specific TBAA. Check the codegen opts directly rather than
  // probing getTBAAAccessInfo, since may_alias types return null
  // accessType even when TBAA is enabled.
  if (cgm.getCodeGenOpts().OptimizationLevel == 0 ||
      cgm.getCodeGenOpts().RelaxedAliasing)
    fieldTBAAInfo = TBAAAccessInfo();

  Address addr = base.getAddress();
  if (auto *classDecl = dyn_cast<CXXRecordDecl>(rec)) {
    if (cgm.getCodeGenOpts().StrictVTablePointers &&
        classDecl->isDynamicClass()) {
      // Getting to any field of a dynamic object requires stripping dynamic
      // information provided by invariant.group. This is because accessing
      // fields may leak the real address of dynamic object, which could
      // result in miscompilation when leaked pointer would be compared.
      auto stripped = cir::StripInvariantGroupOp::create(
          builder, getLoc(field->getSourceRange()), addr.getPointer());
      addr = Address(stripped, addr.getElementType(), addr.getAlignment());
    }
  }

  unsigned recordCVR = base.getVRQualifiers();

  llvm::StringRef fieldName = field->getName();
  unsigned fieldIndex = 0;
  if (cgm.lambdaFieldToName.count(field))
    fieldName = cgm.lambdaFieldToName[field];

  // Fields that are empty for layout purposes are not included in the record
  // layout's field index map because accumulateFields() skips them. Don't try
  // to look them up — emitAddrOfFieldStorage handles them separately.
  if (isEmptyFieldForLayout(getContext(), field)) {
    // Fall through with fieldIndex=0; emitAddrOfFieldStorage will handle it.
  } else if (rec->isUnion()) {
    // Because unions can easily skip invariant barriers, we need to add
    // a barrier every time a CXXRecord field with vptr is referenced.
    if (cgm.getCodeGenOpts().StrictVTablePointers &&
        hasAnyVptr(fieldType, getContext()))
      addr = Address(cir::InvariantGroupOp::create(
                         builder, getLoc(field->getSourceRange()),
                         addr.getPointer()),
                     addr.getElementType(), addr.getAlignment());
    // OGCG wraps union field addresses with llvm.preserve.static.offset for
    // BPF targets. BPF is not a supported CIR target.
    fieldIndex = field->getFieldIndex();

    // For anonymous union member access through indirect field chains, the
    // base pointer may still have the outer struct's CIR record type rather
    // than the union's type. This happens when emitLValueForFieldInitialization
    // walks the IndirectFieldDecl chain — the bitcast at the end of step 1
    // may set the element type to a converted type that doesn't match the
    // union layout. Ensure the base pointer type matches the union's CIR
    // record type so that fieldIndex (from getFieldIndex()) is valid.
    mlir::Type unionCIRTy = convertType(getContext().getCanonicalTagType(rec));
    auto basePtrTy =
        mlir::cast<cir::PointerType>(addr.getPointer().getType());
    if (basePtrTy.getPointee() != unionCIRTy) {
      // The base pointer type doesn't match the union — bitcast to the
      // union's CIR type so GetMemberOp can use the correct field index.
      addr = builder.createElementBitCast(getLoc(field->getSourceRange()),
                                          addr, unionCIRTy);
    }
  } else {
    const CIRGenRecordLayout &layout =
        cgm.getTypes().getCIRGenRecordLayout(field->getParent());
    fieldIndex = layout.getCIRFieldNo(field);
  }

  bool isPreservedAI = IsInPreservedAIRegion ||
                       rec->hasAttr<BPFPreserveAccessIndexAttr>();
  if (rec->isUnion()) {
    if (isPreservedAI)
      addr = emitPreserveUnionAccess(*this, base, addr, field, accessLoc);
    else
      addr = emitAddrOfFieldStorage(addr, field, fieldName, fieldIndex);
  } else {
    bool canEmitPreserve = isPreservedAI &&
                           !isEmptyFieldForLayout(getContext(), field);
    if (canEmitPreserve) {
      auto basePtrTy =
          mlir::dyn_cast<cir::PointerType>(addr.getPointer().getType());
      auto recordTy =
          basePtrTy ? mlir::dyn_cast<cir::RecordType>(basePtrTy.getPointee())
                    : nullptr;
      canEmitPreserve = recordTy && fieldIndex < recordTy.getMembers().size();
    }
    if (canEmitPreserve)
      addr = emitPreserveStructAccess(*this, base, addr, field, fieldIndex,
                                      accessLoc);
    else
      addr = emitAddrOfFieldStorage(addr, field, fieldName, fieldIndex);
  }

  // If this is a reference field, load the reference right now.
  if (fieldType->isReferenceType()) {
    LValue refLVal = makeAddrLValue(addr, fieldType, fieldBaseInfo,
                                    fieldTBAAInfo);
    if (recordCVR & Qualifiers::Volatile)
      refLVal.getQuals().addVolatile();
    addr = emitLoadOfReference(refLVal, getLoc(field->getSourceRange()),
                               &fieldBaseInfo, &fieldTBAAInfo);

    // Qualifiers on the struct don't apply to the referencee.
    recordCVR = 0;
    fieldType = fieldType->getPointeeType();
  }

  // Make sure that the address is pointing to the right type. This is critical
  // for both unions and structs. For potentially-overlapping fields (i.e.
  // [[no_unique_address]]), the record layout stores the base subobject type,
  // so emitAddrOfFieldStorage returns a pointer to that type. We must cast
  // back to the complete field type here. Matches OGCG CGExpr.cpp:5719-5721.
  mlir::Type memTy = convertTypeForMem(fieldType);
  addr = builder.createElementBitCast(getLoc(field->getSourceRange()), addr,
                                      memTy);

  if (field->hasAttr<AnnotateAttr>())
    addr = emitFieldAnnotations(field, addr);

  LValue lv = makeAddrLValue(addr, fieldType, fieldBaseInfo, fieldTBAAInfo);
  lv.getQuals().addCVRQualifiers(recordCVR);

  // __weak attribute on a field is ignored.
  if (lv.getQuals().getObjCGCAttr() == Qualifiers::Weak) {
    cgm.errorNYI(field->getSourceRange(),
                 "emitLValueForField with __weak attribute");
  }

  return lv;
}

LValue CIRGenFunction::emitLValueForFieldInitialization(
    LValue base, const clang::FieldDecl *field, llvm::StringRef fieldName) {
  QualType fieldType = field->getType();

  if (!fieldType->isReferenceType())
    return emitLValueForField(base, field);

  // pointerOverflowDefined: When -fwrapv-pointer is active, GetMemberOp GEPs
  // must not use inbounds/nuw. Handled by cir.pointer_overflow_defined module
  // attribute, checked in GetMemberOp lowering (see LowerToLLVM.cpp).

  unsigned fieldIndex = 0;
  if (!isEmptyFieldForLayout(getContext(), field)) {
    const CIRGenRecordLayout &layout =
        cgm.getTypes().getCIRGenRecordLayout(field->getParent());
    fieldIndex = layout.getCIRFieldNo(field);
  }

  Address v =
      emitAddrOfFieldStorage(base.getAddress(), field, fieldName, fieldIndex);

  // Make sure that the address is pointing to the right type.
  mlir::Type memTy = convertTypeForMem(fieldType);
  v = builder.createElementBitCast(getLoc(field->getSourceRange()), v, memTy);

  // TODO: Generate TBAA information that describes this access as a structure
  // member access and not just an access to an object of the field's type. This
  // should be similar to what we do in emitLValueForField().
  LValueBaseInfo baseInfo = base.getBaseInfo();
  AlignmentSource fieldAlignSource = baseInfo.getAlignmentSource();
  LValueBaseInfo fieldBaseInfo(getFieldAlignmentSource(fieldAlignSource));
  return makeAddrLValue(v, fieldType, fieldBaseInfo);
}

/// Converts a scalar value from its primary IR type (as returned
/// by ConvertType) to its load/store type.
mlir::Value CIRGenFunction::emitToMemory(mlir::Value value, QualType ty) {
  if (auto *atomicTy = ty->getAs<AtomicType>())
    ty = atomicTy->getValueType();

  if (ty->isPackedVectorBoolType(getContext())) {
    // Convert <N x bool> → iP (P = max(N, 8)):
    //   1. Shuffle <N x bool> → <P x bool> (pad with poison lanes)
    //   2. Bitcast <P x bool> → iP
    // If the value is already an integer (e.g. from a coerced parameter),
    // this is a no-op.
    if (mlir::isa<cir::VectorType>(value.getType())) {
      mlir::Type storeTy = convertTypeForMem(ty);
      unsigned storeSize = cast<cir::IntType>(storeTy).getWidth();
      auto vecTy = cast<cir::VectorType>(value.getType());
      unsigned numElems = vecTy.getSize();

      if (numElems != storeSize) {
        // Pad: [0, 1, ..., N-1, -1, -1, ..., -1]
        SmallVector<int64_t> mask(storeSize, -1);
        for (unsigned i = 0; i < numElems; ++i)
          mask[i] = i;
        auto loc = value.getLoc();
        value = builder.createVecShuffle(loc, value, mask);
      }
      // Bitcast <P x bool> → iP
      mlir::Type storeTyFinal = convertTypeForMem(ty);
      return builder.createCast(cir::CastKind::bitcast, value, storeTyFinal);
    }
    return value;
  }

  if (ty->isExtVectorBoolType()) {
    // HLSL ext vector bools are not packed — they use wider integer elements.
    cgm.errorNYI("emitToMemory: HLSL extVectorBoolType");
    return value;
  }

  // OGCG also handles isConstantMatrixBoolType alongside isExtVectorBoolType,
  // performing bool-vector padding and bitcast to the storage integer type.
  assert(!cir::MissingFeatures::matrixType());

  // OGCG sign/zero-extends BitInt to the storage type (next power-of-two).
  // CIR matches this by casting from the semantic bit-width to the storage
  // bit-width using CastKind::integral (which produces sext for signed types
  // and zext for unsigned types).
  if (ty->isBitIntType()) {
    mlir::Type memTy = convertTypeForMem(ty);
    if (value.getType() != memTy)
      return builder.createCast(cir::CastKind::integral, value, memTy);
  }

  // boolZeroExtensionForMemory: CIR uses cir::BoolType abstractly.
  // Bool-to-i8 zero-extension is handled in LowerToLLVM.cpp's
  // emitToMemory (zext i1->i8 on store, trunc i8->i1 on load).

  return value;
}

mlir::Value CIRGenFunction::emitFromMemory(mlir::Value value, QualType ty) {
  if (auto *atomicTy = ty->getAs<AtomicType>())
    ty = atomicTy->getValueType();

  if (ty->isPackedVectorBoolType(getContext())) {
    // Convert iP → <N x bool> (P = max(N, 8)):
    //   1. Bitcast iP → <P x bool>
    //   2. Shuffle <P x bool> → <N x bool> (extract first N elements)
    // If the value is already a vector (e.g. loaded from a non-memory-typed
    // alloca), this is a no-op.
    if (mlir::isa<cir::IntType>(value.getType())) {
      const auto *vecClangTy = ty->castAs<VectorType>();
      unsigned numElems = vecClangTy->getNumElements();
      unsigned paddedSize =
          cast<cir::IntType>(value.getType()).getWidth();

      auto boolTy = cir::BoolType::get(builder.getContext());
      auto paddedVecTy = cir::VectorType::get(boolTy, paddedSize);
      auto loc = value.getLoc();

      // Bitcast iP → <P x bool>
      value = builder.createCast(cir::CastKind::bitcast, value, paddedVecTy);

      if (paddedSize != numElems) {
        // Extract: [0, 1, ..., N-1]
        SmallVector<int64_t> mask(numElems);
        std::iota(mask.begin(), mask.end(), 0);
        value = builder.createVecShuffle(loc, value, mask);
      }
    }
    return value;
  }

  // OGCG truncates BitInt from the storage type (next power-of-two) back to
  // the N-bit semantic type. CIR matches this by truncating via
  // CastKind::integral after loading from memory.
  if (ty->isBitIntType()) {
    mlir::Type valTy = convertType(ty);
    if (value.getType() != valTy)
      return builder.createCast(cir::CastKind::integral, value, valTy);
  }

  // CIR normally keeps bool loads abstract as cir::BoolType and relies on
  // CIR-to-LLVM lowering to materialize the storage-byte truncation. When a
  // caller explicitly loaded the raw storage integer instead (for example, to
  // run UBSan's load_invalid_value check on the untruncated byte), convert it
  // back to the abstract bool value here.
  if (ty->isBooleanType() && mlir::isa<cir::IntType>(value.getType()))
    return builder.createCast(cir::CastKind::int_to_bool, value,
                              convertType(ty));

  return value;
}

void CIRGenFunction::emitStoreOfScalar(mlir::Value value, LValue lvalue,
                                       bool isInit) {
  if (lvalue.getType()->isConstantMatrixType())
    return emitStoreOfScalar(
        value, maybeConvertMatrixAddress(lvalue.getAddress(), lvalue.getType(),
                                         *this),
        lvalue.isVolatile(), lvalue.getType(), lvalue.getBaseInfo(),
        lvalue.getTBAAInfo(), isInit, lvalue.isNontemporal());

  emitStoreOfScalar(value, lvalue.getAddress(), lvalue.isVolatile(),
                    lvalue.getType(), lvalue.getBaseInfo(),
                    lvalue.getTBAAInfo(), isInit, lvalue.isNontemporal());
}

static bool getRangeForLoadType(CIRGenFunction &cgf, QualType ty,
                                llvm::APInt &min, llvm::APInt &end) {
  const auto *enumDecl = ty->getAsEnumDecl();
  bool isBool = ty->hasBooleanRepresentation() && !ty->isVectorType();
  bool isRegularCxxEnum =
      cgf.getLangOpts().CPlusPlus &&
      cgf.getCIRGenModule().getCodeGenOpts().StrictEnums && enumDecl &&
      !enumDecl->isFixed();
  if (!isBool && !isRegularCxxEnum)
    return false;

  // OGCG suppresses !range/!noundef when bool/enum UBSan checks are emitted.
  if ((isBool && cgf.getLangOpts().Sanitize.has(SanitizerKind::Bool)) ||
      (isRegularCxxEnum && cgf.getLangOpts().Sanitize.has(SanitizerKind::Enum)))
    return false;

  if (isBool) {
    min = llvm::APInt(cgf.getContext().getTypeSize(ty), 0);
    end = llvm::APInt(cgf.getContext().getTypeSize(ty), 2);
  } else {
    enumDecl->getValueRange(end, min);
  }
  return true;
}

static bool getRangeForScalarCheckType(CIRGenFunction &cgf, QualType ty,
                                       llvm::APInt &min, llvm::APInt &end,
                                       bool strictEnums, bool isBool) {
  const auto *enumDecl = ty->getAsEnumDecl();
  bool isRegularCxxEnum =
      cgf.getLangOpts().CPlusPlus && strictEnums && enumDecl && !enumDecl->isFixed();
  if (!isBool && !isRegularCxxEnum)
    return false;

  if (isBool) {
    min = llvm::APInt(cgf.getContext().getTypeSize(ty), 0);
    end = llvm::APInt(cgf.getContext().getTypeSize(ty), 2);
  } else {
    enumDecl->getValueRange(end, min);
  }
  return true;
}

static mlir::Value extendSanitizerIntegerValue(CIRGenFunction &cgf,
                                               mlir::Value value,
                                               mlir::Location loc) {
  auto intptrTy =
      mlir::cast<cir::IntType>(cgf.convertType(cgf.getContext().getUIntPtrType()));
  auto intTy = mlir::dyn_cast<cir::IntType>(value.getType());
  if (!intTy || intTy.getWidth() > intptrTy.getWidth())
    return value;

  value = castIntegerToSignedness(cgf.getBuilder(), loc, value,
                                  /*isSigned=*/false);
  return cgf.getBuilder().createCast(loc, cir::CastKind::integral, value,
                                     intptrTy);
}

static bool emitScalarRangeCheck(CIRGenFunction &cgf, mlir::Value value,
                                 QualType ty, SourceLocation loc) {
  bool hasBoolCheck = cgf.sanOpts.has(SanitizerKind::Bool);
  bool hasEnumCheck = cgf.sanOpts.has(SanitizerKind::Enum);
  if (!hasBoolCheck && !hasEnumCheck)
    return false;

  bool isBool = ty->hasBooleanRepresentation() && !ty->isVectorType();
  bool needsBoolCheck = hasBoolCheck && isBool;
  bool needsEnumCheck = hasEnumCheck && ty->isEnumeralType();
  if (!needsBoolCheck && !needsEnumCheck)
    return false;

  auto intTy = mlir::dyn_cast<cir::IntType>(value.getType());
  if (!intTy)
    return false;

  // Single-bit booleans don't need to be checked. This mirrors OGCG's
  // special-case for bitfield-backed bool values.
  if (needsBoolCheck && intTy.getWidth() == 1)
    return false;

  if (needsEnumCheck &&
      cgf.getContext().isTypeIgnoredBySanitizer(SanitizerKind::Enum, ty))
    return false;

  llvm::APInt min, end;
  if (!getRangeForScalarCheckType(cgf, ty, min, end, /*strictEnums=*/true,
                                  isBool))
    return true;

  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Location cirLoc = cgf.getLoc(loc);

  --end;
  mlir::Value check;
  if (!min) {
    value = castIntegerToSignedness(builder, cirLoc, value, /*isSigned=*/false);
    auto bound =
        builder.getConstant(cirLoc, cir::IntAttr::get(value.getType(), end));
    check = cir::CmpOp::create(builder, cirLoc, cir::CmpOpKind::le, value,
                               bound);
  } else {
    value = castIntegerToSignedness(builder, cirLoc, value, /*isSigned=*/true);
    auto upper =
        builder.getConstant(cirLoc, cir::IntAttr::get(value.getType(), end));
    auto lower =
        builder.getConstant(cirLoc, cir::IntAttr::get(value.getType(), min));
    mlir::Value upperCmp = cir::CmpOp::create(builder, cirLoc,
                                              cir::CmpOpKind::le, value, upper);
    mlir::Value lowerCmp = cir::CmpOp::create(builder, cirLoc,
                                              cir::CmpOpKind::ge, value, lower);
    markNoSanitize(upperCmp.getDefiningOp());
    markNoSanitize(lowerCmp.getDefiningOp());
    check = builder.createAnd(cirLoc, upperCmp, lowerCmp);
  }
  markNoSanitize(check.getDefiningOp());

  auto typeSanKind = needsEnumCheck ? SanitizerKind::Enum : SanitizerKind::Bool;
  bool trapOnFailure = cgf.cgm.getCodeGenOpts().SanitizeTrap.has(typeSanKind);
  StringRef handlerKind = "abort";
  if (cgf.cgm.getCodeGenOpts().SanitizeRecover.has(typeSanKind))
    handlerKind = "recover";

  auto ifOp = cir::IfOp::create(
      builder, cirLoc, check, /*withElseRegion=*/true,
      [&](mlir::OpBuilder &, mlir::Location) { builder.createYield(cirLoc); },
      [&](mlir::OpBuilder &, mlir::Location) {
        if (trapOnFailure) {
          auto trap = cir::TrapOp::create(builder, cirLoc);
          trap->setAttr(
              "cir.ubsantrap_handler",
              mlir::IntegerAttr::get(
                  mlir::IntegerType::get(builder.getContext(), 8), 10));
          markNoSanitize(trap);
          return;
        }

        auto intptrTy = cgf.convertType(cgf.getContext().getUIntPtrType());
        mlir::Value dynamicValue = extendSanitizerIntegerValue(cgf, value, cirLoc);
        if (auto *op = dynamicValue.getDefiningOp())
          markNoSanitize(op);

        auto stubFnTy = cir::FuncType::get({intptrTy}, builder.getVoidTy());
        auto stubFn = cgf.cgm.createRuntimeFunction(
            stubFnTy, "__cir_ubsan_load_invalid_value_check", /*attrs=*/{},
            /*isLocal=*/false, /*assumeConvergent=*/false);
        auto call = builder.createCallOp(cirLoc, stubFn, {dynamicValue});

        PresumedLoc presumedLoc =
            cgf.getContext().getSourceManager().getPresumedLoc(loc);
        if (presumedLoc.isValid()) {
          call->setDiscardableAttr("cir.ubsan.source_file",
                                   builder.getStringAttr(presumedLoc.getFilename()));
          call->setDiscardableAttr("cir.ubsan.source_line",
                                   builder.getI32IntegerAttr(presumedLoc.getLine()));
          call->setDiscardableAttr(
              "cir.ubsan.source_column",
              builder.getI32IntegerAttr(presumedLoc.getColumn()));
        }

        SmallString<32> typeName;
        cgf.cgm.getDiags().ConvertArgToString(
            DiagnosticsEngine::ak_qualtype, (intptr_t)ty.getAsOpaquePtr(),
            StringRef(), StringRef(), {}, typeName, {});
        call->setDiscardableAttr("cir.ubsan.type_name",
                                 builder.getStringAttr(typeName));
        setUBSanTypeDescriptorAttrs(cgf, call, ty);
        call->setDiscardableAttr("cir.ubsan.handler_kind",
                                 builder.getStringAttr(handlerKind));
        call->setDiscardableAttr("cir.ubsan.handler_name",
                                 builder.getStringAttr("load_invalid_value"));
        call->setDiscardableAttr("cir.nosanitize",
                                 mlir::UnitAttr::get(builder.getContext()));

        if (handlerKind == "abort") {
          auto unreachable = cir::UnreachableOp::create(builder, cirLoc);
          markNoSanitize(unreachable);
        } else {
          builder.createYield(cirLoc);
        }
      });
  setSanitizerBranchAttrs(builder, ifOp);
  return true;
}

mlir::Value CIRGenFunction::emitLoadOfScalar(Address addr, bool isVolatile,
                                             QualType ty, SourceLocation loc,
                                             LValueBaseInfo baseInfo,
                                             TBAAAccessInfo tbaaInfo,
                                             bool isNontemporal) {
  // Traditional LLVM codegen handles thread local separately, CIR handles
  // as part of getAddrOfGlobalVar (GetGlobalOp).
  mlir::Type eltTy = addr.getElementType();

  if (const auto *clangVecTy = ty->getAs<clang::VectorType>()) {
    // Boolean vectors use `iN` as storage type. Load the packed integer
    // and convert via emitFromMemory (bitcast + shuffle).
    if (clangVecTy->isPackedVectorBoolType(getContext())) {
      // Load the packed integer (iP where P = max(N, 8)), then convert to
      // <N x bool> via bitcast + shuffle.
      mlir::Type memTy = convertTypeForMem(ty);
      Address memAddr = addr.withElementType(builder, memTy);
      mlir::Value rawInt =
          builder.createLoad(getLoc(loc), memAddr, isVolatile);
      return emitFromMemory(rawInt, ty);
    }
    if (clangVecTy->isExtVectorBoolType()) {
      cgm.errorNYI(loc, "emitLoadOfScalar: ExtVectorBoolType");
      return builder.getConstant(getLoc(loc),
                                 cir::PoisonAttr::get(convertType(ty)));
    }

    const auto vecTy = cast<cir::VectorType>(eltTy);

    // Handle vectors of size 3 like size 4 for better performance.
    assert(!cir::MissingFeatures::cirgenABIInfo());
    if (vecTy.getSize() == 3 && !getLangOpts().PreserveVec3Type) {
      auto vec4Ty = cir::VectorType::get(vecTy.getElementType(), 4);
      Address castAddr = addr.withElementType(builder, vec4Ty);
      mlir::Value v = builder.createLoad(getLoc(loc), castAddr, isVolatile);
      SmallVector<int64_t, 3> mask = {0, 1, 2};
      v = builder.createVecShuffle(getLoc(loc), v, mask);
      return v;
    }
  }

  LValue atomicLValue = LValue::makeAddr(addr, ty, baseInfo);
  atomicLValue.setTBAAInfo(tbaaInfo);
  if (ty->isAtomicType() || isLValueSuitableForInlineAtomic(atomicLValue)) {
    return emitAtomicLoad(atomicLValue, loc).getValue();
  }

  // Void type loads don't actually produce a value - this can happen with
  // GNU extension void pointer arithmetic where we have void lvalues.
  // Just return an empty Value since the result is never used.
  if (mlir::isa<cir::VoidType>(eltTy))
    return mlir::Value();

  // For scalar bool loads under -fsanitize=bool, mirror OGCG by validating the
  // raw storage byte before truncating it to the semantic i1 value. If we load
  // the abstract cir.bool directly first, invalid byte values have already
  // been collapsed to 0/1 by the time the sanitizer check runs.
  if (ty->isBooleanType() && sanOpts.has(SanitizerKind::Bool)) {
    unsigned rawBitWidth = cgm.getDataLayout().getTypeSizeInBits(eltTy);
    mlir::Type rawTy = builder.getUIntNTy(rawBitWidth);
    Address rawAddr = addr.withElementType(builder, rawTy);
    mlir::Value rawLoad =
        builder.createLoad(getLoc(loc), rawAddr, isVolatile, isNontemporal);
    auto load = mlir::cast<cir::LoadOp>(rawLoad.getDefiningOp());

    cgm.decorateOperationWithTBAA(load, tbaaInfo);
    (void)emitScalarRangeCheck(*this, rawLoad, ty, loc);
    return emitFromMemory(rawLoad, ty);
  }

  mlir::Value loadOp =
      builder.createLoad(getLoc(loc), addr, isVolatile, isNontemporal);
  auto load = mlir::cast<cir::LoadOp>(loadOp.getDefiningOp());

  cgm.decorateOperationWithTBAA(load, tbaaInfo);

  bool emittedRangeCheck = emitScalarRangeCheck(*this, loadOp, ty, loc);

  // Match OGCG by attaching !range and !noundef metadata to bool/strict-enum
  // loads under optimization when no corresponding UBSan check is emitted.
  if (!emittedRangeCheck && cgm.getCodeGenOpts().OptimizationLevel > 0) {
    llvm::APInt min, end;
    if (getRangeForLoadType(*this, ty, min, end)) {
      auto minTy = builder.getIntegerType(min.getBitWidth());
      auto endTy = builder.getIntegerType(end.getBitWidth());
      load->setDiscardableAttr(
          "cir.range",
          builder.getArrayAttr(
              {mlir::IntegerAttr::get(minTy, min),
               mlir::IntegerAttr::get(endTy, end)}));
      load->setDiscardableAttr("cir.noundef_load",
                               mlir::UnitAttr::get(builder.getContext()));
    }
  }

  // Convert from the memory representation back to the semantic type.
  // For _BitInt types, this truncates from the storage width (e.g. i64)
  // to the semantic width (e.g. i35).
  return emitFromMemory(loadOp, ty);
}

mlir::Value CIRGenFunction::emitLoadOfScalar(LValue lvalue,
                                             SourceLocation loc) {
  return emitLoadOfScalar(lvalue.getAddress(), lvalue.isVolatile(),
                          lvalue.getType(), loc, lvalue.getBaseInfo(),
                          lvalue.getTBAAInfo(), lvalue.isNontemporal());
}

/// Given an expression that represents a value lvalue, this
/// method emits the address of the lvalue, then loads the result as an rvalue,
/// returning the rvalue.
RValue CIRGenFunction::emitLoadOfLValue(LValue lv, SourceLocation loc) {
  assert(!lv.getType()->isFunctionType());
  // CIR has no ObjC-specific load/store mode at this layer.
  if (PointerAuthQualifier ptrAuth =
          lv.getQuals().getPointerAuth().withoutKeyNone()) {
    lv.getQuals().removePointerAuth();
    mlir::Value value = emitLoadOfLValue(lv, loc).getValue();
    return RValue::get(emitPointerAuthUnqualify(
        ptrAuth, value, lv.getType(), lv.getAddress(),
        /*isKnownNonNull=*/false));
  }

  if (lv.getQuals().getObjCLifetime() == Qualifiers::OCL_Weak) {
    if (!getLangOpts().ObjCAutoRefCount)
      return RValue::get(emitARCLoadWeak(lv.getAddress()));

    mlir::Value object = emitARCLoadWeakRetained(lv.getAddress());
    emitARCConsumeObject(object, getLoc(loc));
    return RValue::get(object);
  }

  if (lv.isBitField())
    return emitLoadOfBitfieldLValue(lv, loc);

  if (lv.isSimple()) {
    if (lv.getType()->isConstantMatrixType())
      return RValue::get(
          emitLoadOfScalar(maybeConvertMatrixAddress(lv.getAddress(),
                                                     lv.getType(), *this),
                           lv.isVolatile(), lv.getType(), loc,
                           lv.getBaseInfo(), lv.getTBAAInfo(),
                           lv.isNontemporal()));
    return RValue::get(emitLoadOfScalar(lv, loc));
  }

  if (lv.isVectorElt()) {
    const mlir::Value load = builder.createLoad(getLoc(loc),
                                                lv.getVectorAddress(),
                                                lv.isVolatile());
    return RValue::get(cir::VecExtractOp::create(builder, getLoc(loc), load,
                                                 lv.getVectorIdx()));
  }

  if (lv.isExtVectorElt())
    return emitLoadOfExtVectorElementLValue(lv);

  if (lv.isGlobalReg())
    return emitLoadOfGlobalRegLValue(lv);

  if (lv.isMatrixElt()) {
    const mlir::Value matrix =
        builder.createLoad(getLoc(loc), lv.getMatrixAddress(),
                           lv.isVolatileQualified());
    return RValue::get(cir::VecExtractOp::create(builder, getLoc(loc), matrix,
                                                 lv.getMatrixIdx()));
  }

  cgm.errorNYI(loc, "emitLoadOfLValue");
  return RValue::get(mlir::Value());
}

int64_t CIRGenFunction::getAccessedFieldNo(unsigned int idx,
                                           const mlir::ArrayAttr elts) {
  auto elt = mlir::cast<mlir::IntegerAttr>(elts[idx]);
  return elt.getInt();
}

// If this is a reference to a subset of the elements of a vector, create an
// appropriate shufflevector.
RValue CIRGenFunction::emitLoadOfExtVectorElementLValue(LValue lv) {
  mlir::Location loc = lv.getExtVectorPointer().getLoc();
  mlir::Value vec =
      builder.createLoad(loc, lv.getExtVectorAddress(), lv.isVolatile());

  // HLSL allows treating scalars as one-element vectors. Converting the scalar
  // IR value to a vector here allows the rest of codegen to behave as normal.
  if (getLangOpts().HLSL && !mlir::isa<cir::VectorType>(vec.getType())) {
    cgm.errorNYI(loc, "emitLoadOfExtVectorElementLValue: HLSL");
    return RValue::get(vec);
  }

  const mlir::ArrayAttr elts = lv.getExtVectorElts();

  // If the result of the expression is a non-vector type, we must be extracting
  // a single element. Just codegen as an extractelement.
  const auto *exprVecTy = lv.getType()->getAs<clang::VectorType>();
  if (!exprVecTy) {
    int64_t indexValue = getAccessedFieldNo(0, elts);
    cir::ConstantOp index =
        builder.getConstInt(loc, builder.getSInt64Ty(), indexValue);
    return RValue::get(cir::VecExtractOp::create(builder, loc, vec, index));
  }

  // Always use shuffle vector to try to retain the original program structure
  SmallVector<int64_t> mask;
  for (auto i : llvm::seq<unsigned>(0, exprVecTy->getNumElements()))
    mask.push_back(getAccessedFieldNo(i, elts));

  cir::VecShuffleOp resultVec = builder.createVecShuffle(loc, vec, mask);
  if (lv.getType()->isExtVectorBoolType()) {
    cgm.errorNYI(loc, "emitLoadOfExtVectorElementLValue: ExtVectorBoolType");
    return RValue::get(resultVec);
  }

  return RValue::get(resultVec);
}

bool CIRGenFunction::isUnderlyingBasePointerConstantNull(const Expr *e) {
  const Expr *underlyingBaseExpr = e->IgnoreParens();
  while (auto *baseMemberExpr = dyn_cast<MemberExpr>(underlyingBaseExpr))
    underlyingBaseExpr = baseMemberExpr->getBase()->IgnoreParens();
  return getContext().isSentinelNullExpr(underlyingBaseExpr);
}

LValue
CIRGenFunction::emitPointerToDataMemberBinaryExpr(const BinaryOperator *e) {
  assert((e->getOpcode() == BO_PtrMemD || e->getOpcode() == BO_PtrMemI) &&
         "unexpected binary operator opcode");

  Address baseAddr = Address::invalid();
  if (e->getOpcode() == BO_PtrMemD)
    baseAddr = emitLValue(e->getLHS()).getAddress();
  else
    baseAddr = emitPointerWithAlignment(e->getLHS());

  const auto *memberPtrTy = e->getRHS()->getType()->castAs<MemberPointerType>();

  mlir::Value memberPtr = emitScalarExpr(e->getRHS());

  LValueBaseInfo baseInfo;
  TBAAAccessInfo tbaaInfo;
  bool isInBounds = !getLangOpts().PointerOverflowDefined &&
                    !isUnderlyingBasePointerConstantNull(e->getLHS());
  Address memberAddr = emitCXXMemberDataPointerAddress(
      e, baseAddr, memberPtr, memberPtrTy, &baseInfo, &tbaaInfo, isInBounds);

  return makeAddrLValue(memberAddr, memberPtrTy->getPointeeType(), baseInfo,
                        tbaaInfo);
}

/// Generates lvalue for partial ext_vector access.
Address CIRGenFunction::emitExtVectorElementLValue(LValue lv,
                                                   mlir::Location loc) {
  Address vectorAddress = lv.getExtVectorAddress();
  QualType elementTy = lv.getType()->castAs<VectorType>()->getElementType();
  mlir::Type vectorElementTy = cgm.getTypes().convertType(elementTy);
  Address castToPointerElement =
      vectorAddress.withElementType(builder, vectorElementTy);

  mlir::ArrayAttr extVecElts = lv.getExtVectorElts();
  unsigned idx = getAccessedFieldNo(0, extVecElts);
  mlir::Value idxValue =
      builder.getConstInt(loc, mlir::cast<cir::IntType>(ptrDiffTy), idx);

  mlir::Value elementValue = builder.getArrayElement(
      loc, loc, castToPointerElement.getPointer(), vectorElementTy, idxValue,
      /*shouldDecay=*/false);

  const CharUnits eltSize = getContext().getTypeSizeInChars(elementTy);
  const CharUnits alignment =
      castToPointerElement.getAlignment().alignmentAtOffset(idx * eltSize);
  return Address(elementValue, vectorElementTy, alignment);
}

static cir::FuncOp emitFunctionDeclPointer(CIRGenModule &cgm, GlobalDecl gd) {
  const auto *fd = cast<FunctionDecl>(gd.getDecl());
  if (fd->hasAttr<WeakRefAttr>())
    return cast<cir::FuncOp>(cgm.getWeakRefReference(fd));
  return cgm.getAddrOfFunction(gd);
}

static LValue emitCapturedFieldLValue(CIRGenFunction &cgf, const FieldDecl *fd,
                                      mlir::Value thisValue) {
  return cgf.emitLValueForLambdaField(fd, thisValue);
}

/// Given that we are currently emitting a lambda, emit an l-value for
/// one of its members.
///
LValue CIRGenFunction::emitLValueForLambdaField(const FieldDecl *field,
                                                mlir::Value thisValue) {
  bool hasExplicitObjectParameter = false;
  const auto *methD = dyn_cast_if_present<CXXMethodDecl>(curCodeDecl);
  LValue lambdaLV;
  if (methD) {
    hasExplicitObjectParameter = methD->isExplicitObjectMemberFunction();
    assert(methD->getParent()->isLambda());
    assert(methD->getParent() == field->getParent());
  }
  if (hasExplicitObjectParameter) {
    // For explicit object parameters (C++23 deducing this), derive the closure
    // object from the first parameter instead of the implicit 'this' pointer.
    const VarDecl *d = cast<CXXMethodDecl>(curCodeDecl)->getParamDecl(0);
    auto it = localDeclMap.find(d);
    assert(it != localDeclMap.end() && "explicit parameter not loaded?");
    Address addrOfExplicitObject = it->second;
    if (d->getType()->isReferenceType())
      lambdaLV = emitLoadOfReferenceLValue(addrOfExplicitObject,
                                           getLoc(d->getLocation()),
                                           d->getType(), AlignmentSource::Decl);
    else
      lambdaLV = makeAddrLValue(addrOfExplicitObject,
                                d->getType().getNonReferenceType());

    // If the explicit parameter type is a derived class of the lambda,
    // cast down to the lambda type.
    auto *thisTy = d->getType().getNonReferenceType()->getAsCXXRecordDecl();
    auto *lambdaTy = cast<CXXRecordDecl>(field->getParent());
    if (thisTy != lambdaTy) {
      const CXXCastPath &basePathArray =
          getContext().LambdaCastPaths.at(methD);
      Address base = getAddressOfBaseClass(
          lambdaLV.getAddress(), thisTy,
          llvm::make_range(basePathArray.begin(), basePathArray.end()),
          /*nullCheckValue=*/false, SourceLocation());
      QualType t = getContext().getCanonicalTagType(lambdaTy);
      lambdaLV = makeAddrLValue(base, t);
    }
  } else {
    QualType lambdaTagType =
        getContext().getCanonicalTagType(field->getParent());
    lambdaLV = makeNaturalAlignAddrLValue(thisValue, lambdaTagType);
  }
  return emitLValueForField(lambdaLV, field);
}

LValue CIRGenFunction::emitLValueForLambdaField(const FieldDecl *field) {
  return emitLValueForLambdaField(field, cxxabiThisValue);
}

static LValue emitFunctionDeclLValue(CIRGenFunction &cgf, const Expr *e,
                                     GlobalDecl gd) {
  const FunctionDecl *fd = cast<FunctionDecl>(gd.getDecl());
  cir::FuncOp funcOp = emitFunctionDeclPointer(cgf.cgm, gd);
  mlir::Location loc = cgf.getLoc(e->getSourceRange());
  CharUnits align = cgf.getContext().getDeclAlign(fd);

  (void)0; // Sanitizer-specific CIR parity work remains NYI in this path.

  mlir::Type fnTy = funcOp.getFunctionType();
  mlir::Type ptrTy = cir::PointerType::get(fnTy);
  mlir::Value addr = cir::GetGlobalOp::create(cgf.getBuilder(), loc, ptrTy,
                                              funcOp.getSymName());
  if (e->getType()->isCFIUncheckedCalleeFunctionType()) {
    auto *ctx = &cgf.getMLIRContext();
    auto emptyPath = mlir::ArrayAttr::get(ctx, {});
    addr.getDefiningOp()->setAttr("cir.no_cfi",
                                  mlir::ArrayAttr::get(ctx, {emptyPath}));
  }

  if (funcOp.getFunctionType() != cgf.convertType(fd->getType())) {
    fnTy = cgf.convertType(fd->getType());
    ptrTy = cir::PointerType::get(fnTy);

    addr = cir::CastOp::create(cgf.getBuilder(), addr.getLoc(), ptrTy,
                               cir::CastKind::bitcast, addr);
  }

  if (std::optional<CIRGenPointerAuthInfo> pointerAuthInfo =
          cgf.cgm.getFunctionPointerAuthInfo(fd->getType())) {
    CIRGenRuntimePointerAuthInfo runtimeAuthInfo{
        pointerAuthInfo->authenticationMode, pointerAuthInfo->isIsaPointer,
        pointerAuthInfo->authenticatesNullValues, pointerAuthInfo->key,
        mlir::Value{}};
    if (pointerAuthInfo->discriminator) {
      runtimeAuthInfo.discriminator =
          cgf.getBuilder().getConstantInt(loc, cgf.cgm.uIntPtrTy,
                                          pointerAuthInfo->discriminator);
    }
    addr = cgf.emitPointerAuthSign(runtimeAuthInfo, addr);
  }

  return cgf.makeAddrLValue(Address(addr, fnTy, align), e->getType(),
                            AlignmentSource::Decl);
}

/// Determine whether we can emit a reference to \p vd from the current
/// context, despite not necessarily having seen an odr-use of the variable in
/// this context.
/// TODO(cir): This could be shared with classic codegen.
static bool canEmitSpuriousReferenceToVariable(CIRGenFunction &cgf,
                                               const DeclRefExpr *e,
                                               const VarDecl *vd) {
  // For a variable declared in an enclosing scope, do not emit a spurious
  // reference even if we have a capture, as that will emit an unwarranted
  // reference to our capture state, and will likely generate worse code than
  // emitting a local copy.
  if (e->refersToEnclosingVariableOrCapture())
    return false;

  // For a local declaration declared in this function, we can always reference
  // it even if we don't have an odr-use.
  if (vd->hasLocalStorage()) {
    return vd->getDeclContext() ==
           dyn_cast_or_null<DeclContext>(cgf.curCodeDecl);
  }

  // For a global declaration, we can emit a reference to it if we know
  // for sure that we are able to emit a definition of it.
  vd = vd->getDefinition(cgf.getContext());
  if (!vd)
    return false;

  // Don't emit a spurious reference if it might be to a variable that only
  // exists on a different device / target.
  // FIXME: This is unnecessarily broad. Check whether this would actually be a
  // cross-target reference.
  if (cgf.getLangOpts().OpenMP || cgf.getLangOpts().CUDA ||
      cgf.getLangOpts().OpenCL) {
    return false;
  }

  // We can emit a spurious reference only if the linkage implies that we'll
  // be emitting a non-interposable symbol that will be retained until link
  // time.
  switch (cgf.cgm.getCIRLinkageVarDefinition(vd, /*IsConstant=*/false)) {
  case cir::GlobalLinkageKind::ExternalLinkage:
  case cir::GlobalLinkageKind::LinkOnceODRLinkage:
  case cir::GlobalLinkageKind::WeakODRLinkage:
  case cir::GlobalLinkageKind::InternalLinkage:
  case cir::GlobalLinkageKind::PrivateLinkage:
    return true;
  default:
    return false;
  }
}

LValue CIRGenFunction::emitDeclRefLValue(const DeclRefExpr *e) {
  const NamedDecl *nd = e->getDecl();
  QualType ty = e->getType();
  auto markOpenMPNontemporal = [&](const ValueDecl *vd, LValue lv) {
    if (getLangOpts().OpenMP && isOpenMPNontemporalDecl(vd))
      lv.setNontemporal(true);
    return lv;
  };

  assert(e->isNonOdrUse() != NOUR_Unevaluated &&
         "should not emit an unevaluated operand");

  if (const auto *vd = dyn_cast<VarDecl>(nd)) {
    // Global Named registers access via intrinsics only
    if (vd->getStorageClass() == SC_Register && vd->hasAttr<AsmLabelAttr>() &&
        !vd->isLocalVarDecl()) {
      auto *asm_ = vd->getAttr<AsmLabelAttr>();
      auto regName = cgm.getStringAttrCached(asm_->getLabel());
      return LValue::makeGlobalReg(regName, vd->getType());
    }

    if (e->isNonOdrUse() == NOUR_Constant &&
        (vd->getType()->isReferenceType() ||
         !canEmitSpuriousReferenceToVariable(*this, e, vd))) {
      vd->getAnyInitializer(vd);
      mlir::Attribute val = ConstantEmitter(*this).emitAbstract(
          e->getLocation(), *vd->evaluateValue(), vd->getType());
      assert(val && "failed to emit constant expression");

      Address addr = Address::invalid();
      if (!vd->getType()->isReferenceType()) {
        // Spill the constant value to a global.
        addr = cgm.createUnnamedGlobalFrom(*vd, val,
                                           getContext().getDeclAlign(vd));
        mlir::Type varTy = getTypes().convertTypeForMem(vd->getType());
        auto ptrTy = mlir::cast<cir::PointerType>(addr.getPointer().getType());
        if (ptrTy.getPointee() != varTy) {
          addr = addr.withElementType(getBuilder(), varTy);
        }
      } else {
        // Should we be using the alignment of the constant pointer we
        // emitted?
        CharUnits alignment =
            cgm.getNaturalTypeAlignment(e->getType(), /*baseInfo=*/nullptr);
        mlir::Value ptrVal = getBuilder().getConstant(
            getLoc(e->getExprLoc()), mlir::cast<mlir::TypedAttr>(val));
        // The constant (e.g., GlobalViewAttr for RTTI) may have a different
        // pointer type (ptr<u8>) than expected. Bitcast to match.
        mlir::Type expectedPointeeTy = convertTypeForMem(ty);
        auto expectedPtrTy = getBuilder().getPointerTo(expectedPointeeTy);
        if (ptrVal.getType() != expectedPtrTy)
          ptrVal = getBuilder().createBitcast(
              getLoc(e->getExprLoc()), ptrVal, expectedPtrTy);
        addr = makeNaturalAddressForPointer(ptrVal, ty, alignment);
      }
      return markOpenMPNontemporal(
          vd, makeAddrLValue(addr, ty, AlignmentSource::Decl));
    }

    // Check for captured variables.
    if (e->refersToEnclosingVariableOrCapture()) {
      vd = vd->getCanonicalDecl();
      if (FieldDecl *fd = lambdaCaptureFields.lookup(vd))
        return markOpenMPNontemporal(
            vd, emitCapturedFieldLValue(*this, fd, cxxabiThisValue));
      if (capturedStmtInfo) {
        auto it = localDeclMap.find(vd);
        if (it != localDeclMap.end())
          return markOpenMPNontemporal(
              vd, makeAddrLValue(it->second, ty, AlignmentSource::Decl));
        if (const FieldDecl *fd = capturedStmtInfo->lookup(vd)) {
          QualType tagType =
              getContext().getCanonicalTagType(fd->getParent());
          LValue base = makeNaturalAlignAddrLValue(
              capturedStmtInfo->getContextValue(), tagType);
          return markOpenMPNontemporal(vd, emitLValueForField(base, fd));
        }
      }
      // OGCG falls back to BlockDecl capture via GetAddrOfBlockDecl here.
      // CIR handles block captures via capturedStmtInfo/localDeclMap lookups.
    }
  }

  // OGCG handles WeakRefAttr here via CGM.GetWeakRefReference.
  if (nd->hasAttr<WeakRefAttr>()) {
    const auto *vd = cast<ValueDecl>(nd);
    mlir::Location loc = getLoc(e->getSourceRange());
    mlir::Type declTy = convertTypeForMem(vd->getType());
    mlir::Operation *weakRefTarget = cgm.getWeakRefReference(vd);
    mlir::Value addr;
    if (auto weakRefFn = dyn_cast<cir::FuncOp>(weakRefTarget)) {
      addr = cir::GetGlobalOp::create(
          builder, loc, cir::PointerType::get(weakRefFn.getFunctionType()),
          weakRefFn.getSymNameAttr());
    } else if (auto weakRefGlobal = dyn_cast<cir::GlobalOp>(weakRefTarget)) {
      addr = builder.createGetGlobal(loc, weakRefGlobal);
    } else {
      llvm_unreachable("WeakRef target must be function or global");
    }

    auto addrTy = mlir::cast<cir::PointerType>(addr.getType());
    if (addrTy.getPointee() != declTy) {
      auto declPtrTy = builder.getPointerTo(declTy, addrTy.getAddrSpace());
      addr =
          cir::CastOp::create(builder, loc, declPtrTy, cir::CastKind::bitcast,
                              addr);
    }

    return makeAddrLValue(
        Address(addr, declTy, getContext().getDeclAlign(vd)), ty,
        AlignmentSource::Decl);
  }

  if (const auto *vd = dyn_cast<VarDecl>(nd)) {
    // opAllocaStaticLocal: Static locals are handled below via
    // vd->isStaticLocal() -> getOrCreateStaticVarDecl, which creates a
    // cir::GlobalOp (matching OGCG's getOrCreateStaticVarDecl).
    // opAllocaTLS: Thread-local address materialization is handled by
    // CIRToLLVMGetGlobalOpLowering which emits llvm.threadlocal.address.
    // Check if this is a global variable.
    if (vd->hasLinkage() || vd->isStaticDataMember()) {
      auto iter = localDeclMap.find(vd);
      if (iter != localDeclMap.end() && iter->second.isValid() &&
          !isa_and_nonnull<cir::GetGlobalOp>(
              iter->second.getPointer().getDefiningOp()))
        return markOpenMPNontemporal(
            vd, makeAddrLValue(iter->second, ty, AlignmentSource::Decl));
      return markOpenMPNontemporal(vd, emitGlobalVarDeclLValue(*this, e, vd));
    }

    Address addr = Address::invalid();

    // The variable should generally be present in the local decl map.
    auto iter = localDeclMap.find(vd);
    if (iter != localDeclMap.end()) {
      addr = iter->second;
    } else if (vd->isStaticLocal()) {
      // Static local from an enclosing function — not in our localDeclMap
      // but lowered to a cir.global with a mangled name. Use
      // getOrCreateStaticVarDecl to ensure the parent function that owns
      // this variable gets emitted (triggering deferred emission).
      // Matches OGCG CGExpr.cpp:3579-3583.
      cir::GlobalOp gv = cgm.getOrCreateStaticVarDecl(
          *vd, cgm.getCIRLinkageVarDefinition(vd, /*isConstant=*/false));
      mlir::Type varTy = convertTypeForMem(vd->getType());
      auto ptrTy = getBuilder().getPointerTo(
          gv.getSymType(), gv.getAddrSpaceAttr());
      mlir::Value gvVal = cir::GetGlobalOp::create(
          getBuilder(), getLoc(e->getSourceRange()), ptrTy,
          gv.getSymNameAttr(), vd->getTLSKind() != VarDecl::TLS_None);
      if (auto gvPtrTy = mlir::cast<cir::PointerType>(gvVal.getType());
          gvPtrTy.getPointee() != varTy)
        gvVal = getBuilder().createBitcast(gvVal.getLoc(), gvVal,
                                           getBuilder().getPointerTo(varTy));
      addr = Address(gvVal, varTy, getContext().getDeclAlign(vd));
    } else {
      // Variable declaration was skipped (e.g., goto jumped over it).
      // Emit a late alloca so the variable exists. This matches OGCG which
      // always emits allocas at function entry regardless of reachability.
      mlir::Location varLoc = getLoc(e->getSourceRange());
      mlir::Type cirTy = convertType(vd->getType());
      CharUnits alignment = getContext().getDeclAlign(vd);
      if (vd->isEscapingByref()) {
        const auto &byrefInfo = getBlockByrefInfo(vd);
        cirTy = byrefInfo.type;
        alignment = byrefInfo.byrefAlignment;
      }
      auto allocaVal = emitAlloca(vd->getName(), cirTy, varLoc, alignment,
                                  /*insertIntoFnEntryBlock=*/true);
      addr = Address(allocaVal, cirTy, alignment);
      setAddrOfLocalVar(vd, addr);
    }

    if (getLangOpts().OpenMP && !getLangOpts().OpenMPSimd &&
        vd->hasAttr<OMPThreadPrivateDeclAttr>()) {
      if (getLangOpts().OpenMPUseTLS &&
          getContext().getTargetInfo().isTLSSupported())
        return markOpenMPNontemporal(
            vd, makeAddrLValue(addr, ty, AlignmentSource::Decl));

      mlir::Value threadprivateAddr = mlir::omp::ThreadprivateOp::create(
          getBuilder(), getLoc(e->getExprLoc()), addr.getPointer().getType(),
          addr.getPointer());
      return markOpenMPNontemporal(
          vd, makeAddrLValue(addr.withPointer(threadprivateAddr), ty,
                             AlignmentSource::Decl));
    }

    if (vd->isEscapingByref()) {
      const auto &byrefInfo = getBlockByrefInfo(vd);
      if (addr.getElementType() == byrefInfo.type)
        addr = emitBlockByrefAddress(addr, vd, /*followForward=*/true);
    }

    // Drill into reference types.
    LValue lv;
    if (vd->getType()->isReferenceType()) {
      mlir::Type refStorageTy = convertTypeForMem(vd->getType());
      if (addr.getElementType() == refStorageTy) {
        lv = emitLoadOfReferenceLValue(addr, getLoc(e->getSourceRange()),
                                       vd->getType(), AlignmentSource::Decl);
      } else {
        lv = makeAddrLValue(addr, ty, AlignmentSource::Decl);
      }
    } else {
      lv = makeAddrLValue(addr, ty, AlignmentSource::Decl);
    }

    // Statics are defined as globals, so they are not include in the function's
    // symbol table.
    assert((vd->isStaticLocal() || symbolTable.count(vd)) &&
           "non-static locals should be already mapped");

    // OGCG calls setObjCGCLValueClass on local variable LValues (CGExpr.cpp:3629)
    // and sets ARC precise lifetime (CGExpr.cpp:3625-3628). ObjC GC was
    // deprecated in 2012 and is not supported by CIR. ARC lifetime is
    // handled via explicit NYI paths.

    return markOpenMPNontemporal(vd, lv);
  }

  if (const auto *bd = dyn_cast<BindingDecl>(nd)) {
    if (e->refersToEnclosingVariableOrCapture()) {
      if (auto *fd = lambdaCaptureFields.lookup(bd))
        return emitCapturedFieldLValue(*this, fd, cxxabiThisValue);
    }
    return emitLValue(bd->getBinding());
  }

  if (const auto *fd = dyn_cast<FunctionDecl>(nd)) {
    LValue lv = emitFunctionDeclLValue(*this, e, fd);
    return lv;
  }

  // OGCG handles MSGuidDecl (__declspec(uuid(...))) here. This is an MSVC
  // extension for COM GUIDs — Windows/MSVC is not a supported CIR target.

  if (const auto *tpo = dyn_cast<TemplateParamObjectDecl>(nd)) {
    cir::GlobalOp gv = cgm.getAddrOfTemplateParamObject(tpo);
    CharUnits alignment = getContext().getDeclAlign(tpo);
    mlir::Value addr =
        builder.createGetGlobal(getLoc(e->getSourceRange()), gv);
    return makeAddrLValue(Address(addr, gv.getSymType(), alignment), ty,
                          AlignmentSource::Decl);
  }

  cgm.errorNYI(e->getSourceRange(), "emitDeclRefLValue: unhandled decl type");
  return LValue();
}

LValue CIRGenFunction::emitObjCIvarRefLValue(const ObjCIvarRefExpr *e) {
  mlir::Value baseValue;
  const Expr *baseExpr = e->getBase();
  Qualifiers baseQuals;
  if (e->isArrow()) {
    baseValue = emitScalarExpr(baseExpr);
    if (const auto *objPtrTy = baseExpr->getType()->getAs<ObjCObjectPointerType>())
      baseQuals = objPtrTy->getPointeeType().getQualifiers();
  } else {
    LValue baseLV = emitLValue(baseExpr);
    assert(baseLV.isSimple() && "unexpected non-simple Objective-C ivar base");
    baseValue = baseLV.getPointer();
    baseQuals = baseExpr->getType().getQualifiers();
  }

  const ObjCInterfaceDecl *id = e->getDecl()->getContainingInterface();
  assert(id && "expected Objective-C ivar container");

  QualType interfaceTy{id->getTypeForDecl(), 0};
  QualType objectPtrTy = getContext().getObjCObjectPointerType(interfaceTy);
  QualType ivarTy =
      e->getDecl()->getUsageType(objectPtrTy).withCVRQualifiers(
          baseQuals.getCVRQualifiers());

  mlir::Location loc = getLoc(e->getSourceRange());
  mlir::Value baseBytePtr = builder.createPtrBitcast(baseValue, builder.getUInt8Ty());
  mlir::Value offset;
  if (isObjCClassLayoutKnownStatically(id) &&
      !usesNonFragileGNUObjCRuntime(*this)) {
    uint64_t ivarOffset =
        getContext().lookupFieldBitOffset(id, e->getDecl()) /
        getContext().getCharWidth();
    offset = builder.getConstant(loc, cir::IntAttr::get(
                                          mlir::cast<cir::IntType>(uIntPtrTy),
                                          ivarOffset));
  } else {
    cir::GlobalOp offsetGV =
        getOrCreateObjCIvarOffsetVariable(*this, loc, id, e->getDecl());
    mlir::Value offsetAddr = builder.createGetGlobal(loc, offsetGV);
    CharUnits offsetAlign =
        usesGNUstep2ObjCRuntime(*this)
            ? getContext().getTypeSizeInChars(getContext().IntTy)
            : getPointerAlign();
    cir::LoadOp load =
        builder.createLoad(loc, Address(offsetAddr, offsetGV.getSymType(),
                                        offsetAlign));
    if (!usesGNUObjCRuntime(*this))
      load->setAttr("cir.invariant_load",
                    mlir::UnitAttr::get(builder.getContext()));
    offset = load.getResult();
    if (usesLegacyGNUObjCRuntime(*this)) {
      auto offsetPtrTy = builder.getUInt32Ty();
      mlir::Value offsetPtr = builder.createPtrBitcast(offset, offsetPtrTy);
      cir::LoadOp offsetLoad = builder.createLoad(
          loc, Address(offsetPtr, offsetPtrTy,
                       getContext().getTypeSizeInChars(getContext().IntTy)));
      offset = offsetLoad.getResult();
    }
    if (offset.getType() != uIntPtrTy)
      offset = builder.createIntCast(offset, mlir::cast<cir::IntType>(uIntPtrTy));
  }

  mlir::Value ivarBytePtr = cir::PtrStrideOp::create(
      builder, loc, baseBytePtr.getType(), baseBytePtr, offset,
      cir::GEPNoWrapFlags::inbounds);

  if (!e->getDecl()->isBitField()) {
    mlir::Type ivarMemTy = convertTypeForMem(ivarTy);
    mlir::Value ivarPtr = builder.createPtrBitcast(ivarBytePtr, ivarMemTy);
    return makeNaturalAlignAddrLValue(ivarPtr, ivarTy);
  }

  uint64_t fieldBitOffset = getContext().lookupFieldBitOffset(id, e->getDecl());
  uint64_t bitOffset = fieldBitOffset % getContext().getCharWidth();
  uint64_t alignmentBits = getContext().getTargetInfo().getCharAlign();
  uint64_t bitFieldSize = e->getDecl()->getBitWidthValue();
  CharUnits storageSize = getContext().toCharUnitsFromBits(
      llvm::alignTo(bitOffset + bitFieldSize, alignmentBits));
  CharUnits alignment = getContext().toCharUnitsFromBits(alignmentBits);

  auto *info = new CIRGenBitFieldInfo(
      bitOffset, bitFieldSize, ivarTy->isSignedIntegerOrEnumerationType(),
      getContext().toBits(storageSize), CharUnits::Zero());
  info->name = e->getDecl()->getName();
  info->storageType = cir::IntType::get(builder.getContext(),
                                        getContext().toBits(storageSize),
                                        info->isSigned);

  mlir::Value ivarPtr = builder.createPtrBitcast(ivarBytePtr, info->storageType);
  Address addr(ivarPtr, info->storageType, alignment);
  return LValue::makeBitfield(addr, *info, ivarTy,
                              LValueBaseInfo(AlignmentSource::Decl),
                              TBAAAccessInfo());
}

LValue CIRGenFunction::emitObjCIsaExpr(const ObjCIsaExpr *e) {
  const Expr *baseExpr = e->getBase();
  Address addr = Address::invalid();

  if (baseExpr->isPRValue()) {
    addr = Address(emitScalarExpr(baseExpr), getPointerAlign());
  } else {
    addr = emitLValue(baseExpr).getAddress();
  }

  addr = addr.withElementType(builder, convertTypeForMem(e->getType()));
  return makeAddrLValue(addr, e->getType());
}

mlir::Value CIRGenFunction::evaluateExprAsBool(const Expr *e) {
  QualType boolTy = getContext().BoolTy;
  SourceLocation loc = e->getExprLoc();

  if (e->getType()->getAs<MemberPointerType>()) {
    mlir::Value memPtr = emitScalarExpr(e);
    return builder.createCast(getLoc(e->getExprLoc()),
                              cir::CastKind::member_ptr_to_bool, memPtr,
                              convertType(boolTy));
  }

  // OGCG creates CGFPOptionsRAII here to propagate FP exception/rounding
  // behavior from the expression through to the generated comparison.
  // In CIR, we post-attach FP constraints to the resulting float_to_bool cast.
  mlir::Value result;
  if (!e->getType()->isAnyComplexType())
    result = emitScalarConversion(emitScalarExpr(e), e->getType(), boolTy, loc);
  else
    result = emitComplexToScalarConversion(emitComplexExpr(e), e->getType(),
                                           boolTy, loc);

  FPOptions fpfeatures = e->getFPFeaturesInEffect(getLangOpts());
  if (fpfeatures.isFPConstrained() || IsFPConstrained) {
    if (auto castOp = result.getDefiningOp<cir::CastOp>()) {
      if (castOp.getKind() == cir::CastKind::float_to_bool) {
        auto *ctx = &getMLIRContext();
        if (auto fpExcept = getFPExceptionBehavior(ctx, fpfeatures))
          castOp->setAttr("cir.fp_exception", fpExcept);
        if (auto roundMode = getRoundingMode(ctx, fpfeatures))
          castOp->setAttr("cir.rounding_mode", roundMode);
      }
    }
  }

  return result;
}

LValue CIRGenFunction::emitUnaryOpLValue(const UnaryOperator *e) {
  UnaryOperatorKind op = e->getOpcode();

  // __extension__ doesn't affect lvalue-ness.
  if (op == UO_Extension)
    return emitLValue(e->getSubExpr());

  switch (op) {
  case UO_Deref: {
    QualType t = e->getSubExpr()->getType()->getPointeeType();
    assert(!t.isNull() && "CodeGenFunction::EmitUnaryOpLValue: Illegal type");

    // OGCG callers wrap dereference LValues in EmitCheckedLValue, which calls
    // EmitTypeCheck for null, alignment, and object-size checks. CIR lacks
    // this pattern entirely.
    LValueBaseInfo baseInfo;
    TBAAAccessInfo tbaaInfo;
    Address addr =
        emitPointerWithAlignment(e->getSubExpr(), &baseInfo, &tbaaInfo);

    // Tag 'load' with deref attribute.
    // FIXME: This misses some derefence cases and has problematic interactions
    // with other operators.
    if (auto loadOp = addr.getDefiningOp<cir::LoadOp>())
      loadOp.setIsDerefAttr(mlir::UnitAttr::get(&getMLIRContext()));

    // OGCG calls setNonGC() here for ObjC GC write barriers (deprecated).
    LValue lv = makeAddrLValue(addr, t, baseInfo, tbaaInfo);
    return lv;
  }
  case UO_Real:
  case UO_Imag: {
    LValue lv = emitLValue(e->getSubExpr());
    assert(lv.isSimple() && "real/imag on non-ordinary l-value");

    // __real is valid on scalars. This is a faster way of testing that.
    // __imag can only produce an rvalue on scalars.
    if (e->getOpcode() == UO_Real &&
        !mlir::isa<cir::ComplexType>(lv.getAddress().getElementType())) {
      assert(e->getSubExpr()->getType()->isArithmeticType());
      return lv;
    }

    QualType exprTy = getContext().getCanonicalType(e->getSubExpr()->getType());
    QualType elemTy = exprTy->castAs<clang::ComplexType>()->getElementType();
    mlir::Location loc = getLoc(e->getExprLoc());
    Address component =
        e->getOpcode() == UO_Real
            ? builder.createComplexRealPtr(loc, lv.getAddress())
            : builder.createComplexImagPtr(loc, lv.getAddress());
    LValue elemLV = makeAddrLValue(component, elemTy, lv.getBaseInfo(),
                                    cgm.getTBAAInfoForSubobject(lv, elemTy));
    elemLV.getQuals().addQualifiers(lv.getQuals());
    return elemLV;
  }
  case UO_PreInc:
  case UO_PreDec: {
    cir::UnaryOpKind kind =
        e->isIncrementOp() ? cir::UnaryOpKind::Inc : cir::UnaryOpKind::Dec;
    LValue lv = emitLValue(e->getSubExpr());

    assert(e->isPrefix() && "Prefix operator in unexpected state!");

    if (e->getType()->isAnyComplexType()) {
      emitComplexPrePostIncDec(e, lv, kind, /*isPre=*/true);
    } else {
      emitScalarPrePostIncDec(e, lv, kind, /*isPre=*/true);
    }

    return lv;
  }
  case UO_Extension:
    llvm_unreachable("UnaryOperator extension should be handled above!");
  case UO_Plus:
  case UO_Minus:
  case UO_Not:
  case UO_LNot:
  case UO_AddrOf:
  case UO_PostInc:
  case UO_PostDec:
  case UO_Coawait:
    llvm_unreachable("UnaryOperator of non-lvalue kind!");
  }
  llvm_unreachable("Unknown unary operator kind!");
}

/// If the specified expr is a simple decay from an array to pointer,
/// return the array subexpression.
/// FIXME: this could be abstracted into a common AST helper.
static const Expr *getSimpleArrayDecayOperand(const Expr *e) {
  // If this isn't just an array->pointer decay, bail out.
  const auto *castExpr = dyn_cast<CastExpr>(e);
  if (!castExpr || castExpr->getCastKind() != CK_ArrayToPointerDecay)
    return nullptr;

  // If this is a decay from variable width array, bail out.
  const Expr *subExpr = castExpr->getSubExpr();
  if (subExpr->getType()->isVariableArrayType())
    return nullptr;

  return subExpr;
}

static cir::IntAttr getConstantIndexOrNull(mlir::Value idx) {
  // TODO(cir): should we consider using MLIRs IndexType instead of IntegerAttr?
  if (auto constantOp = idx.getDefiningOp<cir::ConstantOp>())
    return constantOp.getValueAttr<cir::IntAttr>();
  return {};
}

static CharUnits getArrayElementAlign(CharUnits arrayAlign, mlir::Value idx,
                                      CharUnits eltSize) {
  // If we have a constant index, we can use the exact offset of the
  // element we're accessing.
  if (const cir::IntAttr constantIdx = getConstantIndexOrNull(idx)) {
    const CharUnits offset = constantIdx.getValue().getZExtValue() * eltSize;
    return arrayAlign.alignmentAtOffset(offset);
  }
  // Otherwise, use the worst-case alignment for any element.
  return arrayAlign.alignmentOfArrayElement(eltSize);
}

static QualType getFixedSizeElementType(const ASTContext &astContext,
                                        const VariableArrayType *vla) {
  QualType eltType;
  do {
    eltType = vla->getElementType();
  } while ((vla = astContext.getAsVariableArrayType(eltType)));
  return eltType;
}

static mlir::Value getArrayIndexingBound(CIRGenFunction &cgf, const Expr *base,
                                         QualType &indexedType) {
  if (const auto *vt = base->getType()->getAs<clang::VectorType>()) {
    indexedType = base->getType();
    return cgf.getBuilder().getConstInt(cgf.getLoc(base->getExprLoc()),
                                        cgf.getBuilder().getUInt32Ty(),
                                        vt->getNumElements());
  }

  base = base->IgnoreParens();

  if (const auto *ce = dyn_cast<CastExpr>(base)) {
    if (ce->getCastKind() == CK_ArrayToPointerDecay &&
        !ce->getSubExpr()->isFlexibleArrayMemberLike(
            cgf.getContext(), cgf.getLangOpts().getStrictFlexArraysLevel())) {
      indexedType = ce->getSubExpr()->getType();
      const clang::ArrayType *at = indexedType->castAsArrayTypeUnsafe();
      mlir::Location loc = cgf.getLoc(base->getExprLoc());
      if (const auto *cat = dyn_cast<ConstantArrayType>(at))
        return cgf.getBuilder().getConstInt(loc, cgf.ptrDiffTy,
                                            cat->getZExtSize());

      if (const auto *vat = dyn_cast<VariableArrayType>(at)) {
        mlir::Value bound = cgf.getVLASize(vat).numElts;
        if (bound.getType() != cgf.ptrDiffTy)
          bound = cgf.getBuilder().createIntCast(bound, cgf.ptrDiffTy);
        return bound;
      }
      // Ignore pass_object_size here. It's not applicable on decayed pointers.
    }
  }

  QualType eltTy{base->getType()->getPointeeOrArrayElementType(), 0};
  if (mlir::Value pos = cgf.loadPassedObjectSize(base, eltTy)) {
    indexedType = base->getType();
    if (pos.getType() != cgf.ptrDiffTy)
      pos = cgf.getBuilder().createIntCast(pos, cgf.ptrDiffTy);
    return pos;
  }

  return {};
}

static mlir::Value emitArraySubscriptPtr(CIRGenFunction &cgf,
                                         mlir::Location beginLoc,
                                         mlir::Location endLoc, mlir::Value ptr,
                                         mlir::Type eltTy, mlir::Value idx,
                                         bool shouldDecay) {
  CIRGenModule &cgm = cgf.getCIRGenModule();

  // Compute GEP no-wrap flags for the array subscript, matching OGCG's
  // emitArraySubscriptGEP which calls EmitCheckedInBoundsGEP.
  bool signedIndices = false;
  if (auto intTy = mlir::dyn_cast<cir::IntType>(idx.getType()))
    signedIndices = intTy.isSigned();
  cir::GEPNoWrapFlags nwFlags = cir::GEPNoWrapFlags::none;
  if (!cgf.getLangOpts().PointerOverflowDefined) {
    nwFlags = cir::GEPNoWrapFlags::inbounds;
    if (!signedIndices)
      nwFlags = nwFlags | cir::GEPNoWrapFlags::nuw;
  }

  return cgm.getBuilder().getArrayElement(beginLoc, endLoc, ptr, eltTy, idx,
                                          shouldDecay, nwFlags);
}

static Address emitArraySubscriptPtr(CIRGenFunction &cgf,
                                     mlir::Location beginLoc,
                                     mlir::Location endLoc, Address addr,
                                     QualType eltType, mlir::Value idx,
                                     mlir::Location loc,
                                     SourceLocation sourceLoc,
                                     bool signedIndices,
                                     bool shouldDecay,
                                     QualType preserveAccessType) {
  // In some template-heavy paths we can still see dependent QualType wrappers
  // here even though the CIR pointer element type is already concrete.
  // Avoid querying AST size/alignment for dependent QualTypes; use the CIR
  // element type/alignment we already have.
  if (eltType->isDependentType()) {
    const mlir::Value eltPtr =
        emitArraySubscriptPtr(cgf, beginLoc, endLoc, addr.getPointer(),
                              addr.getElementType(), idx, shouldDecay);
    return Address(eltPtr, addr.getElementType(), addr.getAlignment());
  }

  // Determine the element size of the statically-sized base.  This is
  // the thing that the indices are expressed in terms of.
  if (const VariableArrayType *vla =
          cgf.getContext().getAsVariableArrayType(eltType)) {
    eltType = getFixedSizeElementType(cgf.getContext(), vla);
  }

  // We can use that to compute the best alignment of the element.
  const CharUnits eltSize = cgf.getContext().getTypeSizeInChars(eltType);
  const CharUnits eltAlign =
      getArrayElementAlign(addr.getAlignment(), idx, eltSize);

  const mlir::Value basePtr = addr.getPointer();
  const mlir::Type accessEltTy = addr.getElementType();
  mlir::Value eltPtr;
  // Check for BPF preserve_access_index on the base type's pointee record.
  bool isPreservedAI = cgf.IsInPreservedAIRegion;
  if (!isPreservedAI && !preserveAccessType.isNull()) {
    QualType basePointee = preserveAccessType->getPointeeType();
    if (!basePointee.isNull())
      if (const auto *recTy = basePointee->getAs<clang::RecordType>())
        isPreservedAI = recTy->getDecl()
                            ->getMostRecentDecl()
                            ->hasAttr<BPFPreserveAccessIndexAttr>();
  }
  if (isPreservedAI) {
    if (auto constantIndex = getConstantIntValueAndSignedness(idx);
        constantIndex && !constantIndex->first.isNegative() &&
        constantIndex->first.getActiveBits() <= 32) {
      unsigned dimension = 0;
      mlir::Type resultEltTy = accessEltTy;
      if (shouldDecay)
        if (auto arrayTy = mlir::dyn_cast<cir::ArrayType>(accessEltTy)) {
          dimension = 1;
          resultEltTy = arrayTy.getElementType();
        }

      auto basePtrTy = mlir::cast<cir::PointerType>(basePtr.getType());
      auto resultPtrTy =
          cgf.getBuilder().getPointerTo(resultEltTy, basePtrTy.getAddrSpace());
      auto i32Ty = cgf.getBuilder().getSIntNTy(32);
      mlir::Value dimVal =
          cgf.getBuilder().getConstInt(loc, i32Ty, dimension);
      mlir::Value lastIndexVal = cgf.getBuilder().getConstInt(
          loc, i32Ty, constantIndex->first.getZExtValue());
      mlir::Location preserveLoc =
          cgf.getLoc(getPreserveAccessDebugLoc(cgf, sourceLoc));
      cir::LLVMIntrinsicCallOp preserve = cir::LLVMIntrinsicCallOp::create(
          cgf.getBuilder(), preserveLoc,
          cgf.getCIRGenModule().getStringAttrCached(
              "preserve.array.access.index"),
          resultPtrTy, mlir::ValueRange{basePtr, dimVal, lastIndexVal});
      setPreserveAccessIndexSourceTypeAttr(cgf, preserve, preserveAccessType);
      eltPtr = preserve.getResult();
    }
  }
  if (!eltPtr)
    eltPtr = emitArraySubscriptPtr(cgf, beginLoc, endLoc, basePtr,
                                   accessEltTy, idx, shouldDecay);

  if (!cgf.getLangOpts().PointerOverflowDefined &&
      cgf.sanOpts.has(SanitizerKind::PointerOverflow)) {
    auto accessPtrTy = mlir::cast<cir::PointerType>(basePtr.getType());
    mlir::Type stepTy = accessEltTy;
    mlir::Value scaledIndex = idx;
    if (mlir::isa<cir::ArrayType>(accessPtrTy.getPointee()) &&
        shouldDecay &&
        accessPtrTy.getPointee() == accessEltTy) {
      stepTy = mlir::cast<cir::ArrayType>(accessEltTy).getElementType();
      scaledIndex = cgf.getBuilder().createIntCast(scaledIndex, cgf.ptrDiffTy);
      if (auto *op = scaledIndex.getDefiningOp())
        markNoSanitize(op);
    }

    mlir::Type decayedBaseTy = basePtr.getType();
    if (shouldDecay &&
        mlir::isa<cir::ArrayType>(accessPtrTy.getPointee()) &&
        accessPtrTy.getPointee() != accessEltTy) {
      decayedBaseTy =
          cgf.getBuilder().getPointerTo(accessEltTy, accessPtrTy.getAddrSpace());
    }

    mlir::Value checkBasePtr = basePtr;
    if (decayedBaseTy != basePtr.getType()) {
      checkBasePtr = cgf.getBuilder().maybeBuildArrayDecay(beginLoc, basePtr,
                                                           accessEltTy);
      if (auto *op = checkBasePtr.getDefiningOp())
        markNoSanitize(op);
    }

    // GNU pointer arithmetic on `void*` (and other incomplete element types)
    // uses an element size of 1. stepTy can be `!cir.void`, which is unsized;
    // calling getTypeAllocSize on it would assert. Match classic CodeGen by
    // treating unsized elements as size 1 for the bounds/overflow check.
    uint64_t elementSize =
        cir::isSized(stepTy)
            ? cgf.cgm.getDataLayout().getTypeAllocSize(stepTy).getFixedValue()
            : 1;
    auto [byteOffset, offsetOverflow] =
        cgf.emitCheckedByteOffset(loc, scaledIndex, elementSize);
    cgf.emitPointerOverflowCheck(loc, checkBasePtr, byteOffset,
                                 offsetOverflow, signedIndices,
                                 /*isSubtraction=*/false,
                                 sourceLoc);
  }
  // The element type of the resulting Address must match the pointee of the
  // pointer we just built. For most types convertTypeForMem(eltType) already
  // equals that pointee, but a _BitInt(N) whose N is not a byte multiple has a
  // padded storage type (e.g. i8 for _BitInt(7), i128 for _BitInt(65)) that
  // differs from the value type used as the CIR pointer's pointee. The GEP
  // pointee (and the zero-index fast path in emitArraySubscriptExpr) both use
  // the value type, and the pointer's ABI storage width is recovered during
  // LLVM lowering (convertTypeForAddressing), so the emitted stride still
  // matches classic CodeGen. Deriving the element type from eltPtr keeps
  // Address's invariant intact for every path.
  const mlir::Type elementType =
      mlir::cast<cir::PointerType>(eltPtr.getType()).getPointee();
  return Address(eltPtr, elementType, eltAlign);
}

mlir::Value CIRGenFunction::loadPassedObjectSize(const Expr *e, QualType eltTy) {
  uint64_t eltSize = getContext().getTypeSizeInChars(eltTy).getQuantity();
  if (!eltSize)
    return {};

  auto *arrayDeclRef = dyn_cast<DeclRefExpr>(e->IgnoreParenImpCasts());
  if (!arrayDeclRef)
    return {};

  auto *paramDecl = dyn_cast<ParmVarDecl>(arrayDeclRef->getDecl());
  if (!paramDecl)
    return {};

  auto *posAttr = paramDecl->getAttr<PassObjectSizeAttr>();
  if (!posAttr)
    return {};

  int posType = posAttr->getType();
  if (posType != 0 && posType != 1)
    return {};

  auto passedSizeIt = sizeArguments.find(paramDecl);
  if (passedSizeIt == sizeArguments.end())
    return {};

  const ImplicitParamDecl *passedSizeDecl = passedSizeIt->second;
  auto addrIt = localDeclMap.find(passedSizeDecl);
  assert(addrIt != localDeclMap.end() && "passed size not loadable");

  mlir::Value sizeInBytes =
      emitLoadOfScalar(addrIt->second, /*isVolatile=*/false,
                       getContext().getSizeType(), e->getExprLoc(),
                       LValueBaseInfo{}, TBAAAccessInfo{},
                       /*isNontemporal=*/false);
  mlir::Value sizeOfElement = builder.getConstInt(getLoc(e->getExprLoc()),
                                                  ptrDiffTy, eltSize);
  if (sizeInBytes.getType() != ptrDiffTy)
    sizeInBytes = builder.createIntCast(sizeInBytes, ptrDiffTy);
  return cir::BinOp::create(builder, getLoc(e->getExprLoc()), ptrDiffTy,
                            cir::BinOpKind::Div, sizeInBytes, sizeOfElement,
                            {}, {}, {}, {}, {}, {});
}

void CIRGenFunction::emitBoundsCheck(const Expr *arrayExpr,
                                     const Expr *arrayExprBase,
                                     mlir::Value indexVal, QualType indexType,
                                     bool accessed) {
  assert(sanOpts.has(SanitizerKind::ArrayBounds) &&
         "should not be called unless adding bounds checks");

  QualType arrayBaseType;
  mlir::Value boundsVal =
      getArrayIndexingBound(*this, arrayExprBase, arrayBaseType);
  if (!boundsVal)
    return;

  bool indexSigned = indexType->isSignedIntegerOrEnumerationType();
  bool boundsSigned = false;
  if (arrayBaseType->isArrayType())
    boundsSigned = false;
  else
    boundsSigned = getContext().getSizeType()->isSignedIntegerOrEnumerationType();

  unsigned compareWidth = std::max(getContext().getTypeSize(indexType),
                                   getContext().getTypeSize(getContext().getSizeType()));
  auto compareTy = boundsSigned ? builder.getSIntNTy(compareWidth)
                                : builder.getUIntNTy(compareWidth);

  mlir::Location loc = getLoc(arrayExpr->getExprLoc());
  mlir::Value indexInst = indexVal;
  if (auto intTy = mlir::cast<cir::IntType>(indexInst.getType());
      intTy.isSigned() != indexSigned) {
    auto castTy = indexSigned ? builder.getSIntNTy(intTy.getWidth())
                              : builder.getUIntNTy(intTy.getWidth());
    indexInst = builder.createCast(loc, cir::CastKind::integral, indexInst,
                                   castTy);
    if (auto *op = indexInst.getDefiningOp())
      markNoSanitize(op);
  }
  if (indexInst.getType() != compareTy) {
    indexInst = builder.createIntCast(indexInst, compareTy);
    if (auto *op = indexInst.getDefiningOp())
      markNoSanitize(op);
  }

  mlir::Value boundsInst = boundsVal;
  if (auto intTy = mlir::cast<cir::IntType>(boundsInst.getType());
      intTy.isSigned()) {
    boundsInst = builder.createCast(loc, cir::CastKind::integral, boundsInst,
                                    builder.getUIntNTy(intTy.getWidth()));
    if (auto *op = boundsInst.getDefiningOp())
      markNoSanitize(op);
  }
  if (boundsInst.getType() != compareTy) {
    boundsInst = builder.createIntCast(boundsInst, compareTy);
    if (auto *op = boundsInst.getDefiningOp())
      markNoSanitize(op);
  }

  mlir::Value check = cir::CmpOp::create(builder, loc,
                                         accessed ? cir::CmpOpKind::lt
                                                  : cir::CmpOpKind::le,
                                         indexInst, boundsInst);
  markNoSanitize(check.getDefiningOp());

  if (boundsSigned) {
    mlir::Value zero = builder.getConstInt(loc, boundsVal.getType(), 0);
    mlir::Value positiveBounds =
        cir::CmpOp::create(builder, loc, cir::CmpOpKind::gt, boundsVal, zero);
    markNoSanitize(positiveBounds.getDefiningOp());
    check = builder.createAnd(loc, positiveBounds, check);
    if (auto *op = check.getDefiningOp())
      markNoSanitize(op);
  }

  StringRef handlerKind =
      cgm.getCodeGenOpts().SanitizeRecover.has(SanitizerKind::ArrayBounds)
          ? "recover"
          : "abort";
  if (cgm.getCodeGenOpts().SanitizeTrap.has(SanitizerKind::ArrayBounds))
    handlerKind = "trap";

  auto ifOp = cir::IfOp::create(
      builder, loc, check, /*withElseRegion=*/true,
      [&](mlir::OpBuilder &, mlir::Location ifLoc) { builder.createYield(ifLoc); },
      [&](mlir::OpBuilder &, mlir::Location ifLoc) {
        if (handlerKind == "trap") {
          auto trap = cir::TrapOp::create(builder, ifLoc);
          trap->setAttr(
              "cir.ubsantrap_handler",
              mlir::IntegerAttr::get(
                  mlir::IntegerType::get(builder.getContext(), 8), 18));
          markNoSanitize(trap);
          return;
        }

        auto intptrTy = mlir::cast<cir::IntType>(
            convertType(getContext().getUIntPtrType()));
        auto stubFnTy = cir::FuncType::get({intptrTy}, builder.getVoidTy());
        auto stubFn = cgm.createRuntimeFunction(
            stubFnTy, "__cir_ubsan_out_of_bounds", /*attrs=*/{},
            /*isLocal=*/false, /*assumeConvergent=*/false);
        mlir::Value dynamicIndex = indexInst;
        if (dynamicIndex.getType() != intptrTy)
          dynamicIndex = builder.createIntCast(dynamicIndex, intptrTy);
        if (auto *op = dynamicIndex.getDefiningOp())
          markNoSanitize(op);
        auto call = builder.createCallOp(ifLoc, stubFn, {dynamicIndex});
        setUBSanSourceLocationAttrs(*this, call, arrayExpr->getExprLoc());
        setUBSanTypeNameAttr(*this, call, arrayBaseType);
        setUBSanTypeDescriptorAttrs(*this, call, arrayBaseType);
        call->setDiscardableAttr("cir.ubsan.index_type_name",
                                 builder.getStringAttr([&] {
                                   SmallString<32> typeName;
                                   cgm.getDiags().ConvertArgToString(
                                       DiagnosticsEngine::ak_qualtype,
                                       (intptr_t)indexType.getAsOpaquePtr(),
                                       StringRef(), StringRef(), {}, typeName,
                                       {});
                                   return typeName.str().str();
                                 }()));
        uint16_t indexTypeKind = 0xFFFF;
        uint16_t indexTypeInfo = 0;
        if (indexType->isIntegerType()) {
          indexTypeKind = 0;
          indexTypeInfo =
              (llvm::Log2_32(getContext().getTypeSize(indexType)) << 1) |
              (indexType->isSignedIntegerType() ? 1 : 0);
        } else if (indexType->isFloatingType()) {
          indexTypeKind = 1;
          indexTypeInfo = getContext().getTypeSize(indexType);
        }
        auto *ctx = builder.getContext();
        auto i16Ty = mlir::IntegerType::get(ctx, 16);
        call->setDiscardableAttr("cir.ubsan.index_type_kind",
                                 mlir::IntegerAttr::get(i16Ty, indexTypeKind));
        call->setDiscardableAttr("cir.ubsan.index_type_info",
                                 mlir::IntegerAttr::get(i16Ty, indexTypeInfo));
        call->setDiscardableAttr("cir.ubsan.handler_kind",
                                 builder.getStringAttr(handlerKind));
        call->setDiscardableAttr("cir.ubsan.handler_name",
                                 builder.getStringAttr("out_of_bounds"));
        call->setDiscardableAttr("cir.nosanitize",
                                 mlir::UnitAttr::get(builder.getContext()));

        if (handlerKind == "abort") {
          auto unreachable = cir::UnreachableOp::create(builder, ifLoc);
          markNoSanitize(unreachable);
        } else {
          builder.createYield(ifLoc);
        }
      });
  setSanitizerBranchAttrs(builder, ifOp);
}

LValue
CIRGenFunction::emitArraySubscriptExpr(const clang::ArraySubscriptExpr *e,
                                       bool accessed) {
  // A dependent ArraySubscriptExpr on a class-typed base originated from a
  // template definition where operator[] couldn't be resolved.  After
  // substitution the base has a concrete class type but the AST was never
  // rebuilt, so we resolve operator[] here.  For simple containers whose
  // operator[] returns a reference to an element of a contiguous member
  // array (e.g. std::array), we emit a direct GEP into the first field
  // instead of synthesising a full member call.
  if (e->getType()->isDependentType()) {
    const Expr *base = e->getBase()->IgnoreParenImpCasts();
    QualType baseTy = base->getType();
    if (const auto *rd = baseTy->getAsCXXRecordDecl()) {
      // Look up operator[] to determine the element type.
      DeclarationName opName =
          getContext().DeclarationNames.getCXXOperatorName(OO_Subscript);
      auto results = rd->lookup(opName);
      if (!results.empty()) {
        const auto *opDecl = cast<FunctionDecl>(results.front());
        QualType retTy = opDecl->getReturnType().getNonReferenceType();

        // Emit the base struct as an lvalue.
        LValue baseLV = emitLValue(e->getBase());

        // GEP to the first field (the underlying C array) then subscript.
        // This matches the layout of std::array and similar wrappers.
        auto fieldIt = rd->field_begin();
        if (fieldIt != rd->field_end()) {
          const FieldDecl *firstField = *fieldIt;
          QualType fieldTy = firstField->getType();
          // If the first field is an array of the right element type,
          // subscript directly into it.
          if (const auto *arrTy =
                  getContext().getAsConstantArrayType(fieldTy)) {
            QualType elemTy = arrTy->getElementType();
            // Emit the member access to get the array field.
            LValue fieldLV = emitLValueForField(baseLV, firstField);
            // Now subscript into the array.
            mlir::Value idx = emitScalarExpr(e->getIdx());
            Address addr = emitArraySubscriptPtr(
                *this, cgm.getLoc(e->getBeginLoc()),
                cgm.getLoc(e->getEndLoc()), fieldLV.getAddress(),
                elemTy, idx, cgm.getLoc(e->getExprLoc()), e->getExprLoc(),
                mlir::cast<cir::IntType>(idx.getType()).isSigned(),
                /*shouldDecay=*/true, fieldLV.getType());
            return makeAddrLValue(addr, retTy);
          }
        }
      }
    }
    // Dependent-type heuristics didn't match (no CXXRecordDecl, no
    // operator[], or first field is not a constant array). Crash with a
    // diagnostic rather than silently falling through to normal codegen
    // which would mishandle the dependent type.
    cgm.errorNYI(e->getSourceRange(),
                 "emitArraySubscriptExpr: dependent type on non-array class");
    return LValue();
  }

  if (e->getType()->getAs<ObjCObjectType>()) {
    cgm.errorNYI(e->getSourceRange(), "emitArraySubscriptExpr: ObjCObjectType");
    return LValue();
  }

// The index must always be an integer, which is not an aggregate.  Emit it
  // in lexical order (this complexity is, sadly, required by C++17).
  assert((e->getIdx() == e->getLHS() || e->getIdx() == e->getRHS()) &&
         "index was neither LHS nor RHS");

  auto emitIdxAfterBase = [&](bool promote) -> mlir::Value {
    mlir::Value idx = emitScalarExpr(e->getIdx());

    // CIR's GetElementOp and PtrStrideOp require a fundamental integer index,
    // not a bool. Cast !cir.bool to an integer type before use. OGCG handles
    // this via CreateIntCast to IntPtrTy (LLVM's i1 is an IntegerType, but
    // CIR's !cir.bool is a distinct type from !cir.int).
    if (promote && mlir::isa<cir::BoolType>(idx.getType()))
      idx = builder.createBoolToInt(idx, ptrDiffTy);

    // Extend or truncate the index type to 32 or 64-bits.
    auto ptrTy = mlir::dyn_cast<cir::PointerType>(idx.getType());
    if (promote && ptrTy && ptrTy.isPtrTo<cir::IntType>()) {
      cgm.errorNYI(e->getSourceRange(),
                   "emitArraySubscriptExpr: index type cast");
      return idx;
    }

    if (sanOpts.has(SanitizerKind::ArrayBounds))
      emitBoundsCheck(e, e->getBase(), idx, e->getIdx()->getType(), accessed);

    return idx;
  };

  // If the base is a vector type, then we are forming a vector element
  // with this subscript.
  if (e->getBase()->getType()->isSubscriptableVectorType() &&
      !isa<ExtVectorElementExpr>(e->getBase())) {
    const mlir::Value idx = emitIdxAfterBase(/*promote=*/false);
    const LValue lv = emitLValue(e->getBase());
    return LValue::makeVectorElt(lv.getAddress(), idx, e->getBase()->getType(),
                                 lv.getBaseInfo());
  }

  // The HLSL runtime handles subscript expressions on global resource arrays
  // and objects with HLSL buffer layouts.
  if (getLangOpts().HLSL) {
    cgm.errorNYI(e->getSourceRange(), "emitArraySubscriptExpr: HLSL");
    return LValue();
  }

  mlir::Value precomputedIdx;
  bool precomputedZeroIndex = false;
  if (!sanOpts.has(SanitizerKind::ArrayBounds)) {
    precomputedIdx = emitIdxAfterBase(/*promote=*/true);
    if (auto constantIndex = getConstantIntValueAndSignedness(precomputedIdx))
      precomputedZeroIndex = constantIndex->first.isZero();
  }

  // Handle the extvector case we ignored above.
  if (isa<ExtVectorElementExpr>(e->getBase())) {
    mlir::Value idx = sanOpts.has(SanitizerKind::ArrayBounds)
                          ? emitIdxAfterBase(/*promote=*/true)
                          : precomputedIdx;
    const LValue lv = emitLValue(e->getBase());
    Address addr = emitExtVectorElementLValue(lv, cgm.getLoc(e->getExprLoc()));

    QualType elementType = lv.getType()->castAs<VectorType>()->getElementType();
    addr = emitArraySubscriptPtr(*this, cgm.getLoc(e->getBeginLoc()),
                                 cgm.getLoc(e->getEndLoc()), addr, e->getType(),
                                 idx, cgm.getLoc(e->getExprLoc()),
                                 e->getExprLoc(),
                                 mlir::cast<cir::IntType>(idx.getType())
                                     .isSigned(),
                                 /*shouldDecay=*/false, QualType{});

    return makeAddrLValue(addr, elementType, lv.getBaseInfo());
  }

  if (const VariableArrayType *vla =
          getContext().getAsVariableArrayType(e->getType())) {
    // The base must be a pointer, which is not an aggregate.  Emit
    // it.  It needs to be emitted first in case it's what captures
    // the VLA bounds.
    LValueBaseInfo eltBaseInfo;
    TBAAAccessInfo eltTBAAInfo;
    Address addr =
        emitPointerWithAlignment(e->getBase(), &eltBaseInfo, &eltTBAAInfo);

    // The element count here is the total number of non-VLA elements.
    mlir::Value numElements = getVLASize(vla).numElts;
    mlir::Value idx = sanOpts.has(SanitizerKind::ArrayBounds)
                          ? emitIdxAfterBase(/*promote=*/true)
                          : precomputedIdx;
    idx = builder.createIntCast(idx, numElements.getType());

    // Effectively, the multiply by the VLA size is part of the GEP.
    // GEP indexes are signed, and scaling an index isn't permitted to
    // signed-overflow, so we use the same semantics for our explicit
    // multiply.  We suppress this if overflow is not undefined behavior.
    OverflowBehavior overflowBehavior = getLangOpts().PointerOverflowDefined
                                            ? OverflowBehavior::None
                                            : OverflowBehavior::NoSignedWrap;
    idx = builder.createMul(cgm.getLoc(e->getExprLoc()), idx, numElements,
                            overflowBehavior);

    addr = emitArraySubscriptPtr(*this, cgm.getLoc(e->getBeginLoc()),
                                 cgm.getLoc(e->getEndLoc()), addr, e->getType(),
                                 idx, cgm.getLoc(e->getExprLoc()),
                                 e->getExprLoc(), /*signedIndices=*/true,
                                 /*shouldDecay=*/false,
                                 e->getBase()->getType());

    return makeAddrLValue(addr, vla->getElementType(), eltBaseInfo,
                          eltTBAAInfo);
  }

  if (const Expr *array = getSimpleArrayDecayOperand(e->getBase())) {
    LValue arrayLV;
    if (const auto *ase = dyn_cast<ArraySubscriptExpr>(array))
      arrayLV = emitArraySubscriptExpr(ase, /*accessed=*/true);
    else
      arrayLV = emitLValue(array);
    mlir::Value idx = sanOpts.has(SanitizerKind::ArrayBounds)
                          ? emitIdxAfterBase(/*promote=*/true)
                          : precomputedIdx;

    // Propagate the alignment from the array itself to the result.
    const Address addr = emitArraySubscriptPtr(
        *this, cgm.getLoc(array->getBeginLoc()), cgm.getLoc(array->getEndLoc()),
        arrayLV.getAddress(), e->getType(), idx, cgm.getLoc(e->getExprLoc()),
        e->getExprLoc(),
        mlir::cast<cir::IntType>(idx.getType()).isSigned(),
        /*shouldDecay=*/true, arrayLV.getType());

    LValueBaseInfo eltBaseInfo = arrayLV.getBaseInfo();
    TBAAAccessInfo eltTBAAInfo;
    if (!cgm.getCodeGenOpts().NewStructPathTBAA) {
      // Since CIRGenTBAA::getTypeInfoHelper only handles array types for
      // new struct path TBAA, we must use a plain access.
      eltTBAAInfo = cgm.getTBAAInfoForSubobject(arrayLV, e->getType());
    } else if (arrayLV.getTBAAInfo().isMayAlias()) {
      eltTBAAInfo = TBAAAccessInfo::getMayAliasInfo();
    } else if (arrayLV.getTBAAInfo().isIncomplete()) {
      // The array element is complete, even if the array is not.
      eltTBAAInfo = cgm.getTBAAAccessInfo(e->getType());
    } else {
      // The TBAA access info from the array (base) lvalue is ordinary. We
      // will adapt it to create access info for the element.
      eltTBAAInfo = arrayLV.getTBAAInfo();

      // We retain the TBAA struct path (BaseType and Offset members) from
      // the array. In the TBAA representation, we map any array access to
      // the element at index 0, as the index is generally a runtime value.
      // This element has the same offset in the base type as the array
      // itself. If the array lvalue had no base type, there is no point
      // trying to generate one, since an array itself is not a valid base
      // type.

      // We also retain the access type from the base lvalue, but the access
      // size must be updated to the size of an individual element.
      eltTBAAInfo.size =
          getContext().getTypeSizeInChars(e->getType()).getQuantity();
    }

    const LValue lv = makeAddrLValue(addr, e->getType(), eltBaseInfo,
                                     eltTBAAInfo);

    return lv;
  }

  // The base must be a pointer; emit it with an estimate of its alignment.
  // Note: OGCG does not assert isPointerType() here — the base may be an
  // array type that emitPointerWithAlignment handles via decay.

  LValueBaseInfo eltBaseInfo;
  TBAAAccessInfo eltTBAAInfo;
  const Address ptrAddr =
      emitPointerWithAlignment(e->getBase(), &eltBaseInfo, &eltTBAAInfo);
  mlir::Value idx = sanOpts.has(SanitizerKind::ArrayBounds)
                        ? emitIdxAfterBase(/*promote=*/true)
                        : precomputedIdx;
  bool zeroIndex = precomputedZeroIndex;
  if (sanOpts.has(SanitizerKind::ArrayBounds))
    if (auto constantIndex = getConstantIntValueAndSignedness(idx))
      zeroIndex = constantIndex->first.isZero();
  // Don't skip zero-index GEP for preserve_access_index types — BPF CO-RE
  // needs the preserve intrinsic even for index 0.
  bool isPreservedAIBase = IsInPreservedAIRegion;
  if (!isPreservedAIBase) {
    QualType baseType = e->getBase()->getType();
    if (const auto *ptrTy = baseType->getAs<PointerType>())
      if (const auto *recTy = ptrTy->getPointeeType()
                                  ->getUnqualifiedDesugaredType()
                                  ->getAs<clang::RecordType>())
        isPreservedAIBase = recTy->getDecl()
                                ->getMostRecentDecl()
                                ->hasAttr<BPFPreserveAccessIndexAttr>();
  }
  if (zeroIndex && !isPreservedAIBase) {
    const LValue lv =
        makeAddrLValue(ptrAddr, e->getType(), eltBaseInfo, eltTBAAInfo);
    return lv;
  }
  // Propagate the alignment from the array itself to the result.
  const Address addxr = emitArraySubscriptPtr(
      *this, cgm.getLoc(e->getBeginLoc()), cgm.getLoc(e->getEndLoc()), ptrAddr,
      e->getType(), idx, cgm.getLoc(e->getExprLoc()), e->getExprLoc(),
      mlir::cast<cir::IntType>(idx.getType()).isSigned(),
      /*shouldDecay=*/false, e->getBase()->getType());

  const LValue lv = makeAddrLValue(addxr, e->getType(), eltBaseInfo,
                                   eltTBAAInfo);

  return lv;
}

LValue CIRGenFunction::emitMatrixSubscriptExpr(const MatrixSubscriptExpr *e) {
  assert(!e->isIncomplete() &&
         "incomplete matrix subscript expressions should be rejected in Sema");

  LValue base = emitLValue(e->getBase());
  mlir::Value rowIdx = emitMatrixIndexExpr(e->getRowIdx());
  mlir::Value colIdx = emitMatrixIndexExpr(e->getColumnIdx());
  const auto *matrixTy = e->getBase()->getType()->castAs<ConstantMatrixType>();
  const bool isRowMajor =
      getLangOpts().getDefaultMatrixMemoryLayout() ==
      LangOptions::MatrixMemoryLayout::MatrixRowMajor;
  mlir::Location loc = getLoc(e->getSourceRange());
  mlir::Value stride = builder.getConstInt(
      loc, ptrDiffTy,
      isRowMajor ? matrixTy->getNumColumns() : matrixTy->getNumRows());
  mlir::Value scaled = cir::BinOp::create(
      builder, loc, cir::BinOpKind::Mul, isRowMajor ? rowIdx : colIdx, stride,
      /*no_unsigned_wrap=*/false, /*no_signed_wrap=*/false,
      /*saturated=*/false, cir::FastMathFlags::none,
      cir::FPExceptionBehaviorAttr{}, cir::RoundingModeAttr{});
  mlir::Value finalIdx = cir::BinOp::create(
      builder, loc, cir::BinOpKind::Add, scaled,
      isRowMajor ? colIdx : rowIdx, /*no_unsigned_wrap=*/false,
      /*no_signed_wrap=*/false, /*saturated=*/false,
      cir::FastMathFlags::none, cir::FPExceptionBehaviorAttr{},
      cir::RoundingModeAttr{});
  return LValue::makeMatrixElt(
      maybeConvertMatrixAddress(base.getAddress(), e->getBase()->getType(),
                                *this),
      finalIdx, e->getBase()->getType(), base.getBaseInfo(),
      base.getTBAAInfo());
}

LValue CIRGenFunction::emitExtVectorElementExpr(const ExtVectorElementExpr *e) {
  // Emit the base vector as an l-value.
  LValue base;

  // ExtVectorElementExpr's base can either be a vector or pointer to vector.
  if (e->isArrow()) {
    // If it is a pointer to a vector, emit the address and form an lvalue with
    // it.
    LValueBaseInfo baseInfo;
    Address ptr = emitPointerWithAlignment(e->getBase(), &baseInfo);
    const auto *clangPtrTy =
        e->getBase()->getType()->castAs<clang::PointerType>();
    base = makeAddrLValue(ptr, clangPtrTy->getPointeeType(), baseInfo);
    base.getQuals().removeObjCGCAttr();
  } else if (e->getBase()->isGLValue()) {
    // Otherwise, if the base is an lvalue ( as in the case of foo.x.x),
    // emit the base as an lvalue.
    assert(e->getBase()->getType()->isVectorType());
    base = emitLValue(e->getBase());
  } else {
    // Otherwise, the base is a normal rvalue (as in (V+V).x), emit it as such.
    assert(e->getBase()->getType()->isVectorType() &&
           "Result must be a vector");
    mlir::Value vec = emitScalarExpr(e->getBase());

    // Store the vector to memory (because LValue wants an address).
    QualType baseTy = e->getBase()->getType();
    Address vecMem = createMemTemp(baseTy, vec.getLoc(), "tmp");
    if (!getLangOpts().HLSL && baseTy->isExtVectorBoolType()) {
      cgm.errorNYI(e->getSourceRange(),
                   "emitExtVectorElementExpr: ExtVectorBoolType & !HLSL");
      return LValue();
    }
    builder.createStore(vec.getLoc(), vec, vecMem);
    base = makeAddrLValue(vecMem, baseTy, AlignmentSource::Decl);
  }

  QualType type =
      e->getType().withCVRQualifiers(base.getQuals().getCVRQualifiers());

  // Encode the element access list into a vector of unsigned indices.
  SmallVector<uint32_t, 4> indices;
  e->getEncodedElementAccess(indices);

  if (base.isSimple()) {
    SmallVector<int64_t> attrElts(indices.begin(), indices.end());
    mlir::ArrayAttr elts = builder.getI64ArrayAttr(attrElts);
    return LValue::makeExtVectorElt(base.getAddress(), elts, type,
                                    base.getBaseInfo());
  }

  assert(base.isExtVectorElt() && "Can only subscript lvalue vec elts here!");

  mlir::ArrayAttr baseElts = base.getExtVectorElts();

  // Composite the two indices
  SmallVector<int64_t, 4> attrElts;
  for (uint32_t i : indices) {
    attrElts.push_back(getAccessedFieldNo(i, baseElts));
  }
  mlir::ArrayAttr elts = builder.getI64ArrayAttr(attrElts);

  return LValue::makeExtVectorElt(base.getExtVectorAddress(), elts, type,
                                  base.getBaseInfo());
}

LValue CIRGenFunction::emitStringLiteralLValue(const StringLiteral *e,
                                               llvm::StringRef name) {
  cir::GlobalOp globalOp = cgm.getGlobalForStringLiteral(e, name);
  assert(globalOp.getAlignment() && "expected alignment for string literal");
  unsigned align = *(globalOp.getAlignment());
  mlir::Value addr =
      builder.createGetGlobal(getLoc(e->getSourceRange()), globalOp);
  return makeAddrLValue(
      Address(addr, globalOp.getSymType(), CharUnits::fromQuantity(align)),
      e->getType(), AlignmentSource::Decl);
}

LValue CIRGenFunction::emitObjCEncodeExprLValue(const ObjCEncodeExpr *e) {
  cir::GlobalOp globalOp = cgm.getGlobalForObjCEncodeExpr(e);
  assert(globalOp.getAlignment() && "expected alignment for @encode literal");
  unsigned align = *(globalOp.getAlignment());
  mlir::Value addr =
      builder.createGetGlobal(getLoc(e->getSourceRange()), globalOp);
  return makeAddrLValue(
      Address(addr, globalOp.getSymType(), CharUnits::fromQuantity(align)),
      e->getType(), AlignmentSource::Decl);
}

/// Casts are never lvalues unless that cast is to a reference type. If the cast
/// is to a reference, we can have the usual lvalue result, otherwise if a cast
/// is needed by the code generator in an lvalue context, then it must mean that
/// we need the address of an aggregate in order to access one of its members.
/// This can happen for all the reasons that casts are permitted with aggregate
/// result, including noop aggregate casts, and cast from scalar to union.
LValue CIRGenFunction::emitCastLValue(const CastExpr *e) {
  llvm::scope_exit restoreCurCast([this, prev = CurCast] { CurCast = prev; });
  CurCast = e;

  switch (e->getCastKind()) {
  case CK_ToVoid:
  case CK_BitCast:
  case CK_LValueToRValueBitCast:
  case CK_ArrayToPointerDecay:
  case CK_FunctionToPointerDecay:
  case CK_NullToMemberPointer:
  case CK_NullToPointer:
  case CK_IntegralToPointer:
  case CK_PointerToIntegral:
  case CK_PointerToBoolean:
  case CK_IntegralCast:
  case CK_BooleanToSignedIntegral:
  case CK_IntegralToBoolean:
  case CK_IntegralToFloating:
  case CK_FloatingToIntegral:
  case CK_FloatingToBoolean:
  case CK_FloatingCast:
  case CK_FloatingRealToComplex:
  case CK_FloatingComplexToReal:
  case CK_FloatingComplexToBoolean:
  case CK_FloatingComplexCast:
  case CK_FloatingComplexToIntegralComplex:
  case CK_IntegralRealToComplex:
  case CK_IntegralComplexToReal:
  case CK_IntegralComplexToBoolean:
  case CK_IntegralComplexCast:
  case CK_IntegralComplexToFloatingComplex:
  case CK_DerivedToBaseMemberPointer:
  case CK_BaseToDerivedMemberPointer:
  case CK_MemberPointerToBoolean:
  case CK_ReinterpretMemberPointer:
  case CK_AnyPointerToBlockPointerCast:
  case CK_ARCProduceObject:
  case CK_ARCConsumeObject:
  case CK_ARCReclaimReturnedObject:
  case CK_ARCExtendBlockObject:
  case CK_CopyAndAutoreleaseBlockObject:
  case CK_IntToOCLSampler:
  case CK_FloatingToFixedPoint:
  case CK_FixedPointToFloating:
  case CK_FixedPointCast:
  case CK_FixedPointToBoolean:
  case CK_FixedPointToIntegral:
  case CK_IntegralToFixedPoint:
  case CK_MatrixCast:
  case CK_HLSLVectorTruncation:
  case CK_HLSLMatrixTruncation:
  case CK_HLSLArrayRValue:
  case CK_HLSLElementwiseCast:
  case CK_HLSLAggregateSplatCast:
    llvm_unreachable("unexpected cast lvalue");

  case CK_Dependent:
    // A dependent cast can survive into CIRGen when a Sema substitution bug
    // leaves SubstNonTypeTemplateParmExpr with its un-substituted type.
    // Treat as transparent for lvalue emission — the CIR value carries the
    // correct concrete type even though the AST QualType may be dependent.
    // Note: OGCG uses llvm_unreachable for CK_Dependent; we handle it
    // because the Sema bug produces it in instantiated code.
    return emitLValue(e->getSubExpr());

  case CK_BuiltinFnToFnPtr:
    llvm_unreachable("builtin functions are handled elsewhere");

  case CK_Dynamic: {
    LValue lv = emitLValue(e->getSubExpr());
    Address v = lv.getAddress();
    const auto *dce = cast<CXXDynamicCastExpr>(e);
    return makeNaturalAlignAddrLValue(emitDynamicCast(v, dce), e->getType());
  }

  // These are never l-values; just use the aggregate emission code.
  case CK_NonAtomicToAtomic:
  case CK_AtomicToNonAtomic:
    return emitAggExprToLValue(e);

  case CK_ConstructorConversion:
  case CK_UserDefinedConversion:
  case CK_LValueToRValue:
    return emitLValue(e->getSubExpr());

  case CK_ToUnion:
  case CK_ObjCObjectLValueCast:
  case CK_VectorSplat:
  case CK_CPointerToObjCPointerCast:
  case CK_BlockPointerToObjCPointerCast:
    cgm.errorNYI(e->getSourceRange(),
                 "emitCastLValue for unhandled cast kind");
    return LValue();
  case CK_AddressSpaceConversion: {
    LValue lv = emitLValue(e->getSubExpr());
    QualType destTy = getContext().getPointerType(e->getType());

    clang::LangAS srcLangAS = e->getSubExpr()->getType().getAddressSpace();
    mlir::ptr::MemorySpaceAttrInterface srcAS;
    if (clang::isTargetAddressSpace(srcLangAS))
      srcAS = cir::toCIRTargetAddressSpace(getMLIRContext(), srcLangAS);
    else
      srcAS = cir::toCIRLangAddressSpaceAttr(&getMLIRContext(), srcLangAS);

    mlir::Value v = getTargetHooks().performAddrSpaceCast(
        *this, lv.getPointer(), srcAS, convertType(destTy));

    return makeAddrLValue(Address(v, convertTypeForMem(e->getType()),
                                  lv.getAddress().getAlignment()),
                          e->getType(), lv.getBaseInfo());
  }

  case CK_LValueBitCast: {
    // This must be a reinterpret_cast (or c-style equivalent).
    const auto *ce = cast<ExplicitCastExpr>(e);

    cgm.emitExplicitCastExprType(ce, this);
    LValue LV = emitLValue(e->getSubExpr());
    Address V = LV.getAddress().withElementType(
        builder, convertTypeForMem(ce->getTypeAsWritten()->getPointeeType()));

    if (sanOpts.has(SanitizerKind::CFIUnrelatedCast))
      emitVTablePtrCheckForCast(e->getType(), V, /*mayBeNull=*/false,
                                CFICastCheckKind::UnrelatedCast,
                                e->getBeginLoc());

    return makeAddrLValue(V, e->getType(), LV.getBaseInfo(),
                          cgm.mergeTBAAInfoForCast(
                              LV.getTBAAInfo(),
                              cgm.getTBAAAccessInfo(e->getType())));
  }

  case CK_NoOp: {
    // CK_NoOp can model a qualification conversion, which can remove an array
    // bound and change the IR type.
    LValue lv = emitLValue(e->getSubExpr());
    // Propagate the volatile qualifier to LValue, if exists in e.
    if (e->changesVolatileQualification())
      lv.getQuals() = e->getType().getQualifiers();
    if (lv.isSimple()) {
      Address v = lv.getAddress();
      if (v.isValid()) {
        mlir::Type ty = convertTypeForMem(e->getType());
        if (v.getElementType() != ty) {
          auto srcPtrTy =
              mlir::cast<cir::PointerType>(v.getPointer().getType());
          auto dstPtrTy = builder.getPointerTo(ty, srcPtrTy.getAddrSpace());
          lv = makeAddrLValue(
              Address(builder.createBitcast(v.getPointer(), dstPtrTy), ty,
                      v.getAlignment()),
              e->getType(), lv.getBaseInfo());
        }
      }
    }
    return lv;
  }

  case CK_UncheckedDerivedToBase:
  case CK_DerivedToBase: {
    auto *derivedClassDecl = e->getSubExpr()->getType()->castAsCXXRecordDecl();

    LValue lv = emitLValue(e->getSubExpr());
    Address thisAddr = lv.getAddress();

    // Perform the derived-to-base conversion
    Address baseAddr =
        getAddressOfBaseClass(thisAddr, derivedClassDecl, e->path(),
                              /*NullCheckValue=*/false, e->getExprLoc());

    // TODO: Support accesses to members of base classes in TBAA. For now, we
    // conservatively pretend that the complete object is of the base class
    // type.
    return makeAddrLValue(baseAddr, e->getType(), lv.getBaseInfo(),
                          cgm.getTBAAAccessInfo(e->getType()));
  }

  case CK_BaseToDerived: {
    const auto *derivedClassDecl = e->getType()->castAsCXXRecordDecl();
    LValue lv = emitLValue(e->getSubExpr());

    // Perform the base-to-derived conversion.
    // NullCheckValue is false for lvalue casts (reference types) because
    // the source is always an lvalue (never null). Matches OGCG
    // CGExpr.cpp:6132-6134 which hardcodes NullCheckValue=false here.
    // shouldNullCheckClassCastValue is only used in the scalar (pointer)
    // path in CIRGenExprScalar.cpp.
    Address derived = getAddressOfDerivedClass(
        getLoc(e->getSourceRange()), lv.getAddress(), derivedClassDecl,
        e->path(), /*NullCheckValue=*/false);
    // C++11 [expr.static.cast]p2: Behavior is undefined if a downcast is
    // performed and the object is not of the derived type.
    if (sanitizePerformTypeCheck())
      emitTypeCheck(TCK_DowncastReference, e->getExprLoc(),
                    derived.getPointer(), e->getType(),
                    derived.getAlignment());

    if (sanOpts.has(SanitizerKind::CFIDerivedCast))
      emitVTablePtrCheckForCast(e->getType(), derived, /*mayBeNull=*/false,
                                CFICastCheckKind::DerivedCast,
                                e->getBeginLoc());

    return makeAddrLValue(derived, e->getType(), lv.getBaseInfo(),
                          cgm.getTBAAAccessInfo(e->getType()));
  }

  case CK_ZeroToOCLOpaqueType:
    llvm_unreachable("NULL to OpenCL opaque type lvalue cast is not valid");
  }

  llvm_unreachable("Invalid cast kind");
}

static DeclRefExpr *tryToConvertMemberExprToDeclRefExpr(CIRGenFunction &cgf,
                                                        const MemberExpr *me) {
  if (auto *vd = dyn_cast<VarDecl>(me->getMemberDecl())) {
    // Try to emit static variable member expressions as DREs.
    return DeclRefExpr::Create(
        cgf.getContext(), NestedNameSpecifierLoc(), SourceLocation(), vd,
        /*RefersToEnclosingVariableOrCapture=*/false, me->getExprLoc(),
        me->getType(), me->getValueKind(), nullptr, nullptr, me->isNonOdrUse());
  }
  return nullptr;
}

LValue CIRGenFunction::emitMemberExpr(const MemberExpr *e) {
  if (DeclRefExpr *dre = tryToConvertMemberExprToDeclRefExpr(*this, e)) {
    emitIgnoredExpr(e->getBase());
    return emitDeclRefLValue(dre);
  }

  Expr *baseExpr = e->getBase();
  // If this is s.x, emit s as an lvalue.  If it is s->x, emit s as a scalar.
  LValue baseLV;
  if (e->isArrow()) {
    LValueBaseInfo baseInfo;
    TBAAAccessInfo tbaaInfo;
    // OGCG uses EmitCheckedLValue(BaseExpr, TCK_MemberAccess) here, which
    // emits null, alignment, and object-size type checks on the base pointer.
    Address addr = emitPointerWithAlignment(baseExpr, &baseInfo, &tbaaInfo);
    QualType ptrTy = baseExpr->getType()->getPointeeType();
    SanitizerSet skippedChecks;
    bool isBaseCXXThis = isWrappedCXXThis(baseExpr);
    if (isBaseCXXThis)
      skippedChecks.set(SanitizerKind::Alignment, true);
    if (isBaseCXXThis || isa<DeclRefExpr>(baseExpr))
      skippedChecks.set(SanitizerKind::Null, true);
    emitTypeCheck(TCK_MemberAccess, e->getExprLoc(), addr.getPointer(), ptrTy,
                  /*Alignment=*/CharUnits::Zero(), skippedChecks);
    baseLV = makeAddrLValue(addr, ptrTy, baseInfo, tbaaInfo);
  } else {
    baseLV = emitCheckedLValue(baseExpr, TCK_MemberAccess);
  }

  const NamedDecl *nd = e->getMemberDecl();
  if (auto *field = dyn_cast<FieldDecl>(nd)) {
    // OGCG computes IsInBounds to control whether GEPs are marked inbounds.
    // When the base pointer is constant null (offsetof idiom), inbounds must
    // not be set to avoid UB. CIR does not propagate this flag.
    assert(!cir::MissingFeatures::memberExprIsInBounds());
    LValue lv = emitLValueForField(baseLV, field, e->getMemberLoc());
    // OGCG calls setObjCGCLValueClass() here for ObjC GC (deprecated).
    if (getLangOpts().OpenMP &&
        ((isWrappedCXXThis(baseExpr) && isOpenMPNontemporalDecl(field)) ||
         baseLV.isNontemporal()))
      lv.setNontemporal(true);
    return lv;
  }

  if (const auto *fd = dyn_cast<FunctionDecl>(nd)) {
    cir::FuncOp callee = cgm.getAddrOfFunction(GlobalDecl(fd));
    QualType ety = e->getType();
    CharUnits alignment = getContext().getDeclAlign(fd);
    mlir::Location loc = getLoc(e->getSourceRange());
    auto funcPtrTy = builder.getPointerTo(callee.getFunctionType());
    mlir::Value addr = cir::GetGlobalOp::create(builder, loc, funcPtrTy,
                                                 callee.getSymName()).getAddr();
    return makeAddrLValue(
        Address(addr, convertTypeForMem(ety), alignment), ety,
        AlignmentSource::Decl);
  }

llvm_unreachable("Unhandled member declaration!");
}

LValue CIRGenFunction::emitCheckedLValue(const Expr *e,
                                         CIRGenFunction::TypeCheckKind tck) {
  LValue lv;
  if (sanOpts.has(SanitizerKind::ArrayBounds) && isa<ArraySubscriptExpr>(e))
    lv = emitArraySubscriptExpr(cast<ArraySubscriptExpr>(e), /*accessed=*/true);
  else
    lv = emitLValue(e);

  if (!isa<DeclRefExpr>(e) && !lv.isBitField() && lv.isSimple()) {
    SanitizerSet skippedChecks;
    if (const auto *me = dyn_cast<MemberExpr>(e)) {
      bool isBaseCXXThis = isWrappedCXXThis(me->getBase());
      if (isBaseCXXThis)
        skippedChecks.set(SanitizerKind::Alignment, true);
      if (isBaseCXXThis || isa<DeclRefExpr>(me->getBase()))
        skippedChecks.set(SanitizerKind::Null, true);
    }
    emitTypeCheck(tck, e->getExprLoc(), lv.getAddress().getPointer(),
                  e->getType(), lv.getAlignment(), skippedChecks);
  }

  return lv;
}

/// Evaluate an expression into a given memory location.
void CIRGenFunction::emitAnyExprToMem(const Expr *e, Address location,
                                      Qualifiers quals, bool isInit) {
  // FIXME: This function should take an LValue as an argument.
  QualType destType =
      quals.empty() ? e->getType() : getContext().getQualifiedType(e->getType(), quals);
  switch (getEvaluationKind(e->getType())) {
  case cir::TEK_Complex: {
    LValue lv = makeAddrLValue(location, destType);
    emitComplexExprIntoLValue(e, lv, isInit);
    return;
  }

  case cir::TEK_Aggregate: {
    emitAggExpr(e, AggValueSlot::forAddr(location, quals,
                                         AggValueSlot::IsDestructed_t(isInit),
                                         AggValueSlot::IsAliased_t(!isInit),
                                         AggValueSlot::MayOverlap));
    return;
  }

  case cir::TEK_Scalar: {
    LValue lv = makeAddrLValue(location, destType);
    if (PointerAuthQualifier ptrAuth =
            lv.getQuals().getPointerAuth().withoutKeyNone()) {
      mlir::Value value = emitPointerAuthQualify(ptrAuth, e, lv.getAddress());
      lv.getQuals().removePointerAuth();
      emitStoreThroughLValue(RValue::get(value), lv, isInit);
      return;
    }
    RValue rv = RValue::get(emitScalarExpr(e));
    emitStoreThroughLValue(rv, lv);
    return;
  }
  }

  llvm_unreachable("bad evaluation kind");
}

static Address createReferenceTemporary(CIRGenFunction &cgf,
                                        const MaterializeTemporaryExpr *m,
                                        const Expr *inner) {
  // TODO(cir): cgf.getTargetHooks();
  switch (m->getStorageDuration()) {
  case SD_FullExpression:
  case SD_Automatic: {
    QualType ty = inner->getType();

    // Match OGCG: under -fmerge-all-constants, try promoting constant
    // reference temporaries of aggregate type to private global constants.
    if (cgf.cgm.getCodeGenOpts().MergeAllConstants &&
        (ty->isArrayType() || ty->isRecordType()) &&
        ty.isConstantStorage(cgf.getContext(), /*ExcludeCtor=*/true,
                             /*ExcludeDtor=*/false)) {
      if (mlir::Attribute initAttr =
              ConstantEmitter(cgf.cgm).tryEmitAbstract(inner, ty)) {
        auto typedInit = mlir::cast<mlir::TypedAttr>(initAttr);
        auto loc = cgf.getLoc(m->getSourceRange());
        // Match OGCG behavior for merged constants: use a module-unique
        // compiler-generated base name for reference temporary globals.
        // Per-function counters can collide at module scope and trigger symbol
        // redefinition verification failures.
        std::string uniqueName =
            cgf.cgm.getUniqueGlobalName(cgf.getCounterRefTmpAsString());
        cir::GlobalOp gv = CIRGenModule::createGlobalOp(
            cgf.cgm, loc, uniqueName, typedInit.getType(),
            /*isConstant=*/true);
        gv.setLinkageAttr(cir::GlobalLinkageKindAttr::get(
            &cgf.cgm.getMLIRContext(), cir::GlobalLinkageKind::PrivateLinkage));
        gv.setInitialValueAttr(typedInit);
        CharUnits alignment = cgf.getContext().getTypeAlignInChars(ty);
        gv.setAlignment(alignment.getQuantity());

        mlir::Value addr = cgf.getBuilder().createGetGlobal(gv);
        Address result(addr, gv.getSymType(), alignment);
        mlir::Type expectedTy = cgf.convertTypeForMem(ty);
        if (gv.getSymType() != expectedTy)
          result = result.withElementType(cgf.getBuilder(), expectedTy);
        return result;
      }
    }

    // The temporary memory should be created in the same scope as the extending
    // declaration of the temporary materialization expression.
    cir::AllocaOp extDeclAlloca;
    if (const ValueDecl *extDecl = m->getExtendingDecl()) {
      auto extDeclAddrIter = cgf.localDeclMap.find(extDecl);
      if (extDeclAddrIter != cgf.localDeclMap.end()) {
        extDeclAlloca = extDeclAddrIter->second.getDefiningOp<cir::AllocaOp>();
        // Look through address space cast (e.g. for CUDA/HIP/OpenCL).
        if (!extDeclAlloca) {
          if (auto castOp =
                  extDeclAddrIter->second.getDefiningOp<cir::CastOp>()) {
            if (castOp.getKind() == cir::CastKind::address_space)
              extDeclAlloca = castOp.getSrc().getDefiningOp<cir::AllocaOp>();
          }
        }
      }
    }
    mlir::OpBuilder::InsertPoint ip;
    if (extDeclAlloca)
      ip = {extDeclAlloca->getBlock(), extDeclAlloca->getIterator()};
    else if (cgf.isInConditionalBranch()) {
      // When inside a conditional branch (e.g., ternary expression), the
      // temporary must be allocated in a dominating scope. If cleanups are
      // deferred to the outer scope, cleanup code outside the
      // branch needs to reference this alloca. Use the function entry block
      // so the alloca dominates both the construction site (inside the
      // branch) and the destruction site (after the full expression).
      mlir::Block *entryBlock = cgf.getCurFunctionEntryBlock();
      ip = cgf.getBuilder().getBestAllocaInsertPoint(entryBlock);
    }
    return cgf.createMemTemp(ty, cgf.getLoc(m->getSourceRange()),
                             cgf.getCounterRefTmpAsString(), /*alloca=*/nullptr,
                             ip);
  }
  case SD_Thread:
  case SD_Static: {
    auto gv =
        mlir::cast<cir::GlobalOp>(cgf.cgm.getAddrOfGlobalTemporary(m, inner));
    mlir::Value addr = cgf.getBuilder().createGetGlobal(gv);
    assert(gv.getAlignment().has_value() &&
           "This should always have an alignment");
    CharUnits alignment = CharUnits::fromQuantity(gv.getAlignment().value());

    // The global's type may differ from the expected type when a constant
    // initializer was emitted (its type comes from the constant, not the AST).
    // Match OGCG's withElementType pattern: construct with the global's actual
    // type, then cast to the expected type.
    mlir::Type expectedTy = cgf.convertTypeForMem(inner->getType());
    Address result(addr, gv.getSymType(), alignment);
    if (gv.getSymType() != expectedTy)
      result = result.withElementType(cgf.getBuilder(), expectedTy);
    return result;
  }

  case SD_Dynamic:
    llvm_unreachable("temporary can't have dynamic storage duration");
  }
  llvm_unreachable("unknown storage duration");
}

static void pushTemporaryCleanup(CIRGenFunction &cgf,
                                 const MaterializeTemporaryExpr *m,
                                 const Expr *e, Address referenceTemporary) {
  // Objective-C++ ARC:
  //   If we are binding a reference to a temporary that has ownership, we
  //   need to perform retain/release operations on the temporary.
  //
  // FIXME(ogcg): This should be looking at e, not m.
  auto lifetime = m->getType().getObjCLifetime();
  if (lifetime != Qualifiers::OCL_None) {
    switch (lifetime) {
    case Qualifiers::OCL_None:
    case Qualifiers::OCL_ExplicitNone:
      break;

    case Qualifiers::OCL_Autoreleasing:
      // Nothing to do; cleaned up by an autorelease pool.
      return;

    case Qualifiers::OCL_Strong:
    case Qualifiers::OCL_Weak:
      switch (StorageDuration duration = m->getStorageDuration()) {
      case SD_Static:
        // Match OGCG: do not register program-termination cleanup here.
        return;

      case SD_Thread:
        // FIXME(ogcg): thread-lifetime ARC temps still skip cleanup here.
        return;

      case SD_Automatic:
      case SD_FullExpression: {
        CIRGenFunction::Destroyer *destroyer;
        CleanupKind cleanupKind;
        if (lifetime == Qualifiers::OCL_Strong) {
          const ValueDecl *vd = m->getExtendingDecl();
          bool precise = isa_and_nonnull<VarDecl>(vd) &&
                         vd->hasAttr<ObjCPreciseLifetimeAttr>();
          cleanupKind =
              cgf.getCleanupKind(QualType::DK_objc_strong_lifetime);
          destroyer = precise ? &CIRGenFunction::destroyARCStrongPrecise
                              : &CIRGenFunction::destroyARCStrongImprecise;
        } else {
          // __weak objects always get EH cleanups; otherwise exceptions can
          // turn leaks into use-after-free style failures.
          cleanupKind = NormalAndEHCleanup;
          destroyer = &CIRGenFunction::destroyARCWeak;
        }
        if (duration == SD_FullExpression)
          cgf.pushDestroy(cleanupKind, referenceTemporary, m->getType(),
                          destroyer, cleanupKind & EHCleanup);
        else
          cgf.pushLifetimeExtendedDestroy(cleanupKind, referenceTemporary,
                                         m->getType(), destroyer,
                                         cleanupKind & EHCleanup);
        return;
      }

      case SD_Dynamic:
        llvm_unreachable("temporary cannot have dynamic storage duration");
      }
      llvm_unreachable("unknown storage duration");
    }
  }

  const QualType::DestructionKind dk = e->getType().isDestructedType();
  if (dk == QualType::DK_none)
    return;

  switch (m->getStorageDuration()) {
  case SD_Static:
  case SD_Thread: {
    CXXDestructorDecl *referenceTemporaryDtor = nullptr;
    if (const auto *classDecl =
            e->getType()->getBaseElementTypeUnsafe()->getAsCXXRecordDecl();
        classDecl && !classDecl->hasTrivialDestructor())
      // Get the destructor for the reference temporary.
      referenceTemporaryDtor = classDecl->getDestructor();

    if (!referenceTemporaryDtor)
      return;

    if (e->getType()->isArrayType()) {
      // Match OGCG: synthesize the array destroy helper immediately and
      // register it from the enclosing init path instead of deferring the
      // cleanup through the backing temporary GlobalOp.
      const auto *vd = dyn_cast_or_null<VarDecl>(m->getExtendingDecl());
      cir::FuncOp helperFn = cgf.generateDestroyHelper(
          referenceTemporary, e->getType(), CIRGenFunction::destroyCXXObject,
          cgf.getLangOpts().Exceptions, vd);
      assert(helperFn && "expected destroy helper for reference temporary");
      bool isTLS = m->getStorageDuration() == SD_Thread;
      cgf.emitCXXAtExit(cgf.getLoc(e->getSourceRange()), helperFn,
                        mlir::Value{}, isTLS);
      break;
    }

    // Get the destructor function for the reference temporary.
    auto gd = GlobalDecl(referenceTemporaryDtor, Dtor_Complete);
    cir::FuncOp fnOp = cgf.cgm.getAddrAndTypeOfCXXStructor(gd).second;
    assert(fnOp && "expected cir.func for destructor");

    // Emit __cxa_atexit (or __cxa_thread_atexit) directly at the current
    // insertion point. This matches OGCG behavior: for static local
    // reference temporaries the registration is inside the guard block
    // (so it only runs when the temporary is actually constructed), and
    // for global reference temporaries it's in the global init function.
    bool isTLS = m->getStorageDuration() == SD_Thread;
    cgf.emitCXXAtExit(cgf.getLoc(e->getSourceRange()), fnOp,
                      referenceTemporary.getPointer(), isTLS);
    break;
  }

  case SD_FullExpression:
    cgf.pushDestroy(NormalAndEHCleanup, referenceTemporary, e->getType(),
                    CIRGenFunction::destroyCXXObject,
                    /*useEHCleanupForArray=*/true);
    // A std::initializer_list backing array constructed inside a shared
    // cir.cleanup.scope is destroyed by the scope's ONE shared landing pad,
    // which is materialized at end-of-scope (when every such array is already
    // complete). Give this temp cleanup an active flag (false during its
    // construction, set true here once complete) so the shared dispatch skips it
    // when unwinding mid-construction or before this array was reached —
    // otherwise it would destroy unconstructed elements. On the normal path the
    // flag is true, so end-of-full-expression destruction is unaffected.
    //
    // Skip this when the cleanup already has a flag: pushing inside a
    // conditionally-evaluated expression (a ternary arm, a short-circuit RHS)
    // makes pushFullExprCleanup create one, with exactly the semantics wanted
    // here -- false from the outermost conditional's entry, which precedes the
    // array's construction, and true at this push point. Creating a second
    // would assert.
    if (cgf.emittingInitializerListArray) {
      auto &cleanup = cast<EHCleanupScope>(*cgf.ehStack.begin());
      if (!cleanup.hasActiveFlag())
        cgf.initFullExprCleanupWithFlag(cgf.createCleanupActiveFlag());
    }
    break;

  case SD_Automatic:
    cgf.pushLifetimeExtendedDestroy(dk, referenceTemporary, e->getType());
    break;

  case SD_Dynamic:
    llvm_unreachable("temporary cannot have dynamic storage duration");
  }
}

LValue CIRGenFunction::emitMaterializeTemporaryExpr(
    const MaterializeTemporaryExpr *m) {
  const Expr *e = m->getSubExpr();

  assert((!m->getExtendingDecl() || !isa<VarDecl>(m->getExtendingDecl()) ||
          !cast<VarDecl>(m->getExtendingDecl())->isARCPseudoStrong()) &&
         "Reference should never be pseudo-strong!");

  // FIXME: ideally this would use emitAnyExprToMem, however, we cannot do so
  // as that will cause the lifetime adjustment to be lost for ARC
  auto ownership = m->getType().getObjCLifetime();
  if (ownership != Qualifiers::OCL_None &&
      ownership != Qualifiers::OCL_ExplicitNone) {
    Address object = createReferenceTemporary(*this, m, e);

    // createReferenceTemporary may promote ARC-managed constant temps to a
    // global; in that case they are immune to ref-counting traffic and need no
    // dynamic initialization or cleanup.
    mlir::Value objectPtr = object.getPointer();
    if (!objectPtr.getDefiningOp<cir::GetGlobalOp>()) {
      if (auto castOp = objectPtr.getDefiningOp<cir::CastOp>()) {
        if (castOp.getKind() == cir::CastKind::address_space)
          objectPtr = castOp.getSrc();
      }
    }
    if (auto getGlobal = objectPtr.getDefiningOp<cir::GetGlobalOp>()) {
      auto *sym = cgm.getGlobalValue(getGlobal.getName());
      auto globalOp = cast<cir::GlobalOp>(sym);
      if (globalOp.getInitialValue().has_value())
        return makeAddrLValue(object, m->getType(), AlignmentSource::Decl);

      globalOp.setInitialValueAttr(cgm.emitNullConstantAttr(e->getType()));
    }

    LValue refTempDst =
        makeAddrLValue(object, m->getType(), AlignmentSource::Decl);

    switch (getEvaluationKind(e->getType())) {
    case cir::TEK_Scalar:
      emitScalarInit(e, getLoc(e->getSourceRange()), refTempDst,
                     /*capturedByInit=*/false, m->getExtendingDecl());
      break;
    case cir::TEK_Aggregate:
      emitAggExpr(e, AggValueSlot::forAddr(object, e->getType().getQualifiers(),
                                           AggValueSlot::IsDestructed,
                                           AggValueSlot::IsNotAliased,
                                           AggValueSlot::DoesNotOverlap));
      break;
    case cir::TEK_Complex:
      llvm_unreachable("expected scalar or aggregate expression");
    }

    pushTemporaryCleanup(*this, m, e, object);
    return refTempDst;
  }

  SmallVector<const Expr *, 2> commaLHSs;
  SmallVector<SubobjectAdjustment, 2> adjustments;
  e = e->skipRValueSubobjectAdjustments(commaLHSs, adjustments);

  for (const Expr *ignored : commaLHSs)
    emitIgnoredExpr(ignored);

  if (const auto *opaque = dyn_cast<OpaqueValueExpr>(e)) {
    if (opaque->getType()->isRecordType()) {
      assert(adjustments.empty());
      return emitOpaqueValueLValue(opaque);
    }
  }

  // Create and initialize the reference temporary.
  Address object = createReferenceTemporary(*this, m, e);

  // Look through address-space casts to find the underlying GetGlobalOp,
  // matching OGCG's use of stripPointerCasts().
  mlir::Value objectPtr = object.getPointer();
  if (!objectPtr.getDefiningOp<cir::GetGlobalOp>()) {
    if (auto castOp = objectPtr.getDefiningOp<cir::CastOp>()) {
      if (castOp.getKind() == cir::CastKind::address_space)
        objectPtr = castOp.getSrc();
    }
  }
  if (auto getGlobal = objectPtr.getDefiningOp<cir::GetGlobalOp>()) {
    // Look up the underlying GlobalOp to check if it already has an
    // initializer (set by getAddrOfGlobalTemporary for constant temps).
    auto *sym = cgm.getGlobalValue(getGlobal.getName());
    auto globalOp = cast<cir::GlobalOp>(sym);
    if (!globalOp.getInitialValue().has_value()) {
      // Non-constant temporary: set a zero initializer on the global and
      // emit runtime initialization code.
          globalOp.setInitialValueAttr(cgm.emitNullConstantAttr(e->getType()));
      emitAnyExprToMem(e, object, m->getType().getQualifiers(),
                       /*isInitializer=*/true);
    }
    // If the global already has a constant initializer, skip — it was
    // fully initialized by getAddrOfGlobalTemporary.
  } else {
    switch (m->getStorageDuration()) {
    case SD_Automatic:
      // Emit lifetime markers around the temporary.
      if (shouldEmitLifetimeMarkers) {
        builder.createLifetimeStart(getLoc(m->getSourceRange()),
                                    object.getPointer());
        pushCleanupAfterFullExpr<CallLifetimeEnd>(NormalEHLifetimeMarker,
                                                  object);
      }
      break;

    case SD_FullExpression:
      if (shouldEmitLifetimeMarkers) {
        builder.createLifetimeStart(getLoc(m->getSourceRange()),
                                    object.getPointer());
        pushFullExprCleanup<CallLifetimeEnd>(NormalEHLifetimeMarker, object);
      }
      break;

    default:
      break;
    }
    emitAnyExprToMem(e, object, m->getType().getQualifiers(),
                     /*isInitializer=*/true);
  }
  pushTemporaryCleanup(*this, m, e, object);

  // Perform derived-to-base casts and/or field accesses, to get from the
  // temporary object we created (and, potentially, for which we extended
  // the lifetime) to the subobject we're binding the reference to.
  for (SubobjectAdjustment &adjustment : llvm::reverse(adjustments)) {
    switch (adjustment.Kind) {
    case SubobjectAdjustment::DerivedToBaseAdjustment:
      object = getAddressOfBaseClass(
          object, adjustment.DerivedToBase.DerivedClass,
          adjustment.DerivedToBase.BasePath->path(),
          /*nullCheckValue=*/false, e->getExprLoc());
      break;

    case SubobjectAdjustment::FieldAdjustment: {
      LValue lv = makeAddrLValue(object, e->getType(), AlignmentSource::Decl);
      lv = emitLValueForField(lv, adjustment.Field);
      assert(lv.isSimple() &&
             "materialized temporary field is not a simple lvalue");
      object = lv.getAddress();
      break;
    }

    case SubobjectAdjustment::MemberPointerAdjustment: {
      mlir::Value ptr = emitScalarExpr(adjustment.Ptr.RHS);
      object = emitCXXMemberDataPointerAddress(
          e, object, ptr, adjustment.Ptr.MPT, /*baseInfo=*/nullptr);
      break;
    }
    }
  }

  return makeAddrLValue(object, m->getType(), AlignmentSource::Decl);
}

LValue
CIRGenFunction::getOrCreateOpaqueLValueMapping(const OpaqueValueExpr *e) {
  assert(OpaqueValueMapping::shouldBindAsLValue(e));

  auto it = opaqueLValues.find(e);
  if (it != opaqueLValues.end())
    return it->second;

  assert(e->isUnique() && "LValue for a nonunique OVE hasn't been emitted");
  return emitLValue(e->getSourceExpr());
}

RValue
CIRGenFunction::getOrCreateOpaqueRValueMapping(const OpaqueValueExpr *e) {
  assert(!OpaqueValueMapping::shouldBindAsLValue(e));

  auto it = opaqueRValues.find(e);
  if (it != opaqueRValues.end())
    return it->second;

  assert(e->isUnique() && "RValue for a nonunique OVE hasn't been emitted");
  return emitAnyExpr(e->getSourceExpr());
}

LValue CIRGenFunction::emitCompoundLiteralLValue(const CompoundLiteralExpr *e) {
  if (e->isFileScope()) {
    CharUnits align = cgm.getASTContext().getTypeAlignInChars(e->getType());

    // Check if we've already emitted a global for this compound literal.
    auto it = cgm.emittedCompoundLiterals.find(e);
    if (it != cgm.emittedCompoundLiterals.end()) {
      cir::GlobalOp gv = it->second;
      mlir::Value addr =
          builder.createGetGlobal(getLoc(e->getSourceRange()), gv);
      return makeAddrLValue(Address(addr, gv.getSymType(), align), e->getType(),
                            AlignmentSource::Decl);
    }

    ConstantEmitter emitter(cgm);
    LangAS addressSpace = e->getType().getAddressSpace();
    mlir::Attribute c = emitter.tryEmitForInitializer(e->getInitializer(),
                                                      addressSpace,
                                                      e->getType());
    assert(c &&
           "file-scope compound literal did not have constant initializer!");

    // Convert the type's address space to a CIR target address space attribute.
    unsigned targetAS =
        cgm.getASTContext().getTargetAddressSpace(addressSpace);
    mlir::ptr::MemorySpaceAttrInterface addrSpaceAttr;
    if (targetAS)
      addrSpaceAttr = cir::TargetAddressSpaceAttr::get(
          &cgm.getMLIRContext(), targetAS);

    auto gv = CIRGenModule::createGlobalOp(
        cgm, cgm.getLoc(e->getSourceRange()),
        cgm.createGlobalCompoundLiteralName(),
        cgm.getTypes().convertTypeForMem(e->getType()),
        e->getType().isConstantStorage(cgm.getASTContext(), true, false),
        addrSpaceAttr);
    gv.setInitialValueAttr(c);
    gv.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
    gv.setAlignment(align.getAsAlign().value());
    emitter.finalize(gv);

    // Cache the global for future references to this compound literal.
    cgm.emittedCompoundLiterals[e] = gv;

    mlir::Value addr =
        builder.createGetGlobal(getLoc(e->getSourceRange()), gv);
    return makeAddrLValue(
        Address(addr, gv.getSymType(), align),
        e->getType(), AlignmentSource::Decl);
  }

  if (e->getType()->isVariablyModifiedType())
    emitVariablyModifiedType(e->getType());

  Address declPtr = createMemTemp(e->getType(), getLoc(e->getSourceRange()),
                                  ".compoundliteral");
  const Expr *initExpr = e->getInitializer();
  LValue result = makeAddrLValue(declPtr, e->getType(), AlignmentSource::Decl);

  emitAnyExprToMem(initExpr, declPtr, e->getType().getQualifiers(),
                   /*Init*/ true);

  // Block-scope compound literals are destroyed at the end of the enclosing
  // scope in C.
  if (!getLangOpts().CPlusPlus && e->getType().isDestructedType()) {
    cgm.errorNYI(e->getSourceRange(),
                 "emitCompoundLiteralLValue: non C++ DestructedType");
  }

  return result;
}

LValue CIRGenFunction::emitCallExprLValue(const CallExpr *e) {
  RValue rv = emitCallExpr(e);

  if (!rv.isScalar())
    return makeAddrLValue(rv.getAggregateAddress(), e->getType(),
                          AlignmentSource::Decl);

  assert(e->getCallReturnType(getContext())->isReferenceType() &&
         "Can't have a scalar return unless the return type is a "
         "reference type!");

  return makeNaturalAlignPointeeAddrLValue(rv.getValue(), e->getType());
}

LValue CIRGenFunction::emitObjCMessageExprLValue(const ObjCMessageExpr *e) {
  RValue rv = emitObjCMessageExpr(e);

  if (!rv.isScalar())
    return makeAddrLValue(rv.getAggregateAddress(), e->getType(),
                          AlignmentSource::Decl);

  assert(e->getMethodDecl()->getReturnType()->isReferenceType() &&
         "Can't have a scalar return unless the return type is a "
         "reference type!");

  return makeNaturalAlignPointeeAddrLValue(rv.getValue(), e->getType());
}

LValue CIRGenFunction::emitBinaryOperatorLValue(const BinaryOperator *e) {
  // Comma expressions just emit their LHS then their RHS as an l-value.
  if (e->getOpcode() == BO_Comma) {
    emitIgnoredExpr(e->getLHS());
    return emitLValue(e->getRHS());
  }

  if (e->getOpcode() == BO_PtrMemD || e->getOpcode() == BO_PtrMemI)
    return emitPointerToDataMemberBinaryExpr(e);

  assert(e->getOpcode() == BO_Assign && "unexpected binary l-value");

  // Note that in all of these cases, __block variables need the RHS
  // evaluated first just in case the variable gets moved by the RHS.

  switch (CIRGenFunction::getEvaluationKind(e->getType())) {
  case cir::TEK_Scalar: {
    ApplySourceAtomGroup atomGroup(*this);
    if (PointerAuthQualifier ptrAuth =
            e->getLHS()->getType().getPointerAuth().withoutKeyNone()) {
      LValue lv = emitCheckedLValue(e->getLHS(), TCK_Store);
      LValue copiedLV = lv;
      copiedLV.getQuals().removePointerAuth();
      mlir::Value rv =
          emitPointerAuthQualify(ptrAuth, e->getRHS(), copiedLV.getAddress());
      emitNullabilityCheck(copiedLV, rv, e->getExprLoc());
      SourceLocRAIIObject loc{*this, getLoc(e->getExprLoc())};
      emitStoreThroughLValue(RValue::get(rv), copiedLV);
      return lv;
    }

    if (Qualifiers::ObjCLifetime lifetime =
            e->getLHS()->getType().getObjCLifetime()) {
      if (lifetime == Qualifiers::OCL_Strong) {
        mlir::Value rv = emitARCRetainScalarExpr(e->getRHS());
        LValue lv = emitCheckedLValue(e->getLHS(), TCK_Store);
        SourceLocRAIIObject loc{*this, getLoc(e->getExprLoc())};
        mlir::Value oldValue = emitLoadOfScalar(lv, SourceLocation());
        emitStoreThroughLValue(RValue::get(rv), lv);
        emitARCRelease(oldValue, getLoc(e->getExprLoc()));
        return lv;
      }

      if (lifetime == Qualifiers::OCL_Autoreleasing) {
        mlir::Value rv = emitARCRetainAutoreleaseScalarExpr(e->getRHS());
        LValue lv = emitCheckedLValue(e->getLHS(), TCK_Store);
        SourceLocRAIIObject loc{*this, getLoc(e->getExprLoc())};
        emitStoreThroughLValue(RValue::get(rv), lv);
        return lv;
      }

      if (lifetime == Qualifiers::OCL_Weak) {
        mlir::Value rv = emitScalarExpr(e->getRHS());
        LValue lv = emitCheckedLValue(e->getLHS(), TCK_Store);
        SourceLocRAIIObject loc{*this, getLoc(e->getExprLoc())};
        emitARCStoreWeak(lv.getAddress(), rv, /*ignored=*/false);
        return lv;
      }

      // __unsafe_unretained (OCL_ExplicitNone) is an ordinary store with no ARC
      // bookkeeping; fall through to the normal store path below.
      assert(lifetime == Qualifiers::OCL_ExplicitNone &&
             "unexpected ObjC lifetime for scalar assignment");
    }

    RValue rv = emitAnyExpr(e->getRHS());
    LValue lv = emitCheckedLValue(e->getLHS(), TCK_Store);
    if (rv.isScalar())
      emitNullabilityCheck(lv, rv.getValue(), e->getExprLoc());

    SourceLocRAIIObject loc{*this, getLoc(e->getExprLoc())};
    if (lv.isBitField()) {
      mlir::Value srcVal = rv.getValue();
      mlir::Value result = emitStoreThroughBitfieldLValue(rv, lv);
      emitBitfieldConversionCheck(*this, builder, getLoc(e->getExprLoc()),
                                  e->getExprLoc(), srcVal,
                                  e->getRHS()->getType(), result,
                                  e->getLHS()->getType(),
                                  lv.getBitFieldInfo());
    } else
      emitStoreThroughLValue(rv, lv);

    if (getLangOpts().OpenMP)
      checkAndEmitOpenMPLastprivateConditional(e->getLHS());

    // TODO: bitfield conversion check needed here for C++ assignments
    // (C++ assignments are lvalues, so they go through emitBinaryOperatorLValue
    // instead of VisitBinAssign). Need to wire emitBitfieldConversionCheck
    // as a CIRGenFunction method accessible from both paths.

    return lv;
  }

  case cir::TEK_Complex: {
    return emitComplexAssignmentLValue(e);
  }

  case cir::TEK_Aggregate:
    return emitAggExprToLValue(e);
  }
  llvm_unreachable("bad evaluation kind");
}

/// Emit code to compute the specified expression which
/// can have any type.  The result is returned as an RValue struct.
RValue CIRGenFunction::emitAnyExpr(const Expr *e, AggValueSlot aggSlot,
                                   bool ignoreResult) {
  switch (CIRGenFunction::getEvaluationKind(e->getType())) {
  case cir::TEK_Scalar:
    return RValue::get(emitScalarExpr(e, ignoreResult));
  case cir::TEK_Complex:
    return RValue::getComplex(emitComplexExpr(e));
  case cir::TEK_Aggregate: {
    if (!ignoreResult && aggSlot.isIgnored())
      aggSlot = createAggTemp(e->getType(), getLoc(e->getSourceRange()),
                              getCounterAggTmpAsString());
    emitAggExpr(e, aggSlot);
    return aggSlot.asRValue();
  }
  }
  llvm_unreachable("bad evaluation kind");
}

// Detect the unusual situation where an inline version is shadowed by a
// non-inline version. In that case we should pick the external one
// everywhere. That's GCC behavior too.
static bool onlyHasInlineBuiltinDeclaration(const FunctionDecl *fd) {
  for (const FunctionDecl *pd = fd; pd; pd = pd->getPreviousDecl())
    if (!pd->isInlineBuiltinDeclaration())
      return false;
  return true;
}

CIRGenCallee CIRGenFunction::emitDirectCallee(const GlobalDecl &gd) {
  const auto *fd = cast<FunctionDecl>(gd.getDecl());

  if (unsigned builtinID = fd->getBuiltinID()) {
    StringRef ident = cgm.getMangledName(gd);
    std::string fdInlineName = (ident + ".inline").str();

    bool isPredefinedLibFunction =
        cgm.getASTContext().BuiltinInfo.isPredefinedLibFunction(builtinID);
    // Check if the current function has __attribute__((no_builtin)) or
    // __attribute__((no_builtin("name"))) for this specific callee, or if
    // -fno-builtin-<name> was passed on the command line.
    bool hasAttributeNoBuiltin = false;
    if (const auto *callerFD =
            dyn_cast_or_null<FunctionDecl>(curFuncDecl)) {
      if (const auto *nba = callerFD->getAttr<NoBuiltinAttr>()) {
        if (llvm::is_contained(nba->builtinNames(), "*")) {
          // __attribute__((no_builtin)) with no args stores "*" as a wildcard
          // and disables all builtins.
          hasAttributeNoBuiltin = true;
        } else {
          // __attribute__((no_builtin("name"))) disables a specific builtin.
          hasAttributeNoBuiltin =
              llvm::is_contained(nba->builtinNames(), fd->getName());
        }
      }
    }
    if (!hasAttributeNoBuiltin) {
      hasAttributeNoBuiltin =
          llvm::is_contained(getLangOpts().NoBuiltinFuncs, fd->getName());
    }

    // When directing calling an inline builtin, call it through it's mangled
    // name to make it clear it's not the actual builtin.
    auto fn = dyn_cast<cir::FuncOp>(curFn);
    if (fn && fn.getName() != fdInlineName &&
        onlyHasInlineBuiltinDeclaration(fd)) {
      cir::FuncOp clone =
          mlir::cast_or_null<cir::FuncOp>(cgm.getGlobalValue(fdInlineName));

      if (!clone) {
        // Create a forward declaration - the body will be generated in
        // generateCode when the function definition is processed
        cir::FuncOp calleeFunc = emitFunctionDeclPointer(cgm, gd);
        mlir::OpBuilder::InsertionGuard guard(builder);
        builder.setInsertionPointToStart(cgm.getModule().getBody());

        clone = cir::FuncOp::create(builder, calleeFunc.getLoc(), fdInlineName,
                                    calleeFunc.getFunctionType());
        cgm.insertGlobalSymbol(clone);
        clone.setLinkageAttr(cir::GlobalLinkageKindAttr::get(
            &cgm.getMLIRContext(), cir::GlobalLinkageKind::InternalLinkage));
        clone.setSymVisibility("private");
        clone.setInlineKind(cir::InlineKind::AlwaysInline);
      }
      return CIRGenCallee::forDirect(clone, gd);
    }

    // Replaceable builtins provide their own implementation of a builtin. If we
    // are in an inline builtin implementation, avoid trivial infinite
    // recursion. Honor __attribute__((no_builtin("foo"))) or
    // __attribute__((no_builtin)) on the current function unless foo is
    // not a predefined library function which means we must generate the
    // builtin no matter what.
    else if (!isPredefinedLibFunction || !hasAttributeNoBuiltin)
      return CIRGenCallee::forBuiltin(builtinID, fd);
  }

  cir::FuncOp callee = emitFunctionDeclPointer(cgm, gd);

  return CIRGenCallee::forDirect(callee, gd);
}

RValue CIRGenFunction::getUndefRValue(QualType ty) {
  if (ty->isVoidType())
    return RValue::get(nullptr);

  // Use the current location or create an unknown location.
  mlir::Location loc =
      currSrcLoc ? *currSrcLoc : mlir::UnknownLoc::get(&getMLIRContext());

  switch (getEvaluationKind(ty)) {
  case cir::TEK_Complex: {
    mlir::Type complexTy = convertType(ty);
    return RValue::getComplex(
        builder.getConstant(loc, cir::UndefAttr::get(complexTy)));
  }

  // If this is a use of an undefined aggregate type, the aggregate must have
  // an identifiable address. Just because the contents of the value are
  // undefined doesn't mean that the address can't be taken and compared.
  case cir::TEK_Aggregate: {
    Address destPtr = createMemTemp(ty, loc, "undef.agg.tmp");
    return RValue::getAggregate(destPtr);
  }

  case cir::TEK_Scalar:
    return RValue::get(
        builder.getConstant(loc, cir::UndefAttr::get(convertType(ty))));
  }
  llvm_unreachable("bad evaluation kind");
}

RValue CIRGenFunction::emitCall(clang::QualType calleeTy,
                                const CIRGenCallee &origCallee,
                                const clang::CallExpr *e,
                                ReturnValueSlot returnValue,
                                cir::CIRCallOpInterface *callOrInvoke,
                                mlir::Value staticChain) {
  // Get the actual function type. The callee type will always be a pointer to
  // function type or a block pointer type.
  assert(calleeTy->isFunctionPointerType() &&
         "Callee must have function pointer type!");

  const Decl *targetDecl =
      origCallee.getAbstractInfo().getCalleeDecl().getDecl();

  calleeTy = getContext().getCanonicalType(calleeTy);
  auto pointeeTy = cast<PointerType>(calleeTy)->getPointeeType();

  CIRGenCallee callee = origCallee;

  bool cfiUnchecked = calleeTy->hasPointeeToCFIUncheckedCalleeFunctionType();
  if (sanOpts.has(SanitizerKind::Function) &&
      (!targetDecl || !isa<FunctionDecl>(targetDecl)) &&
      !isa<FunctionNoProtoType>(pointeeTy) && !cfiUnchecked) {
    if (auto prefixSig = cgm.getTargetCIRGenInfo().getUBSanFunctionSignature()) {
      mlir::Location loc = getLoc(e->getExprLoc());
      auto prefixTy =
          builder.getAnonRecordTy({builder.getUInt32Ty(), builder.getUInt32Ty()},
                                  /*packed=*/true);
      auto prefixPtrTy = builder.getPointerTo(prefixTy);
      auto intPtrTy = convertType(getContext().getUIntPtrType());

      mlir::Operation *calleePtrOp = callee.getFunctionPointer();
      mlir::Value calleePtr;
      if (auto fn = mlir::dyn_cast<cir::FuncOp>(calleePtrOp)) {
        calleePtr = cir::GetGlobalOp::create(builder, loc,
                                             builder.getPointerTo(
                                                 fn.getFunctionType()),
                                             fn.getSymName());
      } else {
        assert(calleePtrOp->getNumResults() == 1 &&
               "expected single-result indirect callee op");
        calleePtr = calleePtrOp->getResult(0);
      }

      mlir::Value alignedCalleePtr = calleePtr;
      if (cgm.getTriple().isARM() || cgm.getTriple().isThumb()) {
        mlir::Value calleeAddress =
            builder.createPtrToInt(calleePtr, intPtrTy);
        markNoSanitize(calleeAddress.getDefiningOp());
        mlir::Value mask = builder.getUInt64(~uint64_t{1}, loc);
        if (auto intTy = mlir::dyn_cast<cir::IntType>(intPtrTy);
            intTy && intTy.getWidth() == 32)
          mask = builder.getUInt32(~uint32_t{1}, loc);
        mlir::Value alignedAddress =
            builder.createAnd(loc, calleeAddress, mask);
        markNoSanitize(alignedAddress.getDefiningOp());
        alignedCalleePtr = builder.createIntToPtr(alignedAddress,
                                                  calleePtr.getType());
        markNoSanitize(alignedCalleePtr.getDefiningOp());
      }

      mlir::Value prefixBase = builder.createPtrBitcast(alignedCalleePtr, prefixTy);
      auto minusOne = builder.getSInt64(-1, loc);
      mlir::Value calleePrefix = cir::PtrStrideOp::create(
          builder, loc, prefixPtrTy, prefixBase, minusOne,
          cir::GEPNoWrapFlags::none);
      markNoSanitize(calleePrefix.getDefiningOp());

      auto sigPtr = builder.createGetMember(
          loc, builder.getPointerTo(builder.getUInt32Ty()), calleePrefix, "", 0);
      markNoSanitize(sigPtr);
      mlir::Value calleeSig =
          builder.createAlignedLoad(loc, builder.getUInt32Ty(), sigPtr,
                                    CharUnits::fromQuantity(4));
      markNoSanitize(calleeSig.getDefiningOp());
      mlir::Value sigMatch = cir::CmpOp::create(
          builder, loc, cir::CmpOpKind::eq, calleeSig,
          builder.getUInt32(*prefixSig, loc));
      markNoSanitize(sigMatch.getDefiningOp());

      auto sigIf = cir::IfOp::create(
          builder, loc, sigMatch, /*withElseRegion=*/true,
          [&](mlir::OpBuilder &, mlir::Location ifLoc) {
            auto hashPtr = builder.createGetMember(
                ifLoc, builder.getPointerTo(builder.getUInt32Ty()),
                calleePrefix, "", 1);
            markNoSanitize(hashPtr);
            mlir::Value calleeTypeHash =
                builder.createAlignedLoad(ifLoc, builder.getUInt32Ty(), hashPtr,
                                          getPointerAlign());
            markNoSanitize(calleeTypeHash.getDefiningOp());
            mlir::Value typeHashMatch = cir::CmpOp::create(
                builder, ifLoc, cir::CmpOpKind::eq, calleeTypeHash,
                builder.getUInt32(cgm.createUBSanFunctionTypeHash(pointeeTy),
                                  ifLoc));
            markNoSanitize(typeHashMatch.getDefiningOp());

            auto typeCheckIf = cir::IfOp::create(
                builder, ifLoc, typeHashMatch, /*withElseRegion=*/true,
                [&](mlir::OpBuilder &, mlir::Location yieldLoc) {
                  builder.createYield(yieldLoc);
                },
                [&](mlir::OpBuilder &, mlir::Location handlerLoc) {
                  if (cgm.getCodeGenOpts().SanitizeTrap.has(
                          SanitizerKind::Function)) {
                    auto trap = cir::TrapOp::create(builder, handlerLoc);
                    trap->setAttr(
                        "cir.ubsantrap_handler",
                        builder.getI8IntegerAttr(static_cast<uint8_t>(6)));
                    markNoSanitize(trap);
                    return;
                  }

                  auto stubFnTy =
                      cir::FuncType::get({intPtrTy}, builder.getVoidTy());
                  auto stubFn = cgm.createRuntimeFunction(
                      stubFnTy, "__cir_ubsan_function_type_mismatch_handler",
                      /*attrs=*/{}, /*isLocal=*/false,
                      /*assumeConvergent=*/false);
                  mlir::Value ptrAsInt =
                      builder.createPtrToInt(calleePtr, intPtrTy);
                  markNoSanitize(ptrAsInt.getDefiningOp());
                  auto call = builder.createCallOp(handlerLoc, stubFn, {ptrAsInt});
                  setUBSanSourceLocationAttrs(*this, call, e->getBeginLoc());
                  setUBSanTypeNameAttr(*this, call, calleeTy);
                  call->setDiscardableAttr(
                      "cir.ubsan.handler_name",
                      builder.getStringAttr("function_type_mismatch"));
                  StringRef handlerKind = "abort";
                  if (cgm.getCodeGenOpts().SanitizeRecover.has(
                          SanitizerKind::Function))
                    handlerKind = "recover";
                  call->setDiscardableAttr("cir.ubsan.handler_kind",
                                           builder.getStringAttr(handlerKind));
                  call->setDiscardableAttr(
                      "cir.nosanitize",
                      mlir::UnitAttr::get(builder.getContext()));

                  if (handlerKind == "abort") {
                    auto unreachable =
                        cir::UnreachableOp::create(builder, handlerLoc);
                    markNoSanitize(unreachable);
                  } else {
                    auto yield = builder.createYield(handlerLoc);
                    markNoSanitize(yield);
                  }
                });
            setSanitizerBranchAttrs(builder, typeCheckIf);
            builder.createYield(ifLoc);
          },
          [&](mlir::OpBuilder &, mlir::Location elseLoc) {
            builder.createYield(elseLoc);
          });
      sigIf->setAttr("cir.nosanitize",
                     mlir::UnitAttr::get(&getMLIRContext()));
    }
  }

  const auto *fnType = cast<FunctionType>(pointeeTy);

  // When calling an OpenCL kernel, redirect through the stub by adjusting
  // the calling convention. Matches OGCG CGExpr.cpp:6859-6861.
  if (const auto *fd = dyn_cast_or_null<FunctionDecl>(
          callee.getAbstractInfo().getCalleeDecl().getDecl());
      fd &&
      DeviceKernelAttr::isOpenCLSpelling(fd->getAttr<DeviceKernelAttr>())) {
    getCIRGenModule().getTargetCIRGenInfo()
        .setOCLKernelStubCallingConvention(fnType);
  }

  (void)0; // Additional sanitizer-specific CIR parity work remains NYI here.

  CallArgList args;

  // C++17 requires that we evaluate arguments to a call using assignment syntax
  // right-to-left, and that we evaluate arguments to certain other operators
  // left-to-right. Note that we allow this to override the order dictated by
  // the calling convention on the MS ABI, which means that parameter
  // destruction order is not necessarily reverse construction order.
  // FIXME: Revisit this based on C++ committee response to unimplementability.
  EvaluationOrder order = EvaluationOrder::Default;
  if (auto *oce = dyn_cast<CXXOperatorCallExpr>(e)) {
    if (oce->isAssignmentOp())
      order = EvaluationOrder::ForceRightToLeft;
    else {
      switch (oce->getOperator()) {
      case OO_LessLess:
      case OO_GreaterGreater:
      case OO_AmpAmp:
      case OO_PipePipe:
      case OO_Comma:
      case OO_ArrowStar:
        order = EvaluationOrder::ForceLeftToRight;
        break;
      default:
        break;
      }
    }
  }

  // For static operator calls (C++23), the CXXOperatorCallExpr includes the
  // implicit object as the first argument, but the function prototype doesn't
  // have a 'this' parameter. Emit the object as an ignored expression and
  // skip it, matching OGCG.
  auto arguments = e->arguments();
  if (const auto *operatorCall = dyn_cast<CXXOperatorCallExpr>(e)) {
    if (const auto *md =
            dyn_cast_if_present<CXXMethodDecl>(operatorCall->getCalleeDecl());
        md && md->isStatic()) {
      emitIgnoredExpr(e->getArg(0));
      arguments = drop_begin(arguments, 1);
    }
  }

  bool sawDefaultArg = false;
  emitCallArgs(args, dyn_cast<FunctionProtoType>(fnType), arguments,
               e->getDirectCallee(), /*ParamsToSkip=*/0, order,
               &sawDefaultArg);

  const CIRGenFunctionInfo &funcInfo =
      cgm.getTypes().arrangeFreeFunctionCall(args, fnType);

  // C99 6.5.2.2p6:
  //   If the expression that denotes the called function has a type that does
  //   not include a prototype, [the default argument promotions are performed].
  //   If the number of arguments does not equal the number of parameters, the
  //   behavior is undefined. If the function is defined with a type that
  //   includes a prototype, and either the prototype ends with an ellipsis (,
  //   ...) or the types of the arguments after promotion are not compatible
  //   with the types of the parameters, the behavior is undefined. If the
  //   function is defined with a type that does not include a prototype, and
  //   the types of the arguments after promotion are not compatible with those
  //   of the parameters after promotion, the behavior is undefined [except in
  //   some trivial cases].
  // That is, in the general case, we should assume that a call through an
  // unprototyped function type works like a *non-variadic* call. The way we
  // make this work is to cast to the exxact type fo the promoted arguments.
  if (isa<FunctionNoProtoType>(fnType)) {
    cir::FuncType calleeTy = getTypes().getFunctionType(funcInfo);
    // For no-prototype calls that the target considers variadic (e.g. x86-64
    // CC_C), preserve the variadic flag from funcInfo. Otherwise use a
    // non-variadic signature matching the exact promoted argument types.
    if (!funcInfo.isVariadic())
      calleeTy = cir::FuncType::get(calleeTy.getInputs(),
                                    calleeTy.getReturnType(), false);
    auto calleePtrTy = cir::PointerType::get(calleeTy);

    mlir::Operation *fn = callee.getFunctionPointer();
    mlir::Value addr;
    if (auto funcOp = mlir::dyn_cast<cir::FuncOp>(fn)) {
      addr = cir::GetGlobalOp::create(
          builder, getLoc(e->getSourceRange()),
          cir::PointerType::get(funcOp.getFunctionType()), funcOp.getSymName());
    } else {
      addr = fn->getResult(0);
    }

    fn = builder.createBitcast(addr, calleePtrTy).getDefiningOp();
    callee.setFunctionPointer(fn);
  }

  // OGCG also rewrites calling convention for OpenCL kernel stubs. CIR does
  // not materialize OpenCL kernel-stub calls in CIRGen yet, so there is no
  // corresponding call-site rewrite here.

  // Check always_inline / flatten target feature mismatches. This mirrors
  // OGCG's checkTargetFeatures (CodeGenFunction.cpp).
  if (const FunctionDecl *fd = e->getDirectCallee())
    checkTargetFeatures(e->getExprLoc(), fd);

  std::optional<CallDebugLocRAIIObject> callDebugLoc;
  if (sawDefaultArg && currSrcLoc)
    callDebugLoc.emplace(*this, *currSrcLoc);

  cir::CIRCallOpInterface callOp;
  bool isMustTail = (mustTailCall == e);
  RValue callResult = emitCall(funcInfo, callee, returnValue, args, &callOp,
                               getLoc(e->getExprLoc()), isMustTail,
                               staticChain, e->getExprLoc());

  // Expose the call op to the caller (matching OGCG's CallOrInvoke parameter).
  if (callOp)
    if (const auto *calleeDecl = dyn_cast_or_null<FunctionDecl>(targetDecl))
      if (sanOpts.has(SanitizerKind::AllocToken) &&
          (calleeDecl->hasAttr<RestrictAttr>() ||
           calleeDecl->hasAttr<MallocSpanAttr>() ||
           calleeDecl->hasAttr<AllocSizeAttr>()))
        maybeAttachAllocToken(callOp, e);

  if (callOrInvoke)
    *callOrInvoke = callOp;

  // Attach AST CallExpr information for later passes (e.g. IdiomRecognizer).
  if (callOp && e)
    callOp->setAttr("ast", cir::ASTCallExprAttr::get(&cgm.getMLIRContext(), e));

  if (callOp && sanOpts.has(SanitizerKind::CFIICall) &&
      (!targetDecl || !isa<FunctionDecl>(targetDecl)) &&
      !cfiUnchecked) {
    maybeAttachCFIIcallAttrs(*this, callOp, QualType(fnType, 0),
                             e->getBeginLoc());
  }
  if (callOp)
    maybeAttachCallGraphSectionAttrs(callOp, QualType(fnType, 0));

  // If this call expression is marked as coro_elide_safe, add the attribute
  // to the call op. This allows LLVM's coroutine optimizer to perform heap
  // allocation elision. Matches OGCG CGExpr.cpp:6253-6259.
  if (callOp && e && e->isCoroElideSafe()) {
    mlir::NamedAttrList callAttrs;
    if (auto existingExtra = callOp->getAttrOfType<
            cir::ExtraFuncAttributesAttr>("extra_attrs")) {
      for (auto attr : existingExtra.getElements().getValue())
        callAttrs.append(attr.getName(), attr.getValue());
    }
    callAttrs.set("coro_elide_safe",
                  mlir::UnitAttr::get(&cgm.getMLIRContext()));
    callOp->setAttr(
        "extra_attrs",
        cir::ExtraFuncAttributesAttr::get(
            callAttrs.getDictionary(&cgm.getMLIRContext())));
  }

  // Attach raw source location for __attribute__((error/warning)) call sites.
  // The backend uses !srcloc metadata to provide source location in diagnostics.
  // Matches OGCG CGCall.cpp:6052-6058. We merge into extra_attrs so the
  // attribute survives op replacements during release().
  if (callOp && e && callee.isOrdinary()) {
    if (const auto *targetDecl = e->getCalleeDecl()) {
      if (targetDecl->hasAttr<ErrorAttr>()) {
        unsigned rawLoc = e->getExprLoc().getRawEncoding();
        // Merge into existing extra_attrs dict
        mlir::NamedAttrList callAttrs;
        if (auto existingExtra = callOp->getAttrOfType<
                cir::ExtraFuncAttributesAttr>("extra_attrs")) {
          for (auto attr : existingExtra.getElements().getValue())
            callAttrs.append(attr.getName(), attr.getValue());
        }
        callAttrs.set("cir.srcloc",
                      mlir::IntegerAttr::get(
                          mlir::IntegerType::get(&cgm.getMLIRContext(), 64),
                          rawLoc));
        callOp->setAttr(
            "extra_attrs",
            cir::ExtraFuncAttributesAttr::get(
                callAttrs.getDictionary(&cgm.getMLIRContext())));
      }
    }
  }

  // Apply fast-math flags from the CallExpr's FP options to the call op.
  // OGCG sets CGFPOptionsRAII before the call (CGBuiltin.cpp:807, also
  // CGExpr.cpp EmitCallExpr), which stamps FMF on the CallInst via IRBuilder.
  // CIR has no IRBuilder FMF state, so attach FMF as a discardable attribute.
  // Matches the builtin path in CIRGenBuiltin.cpp.
  if (callOp && e) {
    FPOptions fpOpts = e->getFPFeaturesInEffect(getLangOpts());
    cir::FastMathFlags fmf = getFastMathFlags(fpOpts);
    if (fmf != cir::FastMathFlags::none) {
      callOp->setAttr(
          "cir.fmf",
          mlir::IntegerAttr::get(
              mlir::IntegerType::get(&cgm.getMLIRContext(), 32),
              static_cast<uint32_t>(fmf)));
    }
  }

  return callResult;
}

CIRGenCallee CIRGenFunction::emitCallee(const clang::Expr *e) {
  e = e->IgnoreParens();

  // Look through function-to-pointer decay.
  if (const auto *implicitCast = dyn_cast<ImplicitCastExpr>(e)) {
    if (implicitCast->getCastKind() == CK_FunctionToPointerDecay ||
        implicitCast->getCastKind() == CK_BuiltinFnToFnPtr) {
      return emitCallee(implicitCast->getSubExpr());
    }
    // When performing an indirect call through a function pointer lvalue, the
    // function pointer lvalue is implicitly converted to an rvalue through an
    // lvalue-to-rvalue conversion. Other implicit cast kinds (e.g.
    // CK_UserDefinedConversion for surrogate calls) fall through to the
    // indirect reference handling below.
    if (implicitCast->getCastKind() == CK_LValueToRValue) {
      const Expr *subExpr = implicitCast->getSubExpr()->IgnoreParens();
      if (const auto *ptrType = subExpr->getType()->getAs<clang::PointerType>()) {
        QualType functionType = ptrType->getPointeeType();
        assert(functionType->isFunctionType());

        mlir::Value calleePtr;
        CIRGenRuntimePointerAuthInfo runtimeAuthInfo;
        auto materializeLoadedPointer = [&](LValue lv, SourceLocation loc) {
          calleePtr = emitLoadOfScalar(lv, loc);
          if (PointerAuthQualifier ptrAuth =
                  lv.getQuals().getPointerAuth().withoutKeyNone())
            runtimeAuthInfo = emitPointerAuthInfo(ptrAuth, lv.getAddress());
        };

        if (const auto *refExpr = dyn_cast<DeclRefExpr>(subExpr)) {
          if (auto constant = tryEmitAsConstant(refExpr)) {
            if (!constant.isReference()) {
              calleePtr = emitScalarConstant(
                  constant, const_cast<DeclRefExpr *>(refExpr));
            } else {
              LValue lv = constant.getReferenceLValue(
                  *this, const_cast<DeclRefExpr *>(refExpr));
              materializeLoadedPointer(lv, refExpr->getLocation());
            }
          }
        }

        if (!calleePtr) {
          LValue lv = emitLValue(subExpr);
          materializeLoadedPointer(lv, subExpr->getExprLoc());
        }

        GlobalDecl gd;
        if (const auto *vd =
                dyn_cast_or_null<VarDecl>(e->getReferencedDeclOfCallee()))
          gd = GlobalDecl(vd);

        std::optional<CIRGenPointerAuthInfo> pointerAuthInfo;
        mlir::Value runtimeDiscriminator;
        if (runtimeAuthInfo) {
          CIRGenPointerAuthInfo info;
          info.enabled = true;
          info.key = runtimeAuthInfo.key;
          info.authenticationMode = runtimeAuthInfo.authenticationMode;
          info.isIsaPointer = runtimeAuthInfo.isIsaPointer;
          info.authenticatesNullValues = runtimeAuthInfo.authenticatesNullValues;
          if (runtimeAuthInfo.discriminator) {
            if (auto constant =
                    runtimeAuthInfo.discriminator.getDefiningOp<cir::ConstantOp>()) {
              if (auto intAttr =
                      mlir::dyn_cast<cir::IntAttr>(constant.getValue()))
                info.discriminator = intAttr.getValue().getZExtValue();
              else
                runtimeDiscriminator = runtimeAuthInfo.discriminator;
            } else {
              runtimeDiscriminator = runtimeAuthInfo.discriminator;
            }
          }
          pointerAuthInfo = info;
        } else {
          pointerAuthInfo = cgm.getFunctionPointerAuthInfo(functionType);
        }

        CIRGenCalleeInfo calleeInfo(functionType->getAs<FunctionProtoType>(),
                                    gd, pointerAuthInfo,
                                    runtimeDiscriminator);
        return CIRGenCallee(calleeInfo, calleePtr.getDefiningOp());
      }
    }
  } else if (const auto *declRef = dyn_cast<DeclRefExpr>(e)) {
    // Resolve direct calls.
    if (const auto *funcDecl = dyn_cast<FunctionDecl>(declRef->getDecl())) {
      GlobalDecl gd;
      if (DeviceKernelAttr::isOpenCLSpelling(
              funcDecl->getAttr<DeviceKernelAttr>()))
        gd = GlobalDecl(funcDecl, KernelReferenceKind::Stub);
      else
        gd = GlobalDecl(funcDecl);
      return emitDirectCallee(gd);
    }
    // Else: the DeclRefExpr refers to a non-FunctionDecl (e.g. a function
    // reference parameter). Fall through to the indirect reference handling.
  } else if (auto me = dyn_cast<MemberExpr>(e)) {
    if (const auto *fd = dyn_cast<FunctionDecl>(me->getMemberDecl())) {
      emitIgnoredExpr(me->getBase());
      return emitDirectCallee(fd);
    }
    // Else fall through to the indirect reference handling below.
  } else if (auto *pde = dyn_cast<CXXPseudoDestructorExpr>(e)) {
    return CIRGenCallee::forPseudoDestructor(pde);
  } else if (auto *nttp = dyn_cast<SubstNonTypeTemplateParmExpr>(e)) {
    return emitCallee(nttp->getReplacement());
  }

  // Otherwise, we have an indirect reference.
  mlir::Value calleePtr;
  QualType functionType;
  if (const auto *ptrType = e->getType()->getAs<clang::PointerType>()) {
    calleePtr = emitScalarExpr(e);
    functionType = ptrType->getPointeeType();
  } else {
    functionType = e->getType();
    calleePtr = emitLValue(e).getPointer();
  }
  assert(functionType->isFunctionType());

  GlobalDecl gd;
  if (const auto *vd =
          dyn_cast_or_null<VarDecl>(e->getReferencedDeclOfCallee()))
    gd = GlobalDecl(vd);

  CIRGenCalleeInfo calleeInfo(functionType->getAs<FunctionProtoType>(), gd,
                              cgm.getFunctionPointerAuthInfo(functionType));
  CIRGenCallee callee(calleeInfo, calleePtr.getDefiningOp());
  return callee;
}

mlir::Value CIRGenFunction::emitBlockLiteral(const BlockExpr *e) {
  const auto *blockDecl = e->getBlockDecl();

  auto unsupportedCapture = llvm::find_if(
      blockDecl->captures(), [&](const BlockDecl::Capture &capture) {
        if (capture.isEscapingByref())
          return false;
        if (isSupportedCXXDirectBlockCapture(capture))
          return false;
        if (capture.getCopyExpr())
          return true;
        QualType captureTy = capture.getVariable()->getType();
        auto evalKind = getEvaluationKind(captureTy);
        if (captureTy.getObjCLifetime() == Qualifiers::OCL_None)
          return evalKind != cir::TEK_Scalar &&
                 evalKind != cir::TEK_Complex &&
                 evalKind != cir::TEK_Aggregate;
        return !isSupportedObjCStrongDirectBlockCapture(captureTy) &&
               !isSupportedObjCStrongBlockPointerDirectBlockCapture(captureTy) &&
               !isSupportedObjCWeakDirectBlockCapture(captureTy) &&
               !isSupportedObjCUnretainedDirectBlockCapture(captureTy);
      });
  if (unsupportedCapture != blockDecl->capture_end()) {
    cgm.error(e->getExprLoc(),
              "ClangIR code gen Not Yet Implemented: ScalarExprEmitter: block");
    return builder.getNullValue(convertType(e->getType()),
                                getLoc(e->getExprLoc()));
  }

  mlir::Location loc = getLoc(e->getExprLoc());
  if (!blockDecl->hasCaptures())
    return builder.getConstant(
        loc, cgm.getAddrOfGlobalBlockLiteral(loc, curGD, e));

  auto &moduleBuilder = cgm.getBuilder();
  auto voidPtrTy = moduleBuilder.getVoidPtrTy();
  auto int32Ty = moduleBuilder.getSInt32Ty();
  auto ulongTy = mlir::cast<cir::IntType>(cgm.convertType(getContext().UnsignedLongTy));
  auto i8PtrTy = moduleBuilder.getPointerTo(moduleBuilder.getSInt8Ty());
  const bool capturesCXXThis = blockDecl->capturesCXXThis();
  const unsigned thisFieldIndex = 5;
  const unsigned firstCaptureFieldIndex = thisFieldIndex + capturesCXXThis;
  QualType thisCaptureTy;
  if (capturesCXXThis) {
    const auto *method =
        dyn_cast_or_null<CXXMethodDecl>(blockDecl->getNonClosureContext());
    assert(method && "captured C++ this without enclosing C++ method");
    thisCaptureTy = method->getThisType();
  }
  auto getDirectBlockCapturePointerAuthQualifier =
      [&](const BlockDecl::Capture &capture,
          const FieldDecl *lambdaCaptureField) {
        QualType captureFieldTy =
            getDirectBlockCaptureFieldType(capture, lambdaCaptureField);
        if (PointerAuthQualifier qualifier = captureFieldTy.getPointerAuth())
          return qualifier;
        return capture.getVariable()->getType().getPointerAuth();
      };

  const std::string blockName = cgm.getUniqueBlockName(
      e, getBlockMangledName(cgm, curGD, blockDecl));
  const std::string invokeName = blockName + ".invoke";
  const std::string sigName = blockName + ".sig";
  const std::string descName = blockName + ".descriptor";
  const std::string literalName = blockName + ".literal";

  const FunctionProtoType *blockProto = e->getFunctionType();
  const IdentifierInfo *ii = &getContext().Idents.get(".block_descriptor");
  ImplicitParamDecl selfDecl(getContext(), const_cast<BlockDecl *>(blockDecl),
                             SourceLocation(), ii, getContext().VoidPtrTy,
                             ImplicitParamKind::ObjCSelf);
  FunctionArgList params;
  params.push_back(&selfDecl);
  params.append(blockDecl->param_begin(), blockDecl->param_end());

  cir::FuncOp invokeFn = lookupFuncByName(cgm, invokeName);
  if (!invokeFn) {
    const CIRGenFunctionInfo &invokeInfo =
        getTypes().arrangeBlockFunctionDeclaration(blockProto, params);
    cir::FuncType invokeTy = getTypes().getFunctionType(invokeInfo);
    invokeFn = cgm.createCIRFunction(loc, invokeName, invokeTy, nullptr);
    cgm.maybeSetDisableTailCallsForBlockInvoke(invokeFn, blockDecl);
    cgm.setInternalFunctionAttributes(GlobalDecl(blockDecl), invokeFn,
                                      invokeInfo);
    invokeFn.setSymVisibility("private");

    CIRGenFunction blockCGF(cgm, moduleBuilder);
    {
      mlir::OpBuilder::InsertionGuard guard(moduleBuilder);
      blockCGF.generateBlockFunctionCode(e, invokeFn, invokeTy);
    }
  }
  bool usesStret =
      cgm.blockFunctionUsesStret(blockProto, params, invokeFn.getFunctionType());

  StringRef isaName =
      blockDecl->hasCaptures() ? "_NSConcreteStackBlock" : "_NSConcreteGlobalBlock";
  cir::GlobalOp isaGlobal = lookupGlobalByName(cgm, isaName);
  if (!isaGlobal) {
    isaGlobal = CIRGenModule::createGlobalOp(cgm, loc, isaName, voidPtrTy,
                                             /*isConstant=*/false);
    isaGlobal.setLinkage(cir::GlobalLinkageKind::ExternalLinkage);
    isaGlobal.setSymVisibility("private");
  }

  cir::GlobalOp sigGlobal;
  mlir::TypedAttr sigPtrAttr;
  if (!cgm.getCodeGenOpts().DisableBlockSignatureString) {
    sigGlobal = lookupGlobalByName(cgm, sigName);
    if (!sigGlobal) {
      std::string sig = getContext().getObjCEncodingForBlock(e);
      sig.push_back('\0');
      auto sigAttr = mlir::cast<mlir::TypedAttr>(
          moduleBuilder.getString(sig, moduleBuilder.getSInt8Ty(), sig.size()));
      sigGlobal = CIRGenModule::createGlobalOp(cgm, loc, sigName,
                                               sigAttr.getType(),
                                               /*isConstant=*/true);
      sigGlobal.setLinkage(cir::GlobalLinkageKind::PrivateLinkage);
      sigGlobal.setSymVisibility("private");
      sigGlobal.setAlignment(1);
      CIRGenModule::setInitializer(sigGlobal, sigAttr);
      sigGlobal->setAttr("unnamed_addr",
                         mlir::UnitAttr::get(&getMLIRContext()));
    }
    sigPtrAttr = moduleBuilder.getGlobalViewAttr(i8PtrTy, sigGlobal);
  } else {
    sigPtrAttr = moduleBuilder.getConstPtrAttr(i8PtrTy, 0);
  }

  SmallVector<mlir::Type, 8> literalMembers{
      moduleBuilder.getPointerTo(isaGlobal.getSymType()), int32Ty, int32Ty,
      voidPtrTy, voidPtrTy};
  if (capturesCXXThis)
    literalMembers.push_back(convertType(thisCaptureTy));
  for (const auto &capture : blockDecl->captures()) {
    const FieldDecl *lambdaCaptureField =
        capture.isNested() ? lambdaCaptureFields.lookup(capture.getVariable())
                           : nullptr;
    QualType captureFieldTy =
        getDirectBlockCaptureFieldType(capture, lambdaCaptureField);
    literalMembers.push_back(capture.isEscapingByref()
                                 ? mlir::Type(voidPtrTy)
                                 : convertTypeForMem(captureFieldTy));
  }
  bool packedLiteral = shouldPackDirectBlockLiteral(literalMembers);
  CharUnits literalAlign = getDirectBlockLiteralAlignment(literalMembers);
  SmallVector<DirectBlockHelperCaptureInfo, 8> helperCaptures;
  bool capturesNonExternalType = false;
  helperCaptures.reserve(blockDecl->captures().size());
  for (const auto &[index, capture] : llvm::enumerate(blockDecl->captures())) {
    const FieldDecl *lambdaCaptureField =
        capture.isNested() ? lambdaCaptureFields.lookup(capture.getVariable())
                           : nullptr;
    QualType captureFieldTy =
        getDirectBlockCaptureFieldType(capture, lambdaCaptureField);
    auto [copyKind, copyFlags] = computeCopyInfoForDirectBlockCapture(
        capture, captureFieldTy, cgm.getLangOpts());
    auto [disposeKind, disposeFlags] = computeDestroyInfoForDirectBlockCapture(
        capture, captureFieldTy, cgm.getLangOpts());
    helperCaptures.push_back({&capture,
                              capture.getVariable()->getType(),
                              getDirectBlockCapturePointerAuthQualifier(
                                  capture, lambdaCaptureField),
                              getDirectBlockLiteralFieldOffset(
                                  literalMembers, packedLiteral,
                                  firstCaptureFieldIndex + index),
                              copyKind,
                              copyFlags,
                              disposeKind,
                              disposeFlags});

    if (cgm.getLangOpts().CPlusPlus) {
      if (const auto *record = captureFieldTy->getAsCXXRecordDecl();
          record && (capture.getCopyExpr() || !record->hasTrivialDestructor()) &&
          !record->isExternallyVisible()) {
        capturesNonExternalType = true;
      }
    }
  }
  bool hasCopyDisposeHelper =
      llvm::any_of(helperCaptures, [](const DirectBlockHelperCaptureInfo &info) {
        return !info.isConstantOrTrivial();
      });

  SmallVector<mlir::Type, 8> descMembers{ulongTy, ulongTy};
  SmallVector<mlir::Attribute, 8> descValues{
      cir::IntAttr::get(ulongTy, 0), cir::IntAttr::get(ulongTy, 0)};
  cir::FuncOp copyHelper;
  cir::FuncOp destroyHelper;
  std::string copyHelperName;
  std::string destroyHelperName;
  if (hasCopyDisposeHelper) {
    copyHelperName = getDirectBlockCopyDestroyHelperFuncName(
        helperCaptures, literalAlign, DirectBlockCaptureStrKind::CopyHelper,
        cgm);
    destroyHelperName = getDirectBlockCopyDestroyHelperFuncName(
        helperCaptures, literalAlign, DirectBlockCaptureStrKind::DisposeHelper,
        cgm);
    copyHelper = lookupFuncByName(cgm, copyHelperName);
    destroyHelper = lookupFuncByName(cgm, destroyHelperName);
    if (!copyHelper) {
      const CIRGenFunctionInfo &copyHelperInfo =
          getDirectBlockHelperFunctionInfo(cgm, /*isCopy=*/true);
      copyHelper = cgm.createCIRFunction(
          loc, copyHelperName, cgm.getTypes().getFunctionType(copyHelperInfo),
          nullptr);
      setDirectBlockHelperAttributesVisibility(capturesNonExternalType,
                                               copyHelper, copyHelperInfo, cgm);
    }
    if (!destroyHelper) {
      const CIRGenFunctionInfo &destroyHelperInfo =
          getDirectBlockHelperFunctionInfo(cgm, /*isCopy=*/false);
      destroyHelper = cgm.createCIRFunction(
          loc, destroyHelperName,
          cgm.getTypes().getFunctionType(destroyHelperInfo), nullptr);
      setDirectBlockHelperAttributesVisibility(capturesNonExternalType,
                                               destroyHelper, destroyHelperInfo,
                                               cgm);
    }
    if (cgm.getLangOpts().Sanitize.has(SanitizerKind::Thread))
      cgm.markAsIgnoreThreadCheckingAtRuntime(destroyHelper);
    descMembers.push_back(moduleBuilder.getPointerTo(copyHelper.getFunctionType()));
    descMembers.push_back(
        moduleBuilder.getPointerTo(destroyHelper.getFunctionType()));
    descValues.push_back(cir::GlobalViewAttr::get(
        moduleBuilder.getPointerTo(copyHelper.getFunctionType()),
        mlir::FlatSymbolRefAttr::get(&getMLIRContext(),
                                     copyHelper.getSymNameAttr())));
    descValues.push_back(cir::GlobalViewAttr::get(
        moduleBuilder.getPointerTo(destroyHelper.getFunctionType()),
        mlir::FlatSymbolRefAttr::get(&getMLIRContext(),
                                     destroyHelper.getSymNameAttr())));
  }
  descMembers.push_back(i8PtrTy);
  descValues.push_back(sigPtrAttr);

  const auto &layout = cgm.getDataLayout();
  auto fieldOffset = [&](llvm::ArrayRef<mlir::Type> members,
                         unsigned fieldIndex) {
    CharUnits offset;
    for (unsigned i = 0; i < fieldIndex; ++i) {
      offset = offset.alignTo(
          CharUnits::fromQuantity(layout.getABITypeAlign(members[i]).value()));
      offset += CharUnits::fromQuantity(
          layout.getTypeAllocSize(members[i]).getFixedValue());
    }
    return offset.alignTo(CharUnits::fromQuantity(
        layout.getABITypeAlign(members[fieldIndex]).value()));
  };
  auto literalFieldOffset = [&](unsigned fieldIndex) {
    return fieldOffset(literalMembers, fieldIndex);
  };

  mlir::Type layoutTy = i8PtrTy;
  mlir::TypedAttr layoutAttr =
      mlir::cast<mlir::TypedAttr>(moduleBuilder.getConstPtrAttr(i8PtrTy, 0));
  bool hasCapturedVariableLayout =
      cgm.getLangOpts().ObjC &&
      cgm.getLangOpts().getGC() == LangOptions::NonGC;
  if (cgm.getLangOpts().ObjC &&
      (cgm.getLangOpts().getGC() != LangOptions::NonGC ||
       !cgm.getLangOpts().ObjCRuntime.isGNUFamily())) {
    SmallVector<BlockLayoutEntry, 8> blockLayoutEntries;
    unsigned wordSizeInBits = cgm.getTarget().getPointerWidth(LangAS::Default);
    unsigned byteSizeInBits = cgm.getTarget().getCharWidth();
    unsigned wordSizeInBytes = wordSizeInBits / byteSizeInBits;

    for (const auto &[index, capture] : llvm::enumerate(blockDecl->captures())) {
      QualType captureTy = capture.getVariable()->getType();
      const FieldDecl *lambdaCaptureField =
          capture.isNested() ? lambdaCaptureFields.lookup(capture.getVariable())
                             : nullptr;
      QualType captureFieldTy =
          getDirectBlockCaptureFieldType(capture, lambdaCaptureField);
      BlockLayoutOpcode opcode =
          getDirectBlockLayoutOpcode(capture, captureTy, helperCaptures[index]);

      CharUnits fieldSize =
          capture.isEscapingByref()
              ? CharUnits::fromQuantity(wordSizeInBytes)
              : getContext().getTypeSizeInChars(captureFieldTy);
      blockLayoutEntries.push_back(
          {opcode, literalFieldOffset(firstCaptureFieldIndex + index),
           fieldSize});
    }

    llvm::sort(blockLayoutEntries);
    SmallVector<unsigned char, 16> blockLayout;
    for (unsigned i = 0, size = blockLayoutEntries.size(); i < size; ++i) {
      BlockLayoutOpcode opcode = blockLayoutEntries[i].opcode;
      CharUnits startBytePos = blockLayoutEntries[i].bytePos;
      CharUnits endBytePos = startBytePos;
      unsigned j = i + 1;
      while (j < size && opcode == blockLayoutEntries[j].opcode) {
        endBytePos = blockLayoutEntries[j].bytePos;
        ++j;
        ++i;
      }
      CharUnits sizeInBytes =
          endBytePos - startBytePos + blockLayoutEntries[j - 1].size;
      if (j < size) {
        CharUnits gap = blockLayoutEntries[j].bytePos -
                        blockLayoutEntries[j - 1].bytePos -
                        blockLayoutEntries[j - 1].size;
        sizeInBytes += gap;
      }
      CharUnits residueInBytes = CharUnits::Zero();
      if (opcode == BlockLayoutOpcode::NonObjectBytes) {
        residueInBytes = sizeInBytes % wordSizeInBytes;
        sizeInBytes -= residueInBytes;
        opcode = BlockLayoutOpcode::NonObjectWords;
      }

      unsigned sizeInWords = sizeInBytes.getQuantity() / wordSizeInBytes;
      while (sizeInWords >= 16) {
        blockLayout.push_back((static_cast<unsigned char>(opcode) << 4) | 0xF);
        sizeInWords -= 16;
      }
      if (sizeInWords > 0)
        blockLayout.push_back((static_cast<unsigned char>(opcode) << 4) |
                              (sizeInWords - 1));
      if (residueInBytes > CharUnits::Zero())
        blockLayout.push_back(
            (static_cast<unsigned char>(BlockLayoutOpcode::NonObjectBytes) << 4) |
            (residueInBytes.getQuantity() - 1));
    }

    while (!blockLayout.empty()) {
      BlockLayoutOpcode opcode =
          static_cast<BlockLayoutOpcode>(blockLayout.back() >> 4);
      if (opcode == BlockLayoutOpcode::NonObjectBytes ||
          opcode == BlockLayoutOpcode::NonObjectWords) {
        blockLayout.pop_back();
        continue;
      }
      break;
    }

    if (uint64_t inlineLayout =
            getObjCInlineBlockLayoutInstruction(blockLayout)) {
      layoutTy = ulongTy;
      layoutAttr = cir::IntAttr::get(ulongTy, inlineLayout);
      hasCapturedVariableLayout = true;
    } else if (!blockLayout.empty()) {
      blockLayout.push_back(
          static_cast<unsigned char>(BlockLayoutOpcode::Operator) << 4);
      std::string bitmap(blockLayout.begin(), blockLayout.end());
      const std::string layoutName = blockName + ".layout";
      cir::GlobalOp layoutGlobal = lookupGlobalByName(cgm, layoutName);
      if (!layoutGlobal) {
        auto bitmapAttr = mlir::cast<mlir::TypedAttr>(moduleBuilder.getString(
            bitmap, moduleBuilder.getSInt8Ty(), bitmap.size()));
        layoutGlobal = CIRGenModule::createGlobalOp(cgm, loc, layoutName,
                                                    bitmapAttr.getType(),
                                                    /*isConstant=*/true);
        layoutGlobal.setLinkage(cir::GlobalLinkageKind::PrivateLinkage);
        layoutGlobal.setSymVisibility("private");
        layoutGlobal.setAlignment(1);
        CIRGenModule::setInitializer(layoutGlobal, bitmapAttr);
        layoutGlobal->setAttr("unnamed_addr",
                              mlir::UnitAttr::get(&getMLIRContext()));
      }
      layoutAttr = moduleBuilder.getGlobalViewAttr(i8PtrTy, layoutGlobal);
      hasCapturedVariableLayout = true;
    }
  }
  descMembers.push_back(layoutTy);
  descValues.push_back(layoutAttr);
  auto descTy = moduleBuilder.getAnonRecordTy(descMembers);
  literalMembers[4] = moduleBuilder.getPointerTo(descTy);
  auto literalTy = moduleBuilder.getAnonRecordTy(literalMembers, packedLiteral);

  cir::GlobalOp descGlobal = lookupGlobalByName(cgm, descName);
  if (!descGlobal) {
    uint64_t blockSize =
        getDirectBlockLiteralSize(literalMembers, packedLiteral).getQuantity();
    descValues[1] = cir::IntAttr::get(ulongTy, blockSize);
    auto descInit = moduleBuilder.getAnonConstRecord(
        mlir::ArrayAttr::get(&getMLIRContext(), descValues));
    descGlobal = CIRGenModule::createGlobalOp(cgm, loc, descName, descTy,
                                              /*isConstant=*/true);
    descGlobal.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
    descGlobal.setSymVisibility("private");
    descGlobal.setAlignment(cgm.getPointerAlign().getAsAlign().value());
    CIRGenModule::setInitializer(descGlobal, descInit);
    llvm::SmallVector<mlir::Attribute, 2> ptrauthFixups;
    auto addPtrauthFixup = [&](unsigned fieldIndex,
                               const PointerAuthSchema &schema, GlobalDecl decl,
                               QualType ty) {
      if (!schema || !cgm.shouldSignPointer(schema))
        return;
      ptrauthFixups.push_back(moduleBuilder.getArrayAttr({
          moduleBuilder.getI64IntegerAttr(
              fieldOffset(descMembers, fieldIndex).getQuantity()),
          moduleBuilder.getI32IntegerAttr(schema.getKey()),
          moduleBuilder.getI64IntegerAttr(
              cgm.getPointerAuthOtherDiscriminator(schema, decl, ty)),
          moduleBuilder.getBoolAttr(schema.isAddressDiscriminated()),
      }));
    };
    if (hasCopyDisposeHelper) {
      addPtrauthFixup(
          /*fieldIndex=*/2,
          cgm.getCodeGenOpts().PointerAuth.BlockHelperFunctionPointers,
          GlobalDecl(), QualType());
      addPtrauthFixup(
          /*fieldIndex=*/3,
          cgm.getCodeGenOpts().PointerAuth.BlockHelperFunctionPointers,
          GlobalDecl(), QualType());
    }
    if (!ptrauthFixups.empty())
      descGlobal->setAttr("cir.global_const_ptrauth",
                          moduleBuilder.getArrayAttr(ptrauthFixups));
  }

  cir::GlobalOp literalGlobal = lookupGlobalByName(cgm, literalName);
  uint64_t flags = 0;
  if (!blockDecl->hasCaptures())
    flags |= 1u << 28;
  if (!cgm.getCodeGenOpts().DisableBlockSignatureString)
    flags |= 1u << 30;
  if (usesStret)
    flags |= 1u << 29;
  if (hasCapturedVariableLayout)
    flags |= kBlockHasExtendedLayout;
  if (hasCopyDisposeHelper)
    flags |= 1u << 25;

  if (!blockDecl->hasCaptures() && !literalGlobal) {
    auto invokeView = cir::GlobalViewAttr::get(
        moduleBuilder.getPointerTo(invokeFn.getFunctionType()),
        mlir::FlatSymbolRefAttr::get(&getMLIRContext(), invokeFn.getSymNameAttr()));
    auto isaView = moduleBuilder.getGlobalViewAttr(
        moduleBuilder.getPointerTo(isaGlobal.getSymType()), isaGlobal);
    auto descView =
        moduleBuilder.getGlobalViewAttr(moduleBuilder.getPointerTo(descTy),
                                        descGlobal);
    auto literalInit = moduleBuilder.getAnonConstRecord(mlir::ArrayAttr::get(
        &getMLIRContext(),
        {isaView, cir::IntAttr::get(int32Ty, flags),
         cir::IntAttr::get(int32Ty, 0), invokeView, descView}));
    literalGlobal = CIRGenModule::createGlobalOp(cgm, loc, literalName,
                                                 literalTy,
                                                 /*isConstant=*/true);
    literalGlobal.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
    literalGlobal.setSymVisibility("private");
    literalGlobal.setAlignment(cgm.getPointerAlign().getAsAlign().value());
    CIRGenModule::setInitializer(literalGlobal, literalInit);
    literalGlobal->setAttr("cir.objc_arc_inert",
                           mlir::UnitAttr::get(&getMLIRContext()));
  }

  if (blockDecl->hasCaptures()) {
    auto fieldAddr = [&](Address base, unsigned fieldIndex) {
      mlir::Type fieldTy = literalMembers[fieldIndex];
      mlir::Value ptr = builder.createGetMember(
          loc, builder.getPointerTo(fieldTy), base.getPointer(), "", fieldIndex);
      return Address(ptr, fieldTy,
                     literalAlign.alignmentAtOffset(
                         getDirectBlockLiteralFieldOffset(
                             literalMembers, packedLiteral, fieldIndex)));
    };

    if (hasCopyDisposeHelper) {
      auto emitHelperBody = [&](cir::FuncOp fn, bool isCopy) {
        if (!fn.getBody().empty())
          return;

        mlir::OpBuilder::InsertionGuard guard(moduleBuilder);
        mlir::Block *entry = fn.addEntryBlock();
        moduleBuilder.setInsertionPointToStart(entry);

        auto literalPtrTy = moduleBuilder.getPointerTo(literalTy);
        mlir::Value blockValue = moduleBuilder.createBitcast(
            loc, fn.getArgument(0), literalPtrTy);
        mlir::Value otherBlockValue = nullptr;
        if (isCopy) {
          otherBlockValue =
              moduleBuilder.createBitcast(loc, fn.getArgument(1), literalPtrTy);
        }

        CIRGenFunction helperCGF(cgm, moduleBuilder, /*suppressNewContext=*/true);
        helperCGF.curFn = fn.getOperation();
        cir::FuncOp runtimeFn = cgm.createRuntimeFunction(
            isCopy ? cir::FuncType::get(
                         {voidPtrTy, voidPtrTy, int32Ty},
                         moduleBuilder.getVoidTy())
                   : cir::FuncType::get({voidPtrTy, int32Ty},
                                        moduleBuilder.getVoidTy()),
            isCopy ? "_Block_object_assign" : "_Block_object_dispose");

        // The copy helper constructs captures in forward capture order; the
        // dispose helper destroys them in REVERSE order, mirroring OGCG (whose
        // GenerateDestroyHelperFunction pushes per-capture cleanups that unwind
        // LIFO). Emitting dispose in forward order would destroy captures in the
        // wrong order when their destructors have order-dependent side effects.
        auto captures = blockDecl->captures();
        SmallVector<unsigned, 8> captureOrder;
        for (unsigned i = 0, e = captures.size(); i != e; ++i)
          captureOrder.push_back(isCopy ? i : e - 1 - i);
        // In the copy helper, if a later capture's copy-constructor throws while
        // _Block_copy is promoting the block, the captures already copied into
        // the destination must be destroyed. Mirror OGCG CGBlocks
        // pushCaptureCleanup(ForCopyHelper): push an EH-only cleanup per copied
        // non-trivial C++ capture within a scope spanning the whole copy loop.
        // The subsequent capture's copy-ctor call then lowers to a cir.try_call
        // (ehStack.requiresCatchOrCleanup) that unwinds to these cleanups; on
        // normal (non-throwing) exit the EH-only cleanups are deactivated so the
        // successfully-copied captures survive. A LexicalScope (not a plain
        // RunCleanupsScope) is required so cgf.curLexScope is set: emitCallLikeOp
        // asserts a scope exists when it must emit a cir.try_call for a call that
        // may throw (the bespoke helper otherwise establishes no lexical scope).
        std::optional<LexicalScope> copyHelperEHScope;
        cir::FuncOp blockObjectDisposeFn;
        if (isCopy) {
          copyHelperEHScope.emplace(helperCGF, loc, entry);
          // Cleanup-only: we need curLexScope set (so a throwing capture-copy
          // lowers to cir.try_call) and the EH cleanups emitted, but NOT the
          // full scope-termination / return-block machinery — the helper emits
          // its own explicit cir.return terminator.
          copyHelperEHScope->setCleanupOnly();
          // Runtime function used by the EH cleanup to release an already-copied
          // BlockObject/byref capture if a later capture's copy-ctor throws.
          blockObjectDisposeFn = cgm.createRuntimeFunction(
              cir::FuncType::get({voidPtrTy, int32Ty},
                                 moduleBuilder.getVoidTy()),
              "_Block_object_dispose");
        }
        for (unsigned index : captureOrder) {
          const BlockDecl::Capture &capture = captures[index];
          const FieldDecl *lambdaCaptureField =
              capture.isNested() ? lambdaCaptureFields.lookup(capture.getVariable())
                                 : nullptr;
          const DirectBlockHelperCaptureInfo &captureInfo = helperCaptures[index];
          QualType captureFieldTy =
              getDirectBlockCaptureFieldType(capture, lambdaCaptureField);
          PointerAuthQualifier capturePointerAuth = captureInfo.pointerAuth;
          unsigned fieldIndex = firstCaptureFieldIndex + index;
          DirectBlockCaptureEntityKind helperKind =
              isCopy ? captureInfo.copyKind : captureInfo.disposeKind;
          DirectBlockFieldFlags helperFlags =
              isCopy ? captureInfo.copyFlags : captureInfo.disposeFlags;
          if (helperKind == DirectBlockCaptureEntityKind::None)
            continue;

          Address field = Address(
              moduleBuilder.createGetMember(
                  loc, moduleBuilder.getPointerTo(literalMembers[fieldIndex]),
                  blockValue, "", fieldIndex),
              literalMembers[fieldIndex],
              literalAlign.alignmentAtOffset(getDirectBlockLiteralFieldOffset(
                  literalMembers, packedLiteral, fieldIndex)));

          if (isCopy) {
            Address srcField = Address(
                moduleBuilder.createGetMember(
                    loc, moduleBuilder.getPointerTo(literalMembers[fieldIndex]),
                    otherBlockValue, "", fieldIndex),
                literalMembers[fieldIndex],
                literalAlign.alignmentAtOffset(getDirectBlockLiteralFieldOffset(
                    literalMembers, packedLiteral, fieldIndex)));
            switch (helperKind) {
            case DirectBlockCaptureEntityKind::CXXRecord: {
              const auto *copyCtorExpr =
                  cast<CXXConstructExpr>(capture.getCopyExpr()->IgnoreImplicit());
              const auto *ctor = copyCtorExpr->getConstructor();
              const auto *ctorProto =
                  ctor->getType()->castAs<FunctionProtoType>();
              // Use a RunCleanupsScope to ensure temporaries created for
              // default arguments (e.g. const S &Silly = "silly") are
              // destroyed after the constructor call, matching OGCG's
              // EmitSynthesizedCXXCopyCtor.
              RunCleanupsScope scope(helperCGF);
              CallArgList args;
              args.add(RValue::get(field.getPointer()), ctor->getThisType());
              args.add(RValue::get(srcField.getPointer()),
                       ctor->getParamDecl(0)->getType());
              // Emit remaining arguments (default arguments) from the
              // CXXConstructExpr, matching OGCG's
              // EmitSynthesizedCXXCopyCtorCall.
              helperCGF.emitCallArgs(
                  args, ctorProto, drop_begin(copyCtorExpr->arguments(), 1),
                  copyCtorExpr->getConstructor(), /*paramsToSkip=*/1);
              helperCGF.emitCXXConstructorCall(
                  ctor, Ctor_Complete, /*forVirtualBase=*/false,
                  /*delegating=*/false, field, args,
                  AggValueSlot::DoesNotOverlap, copyCtorExpr->getExprLoc());
              break;
            }
            case DirectBlockCaptureEntityKind::AddressDiscriminatedPointerAuth:
              helperCGF.emitPointerAuthCopy(capturePointerAuth, captureFieldTy,
                                            field, srcField);
              break;
            case DirectBlockCaptureEntityKind::ARCWeak:
              helperCGF.emitARCCopyWeak(field, srcField);
              break;
            case DirectBlockCaptureEntityKind::ARCStrong: {
              mlir::Value srcValue = moduleBuilder.createAlignedLoad(
                  loc, literalMembers[fieldIndex], srcField.getPointer(),
                  srcField.getAlignment());
              if (cgm.getCodeGenOpts().OptimizationLevel == 0) {
                mlir::Value nullValue =
                    moduleBuilder.getNullValue(literalMembers[fieldIndex], loc);
                moduleBuilder.createStore(loc, nullValue, field);
                helperCGF.emitARCStoreStrongCall(field, srcValue, loc);
              } else {
                helperCGF.emitARCRetainNonBlock(srcValue, loc);
              }
              break;
            }
            case DirectBlockCaptureEntityKind::NonTrivialCStruct: {
              CIRGenFunction::SourceLocRAIIObject scope(helperCGF, loc);
              helperCGF.emitSupportedNonTrivialCStructCopyConstructor(
                  field, srcField, captureFieldTy);
              break;
            }
            case DirectBlockCaptureEntityKind::BlockObject: {
              mlir::Value dstPtr = field.getPointer();
              if (dstPtr.getType() != voidPtrTy)
                dstPtr = moduleBuilder.createBitcast(loc, dstPtr, voidPtrTy);
              unsigned fieldFlags = helperFlags.getBitMask();
              mlir::Value srcValue =
                  (fieldFlags & kBlockFieldIsByref)
                      ? moduleBuilder.createAlignedLoad(loc, voidPtrTy,
                                                        srcField.getPointer(),
                                                        srcField.getAlignment())
                      : moduleBuilder.createAlignedLoad(
                            loc, literalMembers[fieldIndex],
                            srcField.getPointer(), srcField.getAlignment());
              if (srcValue.getType() != voidPtrTy)
                srcValue =
                    moduleBuilder.createBitcast(loc, srcValue, voidPtrTy);
              helperCGF.emitRuntimeCall(
                  loc, runtimeFn,
                  {dstPtr, srcValue,
                   moduleBuilder.getConstInt(loc, int32Ty, fieldFlags)});
              break;
            }
            case DirectBlockCaptureEntityKind::None:
              llvm_unreachable("trivial direct block copy helper capture");
            }
          } else {
            switch (helperKind) {
            case DirectBlockCaptureEntityKind::CXXRecord: {
              QualType::DestructionKind dtorKind =
                  captureFieldTy.isDestructedType();
              if (dtorKind != QualType::DK_none) {
                assert(dtorKind == QualType::DK_cxx_destructor &&
                       "expected non-trivial C++ direct block capture");
                helperCGF.emitDestroy(field, captureFieldTy,
                                      helperCGF.getDestroyer(dtorKind),
                                      /*useEHCleanupForArray=*/false);
              }
              break;
            }
            case DirectBlockCaptureEntityKind::AddressDiscriminatedPointerAuth:
              continue;
            case DirectBlockCaptureEntityKind::ARCWeak:
              helperCGF.emitARCDestroyWeak(field);
              break;
            case DirectBlockCaptureEntityKind::ARCStrong:
              helperCGF.emitARCDestroyStrong(field, loc, /*isImprecise=*/true);
              break;
            case DirectBlockCaptureEntityKind::NonTrivialCStruct:
              helperCGF.emitDestroy(
                  field, captureFieldTy,
                  helperCGF.getDestroyer(QualType::DK_nontrivial_c_struct),
                  /*useEHCleanupForArray=*/false);
              break;
            case DirectBlockCaptureEntityKind::BlockObject: {
              unsigned fieldFlags = helperFlags.getBitMask();
              mlir::Value srcValue =
                  (fieldFlags & kBlockFieldIsByref)
                      ? moduleBuilder.createAlignedLoad(
                            loc, voidPtrTy, field.getPointer(),
                            field.getAlignment())
                      : moduleBuilder.createAlignedLoad(
                            loc, literalMembers[fieldIndex], field.getPointer(),
                            field.getAlignment());
              if (srcValue.getType() != voidPtrTy)
                srcValue =
                    moduleBuilder.createBitcast(loc, srcValue, voidPtrTy);
              helperCGF.emitRuntimeCall(
                  loc, runtimeFn,
                  {srcValue,
                   moduleBuilder.getConstInt(loc, int32Ty, fieldFlags)});
              break;
            }
            case DirectBlockCaptureEntityKind::None:
              llvm_unreachable("trivial direct block dispose helper capture");
            }
          }
          // Copy helper: after each capture is copied into the destination,
          // register an EH-only cleanup that disposes it. If a LATER capture's
          // copy-constructor throws during _Block_copy, all previously-copied
          // captures (C++ objects, ARC references, byref/BlockObject boxes,
          // non-trivial C structs) are released during unwinding — matching
          // classic pushCaptureCleanup(ForCopyHelper). Only C++ record copies
          // throw, so these cleanups run only on that unwind path and are
          // deactivated on normal completion. PointerAuth/None need no disposal.
          if (isCopy &&
              helperKind != DirectBlockCaptureEntityKind::None &&
              helperKind !=
                  DirectBlockCaptureEntityKind::AddressDiscriminatedPointerAuth)
            helperCGF.ehStack.pushCleanup<DirectBlockCaptureCopyCleanup>(
                EHCleanup, helperKind, field, captureFieldTy,
                captureInfo.disposeFlags.getBitMask(), blockObjectDisposeFn,
                loc);
        }
        // Pop the copy-helper EH cleanups before emitting the return. On the
        // normal (non-throwing) path these EH-only cleanups are deactivated, so
        // the successfully-copied captures survive; they run only on unwind.
        if (copyHelperEHScope)
          copyHelperEHScope.reset();
        cir::ReturnOp::create(moduleBuilder, loc);
      };

      emitHelperBody(copyHelper, /*isCopy=*/true);
      emitHelperBody(destroyHelper, /*isCopy=*/false);
    }

    // Whether the block decl is one of the enclosing return expression's
    // cleanup objects. In that case a captured object's lifetime ends after the
    // full expression (plain pushDestroy); otherwise it is extended to the end
    // of the scope enclosing the block expression (pushLifetimeExtendedDestroy).
    // Mirrors OGCG CGBlocks.cpp:1089-1107 (IsBlockDeclInRetExpr).
    const bool blockDeclInRetExpr = [&] {
      const auto *ewc = dyn_cast_or_null<ExprWithCleanups>(retExpr);
      if (ewc)
        for (const auto &obj : ewc->getObjects())
          if (const auto *bd = obj.dyn_cast<BlockDecl *>())
            if (bd == blockDecl)
              return true;
      return false;
    }();

    // Does any capture require a destructor cleanup? If so and its lifetime is
    // extended to the enclosing scope, that cleanup runs outside the block
    // literal's own full-expression scope, so — exactly as in the conditional-
    // branch case — the block storage and the capture field addresses must be
    // materialized at the function entry to dominate the enclosing-scope
    // cleanup.
    auto captureIsDestructed = [&](const BlockDecl::Capture &capture) {
      if (capture.isEscapingByref())
        return false;
      QualType captureTy = capture.getVariable()->getType();
      if (isSupportedObjCStrongBlockPointerDirectBlockCapture(captureTy) ||
          isSupportedObjCStrongDirectBlockCapture(captureTy) ||
          isSupportedObjCWeakDirectBlockCapture(captureTy))
        return true;
      const FieldDecl *lambdaCaptureField =
          capture.isNested() ? lambdaCaptureFields.lookup(capture.getVariable())
                             : nullptr;
      QualType captureFieldTy =
          getDirectBlockCaptureFieldType(capture, lambdaCaptureField);
      if (isSupportedCXXDirectBlockCapture(capture))
        return captureFieldTy.isDestructedType() != QualType::DK_none;
      // A by-value non-trivial C struct capture needs its captured copy
      // destroyed at the enclosing scope, just like a C++ record capture.
      if (captureFieldTy.isDestructedType() == QualType::DK_nontrivial_c_struct)
        return true;
      return false;
    };
    const bool hoistBlockToEntry =
        isInConditionalBranch() ||
        (!blockDeclInRetExpr &&
         llvm::any_of(blockDecl->captures(), captureIsDestructed));

    mlir::OpBuilder::InsertPoint blockAllocaIP;
    if (hoistBlockToEntry)
      blockAllocaIP = builder.getBestAllocaInsertPoint(getCurFunctionEntryBlock());
    Address blockAddr =
        hoistBlockToEntry
            ? createTempAlloca(literalTy, literalAlign, loc, "block",
                               /*arraySize=*/nullptr, /*allocaAddr=*/nullptr,
                               blockAllocaIP)
            : createTempAlloca(literalTy, literalAlign, loc, "block");
    SmallVector<Address, 8> captureFieldAddrs;
    if (hoistBlockToEntry) {
      mlir::OpBuilder::InsertionGuard guard(builder);
      builder.restoreInsertionPoint(blockAllocaIP);
      for (const auto &[index, capture] : llvm::enumerate(blockDecl->captures()))
        captureFieldAddrs.push_back(
            fieldAddr(blockAddr, firstCaptureFieldIndex + index));
    }
    auto storeAttr = [&](mlir::Value value, unsigned fieldIndex) {
      Address addr = fieldAddr(blockAddr, fieldIndex);
      builder.createStore(loc, value, addr, /*isVolatile=*/false,
                          /*isNontemporal=*/false,
                          builder.getAlignmentAttr(addr.getAlignment().getAsAlign().value()));
    };
    auto storeSignedAttr = [&](mlir::Value value, unsigned fieldIndex,
                               const PointerAuthSchema &schema, GlobalDecl decl,
                               QualType ty) {
      Address addr = fieldAddr(blockAddr, fieldIndex);
      if (CIRGenRuntimePointerAuthInfo pointerAuth =
              getBlockHeaderPointerAuthInfo(*this, schema, addr, decl, ty))
        value = emitPointerAuthSign(pointerAuth, value);
      builder.createStore(loc, value, addr, /*isVolatile=*/false,
                          /*isNontemporal=*/false,
                          builder.getAlignmentAttr(
                              addr.getAlignment().getAsAlign().value()));
    };
    // Using a plain pushDestroy in the normal (non-return-expr) case would
    // schedule the capture's cleanup on the innermost full-expression scope (the
    // block literal initializer), destroying the captured copy before the block
    // is used on a later statement (use-after-destroy). Defer to the enclosing
    // scope via pushLifetimeExtendedDestroy instead; it also handles the
    // conditional-branch case internally (CIRGenDecl.cpp). Only when the block
    // decl is in the enclosing return expression do we end the lifetime after
    // the full expression (plain pushDestroy), mirroring OGCG CGBlocks.cpp:1103.
    auto pushDirectBlockCaptureDestroy =
        [&](QualType::DestructionKind dtorKind, Address addr, QualType ty) {
          if (blockDeclInRetExpr)
            pushDestroy(dtorKind, addr, ty);
          else
            pushLifetimeExtendedDestroy(dtorKind, addr, ty);
        };

    auto invokeView = builder.getConstant(
        loc, cir::GlobalViewAttr::get(
                 builder.getPointerTo(invokeFn.getFunctionType()),
                 mlir::FlatSymbolRefAttr::get(&getMLIRContext(),
                                             invokeFn.getSymNameAttr())));
    auto isaView = builder.getConstant(
        loc, moduleBuilder.getGlobalViewAttr(
                 moduleBuilder.getPointerTo(isaGlobal.getSymType()), isaGlobal));
    auto descView = builder.getConstant(
        loc, moduleBuilder.getGlobalViewAttr(
                 moduleBuilder.getPointerTo(descTy), descGlobal));

    storeAttr(isaView, 0);
    storeAttr(builder.getConstInt(loc, int32Ty, flags), 1);
    storeAttr(builder.getConstInt(loc, int32Ty, 0), 2);
    storeSignedAttr(builder.createBitcast(loc, invokeView, voidPtrTy), 3,
                    cgm.getCodeGenOpts()
                        .PointerAuth.BlockInvocationFunctionPointers,
                    GlobalDecl(), e->getType()->castAs<BlockPointerType>()
                                      ->getPointeeType());
    storeSignedAttr(descView, 4,
                    cgm.getCodeGenOpts().PointerAuth.BlockDescriptorPointers,
                    GlobalDecl(), QualType());

    if (capturesCXXThis) {
      Address thisField = fieldAddr(blockAddr, thisFieldIndex);
      mlir::Value thisValue = loadCXXThis();
      if (thisValue.getType() != thisField.getElementType())
        thisValue = builder.createBitcast(loc, thisValue,
                                          thisField.getElementType());
      builder.createStore(loc, thisValue, thisField);
    }

    for (const auto &[index, capture] : llvm::enumerate(blockDecl->captures())) {
      const VarDecl *variable = capture.getVariable();
      Address addr = captureFieldAddrs.empty()
                         ? fieldAddr(blockAddr, firstCaptureFieldIndex + index)
                         : captureFieldAddrs[index];
      if (capture.isEscapingByref()) {
        Address byrefAddr = getAddrOfLocalVar(variable);
        mlir::Value byrefPtr = byrefAddr.getPointer();
        if (byrefPtr.getType() != voidPtrTy)
          byrefPtr = builder.createBitcast(loc, byrefPtr, voidPtrTy);
        builder.createStore(loc, byrefPtr, addr);
        continue;
      }

      QualType captureTy = variable->getType();
      const FieldDecl *lambdaCaptureField =
          capture.isNested() ? lambdaCaptureFields.lookup(variable) : nullptr;
      QualType captureFieldTy =
          getDirectBlockCaptureFieldType(capture, lambdaCaptureField);
      if (isSupportedObjCStrongBlockPointerDirectBlockCapture(captureTy)) {
        DeclRefExpr declRef(getContext(), const_cast<VarDecl *>(variable),
                            capture.isNested(), captureTy, VK_LValue,
                            SourceLocation());
        mlir::Value value =
            emitLoadOfLValue(emitDeclRefLValue(&declRef), SourceLocation())
                .getValue();
        value = emitARCRetainNonBlock(value, loc);
        if (value.getType() != addr.getElementType())
          value = builder.createBitcast(loc, value, addr.getElementType());
        builder.createStore(loc, value, addr);
        pushDirectBlockCaptureDestroy(QualType::DK_objc_strong_lifetime, addr,
                                      captureTy);
        continue;
      }

      if (isSupportedObjCStrongDirectBlockCapture(captureTy) ||
          isSupportedObjCWeakDirectBlockCapture(captureTy)) {
        DeclRefExpr declRef(getContext(), const_cast<VarDecl *>(variable),
                            capture.isNested(), captureTy, VK_LValue,
                            SourceLocation());
        ImplicitCastExpr l2r(ImplicitCastExpr::OnStack, captureTy,
                             CK_LValueToRValue, &declRef, VK_PRValue,
                             FPOptionsOverride());
        ImplicitParamDecl pseudoVar(getContext(), captureTy,
                                    ImplicitParamKind::Other);
        emitExprAsInit(&l2r, &pseudoVar, makeAddrLValue(addr, captureTy),
                       /*capturedByInit=*/false);
        pushDirectBlockCaptureDestroy(
            isSupportedObjCWeakDirectBlockCapture(captureTy)
                ? QualType::DK_objc_weak_lifetime
                : QualType::DK_objc_strong_lifetime,
            addr, captureTy);
        continue;
      }

      if (isSupportedCXXDirectBlockCapture(capture)) {
        DeclRefExpr declRef(getContext(), const_cast<VarDecl *>(variable),
                            capture.isNested(), captureFieldTy, VK_LValue,
                            SourceLocation());
        Address srcField = emitDeclRefLValue(&declRef).getAddress();
        const auto *copyCtorExpr =
            cast<CXXConstructExpr>(capture.getCopyExpr()->IgnoreImplicit());
        const auto *ctor = copyCtorExpr->getConstructor();
        const auto *ctorProto =
            ctor->getType()->castAs<FunctionProtoType>();
        {
          // Use a RunCleanupsScope to ensure temporaries created for
          // default arguments are destroyed after the constructor call,
          // matching OGCG's EmitSynthesizedCXXCopyCtor.
          RunCleanupsScope scope(*this);
          CallArgList args;
          args.add(RValue::get(addr.getPointer()), ctor->getThisType());
          args.add(RValue::get(srcField.getPointer()),
                   ctor->getParamDecl(0)->getType());
          // Emit remaining arguments (default arguments) from the
          // CXXConstructExpr, matching OGCG's
          // EmitSynthesizedCXXCopyCtorCall.
          emitCallArgs(args, ctorProto,
                       drop_begin(copyCtorExpr->arguments(), 1),
                       copyCtorExpr->getConstructor(), /*paramsToSkip=*/1);
          emitCXXConstructorCall(ctor, Ctor_Complete, /*forVirtualBase=*/false,
                                 /*delegating=*/false, addr, args,
                                 AggValueSlot::DoesNotOverlap,
                                 copyCtorExpr->getExprLoc());
        }
        if (QualType::DestructionKind dtorKind =
                captureFieldTy.isDestructedType();
            dtorKind != QualType::DK_none)
          pushDirectBlockCaptureDestroy(dtorKind, addr, captureFieldTy);
        continue;
      }

      // A by-value non-trivial C struct (e.g. an ObjC-ARC struct with a
      // __strong/__weak member): copy-construct it into the block storage and
      // schedule the captured copy's destruction at the enclosing scope,
      // mirroring OGCG's enterBlockScope. Without this the captured members are
      // neither retained on copy nor released on destroy (leak / UAF).
      if (captureFieldTy.isNonTrivialToPrimitiveCopy() == QualType::PCK_Struct ||
          captureFieldTy.isDestructedType() ==
              QualType::DK_nontrivial_c_struct) {
        DeclRefExpr declRef(getContext(), const_cast<VarDecl *>(variable),
                            capture.isNested(), captureFieldTy, VK_LValue,
                            SourceLocation());
        Address srcField = emitDeclRefLValue(&declRef).getAddress();
        {
          SourceLocRAIIObject scope(*this, loc);
          emitSupportedNonTrivialCStructCopyConstructor(addr, srcField,
                                                        captureFieldTy);
        }
        if (captureFieldTy.isDestructedType() ==
            QualType::DK_nontrivial_c_struct)
          pushDirectBlockCaptureDestroy(QualType::DK_nontrivial_c_struct, addr,
                                        captureFieldTy);
        continue;
      }

      if (captureFieldTy->isReferenceType() && !captureTy->isReferenceType()) {
        DeclRefExpr declRef(getContext(), const_cast<VarDecl *>(variable),
                            capture.isNested(), captureTy, VK_LValue,
                            SourceLocation());
        mlir::Value refValue = emitDeclRefLValue(&declRef).getPointer();
        mlir::Type fieldTy = addr.getElementType();
        if (refValue.getType() != fieldTy)
          refValue = builder.createBitcast(loc, refValue, fieldTy);
        builder.createStore(loc, refValue, addr);
        continue;
      }

      DeclRefExpr declRef(getContext(), const_cast<VarDecl *>(variable),
                          capture.isNested(),
                          captureTy->isReferenceType()
                              ? captureTy.getNonReferenceType()
                              : captureTy,
                          VK_LValue,
                          SourceLocation());
      if (captureTy->isReferenceType()) {
        mlir::Value refValue = emitDeclRefLValue(&declRef).getPointer();
        mlir::Type fieldTy = addr.getElementType();
        if (refValue.getType() != fieldTy)
          refValue = builder.createBitcast(loc, refValue, fieldTy);
        builder.createStore(loc, refValue, addr);
        continue;
      }
      emitAnyExprToMem(&declRef, addr, Qualifiers(), /*isInit=*/true);
    }

    return builder.createBitcast(
        loc, blockAddr.getPointer(),
        mlir::cast<cir::PointerType>(convertType(e->getType())));
  }

  return builder.getConstant(
      loc, moduleBuilder.getGlobalViewAttr(mlir::cast<cir::PointerType>(
               convertType(e->getType())),
                                           literalGlobal));
}

RValue CIRGenFunction::emitBlockCallExpr(const CallExpr *e,
                                         ReturnValueSlot returnValue,
                                         bool ignoreResult) {
  const auto *calleeType = e->getCallee()->getType()->getAs<BlockPointerType>();
  assert(calleeType && "expected block pointer callee");
  const auto *blockType = calleeType->getPointeeType()->castAs<FunctionType>();
  const auto *blockProto = dyn_cast<FunctionProtoType>(blockType);

  mlir::Location loc = getLoc(e->getExprLoc());
  mlir::Value blockPtr = emitScalarExpr(e->getCallee());
  auto headerTy = builder.getAnonRecordTy(
      {builder.getPointerTo(builder.getVoidPtrTy()), builder.getSInt32Ty(),
       builder.getSInt32Ty(), builder.getVoidPtrTy(), builder.getVoidPtrTy()});
  mlir::Value headerPtr =
      builder.createBitcast(blockPtr, builder.getPointerTo(headerTy));
  mlir::Value invokeSlot = builder.createGetMember(
      loc, builder.getPointerTo(builder.getVoidPtrTy()), headerPtr, "", 3);
  mlir::Value rawInvokePtr = builder.createAlignedLoad(
      loc, builder.getVoidPtrTy(), invokeSlot, cgm.getPointerAlign());

  CallArgList args;
  args.add(RValue::get(blockPtr), getContext().VoidPtrTy);
  emitCallArgs(args, blockProto, e->arguments(), e->getDirectCallee(),
               /*ParamsToSkip=*/0, EvaluationOrder::Default);

  const CIRGenFunctionInfo &funcInfo =
      getTypes().arrangeBlockFunctionCall(args, blockType);
  cir::FuncType invokeTy = getTypes().getFunctionType(funcInfo);
  mlir::Value invokePtr =
      builder.createBitcast(rawInvokePtr, builder.getPointerTo(invokeTy));
  CIRGenCallee callee(CIRGenCalleeInfo(blockProto), invokePtr.getDefiningOp());
  return emitCall(funcInfo, callee, returnValue, args, nullptr, loc,
                  /*isMustTail=*/false);
}

RValue CIRGenFunction::emitCallExpr(const clang::CallExpr *e,
                                    ReturnValueSlot returnValue) {
  return emitCallExpr(e, returnValue, /*ignoreResult=*/false);
}

RValue CIRGenFunction::emitCallExpr(const clang::CallExpr *e,
                                    ReturnValueSlot returnValue,
                                    bool ignoreResult) {
  if (e->getCallee()->getType()->isBlockPointerType())
    return emitBlockCallExpr(e, returnValue, ignoreResult);

  if (const auto *ce = dyn_cast<CXXMemberCallExpr>(e))
    return emitCXXMemberCallExpr(ce, returnValue);

  if (const auto *ce = dyn_cast<CUDAKernelCallExpr>(e)) {
    return getCIRGenModule().getCUDARuntime().emitCUDAKernelCallExpr(
        *this, ce, returnValue);
  }

  if (const auto *operatorCall = dyn_cast<CXXOperatorCallExpr>(e)) {
    // If the callee decl is a CXXMethodDecl with an implicit object parameter,
    // we need to emit this as a C++ operator member call.
    if (const CXXMethodDecl *md =
            dyn_cast_or_null<CXXMethodDecl>(operatorCall->getCalleeDecl());
        md && md->isImplicitObjectMemberFunction())
      return emitCXXOperatorMemberCallExpr(operatorCall, md, returnValue);
    // A CXXOperatorCallExpr is created even for explicit object methods, but
    // these should be treated like static function calls. Fall through to do
    // that.
  }

  CIRGenCallee callee = emitCallee(e->getCallee());

  // Handle CUDA device-side printf: transform to vprintf.
  if (getTarget().getTriple().isNVPTX()) {
    if (const auto *fd = e->getDirectCallee()) {
      if (unsigned builtinID = fd->getBuiltinID()) {
        if (builtinID == Builtin::BIprintf ||
            builtinID == Builtin::BI__builtin_printf)
          return RValue::get(emitNVPTXDevicePrintfCallExpr(e));
      }
    }
  }

  if (callee.isBuiltin())
    return emitBuiltinExpr(callee.getBuiltinDecl(), callee.getBuiltinID(), e,
                           returnValue, ignoreResult);

  if (callee.isPseudoDestructor())
    return emitCXXPseudoDestructorExpr(callee.getPseudoDestructorExpr());

  cir::CIRCallOpInterface callOp;
  RValue result =
      emitCall(e->getCallee()->getType(), callee, e, returnValue, &callOp);
  if (callOp && isDeferredCleanupDispatchCandidate(e)) {
    // Carry a per-CallExpr key so the post-lowering cleanup-dispatch fold only
    // merges duplicates from the same `_Defer` body, not unrelated identical
    // deferred calls in the same function.
    callOp->setAttr(
        "cir.defer_cleanup_dispatch_candidate",
        mlir::IntegerAttr::get(mlir::IntegerType::get(&getMLIRContext(), 64),
                               e->getExprLoc().getRawEncoding()));
  }
  return result;
}

/// Emit code to compute the specified expression, ignoring the result.
void CIRGenFunction::emitIgnoredExpr(const Expr *e) {
  if (e->isPRValue()) {
    emitAnyExpr(e, AggValueSlot::ignored(), /*ignoreResult=*/true);
    return;
  }

  // If this is a bitfield-resulting conditional operator, we can special case
  // emit this. The normal 'emitLValue' version of this is particularly
  // difficult to codegen for, since creating a single "LValue" for two
  // different sized arguments here is not particularly doable.
  if (const auto *condOp = dyn_cast<AbstractConditionalOperator>(
          e->IgnoreParenNoopCasts(getContext()))) {
    if (condOp->getObjectKind() == OK_BitField)
      return emitIgnoredConditionalOperator(condOp);
  }

  // Just emit it as an l-value and drop the result.
  emitLValue(e);
}

Address CIRGenFunction::emitArrayToPointerDecay(const Expr *e,
                                                LValueBaseInfo *baseInfo,
                                                TBAAAccessInfo *tbaaInfo) {
  assert(e->getType()->isArrayType() &&
         "Array to pointer decay must have array source type!");

  // Expressions of array type can't be bitfields or vector elements.
  LValue lv = emitLValue(e);
  Address addr = lv.getAddress();

  // If the array type was an incomplete type, we need to make sure
  // the decay ends up being the right type.
  auto lvalueAddrTy = mlir::cast<cir::PointerType>(addr.getPointer().getType());

  if (e->getType()->isVariableArrayType()) {
    // VLA pointers are already decayed, so we don't need to call
    // maybeBuildArrayDecay. But we still need to set the correct TBAA
    // info (element type, not array type) and base info, matching
    // OGCG which falls through to the shared path.
    QualType eltType = e->getType()->castAsArrayTypeUnsafe()->getElementType();
    if (baseInfo)
      *baseInfo = lv.getBaseInfo();
    if (tbaaInfo)
      *tbaaInfo = cgm.getTBAAAccessInfo(eltType);
    return addr;
  }

  [[maybe_unused]] auto pointeeTy =
      mlir::cast<cir::ArrayType>(lvalueAddrTy.getPointee());

  [[maybe_unused]] mlir::Type arrayTy = convertType(e->getType());
  assert(mlir::isa<cir::ArrayType>(arrayTy) && "expected array");
  assert(pointeeTy == arrayTy);

  // The result of this decay conversion points to an array element within the
  // base lvalue. However, since TBAA currently does not support representing
  // accesses to elements of member arrays, we conservatively represent accesses
  // to the pointee object as if it had no any base lvalue specified.
  // TODO: Support TBAA for member arrays.
  QualType eltType = e->getType()->castAsArrayTypeUnsafe()->getElementType();
  if (baseInfo)
    *baseInfo = lv.getBaseInfo();
  if (tbaaInfo)
    *tbaaInfo = cgm.getTBAAAccessInfo(eltType);

  mlir::Value ptr = builder.maybeBuildArrayDecay(
      cgm.getLoc(e->getSourceRange()), addr.getPointer(),
      convertTypeForMem(eltType));
  return Address(ptr, addr.getAlignment());
}

/// Given the address of a temporary variable, produce an r-value of its type.
RValue CIRGenFunction::convertTempToRValue(Address addr, clang::QualType type,
                                           clang::SourceLocation loc) {
  LValue lvalue = makeAddrLValue(addr, type, AlignmentSource::Decl);
  switch (getEvaluationKind(type)) {
  case cir::TEK_Complex:
    return RValue::getComplex(emitLoadOfComplex(lvalue, loc));
  case cir::TEK_Aggregate:
    return lvalue.asAggregateRValue();
  case cir::TEK_Scalar:
    return RValue::get(emitLoadOfScalar(lvalue, loc));
  }
  llvm_unreachable("bad evaluation kind");
}

/// Emit an `if` on a boolean condition, filling `then` and `else` into
/// appropriated regions.
mlir::LogicalResult CIRGenFunction::emitIfOnBoolExpr(const Expr *cond,
                                                     const Stmt *thenS,
                                                     const Stmt *elseS,
                                                     Stmt::Likelihood LH,
                                                     std::optional<int32_t>
                                                         hlslControlFlowHint,
                                                     const Stmt *pgoCounterStmt) {
  mlir::Location thenLoc = getLoc(thenS->getSourceRange());
  std::optional<mlir::Location> elseLoc;
  if (elseS)
    elseLoc = getLoc(elseS->getSourceRange());
  mlir::Location bodyLoc =
      elseLoc ? mlir::FusedLoc::get(&getMLIRContext(), {thenLoc, *elseLoc})
              : thenLoc;
  std::optional<mlir::Location> ifLocOverride;
  if (shouldEmitSourceAtoms())
    ifLocOverride = getLoc(cond->getSourceRange());

  if (containsAnyVarDecl(thenS) || containsAnyVarDecl(elseS)) {
    mlir::Value condV = emitOpOnBoolExpr(bodyLoc, cond->IgnoreParens());
    condV = emitCondLikelihoodViaExpectIntrinsic(condV, LH);
    closeActiveSyntheticTry();
    uint64_t branchAtom = reserveSourceAtomGroup();

    mlir::LogicalResult resThen = mlir::success();
    mlir::LogicalResult resElse = mlir::success();
    auto ifOp = cir::IfOp::create(
        builder, ifLocOverride.value_or(bodyLoc), condV, elseLoc.has_value(),
        /*thenBuilder=*/
        [&](mlir::OpBuilder &, mlir::Location) {
          LexicalScope lexScope{*this, thenLoc, builder.getInsertionBlock()};
          // Record the then-branch source range so that a goto to a label
          // inside this branch is recognized as staying within the scope.
          // Without this, emitGotoStmt's forward-goto walk sees an invalid
          // source range, assumes the goto exits this scope, and emits the
          // branch's RAII cleanups on the goto edge, double-destroying them
          // against the normal scope-exit cleanup.
          lexScope.setClangSourceRange(thenS->getSourceRange());
          if (pgoCounterStmt)
            incrementProfileCounter(pgoCounterStmt);
          resThen = emitStmt(thenS, /*useCurrentScope=*/true);
        },
        /*elseBuilder=*/
        [&](mlir::OpBuilder &, mlir::Location) {
          assert(elseLoc && "Invalid location for elseS.");
          LexicalScope lexScope{*this, *elseLoc, builder.getInsertionBlock()};
          lexScope.setClangSourceRange(elseS->getSourceRange());
          resElse = emitStmt(elseS, /*useCurrentScope=*/true);
        });
    addOpToSpecificSourceAtom(ifOp, condV, branchAtom);
    if (hlslControlFlowHint)
      ifOp->setAttr("cir.hlsl.controlflow.hint",
                    mlir::IntegerAttr::get(
                        mlir::IntegerType::get(&getMLIRContext(), 32),
                        *hlslControlFlowHint));
    return mlir::LogicalResult::success(resThen.succeeded() &&
                                        resElse.succeeded());
  }

  mlir::LogicalResult resThen = mlir::success(), resElse = mlir::success();
  emitIfOnBoolExpr(
      cond, /*thenBuilder=*/
      [&](mlir::OpBuilder &, mlir::Location) {
        LexicalScope lexScope{*this, thenLoc, builder.getInsertionBlock()};
        // See the var-decl path above: record the branch source range so a
        // goto to a label inside the branch is not treated as exiting this
        // scope, which would double-destroy the branch's RAII objects.
        lexScope.setClangSourceRange(thenS->getSourceRange());
        if (pgoCounterStmt)
          incrementProfileCounter(pgoCounterStmt);
        resThen = emitStmt(thenS, /*useCurrentScope=*/true);
      },
      thenLoc,
      /*elseBuilder=*/
      [&](mlir::OpBuilder &, mlir::Location) {
        assert(elseLoc && "Invalid location for elseS.");
        LexicalScope lexScope{*this, *elseLoc, builder.getInsertionBlock()};
        lexScope.setClangSourceRange(elseS->getSourceRange());
        resElse = emitStmt(elseS, /*useCurrentScope=*/true);
      },
      elseLoc, LH, hlslControlFlowHint, std::nullopt, ifLocOverride);

  return mlir::LogicalResult::success(resThen.succeeded() &&
                                      resElse.succeeded());
}

/// Emit an `if` on a boolean condition, filling `then` and `else` into
/// appropriated regions.
cir::IfOp CIRGenFunction::emitIfOnBoolExpr(
    const clang::Expr *cond, BuilderCallbackRef thenBuilder,
    mlir::Location thenLoc, BuilderCallbackRef elseBuilder,
    std::optional<mlir::Location> elseLoc, Stmt::Likelihood LH,
    std::optional<int32_t> hlslControlFlowHint,
    std::optional<uint64_t> sourceAtomGroup,
    std::optional<mlir::Location> ifLocOverride) {
  cond = cond->IgnoreParens();

  // Avoid materializing temporary boolean values for short-circuit conditions
  // when directly emitting control flow. This matches OGCG's branch-oriented
  // emission and prevents extra phi nodes on patterns like `if (a && b)`.
  //
  // Under clang profile instrumentation this short-circuit nesting is skipped:
  // it does not increment the &&/|| region (and condition) counters, so a
  // do-nothing branch path would leave those counters at zero (the body counter
  // and condition counts would be wrong). Falling through to the general path
  // materializes the condition via the scalar emitter (VisitBinLAnd/LOr), which
  // instruments the counters correctly while still short-circuiting RHS
  // evaluation through a cir.ternary.
  bool condConstant = false;
  if (const auto *bo = dyn_cast<BinaryOperator>(cond);
      bo && !constantFoldsToBool(cond, condConstant) &&
      !cgm.getCodeGenOpts().hasProfileClangInstr()) {
    // For && short-circuit conditions, only use the recursive if-nesting
    // approach when there is NO else branch. When there IS an else branch,
    // the nesting duplicates the entire else body at each && level, causing
    // exponential blowup on if-else-if chains (e.g., OpenCV's integral_SIMD
    // with 12 branches × 3 && conditions = 3^12 = 531K duplicated else
    // regions, >15GB memory). `||` never takes the nesting path -- see below.
    //
    // When there IS an else branch, fall through to the general case which
    // materializes the boolean condition and uses a single cir.if with one
    // copy of the else body.
    if (!elseLoc.has_value()) {
      bool rhsConstant = false;
      if (constantFoldsToBool(bo->getRHS(), rhsConstant)) {
        // Fold branchless short-circuit tails to avoid building an extra
        // nested cir.if for patterns like `if (cond && true)` and
        // `if (cond || false)`. This preserves short-circuit side effects and
        // keeps CIR structure aligned with the logical condition.
        if ((bo->getOpcode() == BO_LAnd && rhsConstant) ||
            (bo->getOpcode() == BO_LOr && !rhsConstant))
          return emitIfOnBoolExpr(bo->getLHS(), thenBuilder, thenLoc,
                                  elseBuilder, elseLoc, LH,
                                  hlslControlFlowHint, sourceAtomGroup);
      }

      if (bo->getOpcode() == BO_LAnd) {
        uint64_t lhsAtomGroup = sourceAtomGroup.value_or(reserveSourceAtomGroup());
        return emitIfOnBoolExpr(
            bo->getLHS(),
            /*thenBuilder=*/
            [&](mlir::OpBuilder &b, mlir::Location branchLoc) {
              emitIfOnBoolExpr(bo->getRHS(), thenBuilder, thenLoc, elseBuilder,
                               elseLoc, LH, hlslControlFlowHint);
              cir::YieldOp::create(b, branchLoc);
            },
            thenLoc, elseBuilder, elseLoc, LH, hlslControlFlowHint,
            lhsAtomGroup);
      }

      // `||` deliberately does NOT get the nesting treatment. `&&` can nest
      // because only the innermost level receives `thenBuilder`, so the then
      // statement is emitted exactly once. For `||` both the LHS then-branch
      // and the RHS then-branch are taken branches, so handing `thenBuilder`
      // to both emits the whole then statement twice -- and 2^N times for a
      // chain of N `||`s. OGCG avoids this by branching both arms to a single
      // shared `TrueBlock`; CIR has no shared-successor representation for a
      // `cir.if` nest, so until it does, fall through to the general path.
      //
      // This is not only a size cost. The duplicate gives a `static` callee
      // two call sites where the source had one, so LLVM's InlineCost stops
      // applying LastCallToStaticBonus (it requires hasOneLiveUse), and small
      // helpers survive out of line at -Oz. Later CFG tail-merging can re-merge
      // the duplicated bodies but cannot undo the inlining decision.
      //
      // The general path materializes the condition via VisitBinLOr, which
      // still short-circuits RHS evaluation through a cir.ternary, and emits
      // one copy of the body -- exactly what already happens whenever there is
      // an else clause.
    }
  }

  // A `?:` condition is not given the nested-branch treatment either, for the
  // same reason as `||` above, only more so: `thenBuilder` is handed to both
  // the true and the false arm, and so is `elseBuilder`, so a single
  // `if (c ? a : b)` duplicates both the then statement and the else
  // statement. The general path materializes the value via
  // VisitAbstractConditionalOperator and emits one copy of each. That also
  // keeps the conditional operator's region counter correct under clang
  // profile instrumentation, which the nested form did not increment.

  // Attempt to be as accurate as possible with IfOp location, generate
  // one fused location that has either 2 or 4 total locations, depending
  // on else's availability.
  mlir::Location bodyLoc =
      elseLoc ? mlir::FusedLoc::get(&getMLIRContext(), {thenLoc, *elseLoc})
              : thenLoc;
  mlir::Location loc = ifLocOverride.value_or(bodyLoc);

  // Emit the code with the fully general case.
  mlir::Value condV = emitOpOnBoolExpr(bodyLoc, cond);
  condV = emitCondLikelihoodViaExpectIntrinsic(condV, LH);
  closeActiveSyntheticTry();
  uint64_t branchAtom = sourceAtomGroup.value_or(reserveSourceAtomGroup());
  auto ifOp = cir::IfOp::create(builder, loc, condV, elseLoc.has_value(),
                                /*thenBuilder=*/thenBuilder,
                                /*elseBuilder=*/elseBuilder);
  addOpToSpecificSourceAtom(ifOp, condV, branchAtom);

  if (hlslControlFlowHint)
    ifOp->setAttr("cir.hlsl.controlflow.hint",
                  mlir::IntegerAttr::get(
                      mlir::IntegerType::get(&getMLIRContext(), 32),
                      *hlslControlFlowHint));

  // If the branch has a condition wrapped by __builtin_unpredictable,
  // mark the if op so that lowering can emit !unpredictable metadata.
  // Don't bother if not optimizing because that metadata would not be used.
  // Matches OGCG CodeGenFunction.cpp:2106-2115.
  if (cgm.getCodeGenOpts().OptimizationLevel != 0) {
    if (auto *call = dyn_cast<CallExpr>(cond->IgnoreImpCasts())) {
      if (auto *fd = dyn_cast_or_null<FunctionDecl>(call->getCalleeDecl())) {
        if (fd->getBuiltinID() == Builtin::BI__builtin_unpredictable)
          ifOp->setAttr("cir.unpredictable",
                        mlir::UnitAttr::get(&getMLIRContext()));
      }
    }
  }

  return ifOp;
}

mlir::Value CIRGenFunction::emitCondLikelihoodViaExpectIntrinsic(
    mlir::Value cond, Stmt::Likelihood LH) {
  if (LH == Stmt::LH_None)
    return cond;

  // Don't generate llvm.expect at -O0 as the backend won't use it.
  if (cgm.getCodeGenOpts().OptimizationLevel == 0)
    return cond;

  mlir::Location loc = cond.getLoc();

  // Apply cir.expect directly on the bool condition. This lowers to
  // llvm.expect.i1, matching OGCG which uses llvm.expect.i1 for
  // [[likely]]/[[unlikely]] attributes on if conditions.
  auto boolTy = builder.getBoolTy();
  mlir::Value expected =
      builder.getBool(LH == Stmt::LH_Likely, loc);
  return cir::ExpectOp::create(builder, loc, boolTy, cond, expected,
                               /*prob=*/mlir::FloatAttr{});
}

/// TODO(cir): see EmitBranchOnBoolExpr for extra ideas).
mlir::Value CIRGenFunction::emitOpOnBoolExpr(mlir::Location loc,
                                             const Expr *cond) {
  cond = cond->IgnoreParens();

  // OGCG reverses cond(!x, t, f) -> cond(x, f, t) for efficient codegen.
  // CIR intentionally does not apply this at CodeGen level to preserve
  // diagnostic accuracy; the optimization is deferred to LLVM lowering.

  // Under clang profile instrumentation, do not special-case a ?: condition
  // here: building the ternary directly skips the conditional operator's region
  // counter. Fall through to the scalar emitter (VisitAbstractConditionalOperator)
  // which instruments it while still evaluating only the taken branch.
  if (const ConditionalOperator *condOp = dyn_cast<ConditionalOperator>(cond);
      condOp && !cgm.getCodeGenOpts().hasProfileClangInstr()) {
    Expr *trueExpr = condOp->getTrueExpr();
    Expr *falseExpr = condOp->getFalseExpr();
    mlir::Value condV = emitOpOnBoolExpr(loc, condOp->getCond());

    mlir::Value ternaryOpRes =
        cir::TernaryOp::create(
            builder, loc, condV, /*thenBuilder=*/
            [this, trueExpr](mlir::OpBuilder &b, mlir::Location loc) {
              mlir::Value lhs = emitScalarExpr(trueExpr);
              cir::YieldOp::create(b, loc, lhs);
            },
            /*elseBuilder=*/
            [this, falseExpr](mlir::OpBuilder &b, mlir::Location loc) {
              mlir::Value rhs = emitScalarExpr(falseExpr);
              cir::YieldOp::create(b, loc, rhs);
            })
            .getResult();

    return emitScalarConversion(ternaryOpRes, condOp->getType(),
                                getContext().BoolTy, condOp->getExprLoc());
  }

  if (const CXXThrowExpr *throw_ = dyn_cast<CXXThrowExpr>(cond)) {
    // Conditional operator handling can give us a throw expression as a
    // condition for a case like:
    //   br(c ? throw x : y, t, f) -> br(c, br(throw x, t, f), br(y, t, f)
    // Emit the throw and return a dummy value — the throw terminates
    // execution so any subsequent use of this value is dead code.
    emitCXXThrowExpr(throw_);
    return builder.getBool(false, loc);
  }

  // __builtin_unpredictable detection is handled in emitIfOnBoolExpr where
  // the IfOp is created, rather than here in the condition evaluation.

  // Emit the code with the fully general case.
  return evaluateExprAsBool(cond);
}

mlir::Value CIRGenFunction::emitAlloca(StringRef name, mlir::Type ty,
                                       mlir::Location loc, CharUnits alignment,
                                       bool insertIntoFnEntryBlock,
                                       mlir::Value arraySize) {
  mlir::Block *entryBlock =
      (insertIntoFnEntryBlock || !curLexScope)
          ? getCurFunctionEntryBlock()
          : curLexScope->getEntryBlock();

  // If this is an alloca in the entry basic block of a cir.try and there's
  // a surrounding cir.scope, make sure the alloca ends up in the surrounding
  // scope instead. This is necessary in order to guarantee all SSA values are
  // reachable during cleanups.
  if (auto tryOp =
          llvm::dyn_cast_if_present<cir::TryOp>(entryBlock->getParentOp())) {
    mlir::Operation *tryParent = tryOp->getParentOp();
    if (auto scopeOp = llvm::dyn_cast_if_present<cir::ScopeOp>(tryParent))
      entryBlock = &scopeOp.getScopeRegion().front();
    else if (!tryParent)
      // TryOp::create builds detached regions before the op is attached to
      // the surrounding scope. Use function-entry storage so the alloca stays
      // reachable once the try is inserted.
      entryBlock = getCurFunctionEntryBlock();
  }

  // An alloca must not escape an omp.target region, which is IsolatedFromAbove
  // and is outlined into its own function during lowering. If we are emitting
  // inside such a region (nested below curFn) but the chosen entry block lies
  // outside it, the alloca would be referenced across the isolation boundary
  // and fail verification. Redirect it to the target region's entry block.
  // (Restricted to omp.target so other OpenMP regions, e.g. omp.task and
  // omp.taskloop, keep their existing alloca placement.)
  if (mlir::Block *insertBlock = builder.getInsertionBlock()) {
    for (mlir::Operation *parent = insertBlock->getParentOp();
         parent && parent != curFn; parent = parent->getParentOp()) {
      if (parent->getName().getStringRef() == "omp.target") {
        if (!parent->isAncestor(entryBlock->getParentOp()))
          entryBlock = &parent->getRegion(0).front();
        break;
      }
    }
  }

  return emitAlloca(name, ty, loc, alignment,
                    builder.getBestAllocaInsertPoint(entryBlock), arraySize);
}

mlir::Value CIRGenFunction::emitAlloca(StringRef name, mlir::Type ty,
                                       mlir::Location loc, CharUnits alignment,
                                       mlir::OpBuilder::InsertPoint ip,
                                       mlir::Value arraySize) {
  // Defensive: if `ty` is a default-constructed (null) mlir::Type, calling
  // builder.getPointerTo(ty, ...) → cir::PointerType::get(ty, ...) will
  // dereference ty.getContext() and crash deep inside MLIR's TypeUniquer
  // with no actionable diagnostic. This typically happens when an upstream
  // caller passed the result of CIRGenTypes::convertType() for a NYI type
  // class (which returns {} from its switch default). Convert this into a
  // visible NYI emission and return a null Value; the caller will surface
  // the diagnostic via the frontend's hasErrorOccurred path.
  if (!ty) {
    cgm.errorNYI(loc, "alloca for unconverted type");
    return {};
  }
  // CIR uses its own alloca address space rather than follow the target data
  // layout like original CodeGen. The data layout awareness should be done in
  // the lowering pass instead.
  cir::PointerType localVarPtrTy =
      builder.getPointerTo(ty, getCIRAllocaAddressSpace());
  mlir::IntegerAttr alignIntAttr = getSizeAttrCached(alignment);

  mlir::Value addr;
  {
    mlir::OpBuilder::InsertionGuard guard(builder);
    builder.restoreInsertionPoint(ip);
    addr = builder.createAlloca(loc, /*addr type*/ localVarPtrTy,
                                /*var type*/ ty, name, alignIntAttr, arraySize);
    assert(!cir::MissingFeatures::astVarDeclInterface());

    // Update the alloca insert point cache so subsequent alloca insertions
    // in the same block can skip the O(N) reverse scan.
    if (auto *defOp = addr.getDefiningOp())
      builder.updateAllocaInsertPointCache(defOp->getBlock(), defOp);
  }
  return addr;
}

// Note: this function also emit constructor calls to support a MSVC extensions
// allowing explicit constructor function call.
RValue CIRGenFunction::emitCXXMemberCallExpr(const CXXMemberCallExpr *ce,
                                             ReturnValueSlot returnValue) {
  const Expr *callee = ce->getCallee()->IgnoreParens();

  if (isa<BinaryOperator>(callee))
    return emitCXXMemberPointerCallExpr(ce, returnValue);

  const auto *me = cast<MemberExpr>(callee);
  const auto *md = cast<CXXMethodDecl>(me->getMemberDecl());

  if (md->isStatic()) {
    // The method is static, emit it as we would a regular call.
    CIRGenCallee callee =
        CIRGenCallee::forDirect(cgm.getAddrOfFunction(md), GlobalDecl(md));
    return emitCall(getContext().getPointerType(md->getType()), callee, ce,
                    returnValue);
  }

  bool hasQualifier = me->hasQualifier();
  NestedNameSpecifier qualifier = me->getQualifier();
  bool isArrow = me->isArrow();
  const Expr *base = me->getBase();

  return emitCXXMemberOrOperatorMemberCallExpr(
      ce, md, returnValue, hasQualifier, qualifier, isArrow, base);
}

RValue CIRGenFunction::emitReferenceBindingToExpr(const Expr *e) {
  // Emit the expression as an lvalue.
  LValue lv = emitLValue(e);
  assert(lv.isSimple());
  mlir::Value value = lv.getPointer();

  if (sanitizePerformTypeCheck() && !e->getType()->isFunctionType()) {
    // Match OGCG's EmitReferenceBindingToExpr: directly-bound references must
    // designate an object/function of an appropriate type, or suitably-sized
    // and aligned storage for such an object.
    emitTypeCheck(TCK_ReferenceBinding, e->getExprLoc(), value, e->getType());
  }

  return RValue::get(value);
}

Address CIRGenFunction::emitLoadOfReference(LValue refLVal, mlir::Location loc,
                                            LValueBaseInfo *pointeeBaseInfo,
                                            TBAAAccessInfo *pointeeTBAAInfo) {
  cir::LoadOp load =
      cir::LoadOp::create(builder, loc, refLVal.getAddress().getElementType(),
                          refLVal.getAddress().getPointer());

  if (refLVal.isVolatile())
    load.setIsVolatile(true);

  cgm.decorateOperationWithTBAA(load, refLVal.getTBAAInfo());

  QualType pointeeType = refLVal.getType()->getPointeeType();
  CharUnits align = cgm.getNaturalTypeAlignment(pointeeType, pointeeBaseInfo,
                                                pointeeTBAAInfo,
                                                /*forPointeeType=*/true);

  // Attach !nonnull and !align metadata to reference loads, matching OGCG's
  // EmitLoadOfReference (CGExpr.cpp). References always yield non-null pointers
  // (unless NullPointerIsValid is set), and the loaded pointer has the natural
  // alignment of the pointee type. These are carried as discardable attributes
  // through CIR → LLVM dialect → LLVM IR translation.
  if (!pointeeType->isIncompleteType()) {
    // Emit !nonnull when the pointee target address space is 0 and null
    // pointer is not valid, matching OGCG's EmitLoadOfReference check on
    // getTargetAddressSpace(PTy). This also covers explicit AS0 spellings
    // that are distinct from LangAS::Default but still lower to target AS0.
    if (cgm.getASTContext().getTargetAddressSpace(
            pointeeType.getAddressSpace()) == 0 &&
        !cgm.getCodeGenOpts().NullPointerIsValid)
      load->setDiscardableAttr("cir.nonnull",
                               mlir::UnitAttr::get(builder.getContext()));

    if (pointeeType->isObjectType()) {
      auto alignVal = align.getQuantity();
      if (alignVal > 1)
        load->setDiscardableAttr(
            "cir.ref_align",
            mlir::IntegerAttr::get(
                mlir::IntegerType::get(builder.getContext(), 64), alignVal));
    }
  }
  return Address(load, convertTypeForMem(pointeeType), align);
}

LValue CIRGenFunction::emitLoadOfReferenceLValue(Address refAddr,
                                                 mlir::Location loc,
                                                 QualType refTy,
                                                 AlignmentSource source) {
  LValue refLVal = makeAddrLValue(refAddr, refTy, LValueBaseInfo(source));
  LValueBaseInfo pointeeBaseInfo;
  TBAAAccessInfo pointeeTBAAInfo;
  Address pointeeAddr =
      emitLoadOfReference(refLVal, loc, &pointeeBaseInfo, &pointeeTBAAInfo);
  return makeAddrLValue(pointeeAddr, refLVal.getType()->getPointeeType(),
                        pointeeBaseInfo, pointeeTBAAInfo);
}

void CIRGenFunction::emitTrap(mlir::Location loc, bool createNewBlock) {
  auto trapOp = cir::TrapOp::create(builder, loc);

  // Attach trap-func-name from -ftrap-function flag so LowerToLLVM can
  // emit the correct call-site attribute.
  const std::string &trapFuncName = cgm.getCodeGenOpts().TrapFuncName;
  if (!trapFuncName.empty())
    trapOp->setAttr("cir.trap_func_name", cgm.getStringAttrCached(trapFuncName));

  if (createNewBlock)
    builder.createBlock(builder.getBlock()->getParent());
}

void CIRGenFunction::emitUnreachable(clang::SourceLocation loc,
                                     bool createNewBlock) {
  mlir::Location mlirLoc = getLoc(loc);

  // cir.unreachable is a terminator, so it must be the last op in its block.
  // The insertion point can legitimately sit mid-block when we are already in a
  // dead continuation block created after a prior noreturn call: on ABIs where
  // a noreturn callee has a non-void return type (the AArch32 C++ ABI has
  // destructors return 'this'), emitCall materializes an unused undef result
  // into that block via getUndefRValue and the full-expression cleanup restores
  // the insertion point ahead of it. Emitting here (e.g. for an explicit
  // __builtin_unreachable() right after a noreturn full-expression) would leave
  // the undef result after the terminator. Move to the end of the block so the
  // terminator stays last.
  if (mlir::Block *b = builder.getInsertionBlock();
      b && !b->empty() && !b->back().hasTrait<mlir::OpTrait::IsTerminator>())
    builder.setInsertionPointToEnd(b);

  if (sanOpts.has(SanitizerKind::Unreachable)) {
    bool trapOnFailure =
        cgm.getCodeGenOpts().SanitizeTrap.has(SanitizerKind::Unreachable);

    // Use constant-true so the handler is in the taken (true) branch of the
    // cir.if. The handler must always fire to report the UB.
    auto trueVal =
        cir::ConstantOp::create(builder, mlirLoc, builder.getTrueAttr());
    auto unreachableIf = cir::IfOp::create(
        builder, mlirLoc, trueVal, /*withElseRegion=*/false,
        [&](mlir::OpBuilder &, mlir::Location ifLoc) {
          if (trapOnFailure) {
            auto trap = cir::TrapOp::create(builder, ifLoc);
            // SanitizerHandler::BuiltinUnreachable = 1
            trap->setAttr(
                "cir.ubsantrap_handler",
                mlir::IntegerAttr::get(
                    mlir::IntegerType::get(builder.getContext(), 8), 1));
            markNoSanitize(trap);
            return;
          }

          auto stubFnTy = cir::FuncType::get({}, builder.getVoidTy());
          auto stubFn = cgm.createRuntimeFunction(
              stubFnTy, "__cir_ubsan_builtin_unreachable", /*attrs=*/{},
              /*isLocal=*/false, /*assumeConvergent=*/false);
          auto call = builder.createCallOp(ifLoc, stubFn, {});

          PresumedLoc presumedLoc =
              getContext().getSourceManager().getPresumedLoc(loc);
          if (presumedLoc.isValid()) {
            call->setDiscardableAttr("cir.ubsan.source_file",
                                     builder.getStringAttr(
                                         presumedLoc.getFilename()));
            call->setDiscardableAttr(
                "cir.ubsan.source_line",
                builder.getI32IntegerAttr(presumedLoc.getLine()));
            call->setDiscardableAttr(
                "cir.ubsan.source_column",
                builder.getI32IntegerAttr(presumedLoc.getColumn()));
          }

          call->setDiscardableAttr("cir.ubsan.handler_kind",
                                   builder.getStringAttr("abort"));
          call->setDiscardableAttr("cir.ubsan.handler_name",
                                   builder.getStringAttr("builtin_unreachable"));
          call->setDiscardableAttr("cir.nosanitize",
                                   mlir::UnitAttr::get(builder.getContext()));

          auto unreachable = cir::UnreachableOp::create(builder, ifLoc);
          unreachable->setAttr("cir.nosanitize",
                               mlir::UnitAttr::get(builder.getContext()));
        });

    unreachableIf->setAttr(
        "cir.branch_weights",
        builder.getArrayAttr({builder.getI32IntegerAttr(1048575),
                              builder.getI32IntegerAttr(1)}));
    unreachableIf->setAttr("cir.nosanitize",
                           mlir::UnitAttr::get(builder.getContext()));
  }

  cir::UnreachableOp::create(builder, mlirLoc);
  if (createNewBlock)
    builder.createBlock(builder.getBlock()->getParent());
}

mlir::Value CIRGenFunction::createDummyValue(mlir::Location loc,
                                             clang::QualType qt) {
  mlir::Type t = convertType(qt);
  CharUnits alignment = getContext().getTypeAlignInChars(qt);
  return builder.createDummyValue(loc, t, alignment);
}

//===----------------------------------------------------------------------===//
// CIR builder helpers
//===----------------------------------------------------------------------===//

Address CIRGenFunction::createMemTemp(QualType ty, mlir::Location loc,
                                      const Twine &name, Address *alloca,
                                      mlir::OpBuilder::InsertPoint ip) {
  // FIXME: Should we prefer the preferred type alignment here?
  return createMemTemp(ty, getContext().getTypeAlignInChars(ty), loc, name,
                       alloca, ip);
}

Address CIRGenFunction::createMemTemp(QualType ty, CharUnits align,
                                      mlir::Location loc, const Twine &name,
                                      Address *alloca,
                                      mlir::OpBuilder::InsertPoint ip) {
  Address result = createTempAlloca(convertTypeForMem(ty), align, loc, name,
                                    /*ArraySize=*/nullptr, alloca, ip,
                                    cgm.getLangTempAllocaAddressSpace());
  if (ty->isConstantMatrixType()) {
    assert(!cir::MissingFeatures::matrixType());
    cgm.errorNYI(loc, "temporary matrix value");
  }
  return result;
}

/// This creates a alloca and inserts it into the entry block of the
/// current region.
Address CIRGenFunction::createTempAllocaWithoutCast(
    mlir::Type ty, CharUnits align, mlir::Location loc, const Twine &name,
    mlir::Value arraySize, mlir::OpBuilder::InsertPoint ip) {
  // Emit the aligned alloca directly to avoid creating an unaligned op and
  // then mutating its alignment attribute afterwards. This keeps prolog/allocation
  // hot paths leaner and avoids duplicate attr setup work.
  mlir::Value allocaVal = ip.isSet() ? emitAlloca(name.str(), ty, loc, align, ip, arraySize)
                                     : emitAlloca(name.str(), ty, loc, align,
                                                  /*insertIntoFnEntryBlock=*/false,
                                                  arraySize);
  auto alloca = mlir::cast<cir::AllocaOp>(allocaVal.getDefiningOp());
  return Address(alloca, ty, align);
}

/// This creates a alloca and inserts it into the entry block. The alloca is
/// casted to the destination address space if necessary.
///
/// \p destLangAS controls the target address space for the cast. When not
/// provided (the default), the alloca is cast to the CIR default (generic)
/// address space. When provided, it is cast to the corresponding CIR address
/// space — or not at all if the destination AS matches the alloca AS.
///
/// This distinction matters on targets like AMDGCN where allocas live in
/// AS 5 (private). In OpenCL CL1.2 the language's default address space for
/// locals is also private (AS 5), so no cast is needed. In CL2.0+ and C++
/// the language default is generic (AS 0), so the cast IS needed.
Address CIRGenFunction::createTempAlloca(mlir::Type ty, CharUnits align,
                                         mlir::Location loc, const Twine &name,
                                         mlir::Value arraySize,
                                         Address *allocaAddr,
                                         mlir::OpBuilder::InsertPoint ip,
                                         std::optional<clang::LangAS> destLangAS) {
  Address alloca =
      createTempAllocaWithoutCast(ty, align, loc, name, arraySize, ip);
  if (allocaAddr)
    *allocaAddr = alloca;

  // Alloca always returns a pointer in alloca address space, which may
  // be different from the type defined by the language. For example,
  // in C++ the auto variables are in the default address space. Therefore
  // cast alloca to the default address space when necessary.
  if (alloca.getAddressSpace()) {
    mlir::Attribute dlAllocaAS =
        cgm.getDataLayout().layout.getAllocaMemorySpace();
    if (dlAllocaAS) {
      // Determine the destination address space. If the caller provided one,
      // convert it to the CIR equivalent. Otherwise, use the default (no
      // address space annotation = generic/flat).
      mlir::ptr::MemorySpaceAttrInterface destAS;
      if (destLangAS && *destLangAS != LangAS::Default) {
        destAS = cir::toCIRLangAddressSpaceAttr(&cgm.getMLIRContext(),
                                                *destLangAS);
      }
      // Skip the cast when the destination AS matches the alloca AS.
      if (alloca.getAddressSpace() == destAS)
        return alloca;
      auto destTy = cir::PointerType::get(ty, destAS);
      mlir::Value v = getTargetHooks().performAddrSpaceCast(
          *this, alloca.getPointer(), alloca.getAddressSpace(), destTy);
      return Address(v, ty, align);
    }
  }
  return alloca;
}

/// Create an alloca using the insertion policy selected by
/// \p insertIntoFnEntryBlock:
/// - true: insert in the function entry block;
/// - false: insert in the current lexical scope entry block.
/// Array-size-dependent insertion policy is handled by callers that pass an
/// explicit insertion point overload when needed.
cir::AllocaOp CIRGenFunction::createTempAlloca(mlir::Type ty,
                                               mlir::Location loc,
                                               const Twine &name,
                                               mlir::Value arraySize,
                                               bool insertIntoFnEntryBlock) {
  return mlir::cast<cir::AllocaOp>(emitAlloca(name.str(), ty, loc, CharUnits(),
                                              insertIntoFnEntryBlock, arraySize)
                                       .getDefiningOp());
}

/// This creates an alloca and inserts it into the provided insertion point
cir::AllocaOp CIRGenFunction::createTempAlloca(mlir::Type ty,
                                               mlir::Location loc,
                                               const Twine &name,
                                               mlir::OpBuilder::InsertPoint ip,
                                               mlir::Value arraySize) {
  assert(ip.isSet() && "Insertion point is not set");
  return mlir::cast<cir::AllocaOp>(
      emitAlloca(name.str(), ty, loc, CharUnits(), ip, arraySize)
          .getDefiningOp());
}

/// Try to emit a reference to the given value without producing it as
/// an l-value.  For many cases, this is just an optimization, but it avoids
/// us needing to emit global copies of variables if they're named without
/// triggering a formal use in a context where we can't emit a direct
/// reference to them, for instance if a block or lambda or a member of a
/// local class uses a const int variable or constexpr variable from an
/// enclosing function.
///
/// For named members of enums, this is the only way they are emitted.

enum ConstantEmissionKind {
  CEK_None,
  CEK_AsReferenceOnly,
  CEK_AsValueOrReference,
  CEK_AsValueOnly
};

static bool isConstantEmittableObjectType(QualType type) {
  assert(type.isCanonical());
  assert(!type->isReferenceType());

  // Must be const-qualified but non-volatile.
  Qualifiers qs = type.getLocalQualifiers();
  if (!qs.hasConst() || qs.hasVolatile())
    return false;

  // Otherwise, all object types satisfy this except C++ classes with
  // mutable subobjects or non-trivial copy/destroy behavior.
  if (const auto *rt = dyn_cast<clang::RecordType>(type))
    if (const auto *rd = dyn_cast<CXXRecordDecl>(rt->getDecl())) {
      rd = rd->getDefinitionOrSelf();
      if (rd->hasMutableFields() || !rd->isTrivial())
        return false;
    }

  return true;
}

static ConstantEmissionKind
checkVarTypeForConstantEmission(QualType type) {
  type = type.getCanonicalType();
  if (const auto *ref = dyn_cast<ReferenceType>(type)) {
    if (isConstantEmittableObjectType(ref->getPointeeType()))
      return CEK_AsValueOrReference;
    return CEK_AsReferenceOnly;
  }
  if (isConstantEmittableObjectType(type))
    return CEK_AsValueOnly;
  return CEK_None;
}

CIRGenFunction::ConstantEmission
CIRGenFunction::tryEmitAsConstant(const DeclRefExpr *refExpr) {
  const ValueDecl *value = refExpr->getDecl();

  // The value needs to be an enum constant or a constant variable.
  ConstantEmissionKind cek;
  if (isa<ParmVarDecl>(value)) {
    cek = CEK_None;
  } else if (const auto *var = dyn_cast<VarDecl>(value)) {
    cek = checkVarTypeForConstantEmission(var->getType());
  } else if (isa<EnumConstantDecl>(value)) {
    cek = CEK_AsValueOnly;
  } else {
    cek = CEK_None;
  }
  if (cek == CEK_None)
    return ConstantEmission();

  Expr::EvalResult result;
  bool resultIsReference;
  QualType resultType;

  // It's best to evaluate all the way as an r-value if that's permitted.
  if (cek != CEK_AsReferenceOnly &&
      refExpr->EvaluateAsRValue(result, getContext())) {
    resultIsReference = false;
    resultType = refExpr->getType().getUnqualifiedType();

  // Otherwise, try to evaluate as an l-value.
  } else if (cek != CEK_AsValueOnly &&
             refExpr->EvaluateAsLValue(result, getContext())) {
    resultIsReference = true;
    resultType = value->getType();

  // Failure.
  } else {
    return ConstantEmission();
  }

  // In any case, if the initializer has side-effects, abandon ship.
  if (result.HasSideEffects)
    return ConstantEmission();

  // Emit as a constant.
  // FIXME(cir): have emitAbstract build a TypedAttr instead (this requires
  // somewhat heavy refactoring...)
  mlir::Attribute c = ConstantEmitter(*this).emitAbstract(
      refExpr->getLocation(), result.Val, resultType);
  mlir::TypedAttr cstToEmit = mlir::dyn_cast_if_present<mlir::TypedAttr>(c);
  assert(cstToEmit && "expected a typed attribute");

  // TODO(cir): Thread debug metadata emission for global references once
  // CIR-level debug info support is available.

  // If we emitted a reference constant, we need to dereference that.
  if (resultIsReference)
    return ConstantEmission::forReference(cstToEmit);

  return ConstantEmission::forValue(cstToEmit);
}

CIRGenFunction::ConstantEmission
CIRGenFunction::tryEmitAsConstant(const MemberExpr *me) {
  if (DeclRefExpr *dre = tryToConvertMemberExprToDeclRefExpr(*this, me))
    return tryEmitAsConstant(dre);
  return ConstantEmission();
}

mlir::Value CIRGenFunction::emitScalarConstant(
    const CIRGenFunction::ConstantEmission &constant, Expr *e) {
  assert(constant && "not a constant");
  if (constant.isReference())
    return emitLoadOfLValue(constant.getReferenceLValue(*this, e),
                            e->getExprLoc())
        .getValue();
  mlir::Value result =
      builder.getConstant(getLoc(e->getSourceRange()), constant.getValue());
  // The constant's type may differ from the expression's type when the
  // initializer involves a reinterpret_cast (e.g. A* const p =
  // reinterpret_cast<A*>(&b)). The constant evaluator folds through the cast,
  // producing a GlobalViewAttr with the source type. Insert a bitcast to match.
  mlir::Type exprTy = convertType(e->getType());
  if (result.getType() != exprTy)
    result = builder.createBitcast(result.getLoc(), result, exprTy);

  if (mlir::isa<cir::GlobalViewAttr>(constant.getValue())) {
    if (std::optional<CIRGenPointerAuthInfo> pointerAuth =
            cgm.getFunctionPointerAuthInfo(e->getType())) {
      CIRGenRuntimePointerAuthInfo runtimeAuthInfo{
          pointerAuth->authenticationMode, pointerAuth->isIsaPointer,
          pointerAuth->authenticatesNullValues, pointerAuth->key,
          mlir::Value{}};
      if (pointerAuth->discriminator) {
        runtimeAuthInfo.discriminator =
            builder.getConstantInt(result.getLoc(), cgm.uIntPtrTy,
                                   pointerAuth->discriminator);
      }
      result = emitPointerAuthSign(runtimeAuthInfo, result);
    }
  }
  return result;
}

LValue CIRGenFunction::emitPredefinedLValue(const PredefinedExpr *e) {
  const StringLiteral *sl = e->getFunctionName();
  assert(sl != nullptr && "No StringLiteral name in PredefinedExpr");
  // curFn is a FuncOp in normal functions, but may be a GlobalOp when
  // emitting global dynamic initializers (CIR attaches a ctor region to the
  // GlobalOp instead of creating a synthetic __cxx_global_var_init function).
  // OGCG uses CurFn->getName() unconditionally because it always wraps global
  // init code in a synthetic llvm::Function.
  StringRef fnName;
  if (auto fn = dyn_cast<cir::FuncOp>(curFn))
    fnName = fn.getName();
  else if (auto gv = dyn_cast<cir::GlobalOp>(curFn))
    fnName = gv.getName();
  else
    llvm_unreachable("unexpected curFn type in emitPredefinedLValue");
  fnName.consume_front("\01");
  if (isa_and_nonnull<BlockDecl>(curCodeDecl) && fnName.ends_with(".invoke"))
    fnName = fnName.drop_back(StringRef(".invoke").size());
  std::array<StringRef, 2> nameItems = {
      PredefinedExpr::getIdentKindName(e->getIdentKind()), fnName};
  std::string gvName = llvm::join(nameItems, ".");
  if (const auto *blockDecl = dyn_cast_or_null<BlockDecl>(curCodeDecl)) {
    std::string name = std::string(sl->getString());
    if (!name.empty()) {
      unsigned discriminator =
          cgm.getCXXABI().getMangleContext().getBlockId(blockDecl, true);
      if (discriminator)
        name += "_" + Twine(discriminator + 1).str();
    } else {
      name = std::string(fnName);
    }

    cir::GlobalOp globalOp =
        cgm.getGlobalForConstantCString(getLoc(e->getSourceRange()), name,
                                        gvName);
    assert(globalOp.getAlignment() &&
           "expected alignment for predefined string");
    unsigned align = *(globalOp.getAlignment());
    mlir::Location loc = getLoc(e->getSourceRange());
    mlir::Value addr = builder.createGetGlobal(loc, globalOp);
    mlir::Type lvalueTy = globalOp.getSymType();
    mlir::Type exprTy = convertType(e->getType());
    if (lvalueTy != exprTy) {
      addr = builder.createBitcast(
          loc, addr, builder.getPointerTo(exprTy, globalOp.getAddrSpaceAttr()));
      lvalueTy = exprTy;
    }
    return makeAddrLValue(
        Address(addr, lvalueTy, CharUnits::fromQuantity(align)),
        e->getType(), AlignmentSource::Decl);
  }

  return emitStringLiteralLValue(sl, gvName);
}

LValue CIRGenFunction::emitOpaqueValueLValue(const OpaqueValueExpr *e) {
  assert(OpaqueValueMappingData::shouldBindAsLValue(e));
  return getOrCreateOpaqueLValueMapping(e);
}

namespace {
struct LValueOrRValue {
  LValue lv;
  RValue rv;
};

LValueOrRValue emitPseudoObjectExpr(CIRGenFunction &cgf,
                                    const PseudoObjectExpr *expr,
                                    bool forLValue, AggValueSlot slot) {
  SmallVector<CIRGenFunction::OpaqueValueMappingData, 4> opaques;

  // Find the result expression, if any.
  const Expr *resultExpr = expr->getResultExpr();
  LValueOrRValue result;

  for (PseudoObjectExpr::const_semantics_iterator i = expr->semantics_begin(),
                                                  e = expr->semantics_end();
       i != e; ++i) {
    const Expr *semantic = *i;

    // If this semantic expression is an opaque value, bind it
    // to the result of its source expression.
    if (const auto *ov = dyn_cast<OpaqueValueExpr>(semantic)) {
      // Skip unique OVEs.
      if (ov->isUnique()) {
        assert(ov != resultExpr &&
               "A unique OVE cannot be used as the result expression");
        continue;
      }

      // If this is the result expression, we may need to evaluate
      // directly into the slot.
      using OVMA = CIRGenFunction::OpaqueValueMappingData;
      OVMA opaqueData;
      if (ov == resultExpr && ov->isPRValue() && !forLValue &&
          CIRGenFunction::hasAggregateEvaluationKind(ov->getType())) {
        cgf.emitAggExpr(ov->getSourceExpr(), slot);
        LValue lv = cgf.makeAddrLValue(slot.getAddress(), ov->getType(),
                                       AlignmentSource::Decl);
        opaqueData = OVMA::bind(cgf, ov, lv);
        result.rv = slot.asRValue();

        // Otherwise, emit as normal.
      } else {
        opaqueData = OVMA::bind(cgf, ov, ov->getSourceExpr());

        // If this is the result, also evaluate the result now.
        if (ov == resultExpr) {
          if (forLValue)
            result.lv = cgf.emitLValue(ov);
          else
            result.rv = cgf.emitAnyExpr(ov, slot);
        }
      }

      opaques.push_back(opaqueData);

      // Otherwise, if the expression is the result, evaluate it
      // and remember the result.
    } else if (semantic == resultExpr) {
      if (forLValue)
        result.lv = cgf.emitLValue(semantic);
      else
        result.rv = cgf.emitAnyExpr(semantic, slot);

      // Otherwise, evaluate the expression in an ignored context.
    } else {
      cgf.emitIgnoredExpr(semantic);
    }
  }

  // Unbind all the opaques now.
  for (auto &opaque : opaques)
    opaque.unbind(cgf);

  return result;
}

} // namespace

RValue CIRGenFunction::emitPseudoObjectRValue(const PseudoObjectExpr *expr,
                                              AggValueSlot slot) {
  return emitPseudoObjectExpr(*this, expr, false, slot).rv;
}

LValue CIRGenFunction::emitPseudoObjectLValue(const PseudoObjectExpr *expr) {
  return emitPseudoObjectExpr(*this, expr, true, AggValueSlot::ignored()).lv;
}

namespace {
// Handle the case where the condition is a constant evaluatable simple integer,
// which means we don't have to separately handle the true/false blocks.
std::optional<LValue> handleConditionalOperatorLValueSimpleCase(
    CIRGenFunction &cgf, const AbstractConditionalOperator *e) {
  const Expr *condExpr = e->getCond();
  llvm::APSInt condExprVal;
  if (!cgf.constantFoldsToSimpleInteger(condExpr, condExprVal))
    return std::nullopt;

  const Expr *live = e->getTrueExpr(), *dead = e->getFalseExpr();
  if (!condExprVal.getBoolValue())
    std::swap(live, dead);

  if (cgf.containsLabel(dead))
    return std::nullopt;

  // If the true case is live, we need to track its region.
  // If a throw expression we emit it and return an undefined lvalue
  // because it can't be used.
  if (auto *throwExpr = dyn_cast<CXXThrowExpr>(live->IgnoreParens())) {
    cgf.emitCXXThrowExpr(throwExpr);
    // Return an undefined lvalue - the throw terminates execution
    // so this value will never actually be used
    mlir::Type elemTy = cgf.convertType(dead->getType());
    mlir::Value undefPtr =
        cgf.getBuilder().getNullPtr(cgf.getBuilder().getPointerTo(elemTy),
                                    cgf.getLoc(throwExpr->getSourceRange()));
    return cgf.makeAddrLValue(Address(undefPtr, elemTy, CharUnits::One()),
                              dead->getType());
  }
  return cgf.emitLValue(live);
}

/// Emit the operand of a glvalue conditional operator. This is either a glvalue
/// or a (possibly-parenthesized) throw-expression. If this is a throw, no
/// LValue is returned and the current block has been terminated.
static std::optional<LValue> emitLValueOrThrowExpression(CIRGenFunction &cgf,
                                                         const Expr *operand) {
  if (auto *throwExpr = dyn_cast<CXXThrowExpr>(operand->IgnoreParens())) {
    cgf.emitCXXThrowExpr(throwExpr);
    return std::nullopt;
  }

  return cgf.emitLValue(operand);
}
} // namespace

// Create and generate the 3 blocks for a conditional operator.
// Leaves the 'current block' in the continuation basic block.
template <typename FuncTy>
CIRGenFunction::ConditionalInfo
CIRGenFunction::emitConditionalBlocks(const AbstractConditionalOperator *e,
                                      const FuncTy &branchGenFunc) {
  ConditionalInfo info;
  ConditionalEvaluation eval(*this);
  mlir::Location loc = getLoc(e->getSourceRange());
  CIRGenBuilderTy &builder = getBuilder();

  mlir::Value condV = emitOpOnBoolExpr(loc, e->getCond());
  SmallVector<mlir::OpBuilder::InsertPoint, 2> insertPoints{};
  mlir::Type yieldTy{};

  auto emitBranch = [&](mlir::OpBuilder &b, mlir::Location loc,
                        const Expr *expr, std::optional<LValue> &resultLV) {
    CIRGenFunction::LexicalScope lexScope{*this, loc, b.getInsertionBlock()};
    curLexScope->setAsTernary();

    eval.beginEvaluation();
    resultLV = branchGenFunc(*this, expr);
    mlir::Value resultPtr = resultLV ? resultLV->getPointer() : mlir::Value();
    eval.endEvaluation();

    // Defer cleanups (e.g., temporary destructors) to the parent scope so
    // they fire after the full expression (e.g., after the function call
    // that uses the ternary result), not inside this branch.
    // ConditionalEvaluation::beginEvaluation() set isInConditionalBranch(),
    // so pushFullExprCleanup already created active flags to guard the
    // cleanups — they will only fire if this branch was actually taken.
    lexScope.deferCleanupsToOuterScope();

    if (resultPtr) {
      yieldTy = resultPtr.getType();
      cir::YieldOp::create(b, loc, resultPtr);
    } else {
      // Throw expression: the throw path terminates with cir.unreachable in
      // a prior block, and the builder is at a dead continuation block. Save
      // the insertion point so we can add a yield later (once we know the
      // yield type from the other branch). Add an unreachable to prevent
      // LexicalScope::cleanup() from erasing the dead block.
      insertPoints.push_back(b.saveInsertionPoint());
      cir::UnreachableOp::create(b, loc);
    }
  };

  info.result = cir::TernaryOp::create(
                    builder, loc, condV,
                    /*trueBuilder=*/
                    [&](mlir::OpBuilder &b, mlir::Location loc) {
                      emitBranch(b, loc, e->getTrueExpr(), info.lhs);
                    },
                    /*falseBuilder=*/
                    [&](mlir::OpBuilder &b, mlir::Location loc) {
                      emitBranch(b, loc, e->getFalseExpr(), info.rhs);
                    })
                    .getResult();

  // If both arms are void, so be it.
  if (!yieldTy)
    yieldTy = voidTy;

  // Insert required yields for throw/void branches.
  for (mlir::OpBuilder::InsertPoint &toInsert : insertPoints) {
    mlir::OpBuilder::InsertionGuard guard(builder);
    builder.restoreInsertionPoint(toInsert);

    // Remove placeholder unreachable added to keep the block alive.
    if (mlir::Block *block = builder.getInsertionBlock()) {
      if (block->mightHaveTerminator() &&
          isa<cir::UnreachableOp>(block->getTerminator()))
        block->getTerminator()->erase();
    }

    // Block does not return: build empty yield.
    if (!yieldTy) {
      cir::YieldOp::create(builder, loc);
    } else { // Block returns: set null yield value.
      mlir::Value op0 = builder.getNullValue(yieldTy, loc);
      cir::YieldOp::create(builder, loc, op0);
    }
  }

  return info;
}

void CIRGenFunction::emitIgnoredConditionalOperator(
    const AbstractConditionalOperator *e) {
  if (!e->isGLValue()) {
    // ?: here should be an aggregate.
    assert(hasAggregateEvaluationKind(e->getType()) &&
           "Unexpected conditional operator!");
    emitAggExprToLValue(e);
    return;
  }

  OpaqueValueMapping binding(*this, e);
  if (std::optional<LValue> res =
          handleConditionalOperatorLValueSimpleCase(*this, e))
    return;

  // Emit a void ternary where each branch evaluates the sub-expression for
  // side effects only. This avoids trying to create a combined LValue from
  // two differently-sized bitfield LValues, which is not representable.
  ConditionalEvaluation eval(*this);
  mlir::Location loc = getLoc(e->getSourceRange());
  mlir::Value condV = emitOpOnBoolExpr(loc, e->getCond());

  cir::TernaryOp::create(
      getBuilder(), loc, condV,
      /*trueBuilder=*/
      [&](mlir::OpBuilder &b, mlir::Location loc) {
        CIRGenFunction::LexicalScope lexScope{*this, loc,
                                              b.getInsertionBlock()};
        curLexScope->setAsTernary();
        eval.beginEvaluation();
        emitIgnoredExpr(e->getTrueExpr());
        eval.endEvaluation();
        lexScope.deferCleanupsToOuterScope();
        cir::YieldOp::create(b, loc);
      },
      /*falseBuilder=*/
      [&](mlir::OpBuilder &b, mlir::Location loc) {
        CIRGenFunction::LexicalScope lexScope{*this, loc,
                                              b.getInsertionBlock()};
        curLexScope->setAsTernary();
        eval.beginEvaluation();
        emitIgnoredExpr(e->getFalseExpr());
        eval.endEvaluation();
        lexScope.deferCleanupsToOuterScope();
        cir::YieldOp::create(b, loc);
      });
}

LValue CIRGenFunction::emitConditionalOperatorLValue(
    const AbstractConditionalOperator *expr) {
  if (!expr->isGLValue()) {
    // ?: here should be an aggregate.
    assert(hasAggregateEvaluationKind(expr->getType()) &&
           "Unexpected conditional operator!");
    return emitAggExprToLValue(expr);
  }

  OpaqueValueMapping binding(*this, expr);
  if (std::optional<LValue> res =
          handleConditionalOperatorLValueSimpleCase(*this, expr))
    return *res;

  ConditionalInfo info =
      emitConditionalBlocks(expr, [](CIRGenFunction &cgf, const Expr *e) {
        return emitLValueOrThrowExpression(cgf, e);
      });

  if ((info.lhs && !info.lhs->isSimple()) ||
      (info.rhs && !info.rhs->isSimple())) {
    cgm.errorNYI(expr->getSourceRange(), "conditional operator",
                 "non-simple lvalue (bitfield/vector)");
    // Return a dummy LValue so compilation can continue.
    mlir::Location loc = getLoc(expr->getSourceRange());
    mlir::Type ty = convertType(expr->getType());
    Address addr = createTempAlloca(
        ty, getContext().getTypeAlignInChars(expr->getType()), loc, "unsupported",
        /*arraySize=*/nullptr);
    return makeAddrLValue(addr, expr->getType());
  }

  if (info.lhs && info.rhs) {
    Address lhsAddr = info.lhs->getAddress();
    Address rhsAddr = info.rhs->getAddress();
    Address result(info.result, lhsAddr.getElementType(),
                   std::min(lhsAddr.getAlignment(), rhsAddr.getAlignment()));
    AlignmentSource alignSource =
        std::max(info.lhs->getBaseInfo().getAlignmentSource(),
                 info.rhs->getBaseInfo().getAlignmentSource());
    TBAAAccessInfo tbaaInfo = cgm.mergeTBAAInfoForConditionalOperator(
        info.lhs->getTBAAInfo(), info.rhs->getTBAAInfo());
    return makeAddrLValue(result, expr->getType(), LValueBaseInfo(alignSource),
                          tbaaInfo);
  }

  assert((info.lhs || info.rhs) &&
         "both operands of glvalue conditional are throw-expressions?");
  // One branch is a throw — use info.result (the ternary SSA output) to
  // construct the LValue, not the inner LValue's pointer directly. The
  // inner pointer is defined inside the ternary region and doesn't
  // dominate uses outside.
  std::optional<LValue> &activeLV = info.lhs ? info.lhs : info.rhs;
  Address addr(info.result, activeLV->getAddress().getElementType(),
               activeLV->getAddress().getAlignment());
  return makeAddrLValue(addr, expr->getType(), activeLV->getBaseInfo(),
                        activeLV->getTBAAInfo());
}

/// An LValue is a candidate for having its loads and stores be made atomic if
/// we are operating under /volatile:ms *and* the LValue itself is volatile and
/// performing such an operation can be performed without a libcall.
bool CIRGenFunction::isLValueSuitableForInlineAtomic(LValue lv) {
  if (!cgm.getLangOpts().MSVolatile)
    return false;

  cgm.errorNYI("LValueSuitableForInlineAtomic LangOpts MSVolatile");
  return false;
}
