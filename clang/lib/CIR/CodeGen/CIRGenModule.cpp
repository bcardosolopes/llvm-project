//===- CIRGenModule.cpp - Per-Module state for CIR generation -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is the internal per-translation-unit state used for CIR translation.
//
//===----------------------------------------------------------------------===//

#include "CIRGenModule.h"
#include "CIRGenCUDARuntime.h"
#include "CIRGenCXXABI.h"
#include "CIRGenConstantEmitter.h"
#include "CIRGenFunction.h"
#include "CIRGenRecordLayout.h"
#include "TargetInfo.h"
#include "../Dialect/Transforms/TargetLowering/LowerCall.h"
#include "../Dialect/Transforms/TargetLowering/LowerModule.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclOpenACC.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/GlobalDecl.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/StmtOpenMP.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/AST/TypeBase.h"
#include "clang/Basic/DiagnosticFrontend.h"
#include "clang/Basic/Module.h"
#include "clang/Basic/NoSanitizeList.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Version.h"
#include "clang/Basic/XRayInstr.h"
#include "clang/Basic/XRayLists.h"
#include "clang/CIR/Dialect/IR/CIRAttrs.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/IR/CIRStage.h"
#include "clang/CIR/Dialect/IR/CIRTypes.h"
#include "clang/CIR/Interfaces/CIROpInterfaces.h"
#include "clang/CIR/MissingFeatures.h"
#include "clang/Sema/Sema.h"

#include "CIRGenFunctionInfo.h"
#include "mlir/Dialect/OpenMP/OpenMPDialect.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/AttrTypeSubElements.h"
#include "mlir/IR/Location.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/IR/Verifier.h"

#include "llvm/ProfileData/InstrProf.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/AllocToken.h"
#include "llvm/Support/CRC.h"
#include "llvm/Support/Hash.h"
#include "llvm/Support/SipHash.h"
#include "llvm/ProfileData/SampleProf.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/TargetParser/AArch64TargetParser.h"
#include "llvm/TargetParser/X86TargetParser.h"
#include "llvm/IR/DebugInfoMetadata.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>
#include <llvm/Support/ErrorHandling.h>

using namespace clang;
using namespace clang::CIRGen;

namespace {
constexpr llvm::StringLiteral kTargetClonesCompatIFuncAliasesAttr =
    "cir.target_clones_compat_ifunc_aliases";
constexpr llvm::StringLiteral kCPUDispatchIFuncAliasesAttr =
    "cir.cpu_dispatch_ifunc_aliases";

static std::string qualifyWindowsLibrary(llvm::StringRef lib) {
  // Match OGCG TargetCodeGenInfo::qualifyWindowsLibrary behavior.
  const bool quote = lib.contains(' ');
  std::string arg = quote ? "\"" : "";
  arg += lib;
  if (!lib.ends_with_insensitive(".lib") && !lib.ends_with_insensitive(".a"))
    arg += ".lib";
  arg += quote ? "\"" : "";
  return arg;
}

static std::string getBlockMangledName(CIRGenModule &cgm, GlobalDecl gd,
                                       const BlockDecl *blockDecl,
                                       const VarDecl *initializedVarDecl) {
  MangleContext &mangleCtx = cgm.getCXXABI().getMangleContext();
  const Decl *decl = gd.getDecl();

  SmallString<256> buffer;
  llvm::raw_svector_ostream out(buffer);
  if (!decl) {
    mangleCtx.mangleGlobalBlock(blockDecl, initializedVarDecl, out);
  } else if (const auto *ctor = dyn_cast<CXXConstructorDecl>(decl)) {
    mangleCtx.mangleCtorBlock(ctor, gd.getCtorType(), blockDecl, out);
  } else if (const auto *dtor = dyn_cast<CXXDestructorDecl>(decl)) {
    mangleCtx.mangleDtorBlock(dtor, gd.getDtorType(), blockDecl, out);
  } else {
    mangleCtx.mangleBlock(cast<DeclContext>(decl), blockDecl, out);
  }
  return std::string(out.str());
}

static bool hasNoThrowExtraAttr(cir::FuncOp fn) {
  if (auto extraAttrs = fn.getExtraAttrs())
    return extraAttrs->getElements().get("nothrow") != nullptr;
  return false;
}

static bool hasNoSanitizeThreadAttr(const Decl *decl) {
  SanitizerMask noSanitizeMask;
  for (const auto *attr : decl->specific_attrs<NoSanitizeAttr>())
    noSanitizeMask |= attr->getMask();
  return bool(noSanitizeMask & SanitizerKind::Thread);
}

static bool shouldIgnoreThreadCheckingAtRuntime(const ObjCMethodDecl *omd) {
  const IdentifierInfo *ii = omd->getSelector().getIdentifierInfoForSlot(0);
  return omd->getMethodFamily() == OMF_dealloc ||
         omd->getMethodFamily() == OMF_initialize ||
         (omd->getSelector().isUnarySelector() && ii &&
          ii->isStr(".cxx_destruct"));
}

/// Look up a direct callee FuncOp for a CallOp using an O(1) symbol table.
static cir::FuncOp getCalleeFromSymTable(cir::CallOp call,
                                         mlir::SymbolTable &symTable) {
  if (!call.getCallee())
    return {};
  return symTable.lookup<cir::FuncOp>(*call.getCallee());
}

static void simplifyDeadSyntheticTrys(cir::FuncOp fn,
                                      mlir::SymbolTable &symTable) {
  llvm::SmallVector<cir::TryOp, 4> deadTrys;
  fn.walk([&](cir::TryOp tryOp) {
    if (!tryOp.getSynthetic())
      return;

    auto &tryRegion = tryOp.getTryRegion();
    auto handlerRegions = tryOp.getHandlerRegions();
    if (!llvm::hasSingleElement(tryRegion) ||
        !llvm::hasSingleElement(handlerRegions))
      return;

    mlir::Block &tryBlock = tryRegion.front();
    mlir::Region &handlerRegion = handlerRegions.front();
    if (!llvm::hasSingleElement(handlerRegion))
      return;

    mlir::Block &handlerBlock = handlerRegion.front();
    if (!llvm::hasSingleElement(handlerBlock) ||
        !mlir::isa<cir::ResumeOp>(handlerBlock.front()))
      return;
    if (!mlir::isa<cir::YieldOp>(tryBlock.getTerminator()))
      return;

    bool sawCall = false;
    for (mlir::Operation &op : tryBlock.without_terminator()) {
      auto call = mlir::dyn_cast<cir::CallOp>(op);
      if (!call || !call.getException())
        return;
      cir::FuncOp callee = getCalleeFromSymTable(call, symTable);
      if (!callee || !hasNoThrowExtraAttr(callee))
        return;
      sawCall = true;
    }

    if (sawCall)
      deadTrys.push_back(tryOp);
  });

  for (cir::TryOp tryOp : deadTrys) {
    mlir::Block &tryBlock = tryOp.getTryRegion().front();
    mlir::OpBuilder builder(fn.getContext());
    builder.setInsertionPoint(tryOp);

    for (mlir::Operation &op :
         llvm::make_early_inc_range(tryBlock.without_terminator())) {
      auto call = mlir::cast<cir::CallOp>(op);
      cir::FuncOp callee = getCalleeFromSymTable(call, symTable);
      mlir::Type returnType = call.getNumResults()
                                  ? call.getResult().getType()
                                  : cir::VoidType::get(fn.getContext());
      cir::CallOp newCall = cir::CallOp::create(
          builder, call.getLoc(), mlir::SymbolRefAttr::get(callee), returnType,
          call.getArgOperands(), call.getCallingConv(), call.getSideEffect(),
          /*exception=*/nullptr);
      if (auto extraAttrs = call.getExtraAttrs())
        newCall->setAttr("extra_attrs", *extraAttrs);

      for (mlir::NamedAttribute attr : call->getAttrs()) {
        llvm::StringRef name = attr.getName().strref();
        if (name == "callee" || name == "exception" || name == "extra_attrs" ||
            name == "cir.terminate_on_throw")
          continue;
        newCall->setAttr(attr.getName(), attr.getValue());
      }

      if (call.getNumResults() != 0)
        call->replaceAllUsesWith(newCall->getResults());
      call.erase();
    }

    tryOp.erase();
  }
}

static void lateInferNoThrow(cir::FuncOp fn, mlir::ModuleOp module) {
  bool alreadyNothrow = hasNoThrowExtraAttr(fn);
  if (alreadyNothrow || cir::isInterposableLinkage(fn.getLinkage()))
    return;
  // Don't infer nothrow on functions with disable_sanitizer_instrumentation.
  // These are typically FMV resolvers that OGCG doesn't mark nounwind.
  if (auto extraAttrs = fn.getExtraAttrs())
    if (extraAttrs->getElements().get("disable_sanitizer_instrumentation"))
      return;
  // Don't infer nothrow on compiler-generated functions that OGCG wouldn't
  // add nounwind to. OGCG only adds nounwind based on the FunctionDecl's
  // exception specification, not by analyzing the function body. TLS
  // wrappers (_ZTW*), TLS init functions (_ZTH*, __tls_init), and global
  // init functions (__cxx_global_var_init) should NOT get inferred nothrow.
  llvm::StringRef name = fn.getName();
  if (name.starts_with("_ZTW") || name.starts_with("_ZTH") ||
      name.starts_with("__tls_init") ||
      name.starts_with("__cxx_global_var_init") ||
      name.starts_with("_GLOBAL__sub_I_"))
    return;

  bool mayThrow = false;
  fn.walk([&](mlir::Operation *op) {
    if (auto callOp = mlir::dyn_cast<cir::CIRCallOpInterface>(op)) {
      if (!callOp->hasAttr("nothrow")) {
        mayThrow = true;
        return mlir::WalkResult::interrupt();
      }
    }
    if (mlir::isa<cir::ThrowOp, cir::ResumeOp>(op)) {
      mayThrow = true;
      return mlir::WalkResult::interrupt();
    }
    if (auto dynCast = mlir::dyn_cast<cir::DynamicCastOp>(op)) {
      if (dynCast.isRefCast()) {
        mayThrow = true;
        return mlir::WalkResult::interrupt();
      }
    }
    return mlir::WalkResult::advance();
  });

  if (mayThrow)
    return;

  mlir::NamedAttrList attrs;
  if (auto existingExtraAttrs = fn.getExtraAttrs())
    attrs = mlir::NamedAttrList{existingExtraAttrs->getElements().getValue()};
  auto *ctx = module.getContext();
  attrs.set(cir::NoThrowAttr::get(ctx).getMnemonic(),
            cir::NoThrowAttr::get(ctx));
  fn.setExtraAttrsAttr(
      cir::ExtraFuncAttributesAttr::get(attrs.getDictionary(ctx)));
}

static void finalizeLateNoThrow(mlir::ModuleOp module) {
  // Build a single SymbolTable for O(1) lookups across all functions.
  // This replaces the stale ModuleFuncSymbolCache that was seeded once
  // and never refreshed, causing O(N) fallback lookups for functions
  // emitted after seeding.
  mlir::SymbolTable symTable(module);

  for (auto fn : module.getOps<cir::FuncOp>()) {
    if (!fn.isDeclaration())
      simplifyDeadSyntheticTrys(fn, symTable);
  }

  for (auto fn : module.getOps<cir::FuncOp>()) {
    if (!fn.isDeclaration())
      lateInferNoThrow(fn, module);
  }
}

static std::optional<mlir::omp::DeclareTargetCaptureClause>
getOpenMPDeclareTargetCaptureClause(const ValueDecl *vd) {
  std::optional<OMPDeclareTargetDeclAttr::MapTypeTy> mapType =
      OMPDeclareTargetDeclAttr::isDeclareTargetDeclaration(vd);
  if (!mapType)
    return std::nullopt;

  switch (*mapType) {
  case OMPDeclareTargetDeclAttr::MT_To:
    return mlir::omp::DeclareTargetCaptureClause::to;
  case OMPDeclareTargetDeclAttr::MT_Link:
    return mlir::omp::DeclareTargetCaptureClause::link;
  case OMPDeclareTargetDeclAttr::MT_Enter:
    return mlir::omp::DeclareTargetCaptureClause::enter;
  }

  llvm_unreachable("unexpected OpenMP declare target map type");
}

static mlir::omp::DeclareTargetDeviceType
getOpenMPDeclareTargetDeviceType(const ValueDecl *vd) {
  std::optional<OMPDeclareTargetDeclAttr::DevTypeTy> devType =
      OMPDeclareTargetDeclAttr::getDeviceType(vd);
  if (!devType)
    return mlir::omp::DeclareTargetDeviceType::any;

  switch (*devType) {
  case OMPDeclareTargetDeclAttr::DT_Host:
    return mlir::omp::DeclareTargetDeviceType::host;
  case OMPDeclareTargetDeclAttr::DT_NoHost:
    return mlir::omp::DeclareTargetDeviceType::nohost;
  case OMPDeclareTargetDeclAttr::DT_Any:
    return mlir::omp::DeclareTargetDeviceType::any;
  }

  llvm_unreachable("unexpected OpenMP declare target device type");
}

static std::optional<mlir::omp::DeclareTargetAttr>
getOpenMPDeclareTargetAttr(mlir::MLIRContext &ctx, const ValueDecl *vd) {
  std::optional<mlir::omp::DeclareTargetCaptureClause> captureClause =
      getOpenMPDeclareTargetCaptureClause(vd);
  if (!captureClause)
    return std::nullopt;

  return mlir::omp::DeclareTargetAttr::get(
      &ctx,
      mlir::omp::DeclareTargetDeviceTypeAttr::get(
          &ctx, getOpenMPDeclareTargetDeviceType(vd)),
      mlir::omp::DeclareTargetCaptureClauseAttr::get(&ctx, *captureClause),
      mlir::BoolAttr());
}

static constexpr llvm::StringLiteral openMPDeclareTargetIndirectAttrName =
    "omp.declare_target_indirect";
static constexpr llvm::StringLiteral openMPDeclareTargetParentNameAttrName =
    "omp.declare_target_parent_name";

static bool isOpenMPDeclareTargetIndirect(const FunctionDecl *fd) {
  std::optional<OMPDeclareTargetDeclAttr *> activeAttr =
      OMPDeclareTargetDeclAttr::getActiveAttr(fd);
  return activeAttr && (*activeAttr)->getIndirect();
}

static void attachOpenMPDeclareTargetFunctionAttrs(cir::FuncOp funcOp,
                                                   const FunctionDecl *fd,
                                                   mlir::MLIRContext &ctx) {
  if (std::optional<mlir::omp::DeclareTargetAttr> declareTargetAttr =
          getOpenMPDeclareTargetAttr(ctx, fd))
    funcOp->setAttr("omp.declare_target", *declareTargetAttr);

  if (!isOpenMPDeclareTargetIndirect(fd))
    return;

  funcOp->setAttr(openMPDeclareTargetIndirectAttrName,
                  mlir::UnitAttr::get(&ctx));
  funcOp->setAttr(openMPDeclareTargetParentNameAttrName,
                  mlir::StringAttr::get(&ctx, fd->getName()));
}

static bool shouldUseOpenMPDeclareTargetRefPtr(const VarDecl *vd,
                                               const CIRGenModule &cgm) {
  std::optional<OMPDeclareTargetDeclAttr::MapTypeTy> mapType =
      OMPDeclareTargetDeclAttr::isDeclareTargetDeclaration(vd);
  if (!mapType)
    return false;

  if (*mapType == OMPDeclareTargetDeclAttr::MT_Link)
    return true;

  return (*mapType == OMPDeclareTargetDeclAttr::MT_To ||
          *mapType == OMPDeclareTargetDeclAttr::MT_Enter) &&
         cgm.hasRequiresUnifiedSharedMemory();
}

static unsigned getOpenMPDeclareTargetFileID(const VarDecl *vd,
                                             CIRGenModule &cgm) {
  SourceManager &sm = cgm.getASTContext().getSourceManager();
  SourceLocation loc = vd->getCanonicalDecl()->getBeginLoc();

  auto getFileIDForName = [](llvm::StringRef fileName) -> std::optional<unsigned> {
    if (fileName.empty())
      return std::nullopt;

    llvm::sys::fs::file_status status;
    if (!llvm::sys::fs::status(fileName, status))
      return status.getUniqueID().getFile();
    return std::nullopt;
  };

  PresumedLoc ploc = sm.getPresumedLoc(loc);
  if (ploc.isValid()) {
    if (std::optional<unsigned> fileID = getFileIDForName(ploc.getFilename()))
      return *fileID;

    ploc = sm.getPresumedLoc(loc, /*UseLineDirectives=*/false);
    if (ploc.isValid()) {
      if (std::optional<unsigned> fileID = getFileIDForName(ploc.getFilename()))
        return *fileID;
      return llvm::hash_value(ploc.getFilename());
    }
  }

  return llvm::hash_value(cgm.getMangledName(vd));
}

static std::string getOpenMPDeclareTargetRefPtrName(const VarDecl *vd,
                                                    CIRGenModule &cgm) {
  std::string refPtrName = cgm.getMangledName(vd).str();
  if (!vd->isExternallyVisible())
    refPtrName += ("_" + llvm::utohexstr(getOpenMPDeclareTargetFileID(vd, cgm)));
  refPtrName += "_decl_tgt_ref_ptr";
  return refPtrName;
}

static std::string getObjCImageInfoSection(const llvm::Triple &triple,
                                           unsigned objcABI) {
  if (objcABI == 1)
    return "__OBJC,__image_info,regular";

  switch (triple.getObjectFormat()) {
  case llvm::Triple::UnknownObjectFormat:
  case llvm::Triple::MachO:
    return "__DATA,__objc_imageinfo,regular,no_dead_strip";
  case llvm::Triple::ELF:
    return "objc_imageinfo";
  case llvm::Triple::COFF:
    return ".objc_imageinfo$B";
  case llvm::Triple::Wasm:
  case llvm::Triple::GOFF:
  case llvm::Triple::SPIRV:
  case llvm::Triple::XCOFF:
  case llvm::Triple::DXContainer:
    llvm::report_fatal_error(
        "Objective-C support is unimplemented for object file format");
  }

  llvm_unreachable("Unhandled llvm::Triple::ObjectFormatType enum");
}

static bool hasStrictReturnValueForDecl(const CIRGenModule &cgm,
                                        const FunctionDecl *fd) {
  const LangOptions &langOpts = cgm.getLangOpts();
  bool hasStrictReturn = langOpts.Sanitize.has(clang::SanitizerKind::Memory);
  if (!hasStrictReturn)
    hasStrictReturn = langOpts.CPlusPlus;

  if (hasStrictReturn &&
      !langOpts.Sanitize.has(clang::SanitizerKind::Memory) &&
      fd->isExternC())
    hasStrictReturn = false;

  if (hasStrictReturn && !cgm.getCodeGenOpts().StrictReturn) {
    bool mayDrop = true;
    QualType retTy = fd->getReturnType();
    if (const auto *recordTy = retTy->getAs<RecordType>()) {
      if (const auto *cxxRD =
              llvm::dyn_cast<CXXRecordDecl>(recordTy->getDecl()))
        mayDrop = cxxRD->hasTrivialDestructor();
    }
    if (mayDrop && !langOpts.Sanitize.has(clang::SanitizerKind::Return))
      hasStrictReturn = false;
  }

  return hasStrictReturn;
}

using TypeProvenance = cir::LowerFunctionInfoArgInfo::TypeProvenance;

static bool hasTypeProvenance(TypeProvenance provenance,
                              TypeProvenance flag) {
  return cir::LowerFunctionInfoArgInfo::hasProvenance(provenance, flag);
}

static TypeProvenance getTypeProvenance(QualType type) {
  TypeProvenance provenance = TypeProvenance::None;
  if (type->isBitIntType())
    provenance |= TypeProvenance::BitInt;
  if (type->isMatrixType())
    provenance |= TypeProvenance::Matrix;
  return provenance;
}

static void maybeAddResultNoUndefForFmvDeclStub(CIRGenModule &cgm,
                                                GlobalDecl gd,
                                                cir::FuncOp func) {
  if (!cgm.getTarget().getTriple().isAArch64() || !func.isDeclaration() ||
      func.getNumResults() == 0 ||
      cgm.getModule()->hasAttr("cir.no_noundef_analysis"))
    return;

  const auto *fd = dyn_cast_or_null<FunctionDecl>(gd.getDecl());
  if (!fd || !fd->isMultiVersion() || !hasStrictReturnValueForDecl(cgm, fd) ||
      func->hasAttr("cir.no_noundef_return"))
    return;

  mlir::PatternRewriter rewriter(&cgm.getMLIRContext());
  auto lowerModule = cir::createLowerModule(cgm.getModule(), rewriter);
  if (!lowerModule)
    return;

  TypeProvenance resultProvenance = getTypeProvenance(fd->getReturnType());
  llvm::SmallVector<TypeProvenance, 8> argProvenances;
  argProvenances.reserve(func.getNumArguments());
  for (unsigned i = 0; i < func.getNumArguments(); ++i) {
    TypeProvenance provenance = TypeProvenance::None;
    if (i < fd->getNumParams())
      provenance = getTypeProvenance(fd->getParamDecl(i)->getType());
    argProvenances.push_back(provenance);
  }
  const auto &fnInfo = lowerModule->getTypes().arrangeFreeFunctionType(
      func.getFunctionType(), resultProvenance, argProvenances,
      func.getCallingConv());
  const cir::ABIArgInfo &retAI = fnInfo.getReturnInfo();
  if (retAI.isIndirect())
    return;

  if (!cir::determineNoUndef(func.getFunctionType().getReturnType(), retAI,
                             cgm.getDataLayout()))
    return;

  if (!func.getResultAttr(0, "cir.noundef"))
    func.setResultAttr(0, "cir.noundef",
                       mlir::UnitAttr::get(&cgm.getMLIRContext()));
}

struct TypedefDebugInfo {
  std::string name;
  llvm::SmallVector<std::string, 2> btfDeclTags;
  bool isArrayElementTypedef = false;
  bool isPointerPointee = false;
  bool hasNullBaseType = false;
  bool underlyingIsPointer = false;
  std::string underlyingArrayElementTypedefName;
  std::string underlyingArrayElementTypedefFilePath;
  unsigned underlyingArrayElementTypedefLine = 0;
  bool isConstQualified = false;
  bool isVolatileQualified = false;
  bool isRestrictQualified = false;
  bool pointeeConstQualified = false;
  bool pointeeVolatileQualified = false;
  bool pointeeRestrictQualified = false;
  std::string pointeeName;
  unsigned pointeeSizeInBits = 0;
  unsigned pointeeEncoding = 0;
  std::string pointeeTypedefName;
  std::string pointeeTypedefFilePath;
  unsigned pointeeTypedefLine = 0;
  bool pointeeTypedefUnderlyingIsVector = false;
  unsigned pointeeTypedefVectorElementCount = 0;
  std::string pointeeTypedefElementName;
  unsigned pointeeTypedefElementSizeInBits = 0;
  unsigned pointeeTypedefElementEncoding = 0;
  bool underlyingIsVector = false;
  unsigned vectorElementCount = 0;
  std::string filePath;
  unsigned line = 0;
};

struct BuiltinDebugTypeInfo {
  std::string name;
  unsigned encoding = 0;
  unsigned sizeInBits = 0;
  bool isConstQualified = false;
  bool isVolatileQualified = false;
  bool isRestrictQualified = false;
};

struct EnumDebugTypeInfo {
  std::string name;
  std::string filePath;
  unsigned line = 0;
  std::string baseName;
  unsigned baseEncoding = 0;
  unsigned baseSizeInBits = 0;
  llvm::SmallVector<std::string, 8> enumeratorNames;
  llvm::SmallVector<int64_t, 8> enumeratorValues;
  // Whether the enumerator constants are unsigned. This is language-dependent:
  // in C enumerators always have type int (signed) regardless of the enum's
  // underlying type, whereas in C++ they carry the underlying type's signedness.
  bool enumeratorsUnsigned = false;
  // Whether this is a scoped enumeration (`enum class`/`enum struct`); classic
  // CodeGen marks it with DIFlagEnumClass.
  bool isScoped = false;
  // The RTTI-mangled type identifier used for ODR type uniquing (e.g. "_ZTS1E"),
  // emitted for C++ externally-visible enums to match classic CodeGen. Empty
  // when no identifier applies (e.g. C, or internal-linkage enums).
  std::string identifier;
};

struct PointerLevelDebugInfo {
  llvm::SmallVector<std::string, 2> btfTypeTags;
  bool pointeeConstQualified = false;
  bool pointeeVolatileQualified = false;
  bool pointeeRestrictQualified = false;
};

static llvm::SmallVector<std::string, 2>
collectBTFDeclTagStrings(const Decl *decl);

static llvm::SmallVector<std::string, 2>
collectBTFTypeTagStringsFromQualType(QualType qt) {
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

static void collectPointerDebugInfoFromQualType(
    QualType qt, llvm::SmallVectorImpl<PointerLevelDebugInfo> &levels) {
  if (qt.isNull())
    return;

  llvm::SmallVector<std::string, 2> currentPointerTags =
      collectBTFTypeTagStringsFromQualType(qt);

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

  const auto *ptrTy = qt->getAs<PointerType>();
  if (!ptrTy)
    return;

  QualType pointee = ptrTy->getPointeeType();
  PointerLevelDebugInfo info;
  info.pointeeConstQualified = pointee.isConstQualified();
  info.pointeeVolatileQualified = pointee.isVolatileQualified();
  info.pointeeRestrictQualified = pointee.isRestrictQualified();
  info.btfTypeTags = std::move(currentPointerTags);
  if (info.btfTypeTags.empty())
    info.btfTypeTags = collectBTFTypeTagStringsFromQualType(pointee);
  levels.push_back(std::move(info));
  collectPointerDebugInfoFromQualType(pointee.getUnqualifiedType(), levels);
}

static std::optional<BuiltinDebugTypeInfo>
getBuiltinDebugTypeInfoFromQualType(ASTContext &ctx, QualType qt) {
  if (qt.isNull())
    return std::nullopt;

  BuiltinDebugTypeInfo info;
  info.isConstQualified = qt.isConstQualified();
  info.isVolatileQualified = qt.isVolatileQualified();
  info.isRestrictQualified = qt.isRestrictQualified();

  while (!qt.isNull()) {
    if (const auto *pt = qt->getAs<ParenType>()) {
      qt = pt->getInnerType();
      continue;
    }
    if (const auto *at = qt->getAs<AttributedType>()) {
      qt = at->getModifiedType();
      continue;
    }
    if (const auto *tt = qt->getAs<TypedefType>()) {
      qt = tt->desugar();
      continue;
    }
    if (const auto *sat = qt->getAs<SubstTemplateTypeParmType>()) {
      qt = sat->getReplacementType();
      continue;
    }
    break;
  }

  if (const auto *ct = qt->getAs<ComplexType>()) {
    info.name = "complex";
    info.encoding = ct->isComplexIntegerType() ? llvm::dwarf::DW_ATE_lo_user
                                               : llvm::dwarf::DW_ATE_complex_float;
    info.sizeInBits = ctx.getTypeSize(ctx.getCanonicalType(qt));
    return info;
  }

  if (const auto *vt = qt->getAs<VectorType>()) {
    if (auto eltDbg = getBuiltinDebugTypeInfoFromQualType(ctx, vt->getElementType())) {
      info.name = eltDbg->name;
      info.encoding = eltDbg->encoding;
      info.sizeInBits = ctx.getTypeSize(ctx.getCanonicalType(vt->getElementType()));
      return info;
    }
    return std::nullopt;
  }

  const auto *bt = qt->getAs<BuiltinType>();
  if (!bt)
    return std::nullopt;

  switch (bt->getKind()) {
  case BuiltinType::UChar:
  case BuiltinType::Char_U:
    info.encoding = llvm::dwarf::DW_ATE_unsigned_char;
    break;
  case BuiltinType::Char_S:
  case BuiltinType::SChar:
    info.encoding = llvm::dwarf::DW_ATE_signed_char;
    break;
  case BuiltinType::Char8:
  case BuiltinType::Char16:
  case BuiltinType::Char32:
    info.encoding = llvm::dwarf::DW_ATE_UTF;
    break;
  case BuiltinType::UShort:
  case BuiltinType::UInt:
  case BuiltinType::UInt128:
  case BuiltinType::ULong:
  case BuiltinType::WChar_U:
  case BuiltinType::ULongLong:
    info.encoding = llvm::dwarf::DW_ATE_unsigned;
    break;
  case BuiltinType::Short:
  case BuiltinType::Int:
  case BuiltinType::Int128:
  case BuiltinType::Long:
  case BuiltinType::WChar_S:
  case BuiltinType::LongLong:
    info.encoding = llvm::dwarf::DW_ATE_signed;
    break;
  case BuiltinType::Bool:
    info.encoding = llvm::dwarf::DW_ATE_boolean;
    break;
  case BuiltinType::Half:
  case BuiltinType::Float:
  case BuiltinType::LongDouble:
  case BuiltinType::Float16:
  case BuiltinType::BFloat16:
  case BuiltinType::Float128:
  case BuiltinType::Double:
  case BuiltinType::Ibm128:
    info.encoding = llvm::dwarf::DW_ATE_float;
    break;
  default:
    return std::nullopt;
  }

  info.name = bt->getName(ctx.getPrintingPolicy()).str();
  info.sizeInBits = ctx.getTypeSize(ctx.getCanonicalType(qt));
  return info;
}

static std::optional<EnumDebugTypeInfo>
getEnumDebugTypeInfoFromQualType(ASTContext &ctx, QualType qt) {
  if (qt.isNull())
    return std::nullopt;

  while (!qt.isNull()) {
    if (const auto *pt = qt->getAs<ParenType>()) {
      qt = pt->getInnerType();
      continue;
    }
    if (const auto *at = qt->getAs<AttributedType>()) {
      qt = at->getModifiedType();
      continue;
    }
    if (const auto *tt = qt->getAs<TypedefType>()) {
      qt = tt->desugar();
      continue;
    }
    if (const auto *sat = qt->getAs<SubstTemplateTypeParmType>()) {
      qt = sat->getReplacementType();
      continue;
    }
    break;
  }

  const auto *et = qt->getAs<EnumType>();
  if (!et)
    return std::nullopt;
  const auto *ed = et->getDecl();
  if (!ed)
    return std::nullopt;

  EnumDebugTypeInfo info;
  info.name = ed->getName().str();
  info.isScoped = ed->isScoped();
  // C++ externally-visible tag types carry an RTTI-mangled identifier for ODR
  // type uniquing (classic CodeGen's getTypeIdentifier). C enums get none.
  if (ctx.getLangOpts().CPlusPlus && ed->isExternallyVisible()) {
    auto mangler =
        std::unique_ptr<MangleContext>(ctx.createMangleContext());
    llvm::raw_string_ostream out(info.identifier);
    mangler->mangleCXXRTTIName(QualType(et, 0), out);
  }

  SourceLocation loc = ed->getLocation();
  const SourceManager &sm = ctx.getSourceManager();
  if (loc.isValid()) {
    SourceLocation presumedLoc = sm.getExpansionLoc(loc);
    PresumedLoc ploc = sm.getPresumedLoc(presumedLoc);
    if (ploc.isValid()) {
      info.filePath = ploc.getFilename();
      info.line = ploc.getLine();
    } else {
      SourceLocation spellingLoc = sm.getSpellingLoc(loc);
      if (spellingLoc.isValid()) {
        if (const FileEntry *fe = sm.getFileEntryForID(sm.getFileID(spellingLoc)))
          info.filePath = fe->tryGetRealPathName().str();
        info.line = sm.getSpellingLineNumber(spellingLoc);
      }
    }
  }

  QualType baseType = ed->getIntegerType();
  if (baseType.isNull())
    return std::nullopt;
  if (auto baseDbg = getBuiltinDebugTypeInfoFromQualType(ctx, baseType)) {
    info.baseName = baseDbg->name;
    info.baseEncoding = baseDbg->encoding;
    info.baseSizeInBits = baseDbg->sizeInBits;
  } else {
    return std::nullopt;
  }

  for (const EnumConstantDecl *ecd : ed->enumerators()) {
    info.enumeratorNames.push_back(ecd->getName().str());
    // Extract the value respecting the enumerator's signedness so an unsigned
    // enumerator like `VB = 255` in `enum : unsigned char` keeps the value 255
    // rather than sign-extending the 0xFF bit pattern to -1.
    const llvm::APSInt &initVal = ecd->getInitVal();
    info.enumeratorValues.push_back(initVal.isSigned()
                                        ? initVal.getSExtValue()
                                        : static_cast<int64_t>(
                                              initVal.getZExtValue()));
    // Enumerators within an enum share one signedness; record it so the DWARF
    // DIEnumerator nodes are marked correctly (matches OGCG's per-enumerator
    // APSInt::isUnsigned()).
    info.enumeratorsUnsigned = initVal.isUnsigned();
  }

  return info;
}

static void populateTypedefQualifierDebugInfo(ASTContext &ctx,
                                              QualType underlying,
                                              TypedefDebugInfo &info) {
  info.isConstQualified = underlying.isConstQualified();
  info.isVolatileQualified = underlying.isVolatileQualified();
  info.isRestrictQualified = underlying.isRestrictQualified();

  QualType unwrapped = underlying.getNonReferenceType().getUnqualifiedType();
  if (const auto *parenTy = unwrapped->getAs<ParenType>())
    unwrapped = parenTy->getInnerType();
  if (const auto *attrTy = unwrapped->getAs<AttributedType>())
    unwrapped = attrTy->getModifiedType();

  if (const auto *ptrTy = unwrapped->getAs<PointerType>()) {
    info.underlyingIsPointer = true;
    QualType pointee = ptrTy->getPointeeType();
    info.pointeeConstQualified = pointee.isConstQualified();
    info.pointeeVolatileQualified = pointee.isVolatileQualified();
    info.pointeeRestrictQualified = pointee.isRestrictQualified();
    QualType unqualPointee = pointee.getUnqualifiedType();
    if (const auto *builtin = unqualPointee->getAs<BuiltinType>()) {
      if (builtin->isInteger()) {
        info.pointeeSizeInBits = ctx.getTypeSize(unqualPointee);
        if (builtin->getKind() == BuiltinType::Bool) {
          info.pointeeName = "bool";
          info.pointeeEncoding = llvm::dwarf::DW_ATE_boolean;
        } else if (builtin->getKind() == BuiltinType::Char_U) {
          // Plain `char` (unsigned platform): named "char", not "unsigned char".
          info.pointeeName = "char";
          info.pointeeEncoding = llvm::dwarf::DW_ATE_unsigned_char;
        } else if (builtin->getKind() == BuiltinType::UChar) {
          info.pointeeName = "unsigned char";
          info.pointeeEncoding = llvm::dwarf::DW_ATE_unsigned_char;
        } else if (builtin->getKind() == BuiltinType::Char_S) {
          // Plain `char` (signed platform): named "char", not "signed char".
          info.pointeeName = "char";
          info.pointeeEncoding = llvm::dwarf::DW_ATE_signed_char;
        } else if (builtin->getKind() == BuiltinType::SChar) {
          info.pointeeName = "signed char";
          info.pointeeEncoding = llvm::dwarf::DW_ATE_signed_char;
        } else if (builtin->isUnsignedInteger()) {
          info.pointeeName = "unsigned int";
          info.pointeeEncoding = llvm::dwarf::DW_ATE_unsigned;
        } else {
          info.pointeeName = "int";
          info.pointeeEncoding = llvm::dwarf::DW_ATE_signed;
        }
      }
    }

    QualType typedefPointee = pointee.getNonReferenceType().getUnqualifiedType();
    if (const auto *parenTy = typedefPointee->getAs<ParenType>())
      typedefPointee = parenTy->getInnerType();
    if (const auto *attrTy = typedefPointee->getAs<AttributedType>())
      typedefPointee = attrTy->getModifiedType();

    if (const auto *pointeeTT = typedefPointee->getAs<TypedefType>()) {
      const TypedefNameDecl *pointeeTD = pointeeTT->getDecl();
      info.pointeeTypedefName = pointeeTD->getNameAsString();
      QualType pointeeUnderlying = pointeeTD->getUnderlyingType();
      QualType unwrappedPointeeUnderlying =
          pointeeUnderlying.getNonReferenceType().getUnqualifiedType();
      if (const auto *parenTy = unwrappedPointeeUnderlying->getAs<ParenType>())
        unwrappedPointeeUnderlying = parenTy->getInnerType();
      if (const auto *attrTy =
              unwrappedPointeeUnderlying->getAs<AttributedType>())
        unwrappedPointeeUnderlying = attrTy->getModifiedType();
      if (const auto *vecTy = unwrappedPointeeUnderlying->getAs<VectorType>()) {
        info.pointeeTypedefUnderlyingIsVector = true;
        info.pointeeTypedefVectorElementCount = vecTy->getNumElements();
        if (auto elemDbg =
                getBuiltinDebugTypeInfoFromQualType(ctx, vecTy->getElementType())) {
          info.pointeeTypedefElementName = elemDbg->name;
          info.pointeeTypedefElementEncoding = elemDbg->encoding;
          info.pointeeTypedefElementSizeInBits = elemDbg->sizeInBits;
        }
      }
      SourceLocation pointeeLoc = pointeeTD->getLocation();
      const SourceManager &sm = ctx.getSourceManager();
      if (pointeeLoc.isValid()) {
        SourceLocation presumedLoc = sm.getExpansionLoc(pointeeLoc);
        PresumedLoc ploc = sm.getPresumedLoc(presumedLoc);
        if (ploc.isValid()) {
          info.pointeeTypedefFilePath = ploc.getFilename();
          info.pointeeTypedefLine = ploc.getLine();
        } else {
          SourceLocation spellingLoc = sm.getSpellingLoc(pointeeLoc);
          if (spellingLoc.isValid()) {
            if (const FileEntry *fe =
                    sm.getFileEntryForID(sm.getFileID(spellingLoc)))
              info.pointeeTypedefFilePath = fe->tryGetRealPathName().str();
            info.pointeeTypedefLine = sm.getSpellingLineNumber(spellingLoc);
          }
        }
      }
    }
  }

  if (const auto *vecTy = unwrapped->getAs<VectorType>()) {
    info.underlyingIsVector = true;
    info.vectorElementCount = vecTy->getNumElements();
  }
}

static llvm::SmallVector<TemplateArgument>
getTypeAliasTemplateArgs(const TemplateDecl *td,
                         const TemplateSpecializationType *ty) {
  llvm::SmallVector<TemplateArgument> specArgs;
  ArrayRef substArgs = ty->template_arguments();
  for (const NamedDecl *param : td->getTemplateParameters()->asArray()) {
    if (param->isParameterPack()) {
      specArgs.push_back(TemplateArgument(substArgs));
      break;
    }
    if (substArgs.empty())
      break;
    specArgs.push_back(substArgs.front());
    substArgs = substArgs.drop_front();
  }
  return specArgs;
}

static std::optional<TypedefDebugInfo>
getTypedefDebugInfoFromQualType(ASTContext &ctx, QualType qt,
                                bool pointerContext = false,
                                bool arrayElementContext = false) {
  if (qt.isNull())
    return std::nullopt;

  if (const auto *tt = qt->getAs<TypedefType>()) {
    const TypedefNameDecl *td = tt->getDecl();
    TypedefDebugInfo info;
    info.name = td->getNameAsString();
    info.btfDeclTags = collectBTFDeclTagStrings(td);
    info.isArrayElementTypedef = arrayElementContext;
    info.isPointerPointee = pointerContext;
    QualType underlying = td->getUnderlyingType();
    info.hasNullBaseType = underlying->isVoidType();
    populateTypedefQualifierDebugInfo(ctx, underlying, info);
    if (const auto *arrTy = dyn_cast<ArrayType>(underlying.getTypePtr())) {
      if (auto elemTypedef = getTypedefDebugInfoFromQualType(
              ctx, arrTy->getElementType(), /*pointerContext=*/false,
              /*arrayElementContext=*/true)) {
        info.underlyingArrayElementTypedefName = elemTypedef->name;
        info.underlyingArrayElementTypedefFilePath = elemTypedef->filePath;
        info.underlyingArrayElementTypedefLine = elemTypedef->line;
      }
    }
    SourceLocation loc = td->getLocation();
    const SourceManager &sm = ctx.getSourceManager();
    if (loc.isValid()) {
      SourceLocation presumedLoc = sm.getExpansionLoc(loc);
      PresumedLoc ploc = sm.getPresumedLoc(presumedLoc);
      if (ploc.isValid()) {
        info.filePath = ploc.getFilename();
        info.line = ploc.getLine();
      } else {
        SourceLocation spellingLoc = sm.getSpellingLoc(loc);
        if (spellingLoc.isValid()) {
          if (const FileEntry *fe =
                  sm.getFileEntryForID(sm.getFileID(spellingLoc)))
            info.filePath = fe->tryGetRealPathName().str();
          info.line = sm.getSpellingLineNumber(spellingLoc);
        }
      }
    }
    return info;
  }
  if (const auto *ty = qt->getAs<TemplateSpecializationType>()) {
    if (!ty->isTypeAlias())
      return std::nullopt;
    const TemplateDecl *td = ty->getTemplateName().getAsTemplateDecl();
    if (!td || isa<BuiltinTemplateDecl>(td))
      return std::nullopt;
    auto *aliasDecl = dyn_cast<TypeAliasTemplateDecl>(td);
    if (!aliasDecl)
      return std::nullopt;

    SmallString<128> name;
    llvm::raw_svector_ostream os(name);
    PrintingPolicy pp = ctx.getPrintingPolicy();
    pp.PrintAsCanonical = true;
    pp.SplitTemplateClosers = true;
    ty->getTemplateName().print(os, pp, TemplateName::Qualified::None);
    auto args = getTypeAliasTemplateArgs(td, ty);
    printTemplateArgumentList(os, args, pp,
                              td->getTemplateParameters());

    TypedefDebugInfo info;
    info.name = name.str().str();
    info.btfDeclTags = collectBTFDeclTagStrings(aliasDecl->getTemplatedDecl());
    info.isArrayElementTypedef = arrayElementContext;
    info.isPointerPointee = pointerContext;
    QualType underlying = aliasDecl->getTemplatedDecl()->getUnderlyingType();
    info.hasNullBaseType = underlying->isVoidType();
    populateTypedefQualifierDebugInfo(ctx, underlying, info);
    if (const auto *arrTy = dyn_cast<ArrayType>(underlying.getTypePtr())) {
      if (auto elemTypedef = getTypedefDebugInfoFromQualType(
              ctx, arrTy->getElementType(), /*pointerContext=*/false,
              /*arrayElementContext=*/true)) {
        info.underlyingArrayElementTypedefName = elemTypedef->name;
        info.underlyingArrayElementTypedefFilePath = elemTypedef->filePath;
        info.underlyingArrayElementTypedefLine = elemTypedef->line;
      }
    }
    SourceLocation loc = aliasDecl->getTemplatedDecl()->getLocation();
    const SourceManager &sm = ctx.getSourceManager();
    if (loc.isValid()) {
      SourceLocation presumedLoc = sm.getExpansionLoc(loc);
      PresumedLoc ploc = sm.getPresumedLoc(presumedLoc);
      if (ploc.isValid()) {
        info.filePath = ploc.getFilename();
        info.line = ploc.getLine();
      } else {
        SourceLocation spellingLoc = sm.getSpellingLoc(loc);
        if (spellingLoc.isValid()) {
          if (const FileEntry *fe =
                  sm.getFileEntryForID(sm.getFileID(spellingLoc)))
            info.filePath = fe->tryGetRealPathName().str();
          info.line = sm.getSpellingLineNumber(spellingLoc);
        }
      }
    }
    return info;
  }

  if (const auto *pt = qt->getAs<PointerType>())
    return getTypedefDebugInfoFromQualType(ctx, pt->getPointeeType(),
                                           /*pointerContext=*/true,
                                           arrayElementContext);
  if (const auto *pt = qt->getAs<ReferenceType>())
    return getTypedefDebugInfoFromQualType(ctx, pt->getPointeeType(),
                                           pointerContext, arrayElementContext);
  if (const auto *at = dyn_cast<ArrayType>(qt.getTypePtr()))
    return getTypedefDebugInfoFromQualType(ctx, at->getElementType(),
                                           pointerContext,
                                           /*arrayElementContext=*/true);
  if (const auto *at = qt->getAs<AttributedType>())
    return getTypedefDebugInfoFromQualType(ctx, at->getModifiedType(),
                                           pointerContext, arrayElementContext);
  if (const auto *pt = qt->getAs<ParenType>())
    return getTypedefDebugInfoFromQualType(ctx, pt->getInnerType(),
                                           pointerContext, arrayElementContext);
  return std::nullopt;
}

static const RecordDecl *getRecordDeclFromQualTypeForDebug(QualType qt) {
  if (qt.isNull())
    return nullptr;

  while (!qt.isNull()) {
    if (const auto *rt = qt->getAs<RecordType>())
      return rt->getDecl();
    if (const auto *tt = qt->getAs<TypedefType>()) {
      qt = tt->getDecl()->getUnderlyingType();
      continue;
    }
    if (const auto *pt = qt->getAs<PointerType>()) {
      qt = pt->getPointeeType();
      continue;
    }
    if (const auto *at = dyn_cast<ArrayType>(qt.getTypePtr())) {
      qt = at->getElementType();
      continue;
    }
    if (const auto *ref = qt->getAs<ReferenceType>()) {
      qt = ref->getPointeeType();
      continue;
    }
    if (const auto *attr = qt->getAs<AttributedType>()) {
      qt = attr->getModifiedType();
      continue;
    }
    if (const auto *paren = qt->getAs<ParenType>()) {
      qt = paren->getInnerType();
      continue;
    }
    break;
  }
  return nullptr;
}

static llvm::SmallVector<mlir::Attribute>
collectCIRBTFDeclTagAttrs(mlir::MLIRContext &ctx, const Decl *decl) {
  llvm::SmallVector<mlir::Attribute> tags;
  if (!decl)
    return tags;
  for (const auto *tag : decl->specific_attrs<BTFDeclTagAttr>())
    tags.push_back(mlir::StringAttr::get(&ctx, tag->getBTFDeclTag()));
  return tags;
}

static llvm::SmallVector<std::string, 2>
collectBTFDeclTagStrings(const Decl *decl) {
  llvm::SmallVector<std::string, 2> tags;
  if (!decl)
    return tags;
  for (const auto *tag : decl->specific_attrs<BTFDeclTagAttr>())
    tags.push_back(tag->getBTFDeclTag().str());
  return tags;
}

static llvm::DINode::DIFlags getFieldAccessDebugFlag(const CXXRecordDecl *rd,
                                                     AccessSpecifier access) {
  AccessSpecifier defaultAccess = AS_none;
  if (rd && rd->isClass())
    defaultAccess = AS_private;
  else if (rd && (rd->isStruct() || rd->isUnion()))
    defaultAccess = AS_public;

  if (access == defaultAccess)
    return llvm::DINode::FlagZero;

  switch (access) {
  case AS_private:
    return llvm::DINode::FlagPrivate;
  case AS_protected:
    return llvm::DINode::FlagProtected;
  case AS_public:
    return llvm::DINode::FlagPublic;
  case AS_none:
    return llvm::DINode::FlagZero;
  }
  return llvm::DINode::FlagZero;
}

static void appendUniqueModuleStringAttr(mlir::ModuleOp module,
                                         mlir::Builder &builder,
                                         llvm::StringRef attrName,
                                         llvm::StringRef value) {
  auto existing = mlir::dyn_cast_or_null<mlir::ArrayAttr>(module->getAttr(attrName));
  if (existing) {
    for (mlir::Attribute attr : existing) {
      auto str = mlir::dyn_cast<mlir::StringAttr>(attr);
      if (str && str.getValue() == value)
        return;
    }
  }

  llvm::SmallVector<mlir::Attribute> values;
  if (existing)
    values.append(existing.begin(), existing.end());
  values.push_back(builder.getStringAttr(value));
  module->setAttr(attrName, builder.getArrayAttr(values));
}
} // namespace

static bool isObjCImplementationNonLazy(const ObjCImplDecl *od);

void CIRGenModule::markAsIgnoreThreadCheckingAtRuntime(cir::FuncOp fn) {
  mlir::NamedAttrList attrs;
  if (auto extraAttrs = fn.getExtraAttrs())
    attrs = mlir::NamedAttrList{extraAttrs->getElements().getValue()};
  attrs.erase("sanitize_thread");
  attrs.set("sanitize_thread_no_checking_at_run_time",
            mlir::UnitAttr::get(&getMLIRContext()));
  fn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
      attrs.getDictionary(&getMLIRContext())));
}

static CIRGenCXXABI *createCXXABI(CIRGenModule &cgm) {
  switch (cgm.getASTContext().getCXXABIKind()) {
  case TargetCXXABI::GenericItanium:
  case TargetCXXABI::GenericAArch64:
  case TargetCXXABI::AppleARM64:
  case TargetCXXABI::Fuchsia:
  case TargetCXXABI::GenericARM:
  case TargetCXXABI::iOS:
  case TargetCXXABI::WatchOS:
  case TargetCXXABI::GenericMIPS:
  case TargetCXXABI::WebAssembly:
  case TargetCXXABI::XL:
    return CreateCIRGenItaniumCXXABI(cgm);
  case TargetCXXABI::Microsoft:
    if (!cgm.getLangOpts().CPlusPlus)
      return CreateCIRGenItaniumCXXABI(cgm);
    return CreateCIRGenMicrosoftCXXABI(cgm);
  }

  llvm_unreachable("invalid C++ ABI kind");
}

CIRGenModule::CIRGenModule(mlir::MLIRContext &mlirContext,
                           clang::ASTContext &astContext,
                           const clang::CodeGenOptions &cgo,
                           DiagnosticsEngine &diags, bool lowersToLLVMIR)
    : builder(mlirContext, *this), astContext(astContext),
      langOpts(astContext.getLangOpts()), codeGenOpts(cgo),
      lowersToLLVMIR(lowersToLLVMIR),
      theModule{mlir::ModuleOp::create(mlir::UnknownLoc::get(&mlirContext))},
      diags(diags), target(astContext.getTargetInfo()),
      abi(createCXXABI(*this)), genTypes(*this), vtables(*this) {

  // Initialize cached types
  voidTy = cir::VoidType::get(&getMLIRContext());
  voidPtrTy = cir::PointerType::get(voidTy);
  sInt8Ty = cir::IntType::get(&getMLIRContext(), 8, /*isSigned=*/true);
  sInt16Ty = cir::IntType::get(&getMLIRContext(), 16, /*isSigned=*/true);
  sInt32Ty = cir::IntType::get(&getMLIRContext(), 32, /*isSigned=*/true);
  sInt64Ty = cir::IntType::get(&getMLIRContext(), 64, /*isSigned=*/true);
  sInt128Ty = cir::IntType::get(&getMLIRContext(), 128, /*isSigned=*/true);
  uInt8Ty = cir::IntType::get(&getMLIRContext(), 8, /*isSigned=*/false);
  uInt8PtrTy = cir::PointerType::get(uInt8Ty);
  cirAllocaAddressSpace = getTargetCIRGenInfo().getCIRAllocaAddressSpace();
  uInt16Ty = cir::IntType::get(&getMLIRContext(), 16, /*isSigned=*/false);
  uInt32Ty = cir::IntType::get(&getMLIRContext(), 32, /*isSigned=*/false);
  uInt64Ty = cir::IntType::get(&getMLIRContext(), 64, /*isSigned=*/false);
  uInt128Ty = cir::IntType::get(&getMLIRContext(), 128, /*isSigned=*/false);
  fP16Ty = cir::FP16Type::get(&getMLIRContext());
  bFloat16Ty = cir::BF16Type::get(&getMLIRContext());
  floatTy = cir::SingleType::get(&getMLIRContext());
  doubleTy = cir::DoubleType::get(&getMLIRContext());
  fP80Ty = cir::FP80Type::get(&getMLIRContext());
  fP128Ty = cir::FP128Type::get(&getMLIRContext());

  allocaInt8PtrTy = cir::PointerType::get(uInt8Ty, cirAllocaAddressSpace);

  // Pre-cache common alignment IntegerAttr values to avoid repeated MLIR
  // uniquer lookups (2 lookups per alignment: IntegerType + IntegerAttr).
  auto &b = getBuilder();
  for (unsigned i = 0; i < 6; ++i)
    alignmentAttrCache[i] = b.getI64IntegerAttr(1ULL << i);
  b.setAlignmentAttrCache(alignmentAttrCache);

  PointerAlignInBytes =
      astContext
          .toCharUnitsFromBits(
              astContext.getTargetInfo().getPointerAlign(LangAS::Default))
          .getQuantity();

  const unsigned charSize = astContext.getTargetInfo().getCharWidth();
  uCharTy = cir::IntType::get(&getMLIRContext(), charSize, /*isSigned=*/false);

  const unsigned sizeTypeSize =
      astContext.getTypeSize(astContext.getSignedSizeType());
  SizeSizeInBytes = astContext.toCharUnitsFromBits(sizeTypeSize).getQuantity();
  // In CIRGenTypeCache, UIntPtrTy and SizeType are fields of the same union
  uIntPtrTy =
      cir::IntType::get(&getMLIRContext(), sizeTypeSize, /*isSigned=*/false);
  ptrDiffTy =
      cir::IntType::get(&getMLIRContext(), sizeTypeSize, /*isSigned=*/true);

  tbaa = std::make_unique<CIRGenTBAA>(&mlirContext, astContext, genTypes,
                                      theModule, cgo, langOpts);

  std::optional<cir::SourceLanguage> sourceLanguage = getCIRSourceLanguage();
  if (sourceLanguage)
    theModule->setAttr(
        cir::CIRDialect::getSourceLanguageAttrName(),
        cir::SourceLanguageAttr::get(&mlirContext, *sourceLanguage));
  theModule->setAttr(cir::CIRDialect::getTripleAttrName(),
                     getStringAttrCached(getTriple().str()));
  cir::advanceStage(theModule, cir::CIRStage::CIRGen);
  if (!getTarget().getABI().empty())
    theModule->setAttr(cir::CIRDialect::getTargetABIAttrName(),
                       getStringAttrCached(getTarget().getABI().str()));
  if (langOpts.getClangABICompat() != clang::LangOptions::ClangABI::Latest)
    theModule->setAttr(
        "cir.clang_abi_compat",
        builder.getI32IntegerAttr(
            static_cast<unsigned>(langOpts.getClangABICompat())));
  // -fc++-abi= overrides the target's default C++ ABI. The target-lowering pass
  // rebuilds LangOptions from module attributes, and without this it falls back
  // to the target default and picks the wrong method-pointer representation.
  // Stored as the spelling, not the enum value, so it survives an upstream
  // renumbering of TargetCXXABI::Kind.
  if (langOpts.CXXABI)
    theModule->setAttr(
        "cir.cxx_abi_kind",
        getStringAttrCached(clang::TargetCXXABI::getSpelling(*langOpts.CXXABI)));
  if (langOpts.RelativeCXXABIVTables)
    theModule->setAttr("cir.relative_cxx_abi_vtables",
                       mlir::UnitAttr::get(&mlirContext));
  // The target-lowering pass reconstructs LangOptions from module attributes and
  // needs LangOpts.HIP to decide whether kernel pointer arguments are coerced to
  // the global address space (matching classic AMDGPUABIInfo, which gates the
  // coercion on getLangOpts().HIP). Propagate it via a unit attribute.
  if (langOpts.HIP)
    theModule->setAttr("cir.hip", mlir::UnitAttr::get(&mlirContext));

  // Source-based coverage mapping is emitted after direct-CIR has lowered to
  // LLVM IR, so non-LLVM ClangIR outputs still reject the mode here.
  if (cgo.CoverageMapping && !lowersToLLVMIR)
    errorNYI("source-based coverage mapping (-fcoverage-mapping)");

  // MC/DC coverage instrumentation (test-vector bitmaps and per-condition
  // tracking) is not yet implemented in ClangIR. The shared coverage-mapping
  // generator still emits MC/DC decision regions, so without the backing
  // instrumentation llvm-cov silently reports 0% MC/DC coverage regardless of
  // the tests run; reject the mode rather than emit a misleading binary.
  if (cgo.MCDCCoverage)
    errorNYI("MC/DC code coverage (-fcoverage-mcdc)");

  if (!cgo.CoverageNotesFile.empty())
    theModule->setAttr("cir.coverage_notes_file",
                       getStringAttrCached(cgo.CoverageNotesFile));
  if (!cgo.CoverageDataFile.empty())
    theModule->setAttr("cir.coverage_data_file",
                       getStringAttrCached(cgo.CoverageDataFile));

  // Record -main-file-name so the CIR->LLVM lowering can reproduce classic's
  // compile-unit DW_AT_name (the real source path when set, a synthetic
  // "<stdin>" otherwise). Empty when -main-file-name was not provided.
  if (!cgo.MainFileName.empty())
    theModule->setAttr("cir.main_file_name",
                       getStringAttrCached(cgo.MainFileName));

  // Record -fdebug-compilation-dir and -fdebug-prefix-map so the lowering can
  // give the compile unit classic's DW_AT_comp_dir. Without it, every relative
  // path in the unit's line table has no base directory.
  if (!cgo.DebugCompilationDir.empty())
    theModule->setAttr("cir.debug_compilation_dir",
                       getStringAttrCached(cgo.DebugCompilationDir));
  if (!cgo.DebugPrefixMap.empty()) {
    llvm::SmallVector<mlir::Attribute> prefixMap;
    for (const auto &[from, to] : cgo.DebugPrefixMap)
      prefixMap.push_back(builder.getStrArrayAttr({from, to}));
    theModule->setAttr("cir.debug_prefix_map", builder.getArrayAttr(prefixMap));
  }

  if (cgo.OptimizationLevel > 0 || cgo.OptimizeSize > 0)
    theModule->setAttr(cir::CIRDialect::getOptInfoAttrName(),
                       cir::OptInfoAttr::get(&mlirContext,
                                             cgo.OptimizationLevel,
                                             cgo.OptimizeSize));

  // Record profile-use path for Clang PGO use so CIR->LLVM lowering can
  // materialize module profile summary metadata, matching OGCG Release().
  if (cgo.hasProfileClangUse()) {
    theModule->setAttr("cir.profile_clang_use_path",
                       getStringAttrCached(cgo.ProfileInstrumentUsePath));
    if (!cgo.ProfileRemappingFile.empty())
      theModule->setAttr("cir.profile_remapping_file",
                         getStringAttrCached(cgo.ProfileRemappingFile));
  }

  // Record Clang profile-instrumentation mode so CIR->LLVM lowering can emit
  // module-level profiling metadata (EnableValueProfiling), matching OGCG's
  // CodeGenPGO hooks in Release().
  if (cgo.hasProfileClangInstr())
    theModule->setAttr("cir.profile_clang_instr",
                       mlir::UnitAttr::get(&mlirContext));

  if (cgo.RelaxedAliasing)
    theModule->setAttr("cir.relaxed_aliasing",
                       mlir::UnitAttr::get(&mlirContext));

  if (cgo.NewStructPathTBAA)
    theModule->setAttr(cir::CIRDialect::getNewStructPathTBAAAttrName(),
                       mlir::UnitAttr::get(&mlirContext));

  if (langOpts.AllocTokenMode)
    theModule->setAttr(
        "cir.alloc_token_mode",
        mlir::StringAttr::get(
            &mlirContext,
            llvm::getAllocTokenModeAsString(*langOpts.AllocTokenMode)));
  if (langOpts.AllocTokenMax)
    theModule->setAttr(
        "cir.alloc_token_max",
        mlir::IntegerAttr::get(mlir::IntegerType::get(&mlirContext, 64),
                               *langOpts.AllocTokenMax));
  if (cgo.SanitizeAllocTokenFastABI)
    theModule->setAttr("cir.alloc_token_fast_abi",
                       mlir::UnitAttr::get(&mlirContext));
  if (cgo.SanitizeAllocTokenExtended)
    theModule->setAttr("cir.alloc_token_extended",
                       mlir::UnitAttr::get(&mlirContext));

  // Record when the canonical "int" TBAA tag exists so DirectToLLVM can
  // emit !llvm.errno.tbaa with the same enablement as OGCG.
  if (getTBAAAccessTagInfo(getTBAAAccessInfo(astContext.IntTy)))
    theModule->setAttr("cir.emit_errno_tbaa",
                       mlir::UnitAttr::get(&mlirContext));

  // Emit KCFI module-level attributes when -fsanitize=kcfi is enabled.
  // Matches OGCG CodeGenModule.cpp:1263-1278.
  if (langOpts.Sanitize.has(SanitizerKind::KCFI)) {
    theModule->setAttr("cir.kcfi", mlir::UnitAttr::get(&mlirContext));
    if (cgo.PatchableFunctionEntryOffset)
      theModule->setAttr(
          "cir.kcfi_offset",
          mlir::IntegerAttr::get(mlir::IntegerType::get(&mlirContext, 32),
                                 cgo.PatchableFunctionEntryOffset));
    if (cgo.SanitizeKcfiArity)
      theModule->setAttr("cir.kcfi_arity",
                         mlir::UnitAttr::get(&mlirContext));
    theModule->setAttr(
        "cir.kcfi_hash",
        mlir::StringAttr::get(
            &mlirContext,
            llvm::stringifyKCFIHashAlgorithm(cgo.SanitizeKcfiHash)));
  }

  // Record sanitizer kinds that affect target-lowering ABI decisions.
  // LowerModule reconstructs these LangOptions bits before applying
  // OGCG-style strict-return logic in call-conv lowering.
  if (langOpts.Sanitize.has(SanitizerKind::Memory))
    theModule->setAttr("cir.sanitize_memory",
                       mlir::UnitAttr::get(&mlirContext));
  if (langOpts.Sanitize.has(SanitizerKind::Return))
    theModule->setAttr("cir.sanitize_return",
                       mlir::UnitAttr::get(&mlirContext));

  // Record sanitize-binary-metadata options so serialized CIR can recover the
  // same post-lowering LLVM instrumentation that direct -emit-llvm gets via
  // BackendUtil's SanitizerBinaryMetadataPass hook.
  if (cgo.SanitizeBinaryMetadataCovered)
    theModule->setAttr("cir.sanitize_binary_metadata_covered",
                       mlir::UnitAttr::get(&mlirContext));
  if (cgo.SanitizeBinaryMetadataAtomics)
    theModule->setAttr("cir.sanitize_binary_metadata_atomics",
                       mlir::UnitAttr::get(&mlirContext));
  if (cgo.SanitizeBinaryMetadataUAR)
    theModule->setAttr("cir.sanitize_binary_metadata_uar",
                       mlir::UnitAttr::get(&mlirContext));
  if (!cgo.SanitizeMetadataIgnorelistFiles.empty()) {
    llvm::SmallVector<mlir::Attribute> ignorelistAttrs;
    ignorelistAttrs.reserve(cgo.SanitizeMetadataIgnorelistFiles.size());
    for (llvm::StringRef path : cgo.SanitizeMetadataIgnorelistFiles)
      ignorelistAttrs.push_back(getStringAttrCached(path));
    theModule->setAttr("cir.sanitize_binary_metadata_ignorelist_files",
                       mlir::ArrayAttr::get(&mlirContext, ignorelistAttrs));
  }

  // Record when noundef analysis is disabled (-no-enable-noundef-analysis).
  // The target lowering pass reads this to skip cir.noundef emission.
  if (!cgo.EnableNoundefAttrs)
    theModule->setAttr("cir.no_noundef_analysis",
                       mlir::UnitAttr::get(&mlirContext));

  // Record when __cxa_atexit is disabled (-fno-use-cxa-atexit).
  // LoweringPrepare reads this to emit atexit() instead of __cxa_atexit().
  if (!cgo.CXAAtExit)
    theModule->setAttr("cir.no_cxa_atexit",
                       mlir::UnitAttr::get(&mlirContext));

  // Record the function-pointer ptrauth payload for static destructor
  // registration callbacks. OGCG signs both __cxa_atexit callbacks and
  // atexit wrapper stubs as function pointers of type void(void *).
  {
    FunctionProtoType::ExtProtoInfo EPI(
        astContext.getDefaultCallingConvention(/*IsVariadic=*/false,
                                               /*IsCXXMethod=*/false));
    QualType AtExitCallbackTy =
        astContext.getFunctionType(astContext.VoidTy, {astContext.VoidPtrTy},
                                   EPI);
    if (auto pointerAuth = getFunctionPointerAuthInfo(AtExitCallbackTy)) {
      theModule->setAttr(
          "cir.atexit_callback_ptrauth",
          mlir::ArrayAttr::get(
              &mlirContext,
              {builder.getI32IntegerAttr(pointerAuth->key),
               builder.getI64IntegerAttr(pointerAuth->discriminator)}));
    }
  }

  // Record -fpass-by-value-is-noalias for target lowering to emit noalias
  // on indirect pass-by-value parameters of trivially copyable types.
  if (cgo.PassByValueIsNoAlias)
    theModule->setAttr("cir.pass_by_value_is_noalias",
                       mlir::UnitAttr::get(&mlirContext));

  // Record -fno-strict-return for target lowering to skip noundef on
  // return values of trivially copyable types (OGCG HasStrictReturn).
  if (!cgo.StrictReturn)
    theModule->setAttr("cir.no_strict_return",
                       mlir::UnitAttr::get(&mlirContext));

  // Record -fno-pointer-tbaa for target lowering, whose reconstructed
  // CodeGenOptions would otherwise leave PointerTBAA at its default of 1 and
  // tag pointers "p<depth> <pointee>" where OGCG uses "any pointer".
  if (!cgo.PointerTBAA)
    theModule->setAttr("cir.no_pointer_tbaa",
                       mlir::UnitAttr::get(&mlirContext));

  // Record -mlong-double-64/-mlong-double-128 (LangOpts.LongDoubleSize) so the
  // target-lowering pass can re-apply the override to its reconstructed
  // TargetInfo. Without it, that pass's TargetInfo keeps the target-native
  // long double format and misclassifies long double return values (e.g.
  // coercing an x86_64 `long double` return to x86_fp80 even under
  // -mlong-double-64/-128).
  if (langOpts.LongDoubleSize)
    theModule->setAttr(
        "cir.long_double_size",
        mlir::IntegerAttr::get(mlir::IntegerType::get(&mlirContext, 32),
                               langOpts.LongDoubleSize));

  // Set signed overflow behavior attribute.
  cir::SignedOverflowBehavior sob;
  switch (langOpts.getSignedOverflowBehavior()) {
  case LangOptions::SOB_Defined:
    sob = cir::SignedOverflowBehavior::Defined;
    break;
  case LangOptions::SOB_Undefined:
    sob = cir::SignedOverflowBehavior::Undefined;
    break;
  case LangOptions::SOB_Trapping:
    sob = cir::SignedOverflowBehavior::Trapping;
    break;
  }
  theModule->setAttr(cir::CIRDialect::getSOBAttrName(),
                     cir::SignedOverflowBehaviorAttr::get(&mlirContext, sob));

  // Set type size info attribute.
  theModule->setAttr(
      cir::CIRDialect::getTypeSizeInfoAttrName(),
      cir::TypeSizeInfoAttr::get(&mlirContext, charSize,
                                 astContext.getTypeSize(astContext.IntTy),
                                 sizeTypeSize));

  // Set the module name to be the name of the main file. TranslationUnitDecl
  // often contains invalid source locations and isn't a reliable source for the
  // module location.
  auto &sourceManager = astContext.getSourceManager();
  FileID mainFileId = sourceManager.getMainFileID();
  // Match OGCG module header behavior: preserve the source path spelling
  // (including relative paths from invocation) instead of canonicalizing to
  // a real/absolute path.
  StringRef path;
  if (auto mainFile = sourceManager.getFileEntryRefForID(mainFileId))
    path = mainFile->getName();
  if (!path.empty()) {
    theModule.setSymName(path);
    theModule->setLoc(mlir::FileLineColLoc::get(&mlirContext, path,
                                                /*line=*/0,
                                                /*column=*/0));
  }

  // Generate the module name hash here if needed.
  if (codeGenOpts.UniqueInternalLinkageNames && !path.empty()) {
    std::string pathStr = path.str();
    // Check if a path substitution is needed from the MacroPrefixMap.
    for (const auto &entry : langOpts.MacroPrefixMap)
      if (pathStr.rfind(entry.first, 0) != std::string::npos) {
        pathStr = entry.second + pathStr.substr(entry.first.size());
        break;
      }
    moduleNameHash = llvm::getUniqueInternalLinkagePostfix(pathStr);
  }

  // Set CUDA GPU binary handle.
  if (langOpts.CUDA) {
    std::string cudaBinaryName = codeGenOpts.CudaGpuBinaryFileName;
    if (!cudaBinaryName.empty()) {
      theModule->setAttr(
          cir::CIRDialect::getCUDABinaryHandleAttrName(),
          cir::CUDABinaryHandleAttr::get(&mlirContext, cudaBinaryName));
    }
  }

  // Set wchar_size module flag (matches OGCG CodeGenModule.cpp:1169-1171).
  // Used by ARM backend for build attributes and by TargetLibraryInfo.
  uint64_t wcharWidth =
      astContext.getTypeSizeInChars(astContext.getWideCharType()).getQuantity();
  theModule->setAttr("cir.wchar_size", getI64AttrCached(wcharWidth));

  // Emit Objective-C image-info and ARC module flags for NeXT-family runtimes
  // (matches OGCG CGObjCCommonMac::EmitImageInfo and CGObjC.cpp ARC marker
  // behavior). The ARC RV marker itself is attached lazily when such a call
  // is emitted.
  if (langOpts.ObjC && langOpts.ObjCRuntime.isNeXTFamily()) {
    constexpr unsigned eImageInfoGarbageCollected = 1u << 1;
    constexpr unsigned eImageInfoGCOnly = 1u << 2;
    constexpr unsigned eImageInfoSignedClassRO = 1u << 4;
    constexpr unsigned eImageInfoImageIsSimulated = 1u << 5;
    constexpr unsigned eImageInfoClassProperties = 1u << 6;

    unsigned objcABI = langOpts.ObjCRuntime.isNonFragile() ? 2u : 1u;
    theModule->setAttr("cir.objc_version", builder.getI32IntegerAttr(objcABI));
    theModule->setAttr("cir.objc_image_info_version",
                       builder.getI32IntegerAttr(0));
    theModule->setAttr("cir.objc_image_info_section",
                       getStringAttrCached(
                           getObjCImageInfoSection(getTriple(), objcABI)));

    if (langOpts.getGC() == LangOptions::NonGC) {
      theModule->setAttr("cir.objc_garbage_collection",
                         builder.getI8IntegerAttr(0));
    } else {
      theModule->setAttr("cir.objc_garbage_collection",
                         builder.getI8IntegerAttr(
                             static_cast<uint8_t>(eImageInfoGarbageCollected)));
      if (langOpts.getGC() == LangOptions::GCOnly) {
        theModule->setAttr("cir.objc_gc_only",
                           builder.getI32IntegerAttr(eImageInfoGCOnly));
        theModule->setAttr("cir.objc_gc_only_require",
                           mlir::UnitAttr::get(&mlirContext));
      }
    }

    if (getTriple().isSimulatorEnvironment())
      theModule->setAttr("cir.objc_is_simulated",
                         builder.getI32IntegerAttr(
                             eImageInfoImageIsSimulated));

    theModule->setAttr("cir.objc_class_properties",
                       builder.getI32IntegerAttr(eImageInfoClassProperties));

    if (langOpts.PointerAuthObjcClassROPointers) {
      theModule->setAttr("cir.objc_enforce_class_ro_pointer_signing",
                         builder.getI32IntegerAttr(eImageInfoSignedClassRO));
    } else {
      theModule->setAttr("cir.objc_enforce_class_ro_pointer_signing",
                         builder.getI8IntegerAttr(0));
    }
  }

  // Indicate whether this module was compiled with -fopenmp
  // (matches OGCG CodeGenModule.cpp:1440-1445).
  if (langOpts.OpenMP && !langOpts.OpenMPSimd) {
    theModule->setAttr("cir.openmp",
                       builder.getI32IntegerAttr(langOpts.OpenMP));
    theModule->setAttr("omp.is_target_device",
                       builder.getBoolAttr(langOpts.OpenMPIsTargetDevice));
    theModule->setAttr("omp.is_gpu", builder.getBoolAttr(getTriple().isGPU()));

    if (!langOpts.OMPTargetTriples.empty()) {
      llvm::SmallVector<mlir::Attribute, 4> targetTriples;
      targetTriples.reserve(langOpts.OMPTargetTriples.size());
      for (const llvm::Triple &triple : langOpts.OMPTargetTriples)
        targetTriples.push_back(builder.getStringAttr(triple.str()));
      theModule->setAttr("omp.target_triples",
                         builder.getArrayAttr(targetTriples));
    }
  }
  if (langOpts.OpenMPIsTargetDevice)
    theModule->setAttr("cir.openmp_device",
                       builder.getI32IntegerAttr(langOpts.OpenMP));

  // Set PIC/PIE level module flags (matches OGCG CodeGenModule.cpp:1473-1478).
  if (uint32_t pLevel = langOpts.PICLevel) {
    theModule->setAttr("cir.pic_level", getI64AttrCached(pLevel));
    if (langOpts.PIE)
      theModule->setAttr("cir.pie_level", getI64AttrCached(pLevel));
  }

  // Set frame-pointer module flag (matches OGCG CodeGenModule.cpp:1511-1527).
  if (cgo.getFramePointer() != CodeGenOptions::FramePointerKind::None) {
    unsigned fpKind = static_cast<unsigned>(cgo.getFramePointer());
    theModule->setAttr("cir.frame_pointer",
                       builder.getI32IntegerAttr(fpKind));
  }

  // Set Code Model and Large Data Threshold module flags
  // (matches OGCG CodeGenModule.cpp:1480-1497).
  if (cgo.CodeModel.size() > 0) {
    unsigned cm = llvm::StringSwitch<unsigned>(cgo.CodeModel)
                      .Case("tiny", llvm::CodeModel::Tiny)
                      .Case("small", llvm::CodeModel::Small)
                      .Case("kernel", llvm::CodeModel::Kernel)
                      .Case("medium", llvm::CodeModel::Medium)
                      .Case("large", llvm::CodeModel::Large)
                      .Default(~0u);
    if (cm != ~0u) {
      theModule->setAttr("cir.code_model", builder.getI32IntegerAttr(cm));
      if ((cm == llvm::CodeModel::Medium || cm == llvm::CodeModel::Large) &&
          getTriple().getArch() == llvm::Triple::x86_64) {
        theModule->setAttr("cir.large_data_threshold",
                           getI64AttrCached(cgo.LargeDataThreshold));
      }
    }
  }

  // Set llvm.ident version metadata (matches OGCG CodeGenModule.cpp:8074-8082).
  if (cgo.EmitVersionIdentMetadata) {
    theModule->setAttr("cir.ident",
                       getStringAttrCached(getClangFullVersion()));
  }

  // Windows/platform-specific module flags.
  // cfguard (matches OGCG CodeGenModule.cpp:1124-1134).
  if (cgo.ControlFlowGuard)
    theModule->setAttr("cir.cfguard", builder.getI32IntegerAttr(2));
  else if (cgo.ControlFlowGuardNoChecks)
    theModule->setAttr("cir.cfguard", builder.getI32IntegerAttr(1));
  // ehcontguard (matches OGCG CodeGenModule.cpp:1135-1138).
  if (cgo.EHContGuard)
    theModule->setAttr("cir.ehcontguard", mlir::UnitAttr::get(&mlirContext));
  // ms-kernel (matches OGCG CodeGenModule.cpp:1139-1142).
  if (langOpts.Kernel)
    theModule->setAttr("cir.ms_kernel", mlir::UnitAttr::get(&mlirContext));
  // RegCallv4 (matches OGCG CodeGenModule.cpp:1562-1563).
  if (langOpts.RegCall4)
    theModule->setAttr("cir.regcallv4", mlir::UnitAttr::get(&mlirContext));
  // SemanticInterposition (matches OGCG CodeGenModule.cpp:1113-1115).
  // Prevents optimizer from devirtualizing/inlining across DSO boundaries.
  if (langOpts.SemanticInterposition)
    theModule->setAttr("cir.semantic_interposition",
                       mlir::UnitAttr::get(&mlirContext));
  // Symbol partition (matches OGCG CodeGenModule.cpp:1989).
  if (!cgo.SymbolPartition.empty())
    theModule->setAttr("cir.symbol_partition",
                       getStringAttrCached(cgo.SymbolPartition));
  // Strict-vtable module flags (matches OGCG CodeGenModule.cpp:1143-1157).
  if (cgo.OptimizationLevel > 0 && cgo.StrictVTablePointers)
    theModule->setAttr("cir.strict_vtable_pointers",
                       mlir::UnitAttr::get(&mlirContext));
  // Whole-program-vtables module flag state
  // (matches OGCG CodeGenModule.cpp:1235-1242).
  if (cgo.WholeProgramVTables)
    theModule->setAttr(
        "cir.virtual_function_elim",
        builder.getI32IntegerAttr(cgo.VirtualFunctionElimination ? 1 : 0));
  // Cross-DSO CFI module flag (matches OGCG CodeGenModule.cpp:1230-1233).
  if (cgo.SanitizeCfiCrossDso)
    theModule->setAttr("cir.cross_dso_cfi",
                       mlir::UnitAttr::get(&mlirContext));
  // CFI canonical jump table mode for indirect-call sanitizer.
  // Matches OGCG CodeGenModule.cpp:1244-1247.
  if (langOpts.Sanitize.has(SanitizerKind::CFIICall))
    theModule->setAttr("cir.cfi_canonical_jump_tables",
                       builder.getI32IntegerAttr(
                           cgo.SanitizeCfiCanonicalJumpTables ? 1 : 0));
  // CFI icall integer normalization mode.
  // Matches OGCG CodeGenModule.cpp:1249-1252.
  if (cgo.SanitizeCfiICallNormalizeIntegers)
    theModule->setAttr("cir.cfi_normalize_integers",
                       mlir::UnitAttr::get(&mlirContext));

  bool needsGCOVDebugInfo =
      !cgo.CoverageNotesFile.empty() || !cgo.CoverageDataFile.empty();

  // Store debug compile unit metadata for the DICompileUnit that will be
  // constructed during lowering (matches OGCG CGDebugInfo.cpp:806-870).
  // gcov also needs a compile unit even when the user didn't request -g.
  if (cgo.getDebugInfo() != llvm::codegenoptions::NoDebugInfo ||
      needsGCOVDebugInfo) {
    // The DWARF source language tag.
    unsigned dwarfLang;
    if (langOpts.CPlusPlus) {
      if (langOpts.ObjC)
        dwarfLang = llvm::dwarf::DW_LANG_ObjC_plus_plus;
      else if (cgo.DebugStrictDwarf && cgo.DwarfVersion < 5)
        dwarfLang = llvm::dwarf::DW_LANG_C_plus_plus;
      else if (langOpts.CPlusPlus14)
        dwarfLang = llvm::dwarf::DW_LANG_C_plus_plus_14;
      else if (langOpts.CPlusPlus11)
        dwarfLang = llvm::dwarf::DW_LANG_C_plus_plus_11;
      else
        dwarfLang = llvm::dwarf::DW_LANG_C_plus_plus;
    } else if (langOpts.ObjC) {
      dwarfLang = llvm::dwarf::DW_LANG_ObjC;
    } else if (langOpts.OpenCL &&
               (!cgo.DebugStrictDwarf || cgo.DwarfVersion >= 5)) {
      dwarfLang = llvm::dwarf::DW_LANG_OpenCL;
    } else if (langOpts.C11 &&
               !(cgo.DebugStrictDwarf && cgo.DwarfVersion < 5)) {
      dwarfLang = llvm::dwarf::DW_LANG_C11;
    } else if (langOpts.C99) {
      dwarfLang = llvm::dwarf::DW_LANG_C99;
    } else {
      dwarfLang = llvm::dwarf::DW_LANG_C89;
    }
    theModule->setAttr("cir.debug_lang",
                       builder.getI32IntegerAttr(dwarfLang));
    // The producer string: clang version if emitting ident, empty otherwise
    // (matches OGCG's conditional at CGDebugInfo.cpp:867).
    theModule->setAttr(
        "cir.debug_producer",
        getStringAttrCached(cgo.EmitVersionIdentMetadata
                                  ? getClangFullVersion()
                                  : ""));
    // isOptimized flag (matches OGCG CGDebugInfo.cpp:868-869).
    if (cgo.OptimizationLevel != 0 || cgo.PrepareForLTO ||
        cgo.PrepareForThinLTO)
      theModule->setAttr("cir.debug_is_optimized",
                         mlir::UnitAttr::get(&mlirContext));
    // Debug emission kind (matches OGCG CGDebugInfo.cpp:813-831).
    // We store the MLIR DIEmissionKind enum value (0=None, 1=Full,
    // 2=LineTablesOnly, 3=DebugDirectivesOnly).
    unsigned emissionKind;
    switch (cgo.getDebugInfo()) {
    case llvm::codegenoptions::NoDebugInfo:
    case llvm::codegenoptions::LocTrackingOnly:
      emissionKind = needsGCOVDebugInfo ? 2 : 0; // LineTablesOnly or None
      break;
    case llvm::codegenoptions::DebugLineTablesOnly:
      emissionKind = 2; // LineTablesOnly
      break;
    case llvm::codegenoptions::DebugDirectivesOnly:
      emissionKind = 3; // DebugDirectivesOnly
      break;
    case llvm::codegenoptions::DebugInfoConstructor:
    case llvm::codegenoptions::LimitedDebugInfo:
    case llvm::codegenoptions::FullDebugInfo:
    case llvm::codegenoptions::UnusedTypeInfo:
      emissionKind = 1; // Full
      break;
    }
    theModule->setAttr("cir.debug_emission_kind",
                       builder.getI32IntegerAttr(emissionKind));

    // nameTableKind (matches OGCG CGDebugInfo.cpp:856-862).
    // MLIR DINameTableKind: 0=Default, 1=GNU, 2=None, 3=Apple.
    unsigned nameTableKind = cgo.DebugNameTable;
    if (target.getTriple().isNVPTX())
      nameTableKind = 2; // None
    else if (target.getTriple().getVendor() == llvm::Triple::Apple)
      nameTableKind = 3; // Apple
    theModule->setAttr("cir.debug_name_table_kind",
                       builder.getI32IntegerAttr(nameTableKind));

    // Preserve additional debug-codegen controls needed to reconstruct
    // function-level DISubprogram attributes during direct CIR lowering.
    if (cgo.DwarfVersion)
      theModule->setAttr("cir.debug_dwarf_version",
                         builder.getI32IntegerAttr(cgo.DwarfVersion));
    if (cgo.Dwarf64)
      theModule->setAttr("cir.debug_dwarf64", mlir::UnitAttr::get(&mlirContext));
    theModule->setAttr(
        "cir.debug_debugger_tuning",
        builder.getI32IntegerAttr(
            static_cast<unsigned>(cgo.getDebuggerTuning())));
    if (cgo.DebugCallSiteInfo)
      theModule->setAttr("cir.debug_call_site_info",
                         mlir::UnitAttr::get(&mlirContext));
    if (cgo.DebugKeyInstructions)
      theModule->setAttr("cir.debug_key_instructions",
                         mlir::UnitAttr::get(&mlirContext));
    if (cgo.EmitCodeView)
      theModule->setAttr("cir.debug_emit_codeview",
                         mlir::UnitAttr::get(&mlirContext));
    if (!cgo.CoverageNotesFile.empty() || !cgo.CoverageDataFile.empty() ||
        cgo.DebugInfoForProfiling || cgo.PseudoProbeForProfiling)
      theModule->setAttr("cir.debug_keep_linkage_name_in_line_tables_only",
                         mlir::UnitAttr::get(&mlirContext));

    // splitDebugInlining (matches OGCG CGDebugInfo.cpp:871).
    if (!cgo.SplitDwarfInlining)
      theModule->setAttr("cir.debug_no_split_debug_inlining",
                         mlir::UnitAttr::get(&mlirContext));
  }

  // Set MemProf profile filename module flag (OGCG CodeGenModule.cpp:1410-1415).
  if (!cgo.MemoryProfileOutput.empty()) {
    theModule->setAttr("cir.memprof_filename",
                       getStringAttrCached(cgo.MemoryProfileOutput));
  }

  // Set stack-protector-guard module flags
  // (matches OGCG CodeGenModule.cpp:1547-1557).
  if (!cgo.StackProtectorGuard.empty())
    theModule->setAttr("cir.stack_protector_guard",
                       getStringAttrCached(cgo.StackProtectorGuard));
  if (!cgo.StackProtectorGuardReg.empty())
    theModule->setAttr("cir.stack_protector_guard_reg",
                       getStringAttrCached(cgo.StackProtectorGuardReg));
  if (!cgo.StackProtectorGuardSymbol.empty())
    theModule->setAttr("cir.stack_protector_guard_symbol",
                       getStringAttrCached(cgo.StackProtectorGuardSymbol));
  if (cgo.StackProtectorGuardOffset != INT_MAX)
    theModule->setAttr(
        "cir.stack_protector_guard_offset",
        builder.getI32IntegerAttr(cgo.StackProtectorGuardOffset));

  // Set probe-stack and stack-probe-size module flags
  // (matches OGCG CodeGenModule.cpp:1401-1408).
  if (cgo.StackClashProtector)
    theModule->setAttr("cir.probe_stack",
                       getStringAttrCached("inline-asm"));
  if (cgo.StackProbeSize && cgo.StackProbeSize != 4096)
    theModule->setAttr("cir.stack_probe_size",
                       builder.getI32IntegerAttr(cgo.StackProbeSize));

  // Set pointer overflow defined flag for -fwrapv-pointer.
  // When set, struct member GEPs should not use inbounds/nuw.
  if (langOpts.PointerOverflowDefined)
    theModule->setAttr("cir.pointer_overflow_defined",
                       mlir::UnitAttr::get(&getMLIRContext()));

  // Set RtLibUseGOT module flag for -fno-plt
  // (matches OGCG CodeGenModule.cpp:1500-1501).
  if (cgo.NoPLT)
    theModule->setAttr("cir.rtlib_use_got",
                       mlir::UnitAttr::get(&getMLIRContext()));

  // Set direct-access-external-data module flag for ELF when the requested
  // value differs from the implicit PIC-level default
  // (matches OGCG CodeGenModule.cpp:1502-1506).
  if (getTriple().isOSBinFormatELF() &&
      cgo.DirectAccessExternalData != (langOpts.PICLevel == 0))
    theModule->setAttr(
        "cir.direct_access_external_data",
        builder.getI32IntegerAttr(cgo.DirectAccessExternalData));

  // Record mregparm so the x86 NumRegisterParameters flag reaches LLVM IR.
  if (getTriple().getArch() == llvm::Triple::x86)
    theModule->setAttr("cir.num_register_parameters",
                       builder.getI32IntegerAttr(cgo.NumRegisterParameters));

  // Set SkipRaxSetup module flag for -mskip-rax-setup
  // (matches OGCG CodeGenModule.cpp:1560-1561).
  if (cgo.SkipRaxSetup)
    theModule->setAttr("cir.skip_rax_setup",
                       mlir::UnitAttr::get(&getMLIRContext()));

  // Set MaxTLSAlign module flag (matches OGCG CodeGenModule.cpp:1565-1567).
  if (uint64_t maxTLSAlign = astContext.getTargetInfo().getMaxTLSAlign())
    theModule->setAttr("cir.max_tls_align", getI64AttrCached(maxTLSAlign));

  // Set trap function name from -ftrap-function=name flag so the lowering
  // can apply it as an LLVM "trap-func-name" call-site attribute on all
  // trap/ubsantrap intrinsic calls.
  if (!cgo.TrapFuncName.empty())
    theModule->setAttr("cir.trap_func_name",
                       getStringAttrCached(cgo.TrapFuncName));

  // Set cf-protection module flags (matches OGCG CodeGenModule.cpp:1280-1290).
  if (cgo.CFProtectionReturn &&
      getTarget().checkCFProtectionReturnSupported(getDiags()))
    theModule->setAttr("cir.cf_protection_return",
                       builder.getI32IntegerAttr(1));
  if (cgo.CFProtectionBranch &&
      getTarget().checkCFProtectionBranchSupported(getDiags()))
    theModule->setAttr("cir.cf_protection_branch",
                       builder.getI32IntegerAttr(1));

  // Emit module metadata for MemTag stack build attributes. OGCG carries this
  // at module scope even though stack tagging is driven by function attrs, so
  // object emission and LTO can still derive the right build attribute.
  if (langOpts.Sanitize.has(SanitizerKind::MemtagStack))
    theModule->setAttr("cir.tag_stack_memory_buildattr",
                       builder.getI32IntegerAttr(1));

  // Set AArch64 branch protection module flags.
  // (matches OGCG CodeGenModule.cpp:1328-1360).
  if (getTarget().getTriple().isARM() || getTarget().getTriple().isThumb() ||
      getTarget().getTriple().isAArch64()) {
    if (langOpts.BranchTargetEnforcement)
      theModule->setAttr("cir.bte", builder.getI32IntegerAttr(2));
    if (langOpts.BranchProtectionPAuthLR)
      theModule->setAttr("cir.bp_pauth_lr", builder.getI32IntegerAttr(2));
    if (langOpts.GuardedControlStack)
      theModule->setAttr("cir.gcs", builder.getI32IntegerAttr(2));
    if (langOpts.hasSignReturnAddress())
      theModule->setAttr("cir.sign_return_address",
                         builder.getI32IntegerAttr(2));
    if (langOpts.isSignReturnAddressScopeAll())
      theModule->setAttr("cir.sign_return_address_all",
                         builder.getI32IntegerAttr(2));
    if (!langOpts.isSignReturnAddressWithAKey())
      theModule->setAttr("cir.sign_return_address_with_bkey",
                         builder.getI32IntegerAttr(2));

    if (langOpts.PointerAuthELFGOT)
      theModule->setAttr("cir.ptrauth_elf_got", builder.getI32IntegerAttr(1));

    if (getTriple().isOSLinux()) {
      if (langOpts.PointerAuthCalls) {
        theModule->setAttr("cir.ptrauth_sign_personality",
                           builder.getI32IntegerAttr(1));
      }
      assert(getTriple().isOSBinFormatELF());
      using namespace llvm::ELF;
      uint64_t pAuthABIVersion =
          (langOpts.PointerAuthIntrinsics
           << AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_INTRINSICS) |
          (langOpts.PointerAuthCalls
           << AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_CALLS) |
          (langOpts.PointerAuthReturns
           << AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_RETURNS) |
          (langOpts.PointerAuthAuthTraps
           << AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_AUTHTRAPS) |
          (langOpts.PointerAuthVTPtrAddressDiscrimination
           << AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_VPTRADDRDISCR) |
          (langOpts.PointerAuthVTPtrTypeDiscrimination
           << AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_VPTRTYPEDISCR) |
          (langOpts.PointerAuthInitFini
           << AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_INITFINI) |
          (langOpts.PointerAuthInitFiniAddressDiscrimination
           << AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_INITFINIADDRDISC) |
          (langOpts.PointerAuthELFGOT
           << AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_GOT) |
          (langOpts.PointerAuthIndirectGotos
           << AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_GOTOS) |
          (langOpts.PointerAuthTypeInfoVTPtrDiscrimination
           << AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_TYPEINFOVPTRDISCR) |
          (langOpts.PointerAuthFunctionTypeDiscrimination
           << AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_FPTRTYPEDISCR);
      static_assert(AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_FPTRTYPEDISCR ==
                        AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_LAST,
                    "Update when new enum items are defined");
      if (pAuthABIVersion != 0) {
        theModule->setAttr("cir.aarch64_elf_pauthabi_platform",
                           builder.getI32IntegerAttr(
                               AARCH64_PAUTH_PLATFORM_LLVM_LINUX));
        theModule->setAttr("cir.aarch64_elf_pauthabi_version",
                           builder.getI32IntegerAttr(pAuthABIVersion));
      }
    }

    if (const auto &schema = cgo.PointerAuth.CXXMemberFunctionPointers;
        schema) {
      theModule->setAttr(
          "cir.ptrauth_cxx_member_function_pointer_key",
          builder.getI32IntegerAttr(schema.getKey()));
      theModule->setAttr(
          "cir.ptrauth_cxx_member_function_pointer_auth_mode",
          builder.getI32IntegerAttr(
              static_cast<unsigned>(schema.getAuthenticationMode())));
    }

    if (const auto &schema = cgo.PointerAuth.CXXVTablePointers; schema) {
      theModule->setAttr("cir.ptrauth_cxx_vtable_pointer_key",
                         builder.getI32IntegerAttr(schema.getKey()));
      theModule->setAttr(
          "cir.ptrauth_cxx_vtable_pointer_auth_mode",
          builder.getI32IntegerAttr(
              static_cast<unsigned>(schema.getAuthenticationMode())));
      theModule->setAttr("cir.ptrauth_cxx_vtable_pointer_addr_disc",
                         builder.getBoolAttr(schema.isAddressDiscriminated()));
      theModule->setAttr(
          "cir.ptrauth_cxx_vtable_pointer_other_disc",
          builder.getI32IntegerAttr(
              static_cast<unsigned>(schema.getOtherDiscrimination())));
      if (schema.getOtherDiscrimination() ==
          PointerAuthSchema::Discrimination::Constant) {
        theModule->setAttr(
            "cir.ptrauth_cxx_vtable_pointer_const_disc",
            builder.getI32IntegerAttr(schema.getConstantDiscrimination()));
      }
    }

  }

  // Emit amdhsa_code_object_version module flag
  // (matches OGCG CodeGenModule.cpp:1022-1031).
  if (getTriple().isAMDGPU() ||
      (getTriple().isSPIRV() &&
       getTriple().getVendor() == llvm::Triple::AMD)) {
    if (getTarget().getTargetOpts().CodeObjectVersion !=
        llvm::CodeObjectVersionKind::COV_None) {
      theModule->setAttr(
          "cir.amdhsa_code_object_version",
          builder.getI32IntegerAttr(
              getTarget().getTargetOpts().CodeObjectVersion));
    }
  }

  // Create the CUDA runtime
  if (langOpts.CUDA || langOpts.HIP)
    cudaRuntime.reset(CreateNVCUDARuntime(*this));
}

CIRGenModule::~CIRGenModule() = default;

void CIRGenModule::releaseModule() {
  // After lowering CIR to LLVM IR the CIR module is dead. It is created via
  // mlir::ModuleOp::create and is not held by any OwningOpRef, so without an
  // explicit erase the (often very large) CIR operations stay resident through
  // backend emission. Erase them now to lower peak memory; the MLIRContext and
  // its uniqued storage remain valid and owned elsewhere.
  if (theModule) {
    theModule->erase();
    theModule = {};
  }
}

const cir::CIRDataLayout &CIRGenModule::getDataLayout() const {
  if (!cachedDataLayout)
    cachedDataLayout = std::make_unique<cir::CIRDataLayout>(theModule);
  return *cachedDataLayout;
}

bool CIRGenModule::shouldEmitCUDAGlobalVar(const VarDecl *global) const {
  assert(langOpts.CUDA && "Should not be called by non-CUDA languages");
  // We need to emit host-side 'shadows' for all global
  // device-side variables because the CUDA runtime needs their
  // size and host-side address in order to provide access to
  // their device-side incarnations.

  return !langOpts.CUDAIsDevice || global->hasAttr<CUDADeviceAttr>() ||
         global->hasAttr<CUDAConstantAttr>() ||
         global->hasAttr<CUDASharedAttr>() ||
         global->getType()->isCUDADeviceBuiltinSurfaceType() ||
         global->getType()->isCUDADeviceBuiltinTextureType();
}

void CIRGenModule::printPostfixForExternalizedDecl(llvm::raw_ostream &os,
                                                   const Decl *d) const {
  // ptxas does not allow '.' in symbol names. On the other hand, HIP prefers
  // postfix beginning with '.' since the symbol name can be demangled.
  if (langOpts.HIP)
    os << (isa<VarDecl>(d) ? ".static." : ".intern.");
  else
    os << (isa<VarDecl>(d) ? "__static__" : "__intern__");

  if (getLangOpts().CUID.empty()) {
    errorNYI("printPostfixForExternalizedDecl without CUID");
    return;
  } else {
    os << getASTContext().getCUIDHash();
  }
}

/// FIXME: this could likely be a common helper and not necessarily related
/// with codegen.
/// Return the best known alignment for an unknown pointer to a
/// particular class.
CharUnits CIRGenModule::getClassPointerAlignment(const CXXRecordDecl *rd) {
  if (!rd->hasDefinition())
    return CharUnits::One(); // Hopefully won't be used anywhere.

  auto &layout = astContext.getASTRecordLayout(rd);

  // If the class is final, then we know that the pointer points to an
  // object of that type and can use the full alignment.
  if (rd->isEffectivelyFinal())
    return layout.getAlignment();

  // Otherwise, we have to assume it could be a subclass.
  return layout.getNonVirtualAlignment();
}

CharUnits CIRGenModule::getNaturalTypeAlignment(QualType t,
                                                LValueBaseInfo *baseInfo,
                                                TBAAAccessInfo *tbaaInfo,
                                                bool forPointeeType) {
  if (tbaaInfo)
    *tbaaInfo = getTBAAAccessInfo(t);

  // FIXME: This duplicates logic in ASTContext::getTypeAlignIfKnown, but
  // that doesn't return the information we need to compute baseInfo.

  // Honor alignment typedef attributes even on incomplete types.
  // We also honor them straight for C++ class types, even as pointees;
  // there's an expressivity gap here.
  if (const auto *tt = t->getAs<TypedefType>()) {
    if (unsigned align = tt->getDecl()->getMaxAlignment()) {
      if (baseInfo)
        *baseInfo = LValueBaseInfo(AlignmentSource::AttributedType);
      return astContext.toCharUnitsFromBits(align);
    }
  }

  bool alignForArray = t->isArrayType();

  // Analyze the base element type, so we don't get confused by incomplete
  // array types.
  t = astContext.getBaseElementType(t);

  if (t->isIncompleteType()) {
    // We could try to replicate the logic from
    // ASTContext::getTypeAlignIfKnown, but nothing uses the alignment if the
    // type is incomplete, so it's impossible to test. We could try to reuse
    // getTypeAlignIfKnown, but that doesn't return the information we need
    // to set baseInfo.  So just ignore the possibility that the alignment is
    // greater than one.
    if (baseInfo)
      *baseInfo = LValueBaseInfo(AlignmentSource::Type);
    return CharUnits::One();
  }

  if (baseInfo)
    *baseInfo = LValueBaseInfo(AlignmentSource::Type);

  CharUnits alignment;
  if (t.getQualifiers().hasUnaligned()) {
    alignment = CharUnits::One();
  } else if (forPointeeType && !alignForArray &&
             t->getAsCXXRecordDecl()) {
    // For C++ class pointees, we don't know whether we're pointing at a
    // base or a complete object, so we generally need to use the
    // non-virtual alignment.
    alignment = getClassPointerAlignment(t->getAsCXXRecordDecl());
  } else {
    alignment = astContext.getTypeAlignInChars(t);
  }

  // Cap to the global maximum type alignment unless the alignment
  // was somehow explicit on the type.
  if (unsigned maxAlign = astContext.getLangOpts().MaxTypeAlign) {
    if (alignment.getQuantity() > maxAlign &&
        !astContext.isAlignmentRequired(t))
      alignment = CharUnits::fromQuantity(maxAlign);
  }
  return alignment;
}

const TargetCIRGenInfo &CIRGenModule::getTargetCIRGenInfo() {
  if (theTargetCIRGenInfo)
    return *theTargetCIRGenInfo;

  const llvm::Triple &triple = getTarget().getTriple();
  switch (triple.getArch()) {
  default:
    // Unsupported architectures currently fall through to x86_64.
    [[fallthrough]];

  case llvm::Triple::aarch64:
  case llvm::Triple::aarch64_be:
    theTargetCIRGenInfo = createAArch64TargetCIRGenInfo(genTypes);
    return *theTargetCIRGenInfo;

  case llvm::Triple::arm:
  case llvm::Triple::armeb:
  case llvm::Triple::thumb:
  case llvm::Triple::thumbeb:
    theTargetCIRGenInfo = createARMTargetCIRGenInfo(genTypes);
    return *theTargetCIRGenInfo;

  case llvm::Triple::x86:
    theTargetCIRGenInfo = createX8632TargetCIRGenInfo(genTypes);
    return *theTargetCIRGenInfo;

  case llvm::Triple::x86_64: {
    switch (triple.getOS()) {
    default:
      // Unsupported OS triples currently fall through to Linux.
      [[fallthrough]];

    case llvm::Triple::Linux:
      theTargetCIRGenInfo = createX8664TargetCIRGenInfo(genTypes);
      return *theTargetCIRGenInfo;
    }
  }

  case llvm::Triple::nvptx:
  case llvm::Triple::nvptx64:
    theTargetCIRGenInfo = createNVPTXTargetCIRGenInfo(genTypes);
    return *theTargetCIRGenInfo;

  case llvm::Triple::amdgcn:
    theTargetCIRGenInfo = createAMDGPUTargetCIRGenInfo(genTypes);
    return *theTargetCIRGenInfo;

  case llvm::Triple::spirv64:
    theTargetCIRGenInfo = createSPIRVTargetCIRGenInfo(genTypes);
    return *theTargetCIRGenInfo;
  }
}

mlir::Location CIRGenModule::getLoc(SourceLocation cLoc) {
  assert(cLoc.isValid() && "expected valid source location");
  const unsigned rawEncoding = cLoc.getRawEncoding();
  if (auto it = sourceLocCache.find(rawEncoding); it != sourceLocCache.end())
    return it->second;

  const SourceManager &sm = astContext.getSourceManager();
  PresumedLoc pLoc = sm.getPresumedLoc(cLoc);
  const char *filenamePtr = pLoc.getFilename();
  auto [fileIt, inserted] = sourceFileAttrCache.try_emplace(filenamePtr);
  if (inserted)
    fileIt->second = getStringAttrCached(filenamePtr);
  bool suppressDebugColumns =
      getCodeGenOpts().getDebugInfo() != llvm::codegenoptions::NoDebugInfo &&
      !getCodeGenOpts().DebugColumnInfo;
  unsigned column = suppressDebugColumns ? 0 : pLoc.getColumn();
  mlir::Location loc =
      mlir::FileLineColLoc::get(fileIt->second, pLoc.getLine(), column);
  sourceLocCache.try_emplace(rawEncoding, loc);
  return loc;
}

mlir::Location CIRGenModule::getLoc(SourceRange cRange) {
  assert(cRange.isValid() && "expected a valid source range");
  const uint64_t beginEncoding = cRange.getBegin().getRawEncoding();
  const uint64_t endEncoding = cRange.getEnd().getRawEncoding();
  const uint64_t key = (beginEncoding << 32) | endEncoding;
  if (auto it = sourceRangeLocCache.find(key); it != sourceRangeLocCache.end())
    return it->second;

  mlir::Location begin = getLoc(cRange.getBegin());
  mlir::Location end = getLoc(cRange.getEnd());
  mlir::Attribute metadata;
  mlir::Location loc =
      mlir::FusedLoc::get({begin, end}, metadata, builder.getContext());
  sourceRangeLocCache.try_emplace(key, loc);
  return loc;
}

mlir::DictionaryAttr CIRGenModule::getMustProgressLoopHintAttr() {
  if (mustProgressLoopHintAttr)
    return mustProgressLoopHintAttr;

  auto key = getStringAttrCached("mustprogress");
  auto val = mlir::BoolAttr::get(builder.getContext(), true);
  mustProgressLoopHintAttr =
      mlir::DictionaryAttr::get(builder.getContext(), {{key, val}});
  return mustProgressLoopHintAttr;
}

mlir::Operation *
CIRGenModule::getAddrOfGlobal(GlobalDecl gd, ForDefinition_t isForDefinition) {
  const Decl *d = gd.getDecl();

  if (isa<CXXConstructorDecl>(d) || isa<CXXDestructorDecl>(d))
    return getAddrOfCXXStructor(gd, /*FnInfo=*/nullptr, /*FnType=*/nullptr,
                                /*DontDefer=*/false, isForDefinition);

  if (isa<CXXMethodDecl>(d)) {
    const CIRGenFunctionInfo &fi =
        getTypes().arrangeCXXMethodDeclaration(cast<CXXMethodDecl>(d));
    cir::FuncType ty = getTypes().getFunctionType(fi);
    return getAddrOfFunction(gd, ty, /*ForVTable=*/false, /*DontDefer=*/false,
                             isForDefinition);
  }

  if (isa<FunctionDecl>(d)) {
    const CIRGenFunctionInfo &fi = getTypes().arrangeGlobalDeclaration(gd);
    cir::FuncType ty = getTypes().getFunctionType(fi);
    return getAddrOfFunction(gd, ty, /*ForVTable=*/false, /*DontDefer=*/false,
                             isForDefinition);
  }

  // This helper is used by top-level global emission paths that need the
  // symbol operation itself (GlobalOp/FuncOp), not a function-body address
  // value. Building a transient cir.get_global here requires a valid insertion
  // point and can crash during deferred-emission flows when no insertion point
  // is active (e.g. release-time AST draining of globals).
  return getOrCreateCIRGlobal(cast<VarDecl>(d), /*ty=*/nullptr, isForDefinition)
      .getOperation();
}

static bool isOpenMPTargetDeclAssumedNotEmitted(const ValueDecl *vd,
                                                bool isDevice) {
  std::optional<OMPDeclareTargetDeclAttr::DevTypeTy> devTy =
      OMPDeclareTargetDeclAttr::getDeviceType(vd);
  if (!devTy)
    return false;
  if (!isDevice && *devTy == OMPDeclareTargetDeclAttr::DT_NoHost)
    return true;
  if (isDevice && *devTy == OMPDeclareTargetDeclAttr::DT_Host)
    return true;
  return false;
}

static bool shouldEmitOpenMPTargetDeviceStub(const OMPExecutableDirective &dir) {
  switch (dir.getDirectiveKind()) {
  case llvm::omp::OMPD_target:
  case llvm::omp::OMPD_target_parallel:
  case llvm::omp::OMPD_target_parallel_for:
  case llvm::omp::OMPD_target_parallel_for_simd:
  case llvm::omp::OMPD_target_simd:
  case llvm::omp::OMPD_target_teams:
    return true;
  default:
    return false;
  }
}

void CIRGenModule::scanForOpenMPTargetRegions(const Stmt *s,
                                              llvm::StringRef parentName) {
  if (!s)
    return;

  if (const auto *dir = dyn_cast<OMPExecutableDirective>(s)) {
    if (isOpenMPTargetExecutionDirective(dir->getDirectiveKind())) {
      if (shouldEmitOpenMPTargetDeviceStub(*dir))
        emitOpenMPTargetDeviceStub(parentName, *dir);
      return;
    }

    if (!dir->hasAssociatedStmt() || !dir->getAssociatedStmt())
      return;

    scanForOpenMPTargetRegions(dir->getRawStmt(), parentName);
    return;
  }

  if (const auto *lambda = dyn_cast<LambdaExpr>(s))
    s = lambda->getBody();

  for (const Stmt *child : s->children())
    scanForOpenMPTargetRegions(child, parentName);
}

void CIRGenModule::maybeScanOpenMPTargetDeviceDecl(GlobalDecl gd) {
  if (!getLangOpts().OpenMPIsTargetDevice)
    return;

  if (const auto *fd = dyn_cast<FunctionDecl>(gd.getDecl())) {
    if (!fd->hasBody())
      return;
    fd = fd->getDefinition();
    if (!fd || !fd->getBody())
      return;
    fd = fd->getCanonicalDecl();
    if (!openMPTargetDeviceScannedFunctions.insert(fd).second)
      return;
    scanForOpenMPTargetRegions(fd->getBody(), getMangledName(gd));
    return;
  }

  const auto *var = dyn_cast<VarDecl>(gd.getDecl());
  if (!var)
    return;

  QualType recordTy = var->getType();
  if (const auto *rd = recordTy->getBaseElementTypeUnsafe()->getAsCXXRecordDecl()) {
    for (const CXXConstructorDecl *ctor : rd->ctors())
      maybeScanOpenMPTargetDeviceDecl(GlobalDecl(ctor, Ctor_Complete));
    if (const CXXDestructorDecl *dtor = rd->getDestructor())
      maybeScanOpenMPTargetDeviceDecl(GlobalDecl(dtor, Dtor_Complete));
  }
}

bool CIRGenModule::shouldSkipOpenMPTargetDeviceEmission(GlobalDecl gd) {
  if (!getLangOpts().OpenMPIsTargetDevice)
    return false;

  const auto *vd = dyn_cast<ValueDecl>(gd.getDecl());
  if (!vd)
    return false;

  if (isa<FunctionDecl>(vd))
    maybeScanOpenMPTargetDeviceDecl(gd);

  if (isOpenMPTargetDeclAssumedNotEmitted(vd, /*isDevice=*/true))
    return true;

  if (const auto *fd = dyn_cast<FunctionDecl>(vd)) {
    fd = fd->getCanonicalDecl();
    return !OMPDeclareTargetDeclAttr::isDeclareTargetDeclaration(fd) &&
           openMPTargetGlobalizedFunctions.count(fd) == 0;
  }

  const auto *var = dyn_cast<VarDecl>(vd);
  if (!var)
    return false;

  maybeScanOpenMPTargetDeviceDecl(gd);

  std::optional<OMPDeclareTargetDeclAttr::MapTypeTy> targetMapType =
      OMPDeclareTargetDeclAttr::isDeclareTargetDeclaration(var);
  if (!targetMapType)
    return true;

  if (*targetMapType == OMPDeclareTargetDeclAttr::MT_Link)
    return true;

  if ((*targetMapType == OMPDeclareTargetDeclAttr::MT_To ||
       *targetMapType == OMPDeclareTargetDeclAttr::MT_Enter) &&
      hasRequiresUnifiedSharedMemory())
    return true;

  return false;
}

bool CIRGenModule::markAsOpenMPTargetGlobal(GlobalDecl gd) {
  if (!getLangOpts().OpenMPIsTargetDevice)
    return true;

  const auto *fd = dyn_cast_or_null<FunctionDecl>(gd.getDecl());
  if (!fd)
    return true;
  fd = fd->getCanonicalDecl();

  if (isOpenMPTargetDeclAssumedNotEmitted(fd, /*isDevice=*/true))
    return true;

  if (OMPDeclareTargetDeclAttr::isDeclareTargetDeclaration(fd)) {
    if (fd->hasBody()) {
      if (auto *existing = getGlobalValue(getMangledName(gd)))
        if (auto gv = dyn_cast<cir::CIRGlobalValueInterface>(existing))
          return !gv.isDeclaration();
      return false;
    }
    return true;
  }

  return !openMPTargetGlobalizedFunctions.insert(fd).second;
}

void CIRGenModule::markAsOpenMPTargetGenericFunction(const FunctionDecl *fd) {
  if (!fd || !getLangOpts().OpenMPIsTargetDevice || !getTriple().isGPU())
    return;
  openMPTargetGenericFunctions.insert(fd->getCanonicalDecl());
}

bool CIRGenModule::shouldUseOpenMPTargetSharedLocals(
    const FunctionDecl *fd) const {
  return fd && getLangOpts().OpenMPIsTargetDevice && getTriple().isGPU() &&
         openMPTargetGenericFunctions.count(fd->getCanonicalDecl()) != 0;
}

bool CIRGenModule::isEmittingOpenMPTargetGenericFunction() const {
  return openMPTargetGenericEmissionDepth != 0;
}

void CIRGenModule::pushOpenMPTargetGenericEmission() {
  ++openMPTargetGenericEmissionDepth;
}

void CIRGenModule::popOpenMPTargetGenericEmission() {
  assert(openMPTargetGenericEmissionDepth != 0 &&
         "unbalanced OpenMP target generic emission depth");
  --openMPTargetGenericEmissionDepth;
}

static std::string getMangledNameImpl(CIRGenModule &cgm, GlobalDecl gd,
                                      const NamedDecl *nd,
                                      bool omitMultiVersionMangling);

static bool hasDefinedMultiVersionDefault(ASTContext &astContext,
                                          const FunctionDecl *fd) {
  bool hasDefinedDefaultVersion = false;
  astContext.forEachMultiversionedFunctionVersion(
      fd, [&](const FunctionDecl *curFD) {
        if (hasDefinedDefaultVersion || !curFD->getDefinition())
          return;

        if (const auto *tva = curFD->getAttr<TargetVersionAttr>()) {
          hasDefinedDefaultVersion = tva->isDefaultVersion();
          return;
        }

        if (const auto *tc = curFD->getAttr<TargetClonesAttr>()) {
          for (unsigned i = 0; i < tc->featuresStrs_size(); ++i) {
            if (!tc->isFirstOfVersion(i))
              continue;
            if (tc->isDefaultVersion(i)) {
              hasDefinedDefaultVersion = true;
              return;
            }
          }
        }
      });
  return hasDefinedDefaultVersion;
}

void CIRGenModule::emitGlobalDecl(const clang::GlobalDecl &d,
                                  llvm::StringRef precomputedMangledName) {
  // Match OGCG's OpenMP target-device filtering before creating placeholder
  // symbols so host-only globals/functions don't leak into device modules.
  if (shouldSkipOpenMPTargetDeviceEmission(d))
    return;

  // We call getAddrOfGlobal with isForDefinition set to ForDefinition in
  // order to get a Value with exactly the type we need, not something that
  // might have been created for another decl with the same mangled name but
  // different type.
  mlir::Operation *op = getAddrOfGlobal(d, ForDefinition);

  // In case of different address spaces, we may still get a cast, even with
  // IsForDefinition equal to ForDefinition. Query mangled names table to get
  // GlobalValue.
  if (!op) {
    llvm::StringRef mangledName =
        precomputedMangledName.empty() ? getMangledName(d)
                                       : precomputedMangledName;
    op = getGlobalValue(mangledName);
  }

  assert(op && "expected a valid global op");

  // Check to see if we've already emitted this. This is necessary for a
  // couple of reasons: first, decls can end up in deferred-decls queue
  // multiple times, and second, decls can end up with definitions in unusual
  // ways (e.g. by an extern inline function acquiring a strong function
  // redefinition). Just ignore those cases.
  // TODO: Not sure what to map this to for MLIR
  mlir::Operation *globalValueOp = op;
  if (auto gv = dyn_cast<cir::GetGlobalOp>(op))
    globalValueOp =
        getGlobalValue(gv.getName());

  if (globalValueOp)
    if (auto cirGlobalValue =
            dyn_cast<cir::CIRGlobalValueInterface>(globalValueOp))
      if (!cirGlobalValue.isDeclaration())
        return;

  // If this is OpenMP, OGCG checks whether normal global emission is legal
  // (runtime-specific target handling). CIR currently has no equivalent hook.

  // Otherwise, emit the definition and move on to the next one.
  emitGlobalDefinition(d, op);
}

void CIRGenModule::emitDeferred(unsigned recursionLimit) {
  // Emit code for any potentially referenced deferred decls. Since a previously
  // unused static decl may become used during the generation of code for a
  // static function, iterate until no changes are made.

  assert(!cir::MissingFeatures::cudaSupport());

  while (recursionLimit--) {
    if (!deferredVTables.empty()) {
      emitDeferredVTables();
    }

    // Emitting a vtable doesn't directly cause more vtables to
    // become deferred, although it can cause functions to be
    // emitted that then need those vtables.
    assert(deferredVTables.empty());

    // Stop if we're out of both deferred vtables and deferred declarations.
    if (deferredDeclsToEmit.empty())
      return;

    // Grab the list of decls to emit. If emitGlobalDefinition schedules more
    // work, it will not interfere with this.
    std::vector<DeferredDeclToEmit> curDeclsToEmit;
    curDeclsToEmit.swap(deferredDeclsToEmit);
    deferredDeclsToEmitSet.clear();

    // Iterate over this round's batch until it quiesces.
    SmallVector<DeferredDeclToEmit> pendingDecls(curDeclsToEmit.begin(),
                                                 curDeclsToEmit.end());
    while (!pendingDecls.empty()) {
      SmallVector<DeferredDeclToEmit> nextPendingDecls;
      bool madeProgress = false;
      for (const DeferredDeclToEmit &deferred : pendingDecls) {
        const GlobalDecl &d = deferred.gd;
        auto *decl = d.getDecl();
        assert(decl && "expected decl");
        bool isSystemHeaderDecl =
            astContext.getSourceManager().isInSystemHeader(decl->getLocation());

        if (getCodeGenOpts().ClangIRSkipFunctionsFromSystemHeaders &&
            isSystemHeaderDecl)
          continue;
        emitGlobalDecl(d, deferred.mangledName);
        madeProgress = true;
      }

      if (!madeProgress)
        break;

      pendingDecls = std::move(nextPendingDecls);
    }

    // Continue while there is newly deferred work and budget remains.
    if (deferredVTables.empty() && deferredDeclsToEmit.empty())
      return;
  }
}

void CIRGenModule::emitGlobal(clang::GlobalDecl gd) {
  if (const auto *cd = dyn_cast<clang::OpenACCConstructDecl>(gd.getDecl())) {
    emitGlobalOpenACCDecl(cd);
    return;
  }

  // TODO(OMP): The logic in this function for the 'rest' of the OpenMP
  // declarative declarations is complicated and needs to be done on a per-kind
  // basis, so all of that needs to be added when we implement the individual
  // global-allowed declarations. See uses of `cir::MissingFeatures::openMP
  // throughout this function.

  const auto *global = cast<ValueDecl>(gd.getDecl());

  if (shouldSkipOpenMPTargetDeviceEmission(gd))
    return;

  // Weak references don't produce any output by themselves.
  if (global->hasAttr<WeakRefAttr>())
    return;

  // CUDA/HIP: a device-only alias or ifunc must not be emitted during host
  // compilation (and a host-only one must not be emitted during device
  // compilation); otherwise it would reference an aliasee that is not emitted
  // on this side, producing a dangling symbol reference. This mirrors the
  // host/device filtering applied to ordinary globals below, but must run
  // before the alias/ifunc emission that follows.
  if ((langOpts.CUDA || langOpts.HIP) &&
      (global->hasAttr<AliasAttr>() || global->hasAttr<IFuncAttr>())) {
    if (langOpts.CUDAIsDevice) {
      if (!global->hasAttr<CUDAGlobalAttr>() &&
          !global->hasAttr<CUDADeviceAttr>())
        return;
    } else if (!global->hasAttr<CUDAHostAttr>() &&
               global->hasAttr<CUDADeviceAttr>()) {
      return;
    }
  }

  // If this is an alias definition (which otherwise looks like a declaration)
  // emit it now.
  if (global->hasAttr<AliasAttr>()) {
    emitAliasDefinition(gd);
    return;
  }

  // IFunc like an alias whose value is resolved at runtime by calling resolver.
  if (global->hasAttr<IFuncAttr>()) {
    const IFuncAttr *ifa = global->getAttr<IFuncAttr>();
    StringRef mangledName = getMangledName(gd);
    StringRef resolverName = ifa->getResolver();
    auto funcType = convertType(global->getType());
    cir::GlobalLinkageKind linkage = getFunctionLinkage(gd);
    bool isWeak = global->hasAttr<WeakAttr>() ||
                  global->hasAttr<WeakRefAttr>() || global->isWeakImported();
    cir::VisibilityKind visKind = cir::VisibilityKind::Default;

    if (const auto *nd = dyn_cast<NamedDecl>(global)) {
      LinkageInfo lv = nd->getLinkageAndVisibility();
      // IFuncs are definitions, so they inherit definition visibility even
      // when the visibility was implied by -fvisibility=hidden.
      if (linkage != cir::GlobalLinkageKind::InternalLinkage &&
          linkage != cir::GlobalLinkageKind::PrivateLinkage)
        visKind = getCIRVisibilityKind(lv.getVisibility());
    }

    bool dsoLocal = false;
    {
      bool isLocalLinkage =
          linkage == cir::GlobalLinkageKind::InternalLinkage ||
          linkage == cir::GlobalLinkageKind::PrivateLinkage;
      if (isLocalLinkage) {
        dsoLocal = true;
      } else if (visKind != cir::VisibilityKind::Default && !isWeak) {
        dsoLocal = true;
      } else {
        const llvm::Triple &tt = getTriple();
        if (tt.isOSBinFormatCOFF() ||
            (tt.isOSWindows() && tt.isOSBinFormatMachO())) {
          dsoLocal = !isWeak;
        } else if (tt.isOSBinFormatELF()) {
          llvm::Reloc::Model rm = getCodeGenOpts().RelocationModel;
          if (rm == llvm::Reloc::Static || getLangOpts().PIE)
            dsoLocal = true;
        }
      }
    }

    if (resolverName == mangledName) {
      getDiags().Report(ifa->getLocation(), diag::err_cyclic_alias)
          << /*IsIFunc=*/1;
      return;
    }

    // Ensure a forward declaration exists for the resolver so that when
    // emitGlobal processes the resolver's FunctionDecl, getGlobalValue
    // returns non-null and the resolver is scheduled for deferred emission.
    // Without this, static resolvers stay in deferredDecls forever because
    // nothing else references them by symbol. This mirrors OGCG's
    // GetOrCreateLLVMFunction(IFA->getResolver(), VoidTy, ...) call in
    // emitIFuncDefinition.
    if (!getGlobalValue(resolverName)) {
      // Resolver not yet parsed. Create a placeholder FuncOp declaration
      // with a void() type so getGlobalValue(resolverName) returns
      // non-null when emitGlobal processes the resolver later. The type
      // will be corrected by getOrCreateCIRFunction when the resolver is
      // emitted with its real FunctionDecl.
      auto voidTy = cir::VoidType::get(&getMLIRContext());
      auto placeholderTy = cir::FuncType::get({}, voidTy, false);
      getOrCreateCIRFunction(resolverName, placeholderTy, {}, false, false,
                             false, NotForDefinition);
    }

    // If a FuncOp declaration already exists for this symbol (from a prior
    // call to the ifunc), mark it as an ifunc by setting its aliasee to the
    // resolver. This avoids a name conflict between the FuncOp and AliasOp.
    // The FuncOp is needed for CallOp verification (callee must be FuncOp).
    // During LLVM lowering, FuncOps with aliasee + cir.ifunc are lowered to
    // llvm.mlir.ifunc instead of llvm.mlir.alias.
    if (auto *entry = getGlobalValue(mangledName)) {
      if (auto funcOp = mlir::dyn_cast<cir::FuncOp>(entry)) {
        funcOp.setAliasee(resolverName);
        funcOp->setAttr("cir.ifunc", builder.getUnitAttr());
        setFunctionAttributes(gd, funcOp, /*isIncompleteFunction=*/false,
                              /*isThunk=*/false);
        // Match OGCG SetCommonAttributes → setDSOLocal: internal linkage
        // ifuncs must be dso_local; weak_odr ifuncs under PIC are not.
        setDSOLocal(funcOp.getOperation());
        return;
      }
    }

    // No existing FuncOp — create an AliasOp with is_ifunc.
    mlir::OpBuilder::InsertionGuard guard(builder);
    if (lastGlobalOp)
      builder.setInsertionPointAfter(lastGlobalOp);
    else
      builder.setInsertionPointToStart(getModule().getBody());

    auto op = cir::AliasOp::create(
        builder, getLoc(global->getSourceRange()), mangledName,
        resolverName, funcType,
        linkage, visKind, dsoLocal, /*weak=*/isWeak, /*is_ifunc=*/true,
        /*tls_model=*/cir::TLS_ModelAttr{});
    insertGlobalSymbol(op);
    lastGlobalOp = op;
    return;
  }

  // If this is a cpu_dispatch multiversion function, emit the resolver.
  if (const auto *fd = dyn_cast<FunctionDecl>(global)) {
    if (fd->hasAttr<CPUDispatchAttr>()) {
      emitCPUDispatchDefinition(gd);
      return;
    }
  }

  // CUDA/HIP: Filter globals based on host/device compilation mode.
  if (langOpts.CUDA || langOpts.HIP) {
    if (const auto *vd = dyn_cast<VarDecl>(global)) {
      if (!shouldEmitCUDAGlobalVar(vd))
        return;
    } else if (langOpts.CUDAIsDevice) {
      // When compiling for device, skip functions that are not device-side.
      if (langOpts.OffloadImplicitHostDeviceTemplates) {
        errorNYI("OffloadImplicitHostDeviceTemplates");
      }
      if (langOpts.HIPStdPar) {
        errorNYI("HIPStdPar");
      }

      // Global functions reside on device, so they shouldn't be skipped.
      if (!global->hasAttr<CUDAGlobalAttr>() &&
          !global->hasAttr<CUDADeviceAttr>())
        return;
    } else {
      // We must skip __device__ functions when compiling for host.
      if (!global->hasAttr<CUDAHostAttr>() &&
          global->hasAttr<CUDADeviceAttr>()) {
        return;
      }
    }
  }

  if (const auto *fd = dyn_cast<FunctionDecl>(global)) {
    if (DeviceKernelAttr::isOpenCLSpelling(fd->getAttr<DeviceKernelAttr>()) &&
        fd->doesThisDeclarationHaveABody())
      addDeferredDeclToEmit(GlobalDecl(fd, KernelReferenceKind::Stub));

    // Update deferred annotations with the latest declaration if the function
    // was already used or defined.
    if (fd->hasAttr<AnnotateAttr>()) {
      StringRef mangledName = getMangledName(gd);
      if (getGlobalValue(mangledName))
        deferredAnnotations[mangledName] = fd;
    }
    if (!fd->doesThisDeclarationHaveABody()) {
      // Don't eagerly emit declarations without bodies for AArch64
      // target_version/target_clones. Call references materialize the
      // unsuffixed placeholder on demand, and end-of-TU FMV handling emits the
      // exact versioned declaration stubs. Emitting them here routes later
      // default declarations back through the unsuffixed placeholder path and
      // can overwrite a pre-default use's FMV identity.
      //
      // If a plain default definition was already emitted before the family
      // became multiversioned, still queue deferred FMV finalization so the
      // end-of-TU resolver path can recover that stale base symbol and turn it
      // into the versioned default definition.
      //
      // For non-AArch64 multiversion declarations, preserve the existing
      // behavior: avoid eager resolver emission unless the declaration forces a
      // definition. This matches OGCG's lazy emission for unused declarations
      // like foo_unused_no_defn.
      if (!fd->doesDeclarationForceExternallyVisibleDefinition()) {
        if (getTarget().getTriple().isAArch64()) {
          if ((fd->isTargetVersionMultiVersion() ||
               fd->isTargetClonesMultiVersion()) &&
              hasDefinedMultiVersionDefault(getASTContext(), fd)) {
            std::string baseName = getMangledNameImpl(
                *this, gd, fd, /*omitMultiVersionMangling=*/true);
            if (getGlobalValue(baseName))
              addDeferredMultiVersionResolverToEmit(gd);
          }
          return;
        }
        if (!fd->isMultiVersion())
          return;
      }

      StringRef mangledName = getMangledName(gd);
      const CIRGenFunctionInfo &fi = getTypes().arrangeGlobalDeclaration(gd);
      cir::FuncType ty = getTypes().getFunctionType(fi);
      getOrCreateCIRFunction(mangledName, ty, gd, /*forVTable=*/false,
                             /*dontDefer=*/false);
      return;
    }
  } else {
    const auto *vd = cast<VarDecl>(global);
    assert(vd->isFileVarDecl() && "Cannot emit local var decl as global.");
    if (vd->isThisDeclarationADefinition() != VarDecl::Definition &&
        !astContext.isMSStaticDataMemberInlineDefinition(vd)) {
      if (getLangOpts().OpenMP && !getLangOpts().OpenMPSimd) {
        // Ensure declaration-only declare-target vars still materialize a
        // symbol op so downstream LLVM/OpenMP translation can synthesize the
        // required offload entries and ref-pointer globals.
        if (std::optional<OMPDeclareTargetDeclAttr::MapTypeTy> targetMapType =
                OMPDeclareTargetDeclAttr::isDeclareTargetDeclaration(vd)) {
          if (vd->hasExternalStorage() &&
              *targetMapType != OMPDeclareTargetDeclAttr::MT_Link)
            return;

          eraseUnusedGlobalVarAddr(getAddrOfGlobalVar(vd));
          return;
        }
      }

      // If this declaration may have caused an inline variable definition to
      // change linkage, make sure that it's emitted.
      if (astContext.getInlineVariableDefinitionKind(vd) ==
          ASTContext::InlineVariableDefinitionKind::Strong)
        eraseUnusedGlobalVarAddr(getAddrOfGlobalVar(vd));
      // Otherwise, we can ignore this declaration. The variable will be emitted
      // on its first use.
      return;
    }
  }

  // Defer code generation to first use when possible, e.g. if this is an inline
  // function. If the global must always be emitted, do it eagerly if possible
  // to benefit from cache locality. Deferring code generation is necessary to
  // avoid adding initializers to external declarations.
  if (mustBeEmitted(global) && mayBeEmittedEagerly(global)) {
    // Emit the definition if it can't be deferred.
    emitGlobalDefinition(gd);
    // OGCG calls addEmittedDeferredDecl here to track eagerly emitted decls
    // for incremental compilation support. CIR doesn't support incremental yet.
    return;
  }

  // If we're deferring emission of a C++ variable with an initializer, remember
  // the order in which it appeared in the file. This mirrors OGCG's
  // DelayedCXXInitPosition mechanism to ensure _GLOBAL__sub_I_ calls init
  // functions in TU declaration order.
  if (getLangOpts().CPlusPlus && isa<VarDecl>(global) &&
      cast<VarDecl>(global)->hasInit()) {
    delayedCXXInitPosition[global] = cxxGlobalInitOrder++;
  }

  llvm::StringRef mangledName = getMangledName(gd);
  if (getGlobalValue(mangledName) != nullptr) {
    // The value has already been used and should therefore be emitted.
    addDeferredDeclToEmit(gd);
  } else if (mustBeEmitted(global)) {
    // The value must be emitted, but cannot be emitted eagerly.
    assert(!mayBeEmittedEagerly(global));
    addDeferredDeclToEmit(gd);
  } else {
    // Otherwise, remember that we saw a deferred decl with this name. The first
    // use of the mangled name will cause it to move into deferredDeclsToEmit.
    deferredDecls[mangledName] = gd;
  }
}

/// Emit a multiversion function definition. For the `target` and
/// `target_version` attributes, this simply emits the function body — the
/// resolver/ifunc is a separate concern handled at TU finalization.
/// For `cpu_specific` and `target_clones`, loops over each version index.
/// Note: target_clones mangling is currently x86-only.
void CIRGenModule::emitMultiVersionFunctionDefinition(clang::GlobalDecl gd,
                                                      mlir::Operation *op) {
  const auto *fd = cast<FunctionDecl>(gd.getDecl());

  if (fd->isCPUSpecificMultiVersion()) {
    // Emit a separate function body for each CPU variant.
    auto *spec = fd->getAttr<CPUSpecificAttr>();
    for (unsigned i = 0; i < spec->cpus_size(); ++i)
      emitGlobalFunctionDefinition(gd.getWithMultiVersionIndex(i), nullptr);
  } else if (fd->hasAttr<TargetClonesAttr>()) {
    // Emit a separate function body for each unique version.
    // OGCG loops over TargetClonesAttr::featuresStrs_size() and emits each
    // first-of-version with getWithMultiVersionIndex(I).
    const auto *tc = fd->getAttr<TargetClonesAttr>();
    for (unsigned i = 0; i < tc->featuresStrs_size(); ++i)
      if (tc->isFirstOfVersion(i))
        emitGlobalFunctionDefinition(gd.getWithMultiVersionIndex(i), nullptr);
  } else {
    // For target/target_version: just emit the body normally.
    // OGCG does exactly this at CGModule.cpp:4599.
    emitGlobalFunctionDefinition(gd, op);
  }

  // Ensure resolver/ifunc emission is queued for target_version/target_clones.
  // For AArch64, defer resolver emission until TU finalization.
  // For non-AArch64, create resolver/ifunc immediately (which also queues
  // the resolver body emission via multiVersionFuncs).
  if (!fd->isTargetVersionMultiVersion() && !fd->isTargetClonesMultiVersion())
    return;
  if (getTarget().getTriple().isAArch64())
    addDeferredMultiVersionResolverToEmit(gd);
  else
    (void)getOrCreateMultiVersionResolver(gd);
}

/// Get the linkage for a multi-versioned function resolver/ifunc.
/// Mirrors OGCG's getMultiversionLinkage.
static cir::GlobalLinkageKind
getMultiversionLinkage(CIRGenModule &cgm, GlobalDecl gd) {
  const auto *fd = cast<FunctionDecl>(gd.getDecl());
  if (fd->getFormalLinkage() == Linkage::Internal)
    return cir::GlobalLinkageKind::InternalLinkage;
  return cir::GlobalLinkageKind::WeakODRLinkage;
}

static void addSyntheticCallsiteNonnullAttrs(CIRGenModule &cgm, GlobalDecl gd,
                                             cir::FuncOp func, bool isThunk) {
  if (isThunk || cgm.getCodeGenOpts().NullPointerIsValid)
    return;

  const auto *fd = dyn_cast_or_null<FunctionDecl>(gd.getDecl());
  if (!fd)
    return;

  auto unitAttr = mlir::UnitAttr::get(&cgm.getMLIRContext());

  if (const auto *md = dyn_cast<CXXMethodDecl>(fd);
      md && md->isImplicitObjectMemberFunction() && func.getNumArguments() > 0)
    func.setArgAttr(0, "cir.callsite_nonnull", unitAttr);

  unsigned argOffset = 0;
  if (const auto *md = dyn_cast<CXXMethodDecl>(fd)) {
    if (md->isImplicitObjectMemberFunction())
      argOffset = 1;
    if ((isa<CXXConstructorDecl>(fd) || isa<CXXDestructorDecl>(fd)) &&
        cgm.getCXXABI().needsVTTParameter(gd))
      argOffset++;
  }

  for (unsigned i = 0; i < fd->getNumParams(); ++i) {
    unsigned argIdx = i + argOffset;
    if (argIdx >= func.getNumArguments())
      break;

    QualType paramType = fd->getParamDecl(i)->getType();
    const auto *refTy = paramType->getAs<ReferenceType>();
    if (!refTy)
      continue;

    QualType pointeeTy = refTy->getPointeeType();
    if (cgm.getASTContext().getTargetAddressSpace(pointeeTy.getAddressSpace()) ==
        0)
      func.setArgAttr(argIdx, "cir.callsite_nonnull", unitAttr);
  }
}

static std::string getMangledNameImpl(CIRGenModule &cgm, GlobalDecl gd,
                                      const NamedDecl *nd,
                                      bool omitMultiVersionMangling);

static std::string getMultiVersionResolverName(CIRGenModule &cgm,
                                               GlobalDecl gd,
                                               const FunctionDecl *fd) {
  std::string resolverName = getMangledNameImpl(
      cgm, gd, fd, /*omitMultiVersionMangling=*/true);
  if (cgm.getTarget().supportsIFunc() || fd->isTargetMultiVersion())
    resolverName += ".resolver";
  return resolverName;
}

cir::FuncOp
CIRGenModule::getOrCreateMultiVersionResolver(GlobalDecl gd) {
  const auto *fd = cast<FunctionDecl>(gd.getDecl());
  assert(fd && "Not a FunctionDecl?");

  // Get the base mangled name (without multiversion suffix). Mirrors OGCG's
  // getMangledNameImpl(..., OmitMultiVersionMangling=true).
  std::string baseName = getMangledNameImpl(
      *this, gd, fd, /*omitMultiVersionMangling=*/true);

  // Naming rules mirror OGCG's GetOrCreateMultiVersionResolver:
  //  - target / cpu_dispatch / cpu_specific: ifunc name is "<base>.ifunc"
  //  - target_version / target_clones:      ifunc name is "<base>"
  // Resolver function uses "<base>.resolver" except on non-IFunc platforms
  // for target_clones / target_version, where the base symbol is the resolver.
  std::string resolverName = getMultiVersionResolverName(*this, gd, fd);
  std::string ifuncName = baseName + ".ifunc";
  if (fd->isTargetVersionMultiVersion() || fd->isTargetClonesMultiVersion())
    ifuncName = baseName;

  bool shouldReturnIFunc =
      getTarget().supportsIFunc() && !fd->isCPUSpecificMultiVersion();

  // If the ifunc/resolver already exists, return it.
  if (mlir::Operation *existing = getGlobalValue(ifuncName)) {
    if (auto funcOp = mlir::dyn_cast<cir::FuncOp>(existing)) {
      // On AArch64 we may have created a plain declaration with the base name
      // before knowing whether an ifunc will be emitted. Only return early if
      // we already have an actual ifunc (or ifunc isn't required).
      if (!shouldReturnIFunc || funcOp->hasAttr("cir.ifunc")) {
        if (shouldReturnIFunc && funcOp->hasAttr("cir.ifunc")) {
          setFunctionAttributes(gd, funcOp, /*isIncompleteFunction=*/false,
                                /*isThunk=*/false);
          addSyntheticCallsiteNonnullAttrs(*this, gd, funcOp,
                                           /*isThunk=*/false);
        }
        return funcOp;
      }
    }
  }

  // Get the function type for the multi-versioned function.
  const CIRGenFunctionInfo &fi = getTypes().arrangeGlobalDeclaration(gd);
  cir::FuncType funcType = getTypes().getFunctionType(fi);

  // Defer resolver body emission to TU finalization.
  if (fd->isTargetMultiVersion() || fd->isTargetClonesMultiVersion())
    addDeferredMultiVersionResolverToEmit(gd);

  if (shouldReturnIFunc) {
    // Create the resolver function declaration.
    // The resolver returns a function pointer (ptr in LLVM, typed in CIR).
    auto resolverFuncType =
        cir::FuncType::get({}, cir::PointerType::get(funcType), false);
    auto resolverFunc = getOrCreateCIRFunction(
        resolverName, resolverFuncType, GlobalDecl{}, /*forVTable=*/false,
        /*dontDefer=*/false, /*isThunk=*/false, NotForDefinition);

    // Resolver functions are weak_odr and should NOT be dso_local.
    // OGCG creates them via GetOrCreateLLVMFunction with GlobalDecl{} (no
    // FunctionDecl), so shouldAssumeDSOLocal can't set it. With weak_odr
    // linkage under PIC, dso_local is false. Also suppress noundef on the
    // return — OGCG doesn't set it on resolver returns.
    resolverFunc.setDsoLocal(false);
    resolverFunc->setAttr("cir.no_noundef_return",
                          builder.getUnitAttr());

    // The resolver must be exempt from sanitizer instrumentation, as it can
    // run before the sanitizer is initialized. Matches OGCG
    // CodeGenModule.cpp:5064.
    resolverFunc.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
        builder.getDictionaryAttr(
            {builder.getNamedAttr("disable_sanitizer_instrumentation",
                                  builder.getUnitAttr())})));

    // Create the ifunc FuncOp. Mark it with cir.ifunc + aliasee = resolver.
    auto ifuncFunc = getOrCreateCIRFunction(
        ifuncName, funcType, GlobalDecl{}, /*forVTable=*/false,
        /*dontDefer=*/false, /*isThunk=*/false, NotForDefinition);
    ifuncFunc.setAliasee(resolverName);
    ifuncFunc->setAttr("cir.ifunc", builder.getUnitAttr());
    ifuncFunc.setLinkage(getMultiversionLinkage(*this, gd));

    // The ifunc FuncOp was created with GlobalDecl{} (no Decl), so
    // getOrCreateCIRFunction never called setFunctionAttributes.
    // Set parameter attributes (nonnull, align, dereferenceable on 'this'
    // pointer and reference params) and return-type reference attributes
    // so they propagate to call sites during lowering.
    setFunctionAttributes(gd, ifuncFunc, /*isIncompleteFunction=*/false,
                          /*isThunk=*/false);
    addSyntheticCallsiteNonnullAttrs(*this, gd, ifuncFunc,
                                     /*isThunk=*/false);
    // Match OGCG SetCommonAttributes → setDSOLocal: internal linkage ifuncs
    // must be dso_local; weak_odr ifuncs under PIC are not.
    setDSOLocal(ifuncFunc.getOperation());

    if (fd->isTargetClonesMultiVersion() && !getTarget().getTriple().isAArch64())
      appendUniqueModuleStringAttr(getModule(), builder,
                                   kTargetClonesCompatIFuncAliasesAttr,
                                   baseName);

    return ifuncFunc;
  }

  // Non-ifunc path: return/create a resolver declaration directly.
  return getOrCreateCIRFunction(resolverName, funcType, GlobalDecl{},
                                /*forVTable=*/false);
}

void CIRGenModule::addDeferredMultiVersionResolverToEmit(GlobalDecl gd) {
  const auto *fd = cast<FunctionDecl>(gd.getDecl());
  assert(fd && "Not a FunctionDecl?");

  if (fd->isTargetVersionMultiVersion() || fd->isTargetClonesMultiVersion()) {
    std::string baseName =
        getMangledNameImpl(*this, gd, fd, /*omitMultiVersionMangling=*/true);
    if (!deferredResolversToEmit.insert(baseName).second)
      return;
  }

  multiVersionFuncs.push_back(gd);
}

void CIRGenModule::updateMultiVersionNames(GlobalDecl gd,
                                           const FunctionDecl *fd,
                                           StringRef &mangledName) {
  if (!fd->isMultiVersion())
    return;

  std::string nonTargetName =
      getMangledNameImpl(*this, gd, fd, /*omitMultiVersionMangling=*/true);
  GlobalDecl otherGD = manglings.lookup(nonTargetName);
  if (!otherGD.getDecl())
    return;

  assert(otherGD.getCanonicalDecl()
             .getDecl()
             ->getAsFunction()
             ->isMultiVersion() &&
         "Other GD should now be a multiversioned function");

  const FunctionDecl *otherFD =
      otherGD.getCanonicalDecl().getDecl()->getAsFunction()->getMostRecentDecl();
  std::string otherName = getMangledNameImpl(
      *this, otherGD, otherFD, /*omitMultiVersionMangling=*/false);
  if (otherName == nonTargetName)
    return;

  if (auto existingRecord = manglings.find(nonTargetName);
      existingRecord != manglings.end())
    manglings.remove(&(*existingRecord));

  auto result = manglings.insert(std::make_pair(otherName, otherGD));
  StringRef otherNameRef =
      mangledDeclNames[otherGD.getCanonicalDecl()] = result.first->first();
  if (gd.getCanonicalDecl() == otherGD.getCanonicalDecl())
    mangledName = otherNameRef;

  if (mlir::Operation *entry = getGlobalValue(nonTargetName)) {
    auto newName = mlir::StringAttr::get(&getMLIRContext(), otherNameRef);
    assert(succeeded(
               mlir::SymbolTable::replaceAllSymbolUses(entry, newName, getModule())) &&
           "multiversion rename should update symbol users");

    if (auto funcOp = mlir::dyn_cast<cir::FuncOp>(entry)) {
      eraseGlobalSymbol(funcOp);
      funcOp.setSymName(otherNameRef);
      insertGlobalSymbol(funcOp);
    } else if (auto aliasOp = mlir::dyn_cast<cir::AliasOp>(entry)) {
      eraseGlobalSymbol(aliasOp);
      aliasOp.setSymName(otherNameRef);
      insertGlobalSymbol(aliasOp);
    } else if (auto globalOp = mlir::dyn_cast<cir::GlobalOp>(entry)) {
      eraseGlobalSymbol(globalOp);
      globalOp.setSymName(otherNameRef);
      insertGlobalSymbol(globalOp);
    } else {
      llvm_unreachable("unexpected multiversion symbol kind");
    }
  }
}

bool CIRGenModule::emitAArch64MultiVersionFunction(GlobalDecl gd,
                                                   const FunctionDecl *fd) {
  if (!getTarget().getTriple().isAArch64() ||
      !(fd->isTargetVersionMultiVersion() ||
        fd->isTargetClonesMultiVersion()))
    return false;

  struct VersionInfo {
    cir::FuncOp func = nullptr;
    llvm::SmallVector<StringRef, 8> features;
    bool isDefault = false;
    SourceLocation srcLoc;
  };
  llvm::SmallVector<VersionInfo, 8> versions;

  auto getVersionFunction = [&](StringRef name) -> cir::FuncOp {
    auto *entry = getGlobalValue(name);
    if (!entry)
      return nullptr;
    auto funcOp = mlir::dyn_cast<cir::FuncOp>(entry);
    assert(funcOp && "expected multiversion entry to be a function");
    return funcOp;
  };

  // On AArch64, a resolver is only emitted when a default version is defined
  // in this translation unit.
  bool shouldEmitResolver = false;
  getASTContext().forEachMultiversionedFunctionVersion(
      fd, [&](const FunctionDecl *curFD) {
        bool isDefined = curFD->getDefinition() != nullptr;

        auto moveStaleBaseVersionToDefault = [&](GlobalDecl curGD,
                                                 StringRef baseName,
                                                 StringRef versionName) {
          if (baseName == versionName || getGlobalValue(versionName))
            return;

          GlobalDecl existingGD = manglings.lookup(baseName);
          if (!existingGD.getDecl() ||
              existingGD.getCanonicalDecl() != curGD.getCanonicalDecl())
            return;

          if (auto existingRecord = manglings.find(baseName);
              existingRecord != manglings.end())
            manglings.remove(&(*existingRecord));

          auto result = manglings.insert(std::make_pair(versionName.str(), curGD));
          StringRef versionNameRef =
              mangledDeclNames[curGD.getCanonicalDecl()] = result.first->first();

          mlir::Operation *entry = getGlobalValue(baseName);
          if (!entry)
            return;

          if (auto funcOp = mlir::dyn_cast<cir::FuncOp>(entry)) {
            eraseGlobalSymbol(funcOp);
            funcOp.setSymName(versionNameRef);
            insertGlobalSymbol(funcOp);
          } else if (auto aliasOp = mlir::dyn_cast<cir::AliasOp>(entry)) {
            eraseGlobalSymbol(aliasOp);
            aliasOp.setSymName(versionNameRef);
            insertGlobalSymbol(aliasOp);
          } else if (auto globalOp = mlir::dyn_cast<cir::GlobalOp>(entry)) {
            eraseGlobalSymbol(globalOp);
            globalOp.setSymName(versionNameRef);
            insertGlobalSymbol(globalOp);
          } else {
            llvm_unreachable("unexpected multiversion symbol kind");
          }
        };

        auto ensureVersionFunction = [&](GlobalDecl curGD, VersionInfo &vi) {
          std::string versionName = getMangledNameImpl(
              *this, curGD, curFD, /*omitMultiVersionMangling=*/false);
          if (vi.isDefault && isDefined) {
            std::string baseName = getMangledNameImpl(
                *this, curGD, curFD, /*omitMultiVersionMangling=*/true);
            moveStaleBaseVersionToDefault(curGD, baseName, versionName);
          }
          vi.func = getVersionFunction(versionName);
          if (vi.func)
            return;
          if (curFD->isDefined()) {
            emitGlobalFunctionDefinition(curGD, nullptr);
          } else {
            const CIRGenFunctionInfo &vfi =
                getTypes().arrangeGlobalDeclaration(curGD);
            cir::FuncType vty = getTypes().getFunctionType(vfi);
            cir::FuncOp versionFunc = getAddrOfFunction(
                curGD, vty, /*ForVTable=*/false, /*DontDefer=*/false,
                ForDefinition);
            maybeAddResultNoUndefForFmvDeclStub(*this, curGD, versionFunc);
          }
          vi.func = getVersionFunction(versionName);
          assert(vi.func && "expected multiversion function to exist");
        };

        if (const auto *tva = curFD->getAttr<TargetVersionAttr>()) {
          VersionInfo vi;
          vi.isDefault = tva->isDefaultVersion();
          vi.srcLoc = curFD->getLocation();
          if (vi.isDefault && isDefined)
            shouldEmitResolver = true;

          GlobalDecl curGD{curFD->isDefined() ? curFD->getDefinition() : curFD};
          ensureVersionFunction(curGD, vi);
          tva->getFeatures(vi.features, '+');
          versions.push_back(std::move(vi));
          return;
        }

        if (const auto *tc = curFD->getAttr<TargetClonesAttr>()) {
          for (unsigned i = 0; i < tc->featuresStrs_size(); ++i) {
            if (!tc->isFirstOfVersion(i))
              continue;

            VersionInfo vi;
            vi.isDefault = tc->isDefaultVersion(i);
            vi.srcLoc = curFD->getLocation();
            if (vi.isDefault && isDefined)
              shouldEmitResolver = true;

            GlobalDecl curGD{
                curFD->isDefined() ? curFD->getDefinition() : curFD, i};
            ensureVersionFunction(curGD, vi);
            tc->getFeatures(vi.features, i, '+');
            versions.push_back(std::move(vi));
          }
        }
      });

  if (!shouldEmitResolver)
    return true;

  // Sort by FMV priority (highest first), default case last.
  const TargetInfo &ti = getTarget();
  llvm::stable_sort(versions, [&ti](const VersionInfo &lhs,
                                    const VersionInfo &rhs) {
    if (lhs.isDefault != rhs.isDefault)
      return !lhs.isDefault;
    return ti.getFMVPriority(lhs.features).ugt(ti.getFMVPriority(rhs.features));
  });

  // Diagnose and prune unreachable function versions.
  // Matches OGCG CodeGenModule.cpp:4791-4807.
  for (auto i = versions.begin() + 1, e = versions.end(); i != e; ++i) {
    if (i->isDefault)
      continue;
    llvm::APInt rhs = llvm::AArch64::getCpuSupportsMask(i->features);
    if (std::any_of(versions.begin(), i, [rhs](const VersionInfo &ro) {
          llvm::APInt lhs = llvm::AArch64::getCpuSupportsMask(ro.features);
          return lhs.isSubsetOf(rhs);
        })) {
      getDiags().Report(i->srcLoc, diag::warn_unreachable_version)
          << i->func.getSymName();
      // Erase the CIR function if it exists and has no users.
      if (i->func && i->func->use_empty())
        i->func->erase();
      i->func = nullptr; // Mark as pruned.
    }
  }

  // Ensure resolver/ifunc symbols exist, then materialize resolver body.
  (void)getOrCreateMultiVersionResolver(gd);
  std::string baseName =
      getMangledNameImpl(*this, gd, fd, /*omitMultiVersionMangling=*/true);
  std::string resolverName = baseName + ".resolver";
  auto resolverOp = mlir::dyn_cast_or_null<cir::FuncOp>(
      getGlobalValue(resolverName));
  if (!resolverOp || !resolverOp.isDeclaration())
    return true;

  const CIRGenFunctionInfo &fi = getTypes().arrangeGlobalDeclaration(gd);
  cir::FuncType funcType = getTypes().getFunctionType(fi);
  auto funcPtrType = cir::PointerType::get(funcType);

  auto loc = getLoc(fd->getSourceRange());
  mlir::OpBuilder::InsertionGuard guard(builder);

  auto voidTy = cir::VoidType::get(&getMLIRContext());
  auto uInt64Ty = cir::IntType::get(&getMLIRContext(), 64, false);

  auto getOrCreateExternGlobal = [&](StringRef name,
                                     mlir::Type ty) -> cir::GlobalOp {
    if (auto *existing = getGlobalValue(name))
      return mlir::cast<cir::GlobalOp>(existing);
    mlir::OpBuilder::InsertionGuard g(builder);
    if (lastGlobalOp)
      builder.setInsertionPointAfter(lastGlobalOp);
    else
      builder.setInsertionPointToStart(getModule().getBody());
    auto op = cir::GlobalOp::create(
        builder, loc, name, ty,
        /*isConstant=*/false,
        cir::GlobalLinkageKind::ExternalLinkage);
    op.setDsoLocal(true);
    op.setSymVisibility("private");
    insertGlobalSymbol(op);
    lastGlobalOp = op;
    return op;
  };

  auto cpuFeaturesRecTy = cir::RecordType::get(
      &getMLIRContext(), {uInt64Ty}, /*packed=*/false, /*padded=*/false,
      cir::RecordType::Struct);
  bool aarch64CpuInitialized = false;
  auto emitAArch64CpuInit = [&]() {
    if (aarch64CpuInitialized)
      return;

    auto cpuInitFnTy = cir::FuncType::get({}, voidTy, false);
    auto cpuInitFn = createRuntimeFunction(
        cpuInitFnTy, "__init_cpu_features_resolver", {},
        /*isLocal=*/true);
    // OGCG (ARM.cpp:8049) explicitly forces dso_local(true) on this function.
    cpuInitFn.setDsoLocal(true);
    getOrCreateExternGlobal("__aarch64_cpu_features", cpuFeaturesRecTy);
    cir::CallOp::create(
        builder, loc,
        mlir::SymbolRefAttr::get(&getMLIRContext(), cpuInitFn.getSymName()),
        voidTy, mlir::ValueRange{});
    aarch64CpuInitialized = true;
  };

  mlir::Block *entryBlock = builder.createBlock(&resolverOp.getBody());
  builder.setInsertionPointToStart(entryBlock);

  // Build the resolver as an if-else-trap decision tree, matching OGCG's
  // EmitAArch64MultiVersionResolver. Versions are sorted by priority
  // (highest first). For each version: if it's the default, return
  // unconditionally. Otherwise, check features and branch.
  for (VersionInfo &vi : versions) {
    // Skip unreachable versions that were pruned above.
    if (!vi.func)
      continue;

    if (vi.isDefault || vi.features.empty()) {
      // Default entry: return unconditionally.
      mlir::Value fnAddr = cir::GetGlobalOp::create(
          builder, loc, funcPtrType, vi.func.getSymName());
      cir::ReturnOp::create(builder, loc, fnAddr);
      goto aarch64_resolver_done;
    }

    llvm::APInt featureMask = llvm::AArch64::getCpuSupportsMask(vi.features);
    if (featureMask == 0)
      continue;

    emitAArch64CpuInit();

    mlir::Value fnAddr = cir::GetGlobalOp::create(
        builder, loc, funcPtrType, vi.func.getSymName());
    auto modelAddr = cir::GetGlobalOp::create(
        builder, loc, cir::PointerType::get(cpuFeaturesRecTy),
        "__aarch64_cpu_features");
    auto featuresPtr = cir::GetMemberOp::create(
        builder, loc, cir::PointerType::get(uInt64Ty), modelAddr,
        /*name=*/"", /*index=*/0);
    auto align8 = mlir::IntegerAttr::get(
        mlir::IntegerType::get(&getMLIRContext(), 64), 8);
    auto featuresVal = cir::LoadOp::create(
        builder, loc, featuresPtr, /*isDeref=*/false,
        /*isVolatile=*/false, /*is_nontemporal=*/false,
        /*alignment=*/align8,
        /*sync_scope=*/cir::SyncScopeKindAttr{},
        /*mem_order=*/cir::MemOrderAttr{},
        /*tbaa=*/cir::TBAAAttr{});
    auto maskVal = cir::ConstantOp::create(
        builder, loc, uInt64Ty,
        builder.getUInt64AttrLocalCached(featureMask.getZExtValue()));
    auto andResult = cir::BinOp::create(
        builder, loc, uInt64Ty, cir::BinOpKind::And, featuresVal, maskVal,
        {}, {}, {}, {}, {}, {});
    auto cond = cir::CmpOp::create(
        builder, loc, cir::CmpOpKind::eq, andResult, maskVal);

    // Conditional: if features match, return; else fall through.
    auto &region = resolverOp.getBody();
    auto *curBlock = builder.getInsertionBlock();
    auto *retBlock = new mlir::Block();
    region.push_back(retBlock);
    auto *elseBlock = new mlir::Block();
    region.push_back(elseBlock);

    builder.setInsertionPointToEnd(curBlock);
    cir::BrCondOp::create(builder, loc, cond, retBlock, elseBlock);

    builder.setInsertionPointToStart(retBlock);
    cir::ReturnOp::create(builder, loc, fnAddr);

    builder.setInsertionPointToStart(elseBlock);
  }

  // No default variant matched — trap.
  cir::TrapOp::create(builder, loc);

aarch64_resolver_done:

  resolverOp.setLinkage(getMultiversionLinkage(*this, gd));
  setGlobalVisibility(resolverOp, fd);
  setDSOLocal(resolverOp.getOperation());
  if (supportsCOMDAT() &&
      resolverOp.getLinkage() != cir::GlobalLinkageKind::InternalLinkage)
    resolverOp.setComdat(true);

  // Set default target-specific attributes (e.g. branch-target-enforcement
  // on AArch64). Pass D=nullptr to get global defaults only, matching OGCG
  // setMultiVersionResolverAttributes (CodeGenModule.cpp:5072).
  getTargetCIRGenInfo().setTargetAttributes(/*decl=*/nullptr, resolverOp,
                                            *this);
  return true;
}

void CIRGenModule::emitMultiVersionFunctions() {
  std::vector<GlobalDecl> funcsToEmit;
  multiVersionFuncs.swap(funcsToEmit);

  for (GlobalDecl gd : funcsToEmit) {
    const auto *fd = cast<FunctionDecl>(gd.getDecl());
    assert(fd && "Expected a FunctionDecl");

    if (emitAArch64MultiVersionFunction(gd, fd))
      continue;

    // Handle x86 target and target_clones attributes.
    if (!fd->isTargetMultiVersion() && !fd->isTargetClonesMultiVersion())
      continue;
    // Skip non-x86 multi-version functions.
    if (!getTarget().getTriple().isX86())
      continue;

    // Collect all versions with their features.
    struct VersionInfo {
      std::string mangledName;
      llvm::SmallVector<StringRef, 8> features;
      std::optional<StringRef> architecture;
      bool isDefault = false;
    };
    llvm::SmallVector<VersionInfo, 4> versions;

    getASTContext().forEachMultiversionedFunctionVersion(
        fd, [&](const FunctionDecl *curFD) {
          bool isDefined = curFD->getDefinition() != nullptr;

          if (const auto *ta = curFD->getAttr<TargetAttr>()) {
            VersionInfo vi;
            GlobalDecl curGD{isDefined ? curFD->getDefinition() : curFD};
            vi.mangledName = getMangledName(curGD).str();
            ta->getX86AddedFeatures(vi.features);
            vi.architecture = ta->getX86Architecture();
            vi.isDefault = ta->isDefaultVersion();

            // Ensure the variant function exists.
            if (!getGlobalValue(vi.mangledName)) {
              if (isDefined) {
                emitGlobalFunctionDefinition(curGD, nullptr);
              } else {
                const CIRGenFunctionInfo &fi =
                    getTypes().arrangeGlobalDeclaration(curGD);
                cir::FuncType ty = getTypes().getFunctionType(fi);
                getAddrOfFunction(curGD, ty, /*ForVTable=*/false,
                                  /*DontDefer=*/false, ForDefinition);
              }
            }
            versions.push_back(std::move(vi));
          } else if (const auto *tc = curFD->getAttr<TargetClonesAttr>()) {
            for (unsigned i = 0; i < tc->featuresStrs_size(); ++i) {
              if (!tc->isFirstOfVersion(i))
                continue;

              VersionInfo vi;
              GlobalDecl curGD{isDefined ? curFD->getDefinition() : curFD, i};
              vi.mangledName = getMangledName(curGD).str();
              vi.isDefault = tc->isDefaultVersion(i);
              tc->getX86Feature(vi.features, i);
              vi.architecture = tc->getX86Architecture(i);

              // Ensure the variant function exists.
              if (!getGlobalValue(vi.mangledName)) {
                if (isDefined) {
                  emitGlobalFunctionDefinition(curGD, nullptr);
                } else {
                  const CIRGenFunctionInfo &fi =
                      getTypes().arrangeGlobalDeclaration(curGD);
                  cir::FuncType ty = getTypes().getFunctionType(fi);
                  getAddrOfFunction(curGD, ty, /*ForVTable=*/false,
                                    /*DontDefer=*/false, ForDefinition);
                }
              }
              versions.push_back(std::move(vi));
            }
          }
        });

    // Sort by FMV priority (highest first). Default goes last.
    const TargetInfo &ti = getTarget();
    llvm::stable_sort(versions, [&ti](const VersionInfo &lhs,
                                      const VersionInfo &rhs) {
      // Default always goes last.
      if (lhs.isDefault != rhs.isDefault)
        return !lhs.isDefault;
      llvm::SmallVector<StringRef, 8> lhsFeats{lhs.features};
      if (lhs.architecture)
        lhsFeats.push_back(*lhs.architecture);
      llvm::SmallVector<StringRef, 8> rhsFeats{rhs.features};
      if (rhs.architecture)
        rhsFeats.push_back(*rhs.architecture);
      return ti.getFMVPriority(lhsFeats).ugt(ti.getFMVPriority(rhsFeats));
    });

    bool supportsIFunc = getTarget().supportsIFunc();
    std::string resolverName = getMultiVersionResolverName(*this, gd, fd);

    // Find or create the resolver function.
    auto resolverOp = mlir::dyn_cast_or_null<cir::FuncOp>(
        getGlobalValue(resolverName));
    if (!resolverOp || !resolverOp.isDeclaration()) {
      // Resolver wasn't created by getOrCreateMultiVersionResolver (shouldn't
      // happen normally, but handle it). Skip already-emitted bodies too.
      continue;
    }

    // OGCG creates a backward-compat .ifunc alias (CodeGenModule.cpp:4765-4780)
    // for target_clones. This alias is deprecated and may be removed. For now
    // CIR defers this to avoid complexity with ifunc aliasee lowering.

    // Get the function type.
    const CIRGenFunctionInfo &fi = getTypes().arrangeGlobalDeclaration(gd);
    cir::FuncType funcType = getTypes().getFunctionType(fi);
    auto funcPtrType = cir::PointerType::get(funcType);

    // Build the resolver body.
    auto loc = getLoc(fd->getSourceRange());
    mlir::OpBuilder::InsertionGuard guard(builder);

    auto int32Ty = cir::IntType::get(&getMLIRContext(), 32, true);
    auto cpuFeatures1Ty = cir::ArrayType::get(int32Ty, 1);
    auto cpuModelTy = getX86CpuModelType(getMLIRContext());
    auto cpuFeatures2Ty = cir::ArrayType::get(int32Ty, 3);

    // Pre-create all module-level declarations BEFORE entering the resolver
    // body. createRuntimeFunction / getOrCreateCIRFunction use the builder
    // to insert FuncOps/GlobalOps, so the insertion point must still be at
    // module level.
    auto voidTy = cir::VoidType::get(&getMLIRContext());
    auto cpuInitFnTy = cir::FuncType::get({}, voidTy, false);
    auto cpuInitFn =
        createRuntimeFunction(cpuInitFnTy, "__cpu_indicator_init", {},
                              /*isLocal=*/true);
    // Match OGCG: explicitly force dso_local on __cpu_indicator_init.
    cpuInitFn.setDsoLocal(true);

    // Helper to get or create an extern global at module level.
    auto getOrCreateExternGlobal = [&](StringRef name,
                                       mlir::Type ty) -> cir::GlobalOp {
      if (auto *existing = getGlobalValue(name))
        return mlir::cast<cir::GlobalOp>(existing);
      mlir::OpBuilder::InsertionGuard g(builder);
      if (lastGlobalOp)
        builder.setInsertionPointAfter(lastGlobalOp);
      else
        builder.setInsertionPointToStart(getModule().getBody());
      auto op = cir::GlobalOp::create(
          builder, loc, name, ty,
          /*isConstant=*/false,
          cir::GlobalLinkageKind::ExternalLinkage);
      op.setDsoLocal(true);
      // Extern declarations must have private MLIR visibility.
      op.setSymVisibility("private");
      insertGlobalSymbol(op);
      lastGlobalOp = op;
      return op;
    };

    getOrCreateExternGlobal("__cpu_model", cpuModelTy);

    // Check if any variant needs __cpu_features2 and pre-create it.
    // Must check both explicit features and x86-64 ISA level architectures
    // (e.g. arch=x86-64-v3 maps to AVX2+BMI2+... which live in mask[1..3]).
    // CPU model names (e.g. "ivybridge") use type/subtype checks, not
    // getCpuSupportsMask, and don't need __cpu_features2.
    bool needsCpuFeatures2 = false;
    for (const auto &vi : versions) {
      if (vi.isDefault)
        continue;
      llvm::SmallVector<StringRef, 8> featsToCheck(vi.features);
      // Only x86-64 ISA level strings are valid for getCpuSupportsMask.
      // CPU model names like "ivybridge" are checked via __cpu_model
      // type/subtype comparison and don't use __cpu_features2.
      if (vi.architecture.has_value() &&
          vi.architecture->starts_with("x86-64"))
        featsToCheck.push_back(*vi.architecture);
      if (featsToCheck.empty())
        continue;
      std::array<uint32_t, 4> mask =
          llvm::X86::getCpuSupportsMask(featsToCheck);
      if (mask[1] != 0 || mask[2] != 0 || mask[3] != 0) {
        needsCpuFeatures2 = true;
        break;
      }
    }
    if (needsCpuFeatures2)
      getOrCreateExternGlobal("__cpu_features2", cpuFeatures2Ty);

    // Now create the function body — from here on the builder is inside
    // the resolver and we must only emit value-producing ops, not
    // module-level declarations.
    mlir::Block *entryBlock = resolverOp.addEntryBlock();
    builder.setInsertionPointToStart(entryBlock);
    SmallVector<mlir::Value, 8> resolverArgs(entryBlock->getArguments().begin(),
                                             entryBlock->getArguments().end());

    // Call __cpu_indicator_init().
    cir::CallOp::create(
        builder, loc,
        mlir::SymbolRefAttr::get(&getMLIRContext(), cpuInitFn.getSymName()),
        voidTy, mlir::ValueRange{});

    auto boolTy = cir::BoolType::get(&getMLIRContext());
    auto align4 = mlir::IntegerAttr::get(
        mlir::IntegerType::get(&getMLIRContext(), 64), 4);

    auto loadCpuModelField = [&](unsigned fieldIndex) -> mlir::Value {
      auto modelAddr = cir::GetGlobalOp::create(
          builder, loc, cir::PointerType::get(cpuModelTy), "__cpu_model");
      auto fieldPtr = cir::GetMemberOp::create(
          builder, loc, cir::PointerType::get(int32Ty), modelAddr,
          /*name=*/"", fieldIndex);
      return cir::LoadOp::create(
          builder, loc, fieldPtr, /*isDeref=*/false, /*isVolatile=*/false,
          /*is_nontemporal=*/false, /*alignment=*/align4,
          /*sync_scope=*/cir::SyncScopeKindAttr{},
          /*mem_order=*/cir::MemOrderAttr{}, /*tbaa=*/cir::TBAAAttr{});
    };

    auto loadCpuFeature0 = [&]() -> mlir::Value {
      auto modelAddr = cir::GetGlobalOp::create(
          builder, loc, cir::PointerType::get(cpuModelTy), "__cpu_model");
      auto featuresPtr = cir::GetMemberOp::create(
          builder, loc, cir::PointerType::get(cpuFeatures1Ty), modelAddr,
          /*name=*/"", /*index=*/3);
      auto idx0 = cir::ConstantOp::create(
          builder, loc, int32Ty, builder.getSInt32AttrLocalCached(0));
      auto featPtr = cir::GetElementOp::create(
          builder, loc, cir::PointerType::get(int32Ty), featuresPtr, idx0);
      return cir::LoadOp::create(
          builder, loc, featPtr, /*isDeref=*/false, /*isVolatile=*/false,
          /*is_nontemporal=*/false, /*alignment=*/align4,
          /*sync_scope=*/cir::SyncScopeKindAttr{},
          /*mem_order=*/cir::MemOrderAttr{}, /*tbaa=*/cir::TBAAAttr{});
    };

    auto combineConds = [&](mlir::Value lhs,
                            mlir::Value rhs) -> mlir::Value {
      if (!lhs)
        return rhs;
      return cir::BinOp::create(builder, loc, boolTy, cir::BinOpKind::And, lhs,
                                rhs, {}, {}, {}, {}, {}, {});
    };

    auto emitFeatureMaskCheck =
        [&](std::array<uint32_t, 4> mask) -> mlir::Value {
      mlir::Value localCond;
      if (mask[0] != 0) {
        auto featVal = loadCpuFeature0();
        auto maskVal = cir::ConstantOp::create(
            builder, loc, int32Ty,
            builder.getSInt32AttrLocalCached(mask[0]));
        auto andResult = cir::BinOp::create(
            builder, loc, int32Ty, cir::BinOpKind::And, featVal, maskVal, {},
            {}, {}, {}, {}, {});
        auto thisCond = cir::CmpOp::create(builder, loc, cir::CmpOpKind::eq,
                                           andResult, maskVal);
        localCond = combineConds(localCond, thisCond);
      }
      for (int i = 1; i < 4; ++i) {
        if (mask[i] == 0)
          continue;
        auto feat2Addr = cir::GetGlobalOp::create(
            builder, loc, cir::PointerType::get(cpuFeatures2Ty),
            "__cpu_features2");
        auto idxI = cir::ConstantOp::create(
            builder, loc, int32Ty,
            builder.getSInt32AttrLocalCached(i - 1));
        auto feat2Ptr = cir::GetElementOp::create(
            builder, loc, cir::PointerType::get(int32Ty), feat2Addr, idxI);
        auto feat2Val = cir::LoadOp::create(
            builder, loc, feat2Ptr, /*isDeref=*/false,
            /*isVolatile=*/false, /*is_nontemporal=*/false,
            /*alignment=*/align4,
            /*sync_scope=*/cir::SyncScopeKindAttr{},
            /*mem_order=*/cir::MemOrderAttr{},
            /*tbaa=*/cir::TBAAAttr{});
        auto maskVal = cir::ConstantOp::create(
            builder, loc, int32Ty,
            builder.getSInt32AttrLocalCached(mask[i]));
        auto andResult = cir::BinOp::create(
            builder, loc, int32Ty, cir::BinOpKind::And, feat2Val, maskVal, {},
            {}, {}, {}, {}, {});
        auto thisCond = cir::CmpOp::create(builder, loc, cir::CmpOpKind::eq,
                                           andResult, maskVal);
        localCond = combineConds(localCond, thisCond);
      }
      return localCond;
    };

    auto emitResolverReturn = [&](StringRef calleeName) {
      if (supportsIFunc) {
        auto fnAddr = cir::GetGlobalOp::create(builder, loc, funcPtrType,
                                               calleeName);
        cir::ReturnOp::create(builder, loc, fnAddr.getResult());
        return;
      }

      auto calleeSymRef =
          mlir::SymbolRefAttr::get(&getMLIRContext(), calleeName);
      auto callOp = cir::CallOp::create(builder, loc, calleeSymRef,
                                        funcType.getReturnType(), resolverArgs);
      callOp.setMustTail(true);
      if (callOp.getNumResults() == 0)
        cir::ReturnOp::create(builder, loc);
      else
        cir::ReturnOp::create(builder, loc, callOp.getResult());
    };

    // Build the resolver as an if-else-trap decision tree, matching OGCG's
    // X86 multiversion resolver structure. Non-IFunc platforms must tail-call
    // the selected clone using the resolver's original signature instead of
    // returning a function pointer.
    mlir::Block *curBlock = entryBlock;
    bool emittedDefault = false;
    for (const VersionInfo &vi : versions) {
      builder.setInsertionPointToEnd(curBlock);

      // Generate the feature/architecture check condition.
      // Mirrors OGCG's FormX86ResolverCondition.
      mlir::Value cond;

      // Architecture check (EmitX86CpuIs equivalent).
      if (vi.architecture.has_value()) {
        StringRef arch = *vi.architecture;
        if (arch.starts_with("x86-64")) {
          // x86-64 micro-architecture levels use feature checks.
          std::array<uint32_t, 4> archMask =
              llvm::X86::getCpuSupportsMask({arch});
          cond = emitFeatureMaskCheck(archMask);
        } else {
          // CPU type/subtype check (EmitX86CpuIs equivalent).
          // Look up index (vendor=0, type=1, subtype=2) and expected value.
          auto [cpuIndex, cpuValue] =
              llvm::StringSwitch<std::pair<unsigned, unsigned>>(arch)
#define X86_VENDOR(ENUM, STRING)                                               \
  .Case(STRING, {0u, static_cast<unsigned>(llvm::X86::ENUM)})
#define X86_CPU_TYPE_ALIAS(ENUM, ALIAS)                                        \
  .Case(ALIAS, {1u, static_cast<unsigned>(llvm::X86::ENUM)})
#define X86_CPU_TYPE(ENUM, STR)                                                \
  .Case(STR, {1u, static_cast<unsigned>(llvm::X86::ENUM)})
#define X86_CPU_SUBTYPE_ALIAS(ENUM, ALIAS)                                     \
  .Case(ALIAS, {2u, static_cast<unsigned>(llvm::X86::ENUM)})
#define X86_CPU_SUBTYPE(ENUM, STR)                                             \
  .Case(STR, {2u, static_cast<unsigned>(llvm::X86::ENUM)})
#include "llvm/TargetParser/X86TargetParser.def"
                  .Default({0, 0});
          assert(cpuValue != 0 && "Invalid CPU string");

          auto fieldVal = loadCpuModelField(cpuIndex);
          auto expectedVal = cir::ConstantOp::create(
              builder, loc, int32Ty,
              builder.getSInt32AttrLocalCached(cpuValue));
          cond = cir::CmpOp::create(builder, loc, cir::CmpOpKind::eq, fieldVal,
                                    expectedVal);
        }
      }

      // Compute the feature mask for this variant.
      std::array<uint32_t, 4> featureMask =
          llvm::X86::getCpuSupportsMask(vi.features);
      if (auto featureCond = emitFeatureMaskCheck(featureMask))
        cond = combineConds(cond, featureCond);

      if (!cond) {
        emitResolverReturn(vi.mangledName);
        emittedDefault = true;
        break;
      }

      auto &region = resolverOp.getBody();
      auto *retBlock = new mlir::Block();
      region.push_back(retBlock);
      auto *elseBlock = new mlir::Block();
      region.push_back(elseBlock);

      cir::BrCondOp::create(builder, loc, cond, retBlock, elseBlock);

      builder.setInsertionPointToStart(retBlock);
      emitResolverReturn(vi.mangledName);

      curBlock = elseBlock;
    }

    if (!emittedDefault) {
      builder.setInsertionPointToStart(curBlock);
      cir::TrapOp::create(builder, loc);
    }

    // Set resolver attributes. Matches OGCG setMultiVersionResolverAttributes.
    resolverOp.setLinkage(getMultiversionLinkage(*this, gd));
    setGlobalVisibility(resolverOp, fd);
    setDSOLocal(resolverOp.getOperation());
    // Enable COMDAT on Linux.
    if (supportsCOMDAT() &&
        resolverOp.getLinkage() != cir::GlobalLinkageKind::InternalLinkage)
      resolverOp.setComdat(true);

    // Set default target-specific attributes (e.g. branch-target-enforcement
    // on AArch64). Pass D=nullptr to get global defaults only, matching OGCG
    // setMultiVersionResolverAttributes (CodeGenModule.cpp:5072).
    getTargetCIRGenInfo().setTargetAttributes(/*decl=*/nullptr, resolverOp,
                                              *this);
  }

  // Ensure any deferred decls triggered by variant emission are emitted.
  if (!funcsToEmit.empty())
    emitDeferred(16);

  // If more multi-version functions were added, emit them too.
  if (!multiVersionFuncs.empty())
    emitMultiVersionFunctions();
}

void CIRGenModule::emitGlobalFunctionDefinition(clang::GlobalDecl gd,
                                                mlir::Operation *op) {
  auto const *funcDecl = cast<FunctionDecl>(gd.getDecl());
  const CIRGenFunctionInfo &fi = getTypes().arrangeGlobalDeclaration(gd);
  cir::FuncType funcType = getTypes().getFunctionType(fi);
  cir::FuncOp funcOp = dyn_cast_if_present<cir::FuncOp>(op);
  if (!funcOp || funcOp.getFunctionType() != funcType) {
    funcOp = getAddrOfFunction(gd, funcType, /*ForVTable=*/false,
                               /*DontDefer=*/true, ForDefinition);
  }

  // Already emitted.
  if (!funcOp.isDeclaration())
    return;

  // Track this definition for source-based coverage mapping. The top-level
  // decl dispatcher only sees top-level declarations, so functions emitted
  // on demand (lambda call operators, used inline/member functions) would
  // otherwise be instrumented with PGO counters but never get a coverage
  // mapping record. trackCoverageMappingDecl dedups (SetVector) and is a
  // no-op without -fcoverage-mapping.
  trackCoverageMappingDecl(const_cast<FunctionDecl *>(funcDecl));

  setFunctionLinkage(gd, funcOp);
  setGVProperties(funcOp, funcDecl);
  maybeHandleStaticInExternC(funcDecl, funcOp.getSymName());
  maybeSetTrivialComdat(*funcDecl, funcOp);
  // OGCG calls setLLVMFunctionFEnvAttributes here (before codegen) to set
  // strictfp. CIR sets strictfp in setCIRFunctionAttributesForDefinition
  // (called after codegen at line 2106 below).

  // Save the original declaration before generateCode, which may return a
  // clone for inline builtins (e.g. memcpy → memcpy.inline). OGCG's
  // SetLLVMFunctionAttributesForDefinition is called on the original Fn
  // (line 6618 of CodeGenModule.cpp), not the clone, because GenerateCode
  // only modifies its local copy. We need to match that by also setting
  // definition attributes on the original declaration.
  cir::FuncOp originalFuncOp = funcOp;

  CIRGenFunction cgf(*this, builder);
  curCGF = &cgf;
  {
    mlir::OpBuilder::InsertionGuard guard(builder);
    funcOp = cgf.generateCode(gd, funcOp, funcType);
  }
  curCGF = nullptr;

  if (!funcOp)
    return;

  setNonAliasAttributes(gd, funcOp);
  setCIRFunctionAttributesForDefinition(gd, funcOp);
  attachOpenMPDeclareTargetFunctionAttrs(funcOp, funcDecl, getMLIRContext());

  // For inline builtins, also set definition attributes on the original
  // declaration. In OGCG, SetLLVMFunctionAttributesForDefinition runs on
  // the original function (not the clone), so it gets alwaysinline etc.
  // Also mark the original declaration so CallConvLowering doesn't skip it
  // (it has no call-site users since calls go to the .inline clone).
  if (funcDecl->isInlineBuiltinDeclaration() && originalFuncOp != funcOp) {
    setCIRFunctionAttributesForDefinition(gd, originalFuncOp);
    originalFuncOp->setAttr("cir.inline_builtin_declaration",
                            mlir::UnitAttr::get(&getMLIRContext()));
  }

  // OGCG creates inline builtin clones (e.g. abs.inline) via
  // llvm::Function::Create which starts with NO parameter attributes,
  // so definition-side noundef is absent. Mark the clone so LowerToLLVM
  // can skip definition-side noundef while keeping it on call sites.
  if (funcDecl->isInlineBuiltinDeclaration())
    funcOp->setAttr("cir.inline_builtin_definition",
                    mlir::UnitAttr::get(&getMLIRContext()));

  // Mark parameters that are never noundef:
  //  - member pointers: use -1 as null representation
  //  - nullptr_t: explicitly excluded by OGCG DetermineNoUndef
  //  - _Atomic types: fail isScalarType() in OGCG so no noundef
  // This must run after generateCode which creates the function arguments.
  // Matches OGCG CGCall.cpp:2301-2310.
  {
    unsigned argOffset = 0;
    if (const auto *md = dyn_cast<CXXMethodDecl>(funcDecl))
      if (md->isImplicitObjectMemberFunction())
        argOffset = 1;

    for (unsigned i = 0; i < funcDecl->getNumParams(); ++i) {
      unsigned argIdx = i + argOffset;
      if (argIdx >= funcOp.getNumArguments())
        break;
      QualType paramType = funcDecl->getParamDecl(i)->getType();
      // __mfp8 is not scalar in OGCG (BuiltinType::MFloat8 > NullPtr in enum),
      // so DetermineNoUndef returns false. Same for vectors of __mfp8.
      bool isMfp8 = paramType->isMFloat8Type();
      if (!isMfp8) {
        if (const auto *VT = paramType->getAs<VectorType>())
          isMfp8 = VT->getElementType()->isMFloat8Type();
      }
      if (paramType->isMemberPointerType() || paramType->isNullPtrType() ||
          paramType->isAtomicType() || isMfp8) {
        funcOp.setArgAttr(argIdx, "cir.no_noundef",
                          mlir::UnitAttr::get(&getMLIRContext()));
      }
    }
  }

  if (DeviceKernelAttr::isOpenCLSpelling(
          funcDecl->getAttr<DeviceKernelAttr>())) {
    if (gd.getKernelReferenceKind() == KernelReferenceKind::Stub &&
        !funcDecl->hasAttr<NoInlineAttr>() &&
        !funcDecl->hasAttr<OptimizeNoneAttr>() &&
        !funcOp.getOptNone()) {
      // NVPTX has no backend pass that force-inlines OpenCL kernel stubs
      // (unlike AMDGPU's AMDGPUAlwaysInline), and classic clang leaves the
      // NVPTX kernel entry a thin wrapper that calls the stub. Marking the stub
      // alwaysinline here would inline the body into the entry while the
      // standalone (externally-visible) stub also remains, emitting the kernel
      // body twice. Mark it noinline so the entry stays a thin wrapper and the
      // body is emitted once, matching classic clang's NVPTX output.
      if (getTriple().isNVPTX())
        funcOp.setInlineKind(cir::InlineKind::NoInline);
      else
        funcOp.setInlineKind(cir::InlineKind::AlwaysInline);
    }
  }

  auto getPriority = [this](const auto *attr) -> int {
    Expr *e = attr->getPriority();
    if (e)
      return e->EvaluateKnownConstInt(this->getASTContext()).getExtValue();
    return attr->DefaultPriority;
  };

  if (const ConstructorAttr *ca = funcDecl->getAttr<ConstructorAttr>())
    addGlobalCtor(funcOp, getPriority(ca));
  if (const DestructorAttr *da = funcDecl->getAttr<DestructorAttr>())
    addGlobalDtor(funcOp, getPriority(da), /*isDtorAttrFunc=*/true);

  if (funcDecl->getAttr<AnnotateAttr>())
    deferredAnnotations[getMangledName(gd)] = cast<ValueDecl>(funcDecl);

  // OGCG calls getOpenMPRuntime().emitDeclareTargetFunction() here for
  // functions with OMPDeclareTargetDeclAttr. CIR preserves
  // `omp.declare_target` on cir.func and materializes any target_indirect
  // offload-entry glue later during LLVM IR translation.
}

/// Track functions to be called before main() runs.
void CIRGenModule::addGlobalCtor(cir::FuncOp ctor,
                                 std::optional<int> priority) {
  // Traditional LLVM codegen directly adds the function to the list of global
  // ctors. In CIR we just add a global_ctor attribute to the function. The
  // global list is created in LoweringPrepare.
  //
  // FIXME(from traditional LLVM): Type coercion of void()* types.
  ctor.setGlobalCtorPriority(
      priority.value_or(cir::GlobalCtorAttr::getDefaultPriority()));
}

/// Add a function to the list that will be called when the module is unloaded.
void CIRGenModule::addGlobalDtor(cir::FuncOp dtor, std::optional<int> priority,
                                 bool isDtorAttrFunc) {
  if (codeGenOpts.RegisterGlobalDtorsWithAtExit &&
      (!getASTContext().getTargetInfo().getTriple().isOSAIX() ||
       isDtorAttrFunc)) {
    // Match OGCG AddGlobalDtor behavior:
    // - with -fregister-global-dtors-with-atexit, collect dtor-attribute
    //   functions for registration through __GLOBAL_init_<priority> helpers;
    // - on non-AIX, do the same for regular global dtors.
    //
    // LoweringPrepare consumes this marker to move global dtor entries into
    // global ctor entries that perform __cxa_atexit/atexit registration.
    dtor->setAttr("cir.register_global_dtor_with_atexit",
                  mlir::UnitAttr::get(&getMLIRContext()));
  }

  // FIXME(from traditional LLVM): Type coercion of void()* types.
  dtor.setGlobalDtorPriority(priority);
}

static llvm::StringRef getSymName(mlir::Operation *op) {
  if (auto globalOp = mlir::dyn_cast<cir::GlobalOp>(op))
    return globalOp.getSymName();
  if (auto funcOp = mlir::dyn_cast<cir::FuncOp>(op))
    return funcOp.getSymName();
  if (auto aliasOp = mlir::dyn_cast<cir::AliasOp>(op))
    return aliasOp.getSymName();
  llvm_unreachable("expected GlobalOp, FuncOp, or AliasOp");
}

void CIRGenModule::addUsedGlobal(mlir::Operation *gv) {
  llvmUsed.emplace_back(getSymName(gv));
}

void CIRGenModule::addCompilerUsedGlobal(mlir::Operation *gv) {
  llvmCompilerUsed.emplace_back(getSymName(gv));
}

void CIRGenModule::addUsedOrCompilerUsedGlobal(mlir::Operation *gv) {
  if (getTriple().isOSBinFormatELF())
    llvmCompilerUsed.emplace_back(getSymName(gv));
  else
    llvmUsed.emplace_back(getSymName(gv));
}

void CIRGenModule::emitLLVMUsed() {
  auto setModuleAttr = [&](llvm::StringRef attrName,
                           llvm::SmallVectorImpl<std::string> &list) {
    if (list.empty())
      return;
    // Deduplicate entries — the same symbol may be added via multiple code
    // paths (e.g., decomposition bindings with __attribute__((used))).
    llvm::StringSet<> seen;
    llvm::SmallVector<mlir::Attribute> refs;
    for (const auto &name : list) {
      if (seen.insert(name).second)
        refs.push_back(mlir::FlatSymbolRefAttr::get(&getMLIRContext(), name));
    }
    theModule->setAttr(attrName,
                       mlir::ArrayAttr::get(&getMLIRContext(), refs));
  };
  setModuleAttr(cir::CIRDialect::getLLVMUsedAttrName(), llvmUsed);
  setModuleAttr(cir::CIRDialect::getLLVMCompilerUsedAttrName(),
                llvmCompilerUsed);
}

template <typename SomeDecl>
void CIRGenModule::maybeHandleStaticInExternC(const SomeDecl *d,
                                              llvm::StringRef symName) {
  if (!getLangOpts().CPlusPlus)
    return;

  // Must have 'used' attribute, or else inline assembly can't rely on
  // the name existing.
  if (!d->template hasAttr<UsedAttr>())
    return;

  // Must have internal linkage and an ordinary name.
  if (!d->getIdentifier() || d->getFormalLinkage() != Linkage::Internal)
    return;

  // Must be in an extern "C" context. Entities declared directly within
  // a record are not extern "C" even if the record is in such a context.
  const SomeDecl *first = d->getFirstDecl();
  if (first->getDeclContext()->isRecord() || !first->isInExternCContext())
    return;

  // OK, this is an internal linkage entity inside an extern "C" linkage
  // specification. Make a note of that so we can give it the "expected"
  // mangled name if nothing else is using that name.
  auto result = staticExternCValues.insert(
      std::make_pair(d->getIdentifier(), symName));

  // If we have multiple internal linkage entities with the same name
  // in extern "C" regions, none of them gets that name.
  if (!result.second)
    result.first->second = llvm::StringRef();
}

// Explicit instantiations for FunctionDecl and VarDecl.
template void CIRGenModule::maybeHandleStaticInExternC(const FunctionDecl *,
                                                       llvm::StringRef);
template void CIRGenModule::maybeHandleStaticInExternC(const VarDecl *,
                                                       llvm::StringRef);

static std::optional<llvm::StringRef> getIFuncResolverName(mlir::Operation *op) {
  if (auto funcOp = mlir::dyn_cast<cir::FuncOp>(op)) {
    if (funcOp->hasAttr("cir.ifunc") && funcOp.getAliasee())
      return *funcOp.getAliasee();
    return std::nullopt;
  }

  if (auto aliasOp = mlir::dyn_cast<cir::AliasOp>(op)) {
    if (aliasOp.getIsIfunc())
      return aliasOp.getAliasee();
  }
  return std::nullopt;
}

bool CIRGenModule::checkAndReplaceExternCIFuncs(mlir::Operation *elem,
                                                mlir::Operation *cppFunc) {
  if (elem == cppFunc)
    return false;

  auto uses = mlir::SymbolTable::getSymbolUses(elem, getModule());
  if (!uses)
    return false;

  llvm::SmallVector<mlir::Operation *, 4> ifuncUsers;
  auto elemName = mlir::cast<mlir::SymbolOpInterface>(elem).getName();
  for (const mlir::SymbolTable::SymbolUse &use : *uses) {
    mlir::Operation *user = use.getUser();
    if (auto funcOp = mlir::dyn_cast<cir::FuncOp>(user)) {
      if (funcOp->hasAttr("cir.ifunc") && funcOp.getAliasee() &&
          *funcOp.getAliasee() == elemName) {
        ifuncUsers.push_back(user);
        continue;
      }
      return false;
    }

    if (auto aliasOp = mlir::dyn_cast<cir::AliasOp>(user)) {
      if (aliasOp.getIsIfunc() && aliasOp.getAliasee() == elemName) {
        ifuncUsers.push_back(user);
        continue;
      }
      return false;
    }

    return false;
  }

  if (ifuncUsers.empty())
    return false;

  auto aliaseeAttr = mlir::FlatSymbolRefAttr::get(
      &getMLIRContext(), mlir::cast<mlir::SymbolOpInterface>(cppFunc).getName());
  for (mlir::Operation *user : ifuncUsers)
    user->setAttr("aliasee", aliaseeAttr);

  if (lastGlobalOp == elem)
    lastGlobalOp = nullptr;
  eraseGlobalSymbol(elem);
  elem->erase();
  return true;
}

void CIRGenModule::emitStaticExternCAliases() {
  // OGCG checks shouldEmitStaticExternCAliases() here, which defaults to true
  // for all targets. Skip the check for now since all targets return true.
  for (auto &entry : staticExternCValues) {
    const IdentifierInfo *name = entry.first;
    llvm::StringRef mangledSymName = entry.second;

    // If mangledSymName is empty, that implies there were multiple declarations
    // that each had a claim to the unmangled name. In this case, generation of
    // the alias is suppressed.
    if (mangledSymName.empty())
      break;

    mlir::Operation *target = getGlobalValue(mangledSymName);
    assert(target && "expected target for static extern C alias");

    mlir::Operation *existingElem = getGlobalValue(name->getName());

    // If there is already something by this name, only proceed if it is the
    // synthetic declaration created to service extern "C" ifunc resolvers and
    // we can retarget those ifuncs to the actual C++ symbol.
    if (existingElem &&
        !checkAndReplaceExternCIFuncs(existingElem, target))
      continue;

    // Determine the type for the alias.
    mlir::Type aliasType;
    if (auto globalOp = mlir::dyn_cast<cir::GlobalOp>(target))
      aliasType = globalOp.getSymType();
    else if (auto funcOp = mlir::dyn_cast<cir::FuncOp>(target))
      aliasType = funcOp.getFunctionType();
    else
      llvm_unreachable("unexpected target type for static extern C alias");

    // Create the alias at module level.
    mlir::OpBuilder::InsertionGuard guard(builder);
    if (lastGlobalOp)
      builder.setInsertionPointAfter(lastGlobalOp);
    else
      builder.setInsertionPointToStart(getModule().getBody());

    auto aliaseeRef =
        mlir::FlatSymbolRefAttr::get(&getMLIRContext(), mangledSymName);
    auto typeAttr = mlir::TypeAttr::get(aliasType);
    auto linkageAttr = cir::GlobalLinkageKindAttr::get(
        &getMLIRContext(), cir::GlobalLinkageKind::InternalLinkage);
    auto nameAttr = getStringAttrCached(name->getName());

    auto aliasOp = cir::AliasOp::create(
        builder, target->getLoc(), nameAttr, aliaseeRef, typeAttr,
        linkageAttr, /*global_visibility=*/cir::VisibilityAttr{},
        /*dso_local=*/nullptr, /*weak=*/nullptr,
        /*is_ifunc=*/nullptr, /*tls_model=*/cir::TLS_ModelAttr{});
    insertGlobalSymbol(aliasOp);
    lastGlobalOp = aliasOp;

    addCompilerUsedGlobal(aliasOp);
  }
}

void CIRGenModule::checkAliases() {
  auto reportUndefinedIFunc =
      [&](const ValueDecl *decl, llvm::StringRef unresolvedName) {
        const IFuncAttr *ifa = decl->getAttr<IFuncAttr>();
        assert(ifa && "expected ifunc attr");
        getDiags().Report(ifa->getLocation(), diag::err_alias_to_undefined)
            << /*IsIFunc=*/1 << /*IsFunction=*/1;
        getDiags().Report(ifa->getLocation(), diag::note_alias_requires_mangled_name)
            << /*IsFunction=*/1 << /*IsIFunc=*/1;

        for (const auto &[candidateGD, candidateName] : mangledDeclNames) {
          const auto *nd = dyn_cast<NamedDecl>(candidateGD.getDecl());
          if (!nd)
            continue;
          IdentifierInfo *ii = nd->getIdentifier();
          if (!ii || ii->getName() != unresolvedName)
            continue;

          getDiags().Report(ifa->getLocation(),
                            diag::note_alias_mangled_name_alternative)
              << candidateName
              << FixItHint::CreateReplacement(
                     ifa->getRange(),
                     (Twine("ifunc(\"") + candidateName + "\")").str());
        }
      };

  auto stripInvalidIFunc = [&](mlir::Operation *op, GlobalDecl gd) {
    if (auto funcOp = mlir::dyn_cast<cir::FuncOp>(op)) {
      funcOp->removeAttr("cir.ifunc");
      funcOp->removeAttr("aliasee");
      return;
    }

    auto aliasOp = mlir::cast<cir::AliasOp>(op);
    auto aliasName = aliasOp.getSymName();
    auto funcType = mlir::cast<cir::FuncType>(aliasOp.getSymType());
    auto loc = aliasOp.getLoc();
    auto linkage = aliasOp.getLinkage();
    bool dsoLocal = aliasOp.getDsoLocal();

    if (lastGlobalOp == aliasOp)
      lastGlobalOp = nullptr;
    eraseGlobalSymbol(aliasOp);
    aliasOp.erase();

    auto *funcDecl = cast<FunctionDecl>(gd.getDecl());
    cir::FuncOp replacement = createCIRFunction(loc, aliasName, funcType, funcDecl);
    replacement.setLinkage(linkage);
    replacement.setDsoLocal(dsoLocal);
    setFunctionAttributes(gd, replacement, /*isIncompleteFunction=*/false,
                          /*isThunk=*/false);
  };

  llvm::SmallVector<std::pair<mlir::Operation *, GlobalDecl>, 4> invalidIFuncs;

  for (auto &op : getModule().getBody()->getOperations()) {
    std::optional<llvm::StringRef> resolverName = getIFuncResolverName(&op);
    if (!resolverName)
      continue;

    GlobalDecl gd;
    llvm::StringRef ifuncName;
    if (auto funcOp = mlir::dyn_cast<cir::FuncOp>(&op)) {
      gd = manglings.lookup(funcOp.getSymName());
      ifuncName = funcOp.getSymName();
    } else {
      auto aliasOp = mlir::cast<cir::AliasOp>(&op);
      gd = manglings.lookup(aliasOp.getSymName());
      ifuncName = aliasOp.getSymName();
    }

    const auto *decl = dyn_cast_or_null<ValueDecl>(gd.getDecl());
    if (!decl || !decl->hasAttr<IFuncAttr>())
      continue;

    const IFuncAttr *ifa = decl->getAttr<IFuncAttr>();
    if (*resolverName == ifuncName) {
      getDiags().Report(ifa->getLocation(), diag::err_cyclic_alias)
          << /*IsIFunc=*/1;
      invalidIFuncs.emplace_back(&op, gd);
      continue;
    }

    llvm::StringRef currentName = *resolverName;
    llvm::SmallDenseSet<llvm::StringRef, 4> visitedNames;
    mlir::Operation *resolved = nullptr;
    bool cyclic = false;

    while (true) {
      if (!visitedNames.insert(currentName).second) {
        cyclic = true;
        break;
      }

      resolved = getGlobalValue(currentName);
      if (!resolved)
        break;

      if (auto aliasOp = mlir::dyn_cast<cir::AliasOp>(resolved)) {
        currentName = aliasOp.getAliasee();
        continue;
      }

      if (auto funcOp = mlir::dyn_cast<cir::FuncOp>(resolved)) {
        if (auto aliasee = funcOp.getAliasee()) {
          currentName = *aliasee;
          continue;
        }
      }

      break;
    }

    if (cyclic) {
      getDiags().Report(ifa->getLocation(), diag::err_cyclic_alias)
          << /*IsIFunc=*/1;
      invalidIFuncs.emplace_back(&op, gd);
      continue;
    }

    auto resolverFunc = mlir::dyn_cast_or_null<cir::FuncOp>(resolved);
    if (!resolverFunc || resolverFunc.isDeclaration()) {
      reportUndefinedIFunc(decl, currentName);
      invalidIFuncs.emplace_back(&op, gd);
      continue;
    }

    if (!mlir::isa<cir::PointerType>(resolverFunc.getFunctionType().getReturnType())) {
      getDiags().Report(ifa->getLocation(), diag::err_ifunc_resolver_return);
      invalidIFuncs.emplace_back(&op, gd);
    }
  }

  for (const auto &[op, gd] : invalidIFuncs)
    stripInvalidIFunc(op, gd);
}

void CIRGenModule::handleCXXStaticMemberVarInstantiation(VarDecl *vd) {
  VarDecl::DefinitionKind dk = vd->isThisDeclarationADefinition();
  if (dk == VarDecl::Definition && vd->hasAttr<DLLImportAttr>())
    return;

  TemplateSpecializationKind tsk = vd->getTemplateSpecializationKind();
  // If we have a definition, this might be a deferred decl. If the
  // instantiation is explicit, make sure we emit it at the end.
  if (vd->getDefinition() && tsk == TSK_ExplicitInstantiationDefinition)
    eraseUnusedGlobalVarAddr(getAddrOfGlobalVar(vd));

  emitTopLevelDecl(vd);
}

void CIRGenModule::eraseUnusedGlobalVarAddr(mlir::Value addr) {
  if (!addr)
    return;

  mlir::Operation *addrOp = addr.getDefiningOp();
  if (!addrOp)
    return;

  assert(addrOp->use_empty() && "erasing a global address that is still used");

  llvm::SmallPtrSet<mlir::Operation *, 1> producers;
  for (mlir::Value operand : addrOp->getOperands())
    if (mlir::Operation *producer = operand.getDefiningOp())
      producers.insert(producer);

  addrOp->erase();
  for (mlir::Operation *producer : producers)
    if (producer->use_empty())
      producer->erase();
}

mlir::Operation *CIRGenModule::getGlobalValue(StringRef name) {
  auto it = symbolLookupCache.find(name);
  return it != symbolLookupCache.end() ? it->second : nullptr;
}

cir::GlobalOp
CIRGenModule::createGlobalOp(CIRGenModule &cgm, mlir::Location loc,
                             StringRef name, mlir::Type t, bool isConstant,
                             mlir::ptr::MemorySpaceAttrInterface addrSpace,
                             mlir::Operation *insertPoint) {
  cir::GlobalOp g;
  CIRGenBuilderTy &builder = cgm.getBuilder();

  {
    mlir::OpBuilder::InsertionGuard guard(builder);

    // If an insertion point is provided, we're replacing an existing global,
    // otherwise, create the new global immediately after the last gloabl we
    // emitted.
    if (insertPoint) {
      builder.setInsertionPoint(insertPoint);
    } else {
      // Group global operations together at the top of the module.
      if (cgm.lastGlobalOp) {
        builder.setInsertionPointAfter(cgm.lastGlobalOp);
      } else
        builder.setInsertionPointToStart(cgm.getModule().getBody());
    }

    g = cir::GlobalOp::create(builder, loc, name, t, isConstant);
    if (addrSpace)
      g.setAddrSpaceAttr(addrSpace);
    if (!insertPoint)
      cgm.lastGlobalOp = g;

    // Default to private until we can judge based on the initializer,
    // since MLIR doesn't allow public declarations.
    mlir::SymbolTable::setSymbolVisibility(
        g, mlir::SymbolTable::Visibility::Private);
  }
  cgm.symbolLookupCache[g.getSymNameAttr()] = g;
  return g;
}

Address CIRGenModule::createUnnamedGlobalFrom(const VarDecl &d,
                                              mlir::Attribute constant,
                                              CharUnits align) {
  // Helper to get function name for the global naming convention.
  auto functionName = [&](const DeclContext *dc) -> std::string {
    if (const auto *fd = dyn_cast<FunctionDecl>(dc)) {
      if (const auto *cc = dyn_cast<CXXConstructorDecl>(fd))
        return cc->getNameAsString();
      if (const auto *cd = dyn_cast<CXXDestructorDecl>(fd))
        return cd->getNameAsString();
      return std::string(getMangledName(fd));
    } else if (const auto *om = dyn_cast<ObjCMethodDecl>(dc)) {
      return om->getNameAsString();
    } else if (isa<BlockDecl>(dc)) {
      return "<block>";
    } else if (isa<CapturedDecl>(dc)) {
      return "<captured>";
    } else {
      llvm_unreachable("expected a function or method");
    }
  };

  // Form a simple per-variable cache of these values in case we find we
  // want to reuse them.
  cir::GlobalOp &cacheEntry = initializerConstants[&d];
  if (!cacheEntry || cacheEntry.getInitialValue() != constant) {
    auto ty = mlir::cast<mlir::TypedAttr>(constant).getType();
    bool isConstant = true;
    mlir::ptr::MemorySpaceAttrInterface addrSpace =
        cir::toCIRLangAddressSpaceAttr(&getMLIRContext(),
                                       getGlobalConstantAddressSpace());

    std::string name;
    if (d.hasGlobalStorage())
      name = getMangledName(&d).str() + ".const";
    else if (const DeclContext *dc = d.getParentFunctionOrMethod())
      name = ("__const." + functionName(dc) + "." + d.getName()).str();
    else
      llvm_unreachable("local variable has no parent function or method");

    cir::GlobalOp gv = builder.createVersionedGlobal(
        getModule(), getLoc(d.getLocation()), name, ty, isConstant,
        cir::GlobalLinkageKind::PrivateLinkage);
    insertGlobalSymbol(gv);
    if (addrSpace)
      gv.setAddrSpaceAttr(addrSpace);
    mlir::SymbolTable::setSymbolVisibility(
        gv, mlir::SymbolTable::Visibility::Private);
    gv.setInitialValueAttr(constant);
    gv.setAlignment(align.getAsAlign().value());
    // These are private constants whose address is not significant —
    // mark unnamed_addr to match OGCG.
    gv->setAttr("unnamed_addr", mlir::UnitAttr::get(&getMLIRContext()));

    cacheEntry = gv;
  } else if (cacheEntry.getAlignment() < align.getQuantity()) {
    cacheEntry.setAlignment(align.getAsAlign().value());
  }

  // Create a GetGlobalOp to get a pointer to the global.
  mlir::Type eltTy = mlir::cast<mlir::TypedAttr>(constant).getType();
  auto ptrTy = builder.getPointerTo(cacheEntry.getSymType(),
                                    cacheEntry.getAddrSpaceAttr());
  mlir::Value globalPtr = cir::GetGlobalOp::create(
      builder, getLoc(d.getLocation()), ptrTy, cacheEntry.getSymName());
  return Address(globalPtr, eltTy, align);
}

void CIRGenModule::setCommonAttributes(GlobalDecl gd, mlir::Operation *gv) {
  const Decl *d = gd.getDecl();
  if (isa_and_nonnull<NamedDecl>(d))
    setGVProperties(gv, dyn_cast<NamedDecl>(d));

  if (d && d->hasAttr<UsedAttr>())
    addUsedOrCompilerUsedGlobal(gv);

  if (const auto *vd = dyn_cast_if_present<VarDecl>(d);
      vd &&
      ((codeGenOpts.KeepPersistentStorageVariables &&
        (vd->getStorageDuration() == SD_Static ||
         vd->getStorageDuration() == SD_Thread)) ||
       (codeGenOpts.KeepStaticConsts && vd->getStorageDuration() == SD_Static &&
        vd->getType().isConstQualified())))
    addUsedOrCompilerUsedGlobal(gv);
}

static void
setPragmaClangSectionAttrs(const Decl *d, cir::GlobalOp globalOp,
                           CIRGenModule &cgm) {
  if (!d)
    return;

  if (const auto *sa = d->getAttr<PragmaClangBSSSectionAttr>())
    globalOp->setAttr("cir.pragma_clang_bss_section",
                      cgm.getStringAttrCached(sa->getName()));
  if (const auto *sa = d->getAttr<PragmaClangDataSectionAttr>())
    globalOp->setAttr("cir.pragma_clang_data_section",
                      cgm.getStringAttrCached(sa->getName()));
  if (const auto *sa = d->getAttr<PragmaClangRodataSectionAttr>())
    globalOp->setAttr("cir.pragma_clang_rodata_section",
                      cgm.getStringAttrCached(sa->getName()));
  if (const auto *sa = d->getAttr<PragmaClangRelroSectionAttr>())
    globalOp->setAttr("cir.pragma_clang_relro_section",
                      cgm.getStringAttrCached(sa->getName()));
}

void CIRGenModule::setNonAliasAttributes(GlobalDecl gd, mlir::Operation *op) {
  setCommonAttributes(gd, op);

  // If the declaration has __attribute__((retain)), add it to @llvm.used
  // to prevent the linker from discarding it.
  if (const Decl *retainDecl = gd.getDecl()) {
    if (retainDecl->hasAttr<RetainAttr>())
      addUsedGlobal(op);
  }

  // Set section attribute if the declaration has one.
  const Decl *d = gd.getDecl();
  if (auto globalOp = mlir::dyn_cast<cir::GlobalOp>(op)) {
    if (const auto *sa = d->getAttr<SectionAttr>())
      globalOp.setSectionAttr(getStringAttrCached(sa->getName()));
    // Handle per-variable code_model attribute (OGCG CodeGenModule.cpp:5594-5596).
    if (const auto *cma = d->getAttr<CodeModelAttr>())
      globalOp->setAttr("cir.code_model",
                        builder.getI32IntegerAttr(
                            static_cast<int>(cma->getModel())));
    setPragmaClangSectionAttrs(d, globalOp, *this);
  }
  if (auto funcOp = mlir::dyn_cast<cir::FuncOp>(op)) {
    if (const auto *sa = d->getAttr<SectionAttr>())
      funcOp.setSectionAttr(getStringAttrCached(sa->getName()));
    else if (const auto *sa = d->getAttr<PragmaClangTextSectionAttr>())
      funcOp.setSectionAttr(getStringAttrCached(sa->getName()));
  }
  // OGCG handles CodeSegAttr before falling back to SectionAttr for both
  // functions and globals. CodeSegAttr is Windows PE/COFF only.

  getTargetCIRGenInfo().setTargetAttributes(d, op, *this);
}

std::optional<cir::SourceLanguage> CIRGenModule::getCIRSourceLanguage() const {
  using ClangStd = clang::LangStandard;
  using CIRLang = cir::SourceLanguage;
  auto opts = getLangOpts();

  if (opts.OpenCL && !opts.OpenCLCPlusPlus)
    return CIRLang::OpenCLC;
  if (opts.CPlusPlus)
    return CIRLang::CXX;
  if (opts.C99 || opts.C11 || opts.C17 || opts.C23 || opts.C2y ||
      opts.LangStd == ClangStd::lang_c89 ||
      opts.LangStd == ClangStd::lang_gnu89)
    return CIRLang::C;

  // TODO(cir): support remaining source languages.
  errorNYI("CIR does not yet support the given source language");
  return {};
}

static void setLinkageForGV(cir::GlobalOp &gv, const NamedDecl *nd) {
  // Set linkage and visibility in case we never see a definition.
  LinkageInfo lv = nd->getLinkageAndVisibility();
  // Don't set internal linkage on declarations.
  // "extern_weak" is overloaded in LLVM; we probably should have
  // separate linkage types for this.
  if (isExternallyVisible(lv.getLinkage()) &&
      (nd->hasAttr<WeakAttr>() || nd->isWeakImported()))
    gv.setLinkage(cir::GlobalLinkageKind::ExternalWeakLinkage);
}

using GlobalOp = cir::GlobalOp;
using GlobalViewAttr = cir::GlobalViewAttr;

static llvm::SmallVector<int64_t> indexesOfArrayAttr(mlir::ArrayAttr indexes) {
  llvm::SmallVector<int64_t> inds;
  for (mlir::Attribute i : indexes) {
    auto ind = dyn_cast<mlir::IntegerAttr>(i);
    assert(ind && "expect MLIR integer attribute");
    inds.push_back(ind.getValue().getSExtValue());
  }
  return inds;
}

static bool isViewOnGlobal(GlobalOp glob, GlobalViewAttr view) {
  return view.getSymbol().getValue() == glob.getSymName();
}

static GlobalViewAttr createNewGlobalView(CIRGenModule &cgm, GlobalOp newGlob,
                                          GlobalViewAttr attr,
                                          mlir::Type oldTy) {
  if (!attr.getIndices() || !isViewOnGlobal(newGlob, attr))
    return attr;

  llvm::SmallVector<int64_t> oldInds = indexesOfArrayAttr(attr.getIndices());
  llvm::SmallVector<int64_t> newInds;
  CIRGenBuilderTy &bld = cgm.getBuilder();
  const cir::CIRDataLayout &layout = cgm.getDataLayout();
  auto newTy = newGlob.getSymType();

  auto offset = bld.computeOffsetFromGlobalViewIndices(layout, oldTy, oldInds);
  if (!bld.computeGlobalViewIndicesFromFlatOffset(offset, newTy, layout,
                                                   newInds)) {
    // Decomposition failed — use raw byte offset fallback.
    mlir::IntegerAttr rawOffset = cgm.getI64AttrCached(offset);
    mlir::Type viewTy = attr.getType();
    auto symbol = mlir::FlatSymbolRefAttr::get(newGlob.getSymNameAttr());
    return cir::GlobalViewAttr::get(viewTy, symbol,
                                    /*indices=*/{}, rawOffset);
  }
  // The view's pointee type is the type of the element reached by the index
  // chain, which is invariant under the referenced global's re-layout: padding
  // insertion (e.g. an anonymous-struct/tentative-definition completion in C)
  // only shifts member indices, not member types. Preserve the original view's
  // pointee type. Deriving it from the whole new global type
  // (cir::PointerType::get(newTy)) mistypes an indexed member view as a pointer
  // to the entire record, which breaks CIR verification at every use site.
  mlir::Type viewTy = attr.getType();

  llvm::SmallVector<mlir::Attribute> idxAttrs;
  for (int64_t i : newInds)
    idxAttrs.push_back(bld.getI32IntegerAttr(i));
  mlir::ArrayAttr idxArr =
      idxAttrs.empty() ? mlir::ArrayAttr{} : bld.getArrayAttr(idxAttrs);
  auto symbol = mlir::FlatSymbolRefAttr::get(newGlob.getSymNameAttr());
  return cir::GlobalViewAttr::get(viewTy, symbol, idxArr,
                                  /*rawByteOffset=*/{},
                                  /*inRange=*/{}, attr.getInbounds(),
                                  /*addrAdjustment=*/{});
}

static mlir::Attribute getNewInitValue(CIRGenModule &cgm, GlobalOp newGlob,
                                       mlir::Type oldTy, GlobalOp user,
                                       mlir::Attribute oldInit) {
  if (auto oldView = mlir::dyn_cast<GlobalViewAttr>(oldInit))
    return createNewGlobalView(cgm, newGlob, oldView, oldTy);

  auto getNewInitElements =
      [&](mlir::ArrayAttr oldElements) -> mlir::ArrayAttr {
    llvm::SmallVector<mlir::Attribute> newElements;
    for (auto elt : oldElements) {
      if (auto view = mlir::dyn_cast<GlobalViewAttr>(elt))
        newElements.push_back(createNewGlobalView(cgm, newGlob, view, oldTy));
      else if (mlir::isa<cir::ConstArrayAttr, cir::ConstRecordAttr>(elt))
        newElements.push_back(getNewInitValue(cgm, newGlob, oldTy, user, elt));
      else
        newElements.push_back(elt);
    }
    return mlir::ArrayAttr::get(cgm.getBuilder().getContext(), newElements);
  };

  if (auto oldArray = mlir::dyn_cast<cir::ConstArrayAttr>(oldInit)) {
    auto oldElts = mlir::dyn_cast<mlir::ArrayAttr>(oldArray.getElts());
    // If the elements are not an ArrayAttr (e.g. StringAttr for string
    // literals, or other non-array storage), there are no GlobalViewAttr
    // references to update. Return the attribute unchanged.
    if (!oldElts)
      return oldInit;
    mlir::Attribute newElements = getNewInitElements(oldElts);
    return cgm.getBuilder().getConstArray(
        newElements, mlir::cast<cir::ArrayType>(oldArray.getType()));
  }
  if (auto oldRecord = mlir::dyn_cast<cir::ConstRecordAttr>(oldInit)) {
    mlir::ArrayAttr newMembers = getNewInitElements(oldRecord.getMembers());
    auto recordTy = mlir::cast<cir::RecordType>(oldRecord.getType());
    return cgm.getBuilder().getConstRecordOrZeroAttr(
        newMembers, recordTy.getPacked(), recordTy.getPadded(), recordTy);
  }

  cgm.errorNYI("getNewInitValue: unsupported init attribute type");
  return {};
}

/// If the specified mangled name is not in the module,
/// create and return an mlir GlobalOp with the specified type (TODO(cir):
/// address space).
///
/// TODO(cir):
/// 1. If there is something in the module with the specified name, return
/// it potentially bitcasted to the right type.
///
/// 2. If \p d is non-null, it specifies a decl that correspond to this.  This
/// is used to set the attributes on the global when it is first created.
///
/// 3. If \p isForDefinition is true, it is guaranteed that an actual global
/// with type \p ty will be returned, not conversion of a variable with the same
/// mangled name but some other type.
cir::GlobalOp
CIRGenModule::getOrCreateCIRGlobal(StringRef mangledName, mlir::Type ty,
                                   LangAS langAS, const VarDecl *d,
                                   ForDefinition_t isForDefinition) {
  auto attachSourceVarDeclAttr = [&](cir::GlobalOp globalOp) {
    if (!d || !getCodeGenOpts().hasReducedDebugInfo())
      return;

    const VarDecl *sourceDecl = d;
    if (const auto *definition = d->getDefinition())
      sourceDecl = definition;

    if (auto existingAttr =
            globalOp->getAttrOfType<cir::ASTVarDeclAttr>("cir.debug.var_decl");
        existingAttr && existingAttr.getAst() == sourceDecl)
      return;

    globalOp->setAttr("cir.debug.var_decl",
                      cir::ASTVarDeclAttr::get(&getMLIRContext(), sourceDecl));
  };

  auto attachOpenMPDeclareTargetAttr = [&](cir::GlobalOp globalOp) {
    if (!d)
      return;
    if (std::optional<mlir::omp::DeclareTargetAttr> declareTargetAttr =
            getOpenMPDeclareTargetAttr(getMLIRContext(), d))
      globalOp->setAttr("omp.declare_target", *declareTargetAttr);
  };

  // Lookup the entry, lazily creating it if necessary.
  cir::GlobalOp entry;
  if (mlir::Operation *v = getGlobalValue(mangledName)) {
    entry = dyn_cast<cir::GlobalOp>(v);
    if (!entry) {
      // The existing symbol is not a GlobalOp (e.g., it's a FuncOp from a
      // function declaration that shares an asm label with this variable).
      // This happens when a function and a variable share the same asm-level
      // name, e.g.:
      //   void foo3() __asm__("var");
      //   int foo4 __asm__("var") = 4;
      // OGCG handles this transparently because llvm::GlobalVariable isa
      // llvm::GlobalValue and opaque pointers allow coexistence. In CIR,
      // rename the old FuncOp and update all existing references, then let
      // the GlobalOp take the original name. This mirrors the reverse case
      // handled in getOrCreateCIRFunction.
      if (auto funcOp = mlir::dyn_cast<cir::FuncOp>(v)) {
        std::string renamedName = (mangledName + ".renamed").str();
        unsigned renamedSuffix = 0;
        while (auto *existing = getGlobalValue(renamedName)) {
          if (existing == funcOp)
            break;
          renamedName =
              (mangledName + ".renamed." + llvm::Twine(++renamedSuffix)).str();
        }

        // Update all call-site and get_global references to the old name.
        auto oldNameAttr = funcOp.getSymNameAttr();
        auto newCalleeRef =
            mlir::FlatSymbolRefAttr::get(&getMLIRContext(), renamedName);
        getModule().walk([&](mlir::Operation *op) {
          if (auto callOp = mlir::dyn_cast<cir::CallOp>(op)) {
            if (auto callee = callOp.getCalleeAttr())
              if (callee.getAttr() == oldNameAttr)
                callOp.setCalleeAttr(newCalleeRef);
          } else if (auto tryCallOp = mlir::dyn_cast<cir::TryCallOp>(op)) {
            if (auto callee = tryCallOp.getCalleeAttr())
              if (callee.getAttr() == oldNameAttr)
                tryCallOp.setCalleeAttr(newCalleeRef);
          } else if (auto ggo = mlir::dyn_cast<cir::GetGlobalOp>(op)) {
            if (ggo.getNameAttr().getAttr() == oldNameAttr)
              ggo.setName(renamedName);
          }
        });

        // Rename the FuncOp and re-register in the symbol table cache.
        // Store the original name so LowerToLLVM can map it back.
        funcOp->setAttr("cir.asm_label_original_name",
                        mlir::StringAttr::get(&getMLIRContext(), mangledName));
        eraseGlobalSymbol(funcOp);
        funcOp.setSymName(renamedName);
        insertGlobalSymbol(funcOp);
      } else {
        // Unknown op type — erase and let the GlobalOp take its place.
        if (lastGlobalOp == v)
          lastGlobalOp = nullptr;
        eraseGlobalSymbol(v);
        v->erase();
      }
    }
  }

  if (entry) {
    // If this entry was created as extern_weak due to a weakref, reset its
    // linkage if the actual declaration is not weak. Matches OGCG's
    // WeakRefReferences.erase(Entry) logic in GetOrCreateLLVMGlobal.
    if (weakRefReferences.erase(entry.getOperation())) {
      if (d && !d->hasAttr<WeakAttr>())
        entry.setLinkage(cir::GlobalLinkageKind::ExternalLinkage);
    }

    attachSourceVarDeclAttr(entry);
    attachOpenMPDeclareTargetAttr(entry);
    if (entry.getSymType() == ty)
      return entry;

    // If there are two attempts to define the same mangled name, issue an
    // error. OGCG checks lookupRepresentativeDecl and issues
    // diag::err_duplicate_mangled_name but continues execution — we match
    // that by falling through to create a new global with the correct type.
    // The symbol replacement logic below handles updating references.
    //
    // TODO(cir): look at mlir::GlobalValue::isDeclaration for all aspects of
    // recognizing the global as a declaration, for now only check if
    // initializer is present.
    if (isForDefinition && !entry.isDeclaration()) {
      // Fall through — the new global will replace the old one.
    }

    // Address space check removed because it is unnecessary because CIR records
    // address space info in types.

    // (If global is requested for a definition, we always need to create a new
    // global, not just return a bitcast.)
    if (!isForDefinition)
      return entry;
  }

  mlir::Location loc =
      d ? getLoc(d->getSourceRange()) : builder.getUnknownLoc();

  // Calculate constant storage flag before creating the global. OGCG uses
  // (false, false) at creation time — ctors/dtors are not excluded yet because
  // we don't know at creation whether NeedsGlobalCtor/Dtor will be set. The
  // definitive constant check with (true, true) happens in
  // emitGlobalVarDefinition.
  bool isConstant = false;
  if (d) {
    isConstant = d->getType().isConstantStorage(
        astContext, /*ExcludeCtor=*/false, /*ExcludeDtor=*/false);
  }

  // Compute the CIR address space from the language address space.
  mlir::ptr::MemorySpaceAttrInterface declCIRAS =
      cir::toCIRLangAddressSpaceAttr(&getMLIRContext(), langAS);

  // mlir::SymbolTable::Visibility::Public is the default, no need to explicitly
  // mark it as such.
  cir::GlobalOp gv =
      CIRGenModule::createGlobalOp(*this, loc, mangledName, ty, isConstant,
                                   /*addrSpace=*/declCIRAS,
                                   /*insertPoint=*/entry.getOperation());
  attachSourceVarDeclAttr(gv);
  attachOpenMPDeclareTargetAttr(gv);

  // If we created a new global to replace an existing one (e.g., when a
  // tentative definition is completed with a different type), we need to
  // update all uses of the old global and erase it.
  if (entry) {
    assert(entry.getSymName() == gv.getSymName() && "symbol names must match");
    // If types match, the old entry should have been returned earlier.
    // If we're here with an entry, types must differ.
    auto oldTy = entry.getSymType();
    auto newTy = gv.getSymType();
    if (oldTy != newTy) {
      llvm::SmallVector<cir::GetGlobalOp, 8> getGlobalUsers;
      llvm::SmallVector<cir::GlobalOp, 4> globalUsers;
      llvm::SmallVector<cir::ConstantOp, 4> constantUsers;
      auto targetNameAttr = entry.getSymNameAttr();
      llvm::DenseMap<mlir::Attribute, bool> attrHasTargetRefCache;
      auto hasTargetGlobalViewRef = [&](mlir::Attribute attr) {
        if (auto it = attrHasTargetRefCache.find(attr);
            it != attrHasTargetRefCache.end())
          return it->second;
        bool hasRef = attr.walk([&](mlir::Attribute nested) {
                        if (auto gv = mlir::dyn_cast<cir::GlobalViewAttr>(nested);
                            gv && gv.getSymbol().getRootReference() == targetNameAttr)
                          return mlir::WalkResult::interrupt();
                        return mlir::WalkResult::advance();
                      }).wasInterrupted();
        attrHasTargetRefCache.try_emplace(attr, hasRef);
        return hasRef;
      };

      // Avoid getSymbolUses() which walks all attributes of every operation
      // in the module — O(N) per call with expensive DenseMap allocation churn.
      // Use a direct walk checking specific op types instead.
      {
        // Function-local users dominate in normal codegen paths. Scan function
        // bodies directly instead of walking every operation in the module.
        for (auto fn : theModule.getOps<cir::FuncOp>()) {
          // Declaration-only functions have no body operations that can
          // reference globals through cir.get_global or constant payloads.
          // Skip them to reduce full-module user scan overhead in
          // declaration-heavy translation units.
          if (fn.isDeclaration())
            continue;

          fn.walk([&](mlir::Operation *op) {
            if (auto ggo = dyn_cast<cir::GetGlobalOp>(op)) {
              if (ggo.getNameAttr().getAttr() == targetNameAttr)
                getGlobalUsers.push_back(ggo);
            } else if (auto c = dyn_cast<cir::ConstantOp>(op)) {
              if (hasTargetGlobalViewRef(c.getValue()))
                constantUsers.push_back(c);
            }
          });
        }

        // Top-level users: global initializers and non-function regions.
        for (mlir::Operation &op : theModule.getBody()->getOperations()) {
          if (isa<cir::FuncOp>(op))
            continue;

          op.walk([&](mlir::Operation *nestedOp) {
            if (auto ggo = dyn_cast<cir::GetGlobalOp>(nestedOp)) {
              if (ggo.getNameAttr().getAttr() == targetNameAttr)
                getGlobalUsers.push_back(ggo);
            } else if (auto c = dyn_cast<cir::ConstantOp>(nestedOp)) {
              if (hasTargetGlobalViewRef(c.getValue()))
                constantUsers.push_back(c);
            }
          });

          if (auto glob = dyn_cast<cir::GlobalOp>(&op)) {
            if (glob == entry)
              continue;
            if (auto init = glob.getInitialValue();
                init && hasTargetGlobalViewRef(init.value()))
              globalUsers.push_back(glob);
          }
        }
      }

      for (auto ggo : getGlobalUsers) {
        auto useOpResultValue = ggo.getAddr();
        // The existing users were built against the placeholder's pointer type,
        // which may use a different address space than the completed definition
        // (e.g. an extern array placeholder in the global address space later
        // defined in the constant address space).
        auto origPtrTy =
            mlir::cast<cir::PointerType>(useOpResultValue.getType());
        // The get_global now refers to gv, so its result must match gv's
        // address space.
        useOpResultValue.setType(
            cir::PointerType::get(newTy, gv.getAddrSpaceAttr()));

        mlir::OpBuilder::InsertionGuard guard(builder);
        builder.setInsertionPointAfter(ggo);
        mlir::Type ptrTy = builder.getPointerTo(oldTy, gv.getAddrSpaceAttr());
        mlir::Value cast =
            builder.createBitcast(ggo->getLoc(), useOpResultValue, ptrTy);
        mlir::Operation *bitcastOp = cast.getDefiningOp();
        // If the definition landed in a different address space than the
        // existing users expect, address-space cast back so those users remain
        // valid (a bitcast cannot cross address spaces).
        if (origPtrTy.getAddrSpace() != gv.getAddrSpaceAttr())
          cast = builder.createAddrSpaceCast(ggo->getLoc(), cast, origPtrTy);
        useOpResultValue.replaceAllUsesExcept(cast, bitcastOp);
      }

      for (auto glob : globalUsers) {
        if (auto init = glob.getInitialValue()) {
          auto nw = getNewInitValue(*this, gv, oldTy, glob, init.value());
          glob.setInitialValueAttr(nw);
        }
      }

      for (auto c : constantUsers) {
        mlir::Attribute init = getNewInitValue(*this, gv, oldTy, gv, c.getValue());
        auto typedAttr = mlir::cast<mlir::TypedAttr>(init);
        mlir::OpBuilder::InsertionGuard guard(builder);
        builder.setInsertionPointAfter(c);
        auto newUser = cir::ConstantOp::create(builder, c.getLoc(), typedAttr);
        c.replaceAllUsesWith(newUser.getOperation());
        c.erase();
      }
    }
    // Remove old global from the module. If lastGlobalOp was pointing to the
    // old entry, update it to the replacement to avoid a dangling reference.
    if (lastGlobalOp == entry)
      lastGlobalOp = gv;
    entry.erase();
  }

  // This is the first use or definition of a mangled name.  If there is a
  // deferred decl with this name, remember that we need to emit it at the end
  // of the file.
  auto ddi = deferredDecls.find(mangledName);
  if (ddi != deferredDecls.end()) {
    // Move the potentially referenced deferred decl to the DeferredDeclsToEmit
    // list, and remove it from DeferredDecls (since we don't need it anymore).
    addDeferredDeclToEmit(ddi->second);
    deferredDecls.erase(ddi);
  }

  // Handle things which are present even on external declarations.
  if (d) {
    maybeRecordAddressSanitizerGlobalMetadata(d, /*isDynInit=*/false, gv);
    maybeRecordTypeSanGlobalMetadata(d, gv);
    if (theModule->hasAttr("cir.debug_lang")) {
      if (auto btfTags = collectCIRBTFDeclTagAttrs(getMLIRContext(), d);
          !btfTags.empty())
        gv->setAttr("cir.debug.btf_decl_tag", builder.getArrayAttr(btfTags));

      // _Atomic strips to its value type in CIR, so peel it here and emit a
      // qual_atomic marker; the lowering rebuilds the DW_TAG_atomic_type wrapper.
      QualType globalDebugTy = d->getType();
      bool globalDebugAtomic = false;
      if (const auto *at = globalDebugTy->getAs<AtomicType>()) {
        globalDebugAtomic = true;
        globalDebugTy = at->getValueType();
      }
      if (auto builtinDbg =
              getBuiltinDebugTypeInfoFromQualType(astContext, globalDebugTy)) {
        gv->setAttr("cir.debug.builtin_name",
                    getStringAttrCached(builtinDbg->name));
        gv->setAttr("cir.debug.builtin_encoding",
                    builder.getI32IntegerAttr(builtinDbg->encoding));
        gv->setAttr("cir.debug.builtin_size",
                    builder.getI64IntegerAttr(builtinDbg->sizeInBits));
        if (builtinDbg->isConstQualified)
          gv->setAttr("cir.debug.qual_const",
                      mlir::UnitAttr::get(&getMLIRContext()));
        if (builtinDbg->isVolatileQualified)
          gv->setAttr("cir.debug.qual_volatile",
                      mlir::UnitAttr::get(&getMLIRContext()));
        if (builtinDbg->isRestrictQualified)
          gv->setAttr("cir.debug.qual_restrict",
                      mlir::UnitAttr::get(&getMLIRContext()));
        if (globalDebugAtomic)
          gv->setAttr("cir.debug.qual_atomic",
                      mlir::UnitAttr::get(&getMLIRContext()));
      }

      if (auto dbg =
              getTypedefDebugInfoFromQualType(astContext, d->getType())) {
        gv->setAttr("cir.debug.typedef_name", getStringAttrCached(dbg->name));
        if (!dbg->btfDeclTags.empty()) {
          llvm::SmallVector<mlir::Attribute> typedefTags;
          typedefTags.reserve(dbg->btfDeclTags.size());
          for (llvm::StringRef tag : dbg->btfDeclTags)
            typedefTags.push_back(getStringAttrCached(tag));
          gv->setAttr("cir.debug.typedef_btf_decl_tag",
                      builder.getArrayAttr(typedefTags));
        }
        if (dbg->isPointerPointee)
          gv->setAttr("cir.debug.typedef_is_pointer_pointee",
                      mlir::UnitAttr::get(&getMLIRContext()));
        if (dbg->isArrayElementTypedef)
          gv->setAttr("cir.debug.typedef_is_array_element",
                      mlir::UnitAttr::get(&getMLIRContext()));
        if (dbg->hasNullBaseType)
          gv->setAttr("cir.debug.typedef_null_base",
                      mlir::UnitAttr::get(&getMLIRContext()));
        if (dbg->isConstQualified)
          gv->setAttr("cir.debug.typedef_const",
                      mlir::UnitAttr::get(&getMLIRContext()));
        if (dbg->isVolatileQualified)
          gv->setAttr("cir.debug.typedef_volatile",
                      mlir::UnitAttr::get(&getMLIRContext()));
        if (dbg->isRestrictQualified)
          gv->setAttr("cir.debug.typedef_restrict",
                      mlir::UnitAttr::get(&getMLIRContext()));
        if (dbg->pointeeConstQualified)
          gv->setAttr("cir.debug.typedef_pointee_const",
                      mlir::UnitAttr::get(&getMLIRContext()));
        if (dbg->pointeeVolatileQualified)
          gv->setAttr("cir.debug.typedef_pointee_volatile",
                      mlir::UnitAttr::get(&getMLIRContext()));
        if (dbg->pointeeRestrictQualified)
          gv->setAttr("cir.debug.typedef_pointee_restrict",
                      mlir::UnitAttr::get(&getMLIRContext()));
        if (dbg->underlyingIsPointer || (!dbg->isPointerPointee &&
                                         d->getType()->isPointerType())) {
          gv->setAttr("cir.debug.typedef_underlying_pointer",
                      mlir::UnitAttr::get(&getMLIRContext()));
          if (const auto *ptrTy = d->getType()->getAs<PointerType>()) {
            QualType pointee = ptrTy->getPointeeType();
            if (pointee.isConstQualified())
              gv->setAttr("cir.debug.typedef_pointee_const",
                          mlir::UnitAttr::get(&getMLIRContext()));
            if (pointee.isVolatileQualified())
              gv->setAttr("cir.debug.typedef_pointee_volatile",
                          mlir::UnitAttr::get(&getMLIRContext()));
            if (pointee.isRestrictQualified())
              gv->setAttr("cir.debug.typedef_pointee_restrict",
                          mlir::UnitAttr::get(&getMLIRContext()));

            QualType unqualPointee = pointee.getUnqualifiedType();
            if (const auto *builtin = unqualPointee->getAs<BuiltinType>()) {
              llvm::StringRef pointeeName;
              unsigned pointeeEncoding = 0;
              if (builtin->getKind() == BuiltinType::Bool) {
                pointeeName = "bool";
                pointeeEncoding = llvm::dwarf::DW_ATE_boolean;
              } else if (builtin->getKind() == BuiltinType::Char_U) {
                // Plain `char` (unsigned platform): "char", not "unsigned char".
                pointeeName = "char";
                pointeeEncoding = llvm::dwarf::DW_ATE_unsigned_char;
              } else if (builtin->getKind() == BuiltinType::UChar) {
                pointeeName = "unsigned char";
                pointeeEncoding = llvm::dwarf::DW_ATE_unsigned_char;
              } else if (builtin->getKind() == BuiltinType::Char_S) {
                // Plain `char` (signed platform): "char", not "signed char".
                pointeeName = "char";
                pointeeEncoding = llvm::dwarf::DW_ATE_signed_char;
              } else if (builtin->getKind() == BuiltinType::SChar) {
                pointeeName = "signed char";
                pointeeEncoding = llvm::dwarf::DW_ATE_signed_char;
              } else if (builtin->isUnsignedInteger()) {
                pointeeName = "unsigned int";
                pointeeEncoding = llvm::dwarf::DW_ATE_unsigned;
              } else if (builtin->isInteger()) {
                pointeeName = "int";
                pointeeEncoding = llvm::dwarf::DW_ATE_signed;
              }
              if (!pointeeName.empty()) {
                gv->setAttr("cir.debug.typedef_pointee_name",
                            getStringAttrCached(pointeeName));
                gv->setAttr("cir.debug.typedef_pointee_bits",
                            builder.getI32IntegerAttr(
                                astContext.getTypeSize(unqualPointee)));
                gv->setAttr("cir.debug.typedef_pointee_encoding",
                            builder.getI32IntegerAttr(pointeeEncoding));
              }
            }

            if (!dbg->pointeeTypedefName.empty())
              gv->setAttr("cir.debug.typedef_pointee_typedef_name",
                          getStringAttrCached(dbg->pointeeTypedefName));
            if (!dbg->pointeeTypedefFilePath.empty())
              gv->setAttr("cir.debug.typedef_pointee_typedef_file",
                          getStringAttrCached(dbg->pointeeTypedefFilePath));
            if (dbg->pointeeTypedefLine)
              gv->setAttr("cir.debug.typedef_pointee_typedef_line",
                          builder.getI32IntegerAttr(dbg->pointeeTypedefLine));
            if (dbg->pointeeTypedefUnderlyingIsVector)
              gv->setAttr("cir.debug.typedef_pointee_typedef_underlying_vector",
                          mlir::UnitAttr::get(&getMLIRContext()));
            if (dbg->pointeeTypedefVectorElementCount)
              gv->setAttr("cir.debug.typedef_pointee_typedef_vector_count",
                          builder.getI32IntegerAttr(
                              dbg->pointeeTypedefVectorElementCount));
            if (!dbg->pointeeTypedefElementName.empty())
              gv->setAttr("cir.debug.typedef_pointee_typedef_elem_name",
                          getStringAttrCached(dbg->pointeeTypedefElementName));
            if (dbg->pointeeTypedefElementSizeInBits)
              gv->setAttr("cir.debug.typedef_pointee_typedef_elem_bits",
                          builder.getI32IntegerAttr(
                              dbg->pointeeTypedefElementSizeInBits));
            if (dbg->pointeeTypedefElementEncoding)
              gv->setAttr("cir.debug.typedef_pointee_typedef_elem_encoding",
                          builder.getI32IntegerAttr(
                              dbg->pointeeTypedefElementEncoding));
          }
        } else {
          if (dbg->underlyingIsVector) {
            gv->setAttr("cir.debug.typedef_underlying_vector",
                        mlir::UnitAttr::get(&getMLIRContext()));
            if (dbg->vectorElementCount)
              gv->setAttr("cir.debug.typedef_vector_count",
                          builder.getI32IntegerAttr(dbg->vectorElementCount));
          }
          if (!dbg->pointeeName.empty())
            gv->setAttr("cir.debug.typedef_pointee_name",
                        getStringAttrCached(dbg->pointeeName));
          if (dbg->pointeeSizeInBits)
            gv->setAttr("cir.debug.typedef_pointee_bits",
                        builder.getI32IntegerAttr(dbg->pointeeSizeInBits));
          if (dbg->pointeeEncoding)
            gv->setAttr("cir.debug.typedef_pointee_encoding",
                        builder.getI32IntegerAttr(dbg->pointeeEncoding));
          if (!dbg->pointeeTypedefName.empty())
            gv->setAttr("cir.debug.typedef_pointee_typedef_name",
                        getStringAttrCached(dbg->pointeeTypedefName));
          if (!dbg->pointeeTypedefFilePath.empty())
            gv->setAttr("cir.debug.typedef_pointee_typedef_file",
                        getStringAttrCached(dbg->pointeeTypedefFilePath));
          if (dbg->pointeeTypedefLine)
            gv->setAttr("cir.debug.typedef_pointee_typedef_line",
                        builder.getI32IntegerAttr(dbg->pointeeTypedefLine));
          if (dbg->pointeeTypedefUnderlyingIsVector)
            gv->setAttr("cir.debug.typedef_pointee_typedef_underlying_vector",
                        mlir::UnitAttr::get(&getMLIRContext()));
          if (dbg->pointeeTypedefVectorElementCount)
            gv->setAttr("cir.debug.typedef_pointee_typedef_vector_count",
                        builder.getI32IntegerAttr(
                            dbg->pointeeTypedefVectorElementCount));
          if (!dbg->pointeeTypedefElementName.empty())
            gv->setAttr("cir.debug.typedef_pointee_typedef_elem_name",
                        getStringAttrCached(dbg->pointeeTypedefElementName));
          if (dbg->pointeeTypedefElementSizeInBits)
            gv->setAttr("cir.debug.typedef_pointee_typedef_elem_bits",
                        builder.getI32IntegerAttr(
                            dbg->pointeeTypedefElementSizeInBits));
          if (dbg->pointeeTypedefElementEncoding)
            gv->setAttr("cir.debug.typedef_pointee_typedef_elem_encoding",
                        builder.getI32IntegerAttr(
                            dbg->pointeeTypedefElementEncoding));
        }
        if (!dbg->filePath.empty())
          gv->setAttr("cir.debug.typedef_file",
                      getStringAttrCached(dbg->filePath));
        if (dbg->line)
          gv->setAttr("cir.debug.typedef_line",
                      builder.getI32IntegerAttr(dbg->line));
        if (!dbg->underlyingArrayElementTypedefName.empty())
          gv->setAttr("cir.debug.typedef_underlying_array_elem_typedef_name",
                      getStringAttrCached(
                          dbg->underlyingArrayElementTypedefName));
        if (!dbg->underlyingArrayElementTypedefFilePath.empty())
          gv->setAttr("cir.debug.typedef_underlying_array_elem_typedef_file",
                      getStringAttrCached(
                          dbg->underlyingArrayElementTypedefFilePath));
        if (dbg->underlyingArrayElementTypedefLine)
          gv->setAttr("cir.debug.typedef_underlying_array_elem_typedef_line",
                      builder.getI32IntegerAttr(
                          dbg->underlyingArrayElementTypedefLine));
      }
      llvm::SmallVector<PointerLevelDebugInfo, 4> pointerDebugInfo;
      collectPointerDebugInfoFromQualType(d->getType(), pointerDebugInfo);
      if (!pointerDebugInfo.empty()) {
        llvm::SmallVector<mlir::Attribute> btfTagLevels;
        llvm::SmallVector<mlir::Attribute> pointeeConstLevels;
        llvm::SmallVector<mlir::Attribute> pointeeVolatileLevels;
        llvm::SmallVector<mlir::Attribute> pointeeRestrictLevels;
        btfTagLevels.reserve(pointerDebugInfo.size());
        pointeeConstLevels.reserve(pointerDebugInfo.size());
        pointeeVolatileLevels.reserve(pointerDebugInfo.size());
        pointeeRestrictLevels.reserve(pointerDebugInfo.size());
        for (const PointerLevelDebugInfo &info : pointerDebugInfo) {
          llvm::SmallVector<mlir::Attribute> btfTags;
          btfTags.reserve(info.btfTypeTags.size());
          for (llvm::StringRef tag : info.btfTypeTags)
            btfTags.push_back(getStringAttrCached(tag));
          btfTagLevels.push_back(builder.getArrayAttr(btfTags));
          pointeeConstLevels.push_back(builder.getBoolAttr(
              info.pointeeConstQualified));
          pointeeVolatileLevels.push_back(builder.getBoolAttr(
              info.pointeeVolatileQualified));
          pointeeRestrictLevels.push_back(builder.getBoolAttr(
              info.pointeeRestrictQualified));
        }
        gv->setAttr("cir.debug.pointer_chain_btf_type_tags",
                    builder.getArrayAttr(btfTagLevels));
        gv->setAttr("cir.debug.pointer_chain_pointee_const",
                    builder.getArrayAttr(pointeeConstLevels));
        gv->setAttr("cir.debug.pointer_chain_pointee_volatile",
                    builder.getArrayAttr(pointeeVolatileLevels));
        gv->setAttr("cir.debug.pointer_chain_pointee_restrict",
                    builder.getArrayAttr(pointeeRestrictLevels));
      }
      if (auto enumDbg = getEnumDebugTypeInfoFromQualType(astContext, d->getType())) {
        gv->setAttr("cir.debug.enum_name", getStringAttrCached(enumDbg->name));
        if (!enumDbg->filePath.empty())
          gv->setAttr("cir.debug.enum_file",
                      getStringAttrCached(enumDbg->filePath));
        if (enumDbg->line)
          gv->setAttr("cir.debug.enum_line",
                      builder.getI32IntegerAttr(enumDbg->line));
        gv->setAttr("cir.debug.enum_base_name",
                    getStringAttrCached(enumDbg->baseName));
        gv->setAttr("cir.debug.enum_base_encoding",
                    builder.getI32IntegerAttr(enumDbg->baseEncoding));
        gv->setAttr("cir.debug.enum_base_size",
                    builder.getI64IntegerAttr(enumDbg->baseSizeInBits));
        llvm::SmallVector<mlir::Attribute> enumNames;
        llvm::SmallVector<mlir::Attribute> enumValues;
        enumNames.reserve(enumDbg->enumeratorNames.size());
        enumValues.reserve(enumDbg->enumeratorValues.size());
        for (auto [name, value] :
             llvm::zip(enumDbg->enumeratorNames, enumDbg->enumeratorValues)) {
          enumNames.push_back(getStringAttrCached(name));
          enumValues.push_back(builder.getI64IntegerAttr(value));
        }
        gv->setAttr("cir.debug.enum_enumerator_names",
                    builder.getArrayAttr(enumNames));
        gv->setAttr("cir.debug.enum_enumerator_values",
                    builder.getArrayAttr(enumValues));
        gv->setAttr("cir.debug.enum_is_unsigned",
                    builder.getBoolAttr(enumDbg->enumeratorsUnsigned));
        if (enumDbg->isScoped)
          gv->setAttr("cir.debug.enum_is_scoped", builder.getBoolAttr(true));
        if (!enumDbg->identifier.empty())
          gv->setAttr("cir.debug.enum_identifier",
                      getStringAttrCached(enumDbg->identifier));
      }

      if (const auto *rd = getRecordDeclFromQualTypeForDebug(d->getType())) {
        // Debug field/size metadata requires a complete definition. Forward
        // declarations can appear in pointer/reference globals; avoid querying
        // ASTRecordLayout for incomplete records.
        if (const auto *recordDef = rd->getDefinition()) {
          // Check cache first: all globals with the same RecordDecl definition
          // produce identical debug field/record attributes.
          if (auto cacheIt = recordDebugFieldAttrCache.find(recordDef);
              cacheIt != recordDebugFieldAttrCache.end()) {
            for (auto namedAttr : cacheIt->second)
              gv->setAttr(namedAttr.getName(), namedAttr.getValue());
          } else {
          const auto *cxxRD = dyn_cast<CXXRecordDecl>(recordDef);
          llvm::SmallVector<mlir::Attribute> fieldFlags;
          llvm::SmallVector<mlir::Attribute> fieldNames;
          llvm::SmallVector<mlir::Attribute> fieldLines;
          llvm::SmallVector<mlir::Attribute> fieldOffsets;
          llvm::SmallVector<mlir::Attribute> fieldIsBitField;
          llvm::SmallVector<mlir::Attribute> fieldBitSizes;
          llvm::SmallVector<mlir::Attribute> fieldBitOffsets;
          llvm::SmallVector<mlir::Attribute> fieldBitBaseNames;
          llvm::SmallVector<mlir::Attribute> fieldBitBaseEncodings;
          llvm::SmallVector<mlir::Attribute> fieldBitBaseSizes;
          llvm::SmallVector<mlir::Attribute> fieldBaseNames;
          llvm::SmallVector<mlir::Attribute> fieldBaseEncodings;
          llvm::SmallVector<mlir::Attribute> fieldBaseSizes;
          const ASTRecordLayout &layout =
              astContext.getASTRecordLayout(recordDef);
          unsigned fieldIdx = 0;
          for (const FieldDecl *fd : recordDef->fields()) {
            llvm::DINode::DIFlags flag =
                cxxRD ? getFieldAccessDebugFlag(cxxRD, fd->getAccess())
                      : llvm::DINode::FlagZero;
            fieldFlags.push_back(
                builder.getI32IntegerAttr(static_cast<uint32_t>(flag)));
            fieldNames.push_back(getStringAttrCached(fd->getName()));
            SourceLocation floc = fd->getLocation();
            unsigned line = 0;
            if (floc.isValid()) {
              SourceLocation presumedLoc =
                  astContext.getSourceManager().getExpansionLoc(floc);
              PresumedLoc ploc =
                  astContext.getSourceManager().getPresumedLoc(presumedLoc);
              if (ploc.isValid())
                line = ploc.getLine();
            }
            fieldLines.push_back(builder.getI32IntegerAttr(line));
            fieldOffsets.push_back(
                builder.getI64IntegerAttr(layout.getFieldOffset(fieldIdx)));

            bool isBitField = fd->isBitField();
            fieldIsBitField.push_back(
                builder.getI8IntegerAttr(isBitField ? 1 : 0));
            auto fieldBaseDbg =
                getBuiltinDebugTypeInfoFromQualType(astContext, fd->getType());
            if (fieldBaseDbg) {
              fieldBaseNames.push_back(getStringAttrCached(fieldBaseDbg->name));
              fieldBaseEncodings.push_back(
                  builder.getI32IntegerAttr(fieldBaseDbg->encoding));
              fieldBaseSizes.push_back(
                  builder.getI64IntegerAttr(fieldBaseDbg->sizeInBits));
            } else {
              fieldBaseNames.push_back(getStringAttrCached(""));
              fieldBaseEncodings.push_back(builder.getI32IntegerAttr(0));
              fieldBaseSizes.push_back(builder.getI64IntegerAttr(0));
            }
            if (isBitField) {
              fieldBitSizes.push_back(
                  builder.getI64IntegerAttr(fd->getBitWidthValue()));
              fieldBitOffsets.push_back(
                  builder.getI64IntegerAttr(layout.getFieldOffset(fieldIdx)));
              auto bitBaseDbg = fieldBaseDbg;
              if (bitBaseDbg) {
                fieldBitBaseNames.push_back(
                    getStringAttrCached(bitBaseDbg->name));
                fieldBitBaseEncodings.push_back(
                    builder.getI32IntegerAttr(bitBaseDbg->encoding));
                fieldBitBaseSizes.push_back(
                    builder.getI64IntegerAttr(bitBaseDbg->sizeInBits));
              } else {
                fieldBitBaseNames.push_back(getStringAttrCached(""));
                fieldBitBaseEncodings.push_back(builder.getI32IntegerAttr(0));
                fieldBitBaseSizes.push_back(builder.getI64IntegerAttr(0));
              }
            } else {
              fieldBitSizes.push_back(builder.getI64IntegerAttr(0));
              fieldBitOffsets.push_back(builder.getI64IntegerAttr(0));
              fieldBitBaseNames.push_back(getStringAttrCached(""));
              fieldBitBaseEncodings.push_back(builder.getI32IntegerAttr(0));
              fieldBitBaseSizes.push_back(builder.getI64IntegerAttr(0));
            }
            ++fieldIdx;
          }
          if (!fieldFlags.empty()) {
            gv->setAttr("cir.debug.field_access_flags",
                        builder.getArrayAttr(fieldFlags));
            gv->setAttr("cir.debug.field_names",
                        builder.getArrayAttr(fieldNames));
            gv->setAttr("cir.debug.field_lines",
                        builder.getArrayAttr(fieldLines));
            gv->setAttr("cir.debug.field_offsets",
                        builder.getArrayAttr(fieldOffsets));
            gv->setAttr("cir.debug.field_is_bitfield",
                        builder.getArrayAttr(fieldIsBitField));
            gv->setAttr("cir.debug.field_bit_sizes",
                        builder.getArrayAttr(fieldBitSizes));
            gv->setAttr("cir.debug.field_bit_offsets",
                        builder.getArrayAttr(fieldBitOffsets));
            gv->setAttr("cir.debug.field_bit_base_names",
                        builder.getArrayAttr(fieldBitBaseNames));
            gv->setAttr("cir.debug.field_bit_base_encodings",
                        builder.getArrayAttr(fieldBitBaseEncodings));
            gv->setAttr("cir.debug.field_bit_base_sizes",
                        builder.getArrayAttr(fieldBitBaseSizes));
            gv->setAttr("cir.debug.field_base_names",
                        builder.getArrayAttr(fieldBaseNames));
            gv->setAttr("cir.debug.field_base_encodings",
                        builder.getArrayAttr(fieldBaseEncodings));
            gv->setAttr("cir.debug.field_base_sizes",
                        builder.getArrayAttr(fieldBaseSizes));
          }

          SourceLocation rloc = recordDef->getLocation();
          unsigned rline = 0;
          if (rloc.isValid()) {
            SourceLocation presumedLoc =
                astContext.getSourceManager().getExpansionLoc(rloc);
            PresumedLoc ploc =
                astContext.getSourceManager().getPresumedLoc(presumedLoc);
            if (ploc.isValid())
              rline = ploc.getLine();
          }
          gv->setAttr("cir.debug.record_name",
                      getStringAttrCached(recordDef->getName()));
          gv->setAttr("cir.debug.record_line", builder.getI32IntegerAttr(rline));
          gv->setAttr("cir.debug.record_size",
                      builder.getI64IntegerAttr(layout.getSize().getQuantity() *
                                                astContext.getCharWidth()));
          uint32_t recordTag = llvm::dwarf::DW_TAG_structure_type;
          switch (recordDef->getTagKind()) {
          case TagTypeKind::Class:
            recordTag = llvm::dwarf::DW_TAG_class_type;
            break;
          case TagTypeKind::Union:
            recordTag = llvm::dwarf::DW_TAG_union_type;
            break;
          case TagTypeKind::Struct:
          default:
            recordTag = llvm::dwarf::DW_TAG_structure_type;
            break;
          }
          gv->setAttr("cir.debug.record_tag",
                      builder.getI32IntegerAttr(recordTag));
          // Cache the record/field debug attrs for reuse by subsequent globals
          // of the same type, avoiding O(fields) re-iteration per global.
          llvm::SmallVector<mlir::NamedAttribute> recordDebugAttrs;
          for (auto attr : gv->getAttrs()) {
            auto name = attr.getName().strref();
            if (name.starts_with("cir.debug.field_") ||
                name.starts_with("cir.debug.record_"))
              recordDebugAttrs.push_back(attr);
          }
          recordDebugFieldAttrCache[recordDef] =
              mlir::DictionaryAttr::get(&getMLIRContext(), recordDebugAttrs);
          } // end else (cache miss)
        }
      }
    }

    // OpenMP target-device legality is decided before normal global emission.
    // Decls that still reach this point (for example device threadprivate
    // globals materialized from EmitOMPThreadPrivateDecl) should be emitted as
    // ordinary CIR globals like OGCG's GetAddrOfGlobalVar path.

    gv.setAlignmentAttr(getSize(astContext.getDeclAlign(d)));

    setLinkageForGV(gv, d);

    if (d->getTLSKind()) {
      if (d->getTLSKind() == VarDecl::TLS_Dynamic)
        cxxThreadLocals.push_back(d);
      setTLSMode(gv, *d);
    }

    setGVProperties(gv, d);

    // If required by the ABI, treat declarations of static data members with
    // inline initializers as definitions.
    if (astContext.isMSStaticDataMemberInlineDefinition(d)) {
      errorNYI(d->getSourceRange(), "MS static data member inline definition");
    }

    if (d->hasExternalStorage()) {
      if (const auto *sa = d->getAttr<SectionAttr>())
        gv.setSectionAttr(getStringAttrCached(sa->getName()));
    }

    // Handle XCore specific ABI requirements.
    if (getTriple().getArch() == llvm::Triple::xcore) {
      errorNYI(d->getSourceRange(), "XCore specific ABI requirements");
    }

    // Check if we a have a const declaration with an initializer, we may be
    // able to emit it as available_externally to expose it's value to the
    // optimizer.
    if (getLangOpts().CPlusPlus &&
        gv.getLinkage() == cir::GlobalLinkageKind::ExternalLinkage &&
        d->getType().isConstQualified() && gv.isDeclaration() &&
        !d->hasDefinition() && d->hasInit() && !d->hasAttr<DLLImportAttr>()) {
      const auto *record =
          astContext.getBaseElementType(d->getType())->getAsCXXRecordDecl();
      bool hasMutableFields = record && record->hasMutableFields();
      if (!hasMutableFields) {
        const VarDecl *initDecl;
        const Expr *initExpr = d->getAnyInitializer(initDecl);
        if (initExpr) {
          ConstantEmitter emitter(*this);
          mlir::Attribute init = emitter.tryEmitForInitializer(*initDecl);
          if (init) {
            auto initType = mlir::dyn_cast<mlir::TypedAttr>(init);
            if (initType && initType.getType() != gv.getSymType()) {
              // The type of the initializer does not match the definition.
              // This happens when an initializer has a different type from
              // the type of the global (because of padding at the end of a
              // structure for instance).
              //
              // Before destroying the operation, drop it from the
              // symbol-lookup cache and clear lastGlobalOp if it points at
              // this op. Otherwise the recursive getOrCreateCIRGlobal call
              // below will look the freed pointer back up via
              // getGlobalValue(mangledName) and use-after-free it
              // (heap-use-after-free at dyn_cast<cir::GlobalOp> /
              // mlir::Operation::getName, observed on heavy-template DuckDB
              // TUs e.g. csv_file_handle's __const.* tables).
              mlir::Operation *gvOp = gv.getOperation();
              if (lastGlobalOp == gvOp)
                lastGlobalOp = nullptr;
              eraseGlobalSymbol(gvOp);
              gv.erase();
              gv = getOrCreateCIRGlobal(d, initType.getType(),
                                        ForDefinition_t(false));
            }
            setInitializer(gv, init);
            gv.setConstant(true);
            gv.setLinkage(
                cir::GlobalLinkageKind::AvailableExternallyLinkage);
            gv.setVisibility(getMLIRVisibilityFromCIRLinkage(
                cir::GlobalLinkageKind::AvailableExternallyLinkage));
            emitter.finalize(gv);
          }
        }
      }
    }
  }

  return gv;
}

cir::GlobalOp
CIRGenModule::getOrCreateCIRGlobal(const VarDecl *d, mlir::Type ty,
                                   ForDefinition_t isForDefinition) {
  assert(d->hasGlobalStorage() && "Not a global variable");
  QualType astTy = d->getType();
  if (!ty)
    ty = getTypes().convertTypeForMem(astTy);

  StringRef mangledName = getMangledName(d);
  return getOrCreateCIRGlobal(mangledName, ty, getGlobalVarAddressSpace(d), d,
                              isForDefinition);
}

mlir::Value CIRGenModule::getAddrOfOpenMPDeclareTargetVar(const VarDecl *d) {
  if (!getLangOpts().OpenMPIsTargetDevice ||
      !shouldUseOpenMPDeclareTargetRefPtr(d, *this))
    return {};

  mlir::Location loc = getLoc(d->getSourceRange());
  mlir::Type symType = getTypes().convertTypeForMem(d->getType());
  auto pointeeAddrSpace =
      cir::toCIRLangAddressSpaceAttr(&getMLIRContext(), getGlobalVarAddressSpace(d));
  auto refValueTy = builder.getPointerTo(symType, pointeeAddrSpace);
  std::string refPtrName = getOpenMPDeclareTargetRefPtrName(d, *this);

  cir::GlobalOp refPtrGlobal =
      dyn_cast_or_null<cir::GlobalOp>(getGlobalValue(refPtrName));
  if (!refPtrGlobal) {
    refPtrGlobal = createGlobalOp(*this, loc, refPtrName, refValueTy,
                                  /*isConstant=*/false);
  }

  refPtrGlobal.setLinkage(cir::GlobalLinkageKind::WeakAnyLinkage);
  refPtrGlobal.setVisibility(
      getMLIRVisibilityFromCIRLinkage(cir::GlobalLinkageKind::WeakAnyLinkage));
  refPtrGlobal.setAlignment(getPointerAlign().getQuantity());
  if (refPtrGlobal.isDeclaration())
    refPtrGlobal.setInitialValueAttr(builder.getConstNullPtrAttr(refValueTy));

  mlir::Type refPtrTy =
      builder.getPointerTo(refPtrGlobal.getSymType(), refPtrGlobal.getAddrSpaceAttr());
  return cir::GetGlobalOp::create(builder, loc, refPtrTy,
                                  refPtrGlobal.getSymNameAttr(),
                                  /*threadLocal=*/false);
}

/// Return the mlir::Value for the address of the given global variable. If
/// \p ty is non-null and if the global doesn't exist, then it will be created
/// with the specified type instead of whatever the normal requested type would
/// be. If \p isForDefinition is true, it is guaranteed that an actual global
/// with type \p ty will be returned, not conversion of a variable with the same
/// mangled name but some other type.
mlir::Value CIRGenModule::getAddrOfGlobalVar(const VarDecl *d, mlir::Type ty,
                                             ForDefinition_t isForDefinition) {
  assert(d->hasGlobalStorage() && "Not a global variable");
  QualType astTy = d->getType();
  if (!ty)
    ty = getTypes().convertTypeForMem(astTy);

  // Keep variable aliases as alias symbols. Materialize a constant address
  // view directly instead of forcing getOrCreateCIRGlobal, which would erase
  // a pre-existing AliasOp with the same symbol name.
  StringRef mangledName = getMangledName(d);
  if (auto aliasOp =
          dyn_cast_or_null<cir::AliasOp>(getGlobalValue(mangledName))) {
    mlir::Type aliasPtrTy = builder.getPointerTo(aliasOp.getSymType());
    mlir::Value addr = cir::GetGlobalOp::create(
        builder, getLoc(d->getSourceRange()), aliasPtrTy,
        aliasOp.getSymNameAttr(), /*threadLocal=*/false);
    auto expectedPtrTy = builder.getPointerTo(ty);
    if (addr.getType() != expectedPtrTy)
      addr = builder.createBitcast(addr.getLoc(), addr, expectedPtrTy);
    return addr;
  }

  bool tlsAccess = d->getTLSKind() != VarDecl::TLS_None;
  cir::GlobalOp g = getOrCreateCIRGlobal(d, ty, isForDefinition);
  mlir::Type ptrTy = builder.getPointerTo(g.getSymType(), g.getAddrSpaceAttr());
  return cir::GetGlobalOp::create(builder, getLoc(d->getSourceRange()), ptrTy,
                                  g.getSymNameAttr(), tlsAccess);
}

cir::GlobalViewAttr CIRGenModule::getAddrOfGlobalVarAttr(const VarDecl *d) {
  assert(d->hasGlobalStorage() && "Not a global variable");
  mlir::Type ty = getTypes().convertTypeForMem(d->getType());

  StringRef mangledName = getMangledName(d);
  if (auto aliasOp =
          dyn_cast_or_null<cir::AliasOp>(getGlobalValue(mangledName))) {
    cir::PointerType ptrTy = builder.getPointerTo(aliasOp.getSymType());
    auto symbol =
        mlir::FlatSymbolRefAttr::get(&getMLIRContext(), aliasOp.getSymNameAttr());
    auto view = cir::GlobalViewAttr::get(ptrTy, symbol);
    if (ptrTy.getPointee() == ty)
      return view;
    return cir::GlobalViewAttr::get(builder.getPointerTo(ty), symbol);
  }

  cir::GlobalOp globalOp = getOrCreateCIRGlobal(d, ty, NotForDefinition);
  cir::PointerType ptrTy =
      builder.getPointerTo(globalOp.getSymType(), globalOp.getAddrSpaceAttr());
  return builder.getGlobalViewAttr(ptrTy, globalOp);
}

cir::GlobalViewAttr CIRGenModule::getAddrOfFunctionAttr(const ValueDecl *vd,
                                                        bool noCFI) {
  vd = cast<ValueDecl>(vd->getMostRecentDecl());
  auto *fd = cast<FunctionDecl>(vd);

  cir::FuncOp funcOp;
  if (fd->hasAttr<WeakRefAttr>())
    funcOp = dyn_cast<cir::FuncOp>(getWeakRefReference(fd));
  else
    funcOp = getAddrOfFunction(fd);
  assert(funcOp && "function decl should lower to cir.func");

  cir::PointerType ptrTy = builder.getPointerTo(funcOp.getFunctionType());
  auto symbol =
      mlir::FlatSymbolRefAttr::get(&getMLIRContext(), funcOp.getSymNameAttr());
  return cir::GlobalViewAttr::get(ptrTy, symbol, /*indices=*/{},
                                  /*inbounds=*/false, noCFI);
}

cir::GlobalOp CIRGenModule::getAddrOfTemplateParamObject(
    const TemplateParamObjectDecl *tpo) {
  StringRef name = getMangledName(tpo);

  if (cir::GlobalOp gv =
          dyn_cast_or_null<cir::GlobalOp>(getGlobalValue(name)))
    return gv;

  ConstantEmitter emitter(*this);
  mlir::Attribute init =
      emitter.emitForInitializer(tpo->getValue(), tpo->getType());

  mlir::Type ty = getTypes().convertTypeForMem(tpo->getType());
  // TemplateParamObjectDecl typically has an invalid source location
  // (it's a compiler-generated object), so use UnknownLoc.
  auto loc = tpo->getLocation().isValid()
                 ? getLoc(tpo->getLocation())
                 : mlir::UnknownLoc::get(&getMLIRContext());

  cir::GlobalLinkageKind linkage =
      isExternallyVisible(tpo->getLinkageAndVisibility().getLinkage())
          ? cir::GlobalLinkageKind::LinkOnceODRLinkage
          : cir::GlobalLinkageKind::InternalLinkage;

  auto gv = createGlobalOp(*this, loc, name, ty, /*isConstant=*/true);
  gv.setLinkage(linkage);
  gv.setInitialValueAttr(init);
  if (linkage == cir::GlobalLinkageKind::LinkOnceODRLinkage)
    mlir::SymbolTable::setSymbolVisibility(
        gv, mlir::SymbolTable::Visibility::Public);
  emitter.finalize(gv);

  setGVProperties(gv, tpo);
  if (supportsCOMDAT() && linkage == cir::GlobalLinkageKind::LinkOnceODRLinkage)
    gv.setComdat(true);

  return gv;
}

cir::GlobalOp CIRGenModule::getAddrOfUnnamedGlobalConstantDecl(
    const UnnamedGlobalConstantDecl *gcd) {
  cir::GlobalOp &entry = unnamedGlobalConstantDeclMap[gcd];
  if (entry)
    return entry;

  ConstantEmitter emitter(*this);
  mlir::Attribute init =
      emitter.emitForInitializer(gcd->getValue(), gcd->getType());

  mlir::Type ty = getTypes().convertTypeForMem(gcd->getType());
  auto loc = mlir::UnknownLoc::get(&getMLIRContext());

  auto gv = builder.createVersionedGlobal(
      getModule(), loc, ".constant", ty, /*isConstant=*/true,
      cir::GlobalLinkageKind::PrivateLinkage);
  insertGlobalSymbol(gv);
  mlir::SymbolTable::setSymbolVisibility(
      gv, mlir::SymbolTable::Visibility::Private);
  gv.setInitialValueAttr(init);
  gv->setAttr("unnamed_addr", mlir::UnitAttr::get(&getMLIRContext()));
  emitter.finalize(gv);

  entry = gv;
  return gv;
}

/// Recursively walk an MLIR attribute tree, replacing any GlobalViewAttr whose
/// symbol matches \p oldName with one referencing \p newName. This is needed
/// because MLIR attributes are immutable value-semantic objects — unlike LLVM
/// IR, we cannot use replaceAllUsesWith to update symbol references embedded
/// inside attributes.
static mlir::Attribute replaceSymbolInAttr(mlir::Attribute attr,
                                           llvm::StringRef oldName,
                                           llvm::StringRef newName) {
  if (auto gva = mlir::dyn_cast<cir::GlobalViewAttr>(attr)) {
    if (gva.getSymbol().getValue() == oldName) {
      auto newSymbol =
          mlir::FlatSymbolRefAttr::get(attr.getContext(), newName);
      return cir::GlobalViewAttr::get(gva.getType(), newSymbol,
                                      gva.getIndices(), gva.getRawByteOffset(),
                                      gva.getInRange(), gva.getInbounds(),
                                      gva.getAddrAdjustment());
    }
    return attr;
  }

  if (auto rec = mlir::dyn_cast<cir::ConstRecordAttr>(attr)) {
    bool changed = false;
    llvm::SmallVector<mlir::Attribute> newMembers;
    for (auto member : rec.getMembers()) {
      auto replaced = replaceSymbolInAttr(member, oldName, newName);
      if (replaced != member)
        changed = true;
      newMembers.push_back(replaced);
    }
    if (!changed)
      return attr;
    auto membersAttr = mlir::ArrayAttr::get(attr.getContext(), newMembers);
    return cir::ConstRecordAttr::get(mlir::cast<cir::RecordType>(rec.getType()),
                                     membersAttr);
  }

  if (auto arr = mlir::dyn_cast<cir::ConstArrayAttr>(attr)) {
    auto elts = arr.getElts();
    if (auto arrElts = mlir::dyn_cast<mlir::ArrayAttr>(elts)) {
      bool changed = false;
      llvm::SmallVector<mlir::Attribute> newElts;
      for (auto elt : arrElts) {
        auto replaced = replaceSymbolInAttr(elt, oldName, newName);
        if (replaced != elt)
          changed = true;
        newElts.push_back(replaced);
      }
      if (!changed)
        return attr;
      auto newEltsAttr = mlir::ArrayAttr::get(attr.getContext(), newElts);
      return cir::ConstArrayAttr::get(mlir::cast<cir::ArrayType>(arr.getType()),
                                      newEltsAttr, arr.getTrailingZerosNum());
    }
    // StringAttr elements can't contain GlobalViewAttr.
    return attr;
  }

  if (auto ti = mlir::dyn_cast<cir::TypeInfoAttr>(attr)) {
    bool changed = false;
    llvm::SmallVector<mlir::Attribute> newData;
    for (auto datum : ti.getData()) {
      auto replaced = replaceSymbolInAttr(datum, oldName, newName);
      if (replaced != datum)
        changed = true;
      newData.push_back(replaced);
    }
    if (!changed)
      return attr;
    auto dataAttr = mlir::ArrayAttr::get(attr.getContext(), newData);
    return cir::TypeInfoAttr::get(ti.getType(), dataAttr);
  }

  // All other attribute types can't contain GlobalViewAttr symbol references.
  return attr;
}

struct LegacyGNUSelectorReplacement {
  mlir::FlatSymbolRefAttr listSymbol;
  unsigned selectorIndex = 0;
};

static mlir::ArrayAttr
getLegacyGNUSelectorIndices(CIRGenBuilderTy &builder, unsigned selectorIndex,
                            mlir::ArrayAttr originalIndices) {
  if (selectorIndex == 0 && !originalIndices)
    return {};

  llvm::SmallVector<mlir::Attribute> combinedIndices;
  combinedIndices.push_back(builder.getI32IntegerAttr(selectorIndex));
  if (originalIndices)
    llvm::append_range(combinedIndices, originalIndices.getValue());
  return builder.getArrayAttr(combinedIndices);
}

static void replaceLegacyGNUSelectorUsesInModule(
    CIRGenModule &cgm,
    const llvm::StringMap<LegacyGNUSelectorReplacement> &replacements) {
  if (replacements.empty())
    return;

  CIRGenBuilderTy &builder = cgm.getBuilder();
  mlir::AttrTypeReplacer replacer;
  replacer.addReplacement(
      [&](cir::GlobalViewAttr view)
          -> std::optional<std::pair<mlir::Attribute, mlir::WalkResult>> {
        auto it = replacements.find(view.getSymbol().getValue());
        if (it == replacements.end())
          return std::nullopt;

        mlir::ArrayAttr newIndices = getLegacyGNUSelectorIndices(
            builder, it->second.selectorIndex, view.getIndices());
        auto rewritten = cir::GlobalViewAttr::get(
            view.getType(), it->second.listSymbol, newIndices,
            view.getRawByteOffset(), view.getInRange(), view.getInbounds(),
            view.getAddrAdjustment(), view.getNoCFI());
        return std::make_pair(mlir::Attribute(rewritten),
                              mlir::WalkResult::skip());
      });
  replacer.recursivelyReplaceElementsIn(cgm.getModule());
}

mlir::Operation *
CIRGenModule::getAddrOfGlobalTemporary(const MaterializeTemporaryExpr *expr,
                                       const Expr *init) {
  assert((expr->getStorageDuration() == SD_Static ||
          expr->getStorageDuration() == SD_Thread) &&
         "not a global temporary");
  const auto *vd = cast<VarDecl>(expr->getExtendingDecl());

  // If we're not materializing a subobject of the temporary, keep the
  // cv-qualifiers from the type of the MaterializeTemporaryExpr.
  QualType materializedType = init->getType();
  if (init == expr->getSubExpr())
    materializedType = expr->getType();

  CharUnits align = astContext.getTypeAlignInChars(materializedType);

  auto insertResult = materializedGlobalTemporaryMap.insert({expr, nullptr});
  if (!insertResult.second) {
    // We've seen this before: either we already created it or we're in the
    // process of doing so.
    if (!insertResult.first->second) {
      // We recursively re-entered this function, probably during emission of
      // the initializer. Create a placeholder.
      mlir::Type type = getTypes().convertTypeForMem(materializedType);
      auto loc = getLoc(expr->getSourceRange());
      insertResult.first->second =
          createGlobalOp(*this, loc, ".ref.tmp", type).getOperation();
    }
    return insertResult.first->second;
  }

  // FIXME: If an externally-visible declaration extends multiple temporaries,
  // we need to give each temporary the same name in every translation unit (and
  // we also need to make the temporaries externally-visible).
  SmallString<256> name;
  llvm::raw_svector_ostream out(name);
  getCXXABI().getMangleContext().mangleReferenceTemporary(
      vd, expr->getManglingNumber(), out);

  APValue *value = nullptr;
  if (expr->getStorageDuration() == SD_Static && vd->evaluateValue()) {
    // If the initializer of the extending declaration is a constant
    // initializer, we should have a cached constant initializer for this
    // temporary. Note that this might have a different value from the value
    // computed by evaluating the initializer if the surrounding constant
    // expression modifies the temporary.
    value = expr->getOrCreateValue(false);
  }

  // Try evaluating it now, it might have a constant initializer.
  Expr::EvalResult evalResult;
  if (!value && init->EvaluateAsRValue(evalResult, astContext) &&
      !evalResult.hasSideEffects())
    value = &evalResult.Val;
  std::optional<ConstantEmitter> emitter;
  mlir::Attribute initialValue = nullptr;
  bool isConstant = false;
  mlir::Type type;
  if (value) {
    // The temporary has a constant initializer, use it.
    emitter.emplace(*this);
    initialValue = emitter->emitForInitializer(*value, materializedType);
    isConstant = materializedType.isConstantStorage(astContext,
                                                    /*ExcludeCtor*/ value,
                                                    /*ExcludeDtor*/ false);
    if (initialValue)
      type = mlir::cast<mlir::TypedAttr>(initialValue).getType();
    else
      type = getTypes().convertTypeForMem(materializedType);
  } else {
    // No initializer, the initialization will be provided when we
    // initialize the declaration which performed lifetime extension.
    type = getTypes().convertTypeForMem(materializedType);
  }

  // Create a global variable for this lifetime-extended temporary.
  cir::GlobalLinkageKind linkage = getCIRLinkageVarDefinition(vd, isConstant);
  if (linkage == cir::GlobalLinkageKind::ExternalLinkage) {
    const VarDecl *initVD;
    if (vd->isStaticDataMember() && vd->getAnyInitializer(initVD) &&
        isa<CXXRecordDecl>(initVD->getLexicalDeclContext())) {
      // Temporaries defined inside a class get linkonce_odr linkage because the
      // class can be defined in multiple translation units.
      linkage = cir::GlobalLinkageKind::LinkOnceODRLinkage;
    } else {
      // There is no need for this temporary to have external linkage if the
      // VarDecl has external linkage.
      linkage = cir::GlobalLinkageKind::InternalLinkage;
    }
  }

  auto loc = getLoc(expr->getSourceRange());
  cir::GlobalOp gv = createGlobalOp(*this, loc, name, type, isConstant);
  if (initialValue)
    gv.setInitialValueAttr(initialValue);

  if (emitter)
    emitter->finalize(gv);

  // Set visibility and DSO-local properties from the extending declaration.
  // This propagates hidden visibility from hidden-visibility classes to their
  // static data member reference temporaries (matching OGCG's setGVProperties).
  if (linkage != cir::GlobalLinkageKind::InternalLinkage &&
      linkage != cir::GlobalLinkageKind::PrivateLinkage) {
    setGlobalVisibility(gv, vd);
    setDSOLocal(static_cast<mlir::Operation *>(gv));
  }

  gv.setAlignmentAttr(getSize(align));
  gv.setLinkageAttr(
      cir::GlobalLinkageKindAttr::get(&getMLIRContext(), linkage));
  gv.setVisibility(getMLIRVisibilityFromCIRLinkage(linkage));
  if (supportsCOMDAT() && cir::isWeakForLinker(linkage))
    gv.setComdat(true);
  if (vd->getTLSKind())
    setTLSMode(gv, *vd);
  // Update the map with the new temporary. If we created a placeholder above,
  // replace it with the new global now.
  mlir::Operation *&entry = materializedGlobalTemporaryMap[expr];
  if (entry) {
    // The placeholder's symbol ref may be embedded in GlobalViewAttr attributes
    // within the initializer. MLIR attributes are immutable value-semantic
    // objects, so replaceAllUsesWith only handles SSA Value uses, not symbol
    // references inside attributes. We must walk the initializer attribute tree
    // and replace any GlobalViewAttr referencing the placeholder symbol with
    // the real global's symbol.
    auto placeholder = mlir::cast<cir::GlobalOp>(entry);
    auto placeholderName = placeholder.getSymName();
    if (auto initAttr = gv.getInitialValue()) {
      auto replaced =
          replaceSymbolInAttr(*initAttr, placeholderName, gv.getSymName());
      if (replaced != *initAttr)
        gv.setInitialValueAttr(replaced);
    }
    entry->replaceAllUsesWith(gv);
    eraseGlobalSymbol(entry);
    entry->erase();
  }
  entry = gv;

  return gv;
}

void CIRGenModule::emitGlobalVarDefinition(const clang::VarDecl *vd,
                                           bool isTentative) {
  // OpenCL global variables of sampler type are translated to function calls,
  // therefore no need to be translated.
  if (getLangOpts().OpenCL && vd->getType()->isSamplerT()) {
    errorNYI(vd->getSourceRange(),
             "emit OpenCL sampler type/OpenMP global variable");
    return;
  }

  // HLSL default buffer constants are emitted during HLSLBufferDecl codegen.
  // Match OGCG by skipping standalone emission for hlsl_constant globals.
  if (getLangOpts().HLSL &&
      vd->getType().getAddressSpace() == LangAS::hlsl_constant)
    return;

  // Whether the definition of the variable is available externally.
  // If yes, we shouldn't emit the GloablCtor and GlobalDtor for the variable
  // since this is the job for its original source.
  bool isDefinitionAvailableExternally =
      astContext.GetGVALinkageForVariable(vd) == GVA_AvailableExternally;

  // It is useless to emit the definition for an available_externally variable
  // which can't be marked as const.
  if (isDefinitionAvailableExternally &&
      (!vd->hasConstantInitialization() ||
       // TODO: Update this when we have interface to check constexpr
       // destructor.
       vd->needsDestruction(astContext) ||
       !vd->getType().isConstantStorage(astContext, true, true)))
    return;

  mlir::Attribute init;
  bool needsGlobalCtor = false;
  bool needsGlobalDtor =
      !isDefinitionAvailableExternally &&
      vd->needsDestruction(astContext) == QualType::DK_cxx_destructor;
  const VarDecl *initDecl;
  const Expr *initExpr = vd->getAnyInitializer(initDecl);

  std::optional<ConstantEmitter> emitter;

  // CUDA E.2.4.1 "__shared__ variables cannot have an initialization
  // as part of their declaration."  Sema has already checked for
  // error cases, so we just need to set Init to UndefValue.
  bool isCudaSharedVar =
      getLangOpts().CUDAIsDevice && vd->hasAttr<CUDASharedAttr>();
  // Shadows of initialized device-side global variables are also left
  // undefined.
  // Managed Variables should be initialized on both host side and device side.
  bool isCudaShadowVar =
      !getLangOpts().CUDAIsDevice && !vd->hasAttr<HIPManagedAttr>() &&
      (vd->hasAttr<CUDAConstantAttr>() || vd->hasAttr<CUDADeviceAttr>() ||
       vd->hasAttr<CUDASharedAttr>());
  bool isCudaDeviceShadowVar =
      getLangOpts().CUDAIsDevice && !vd->hasAttr<HIPManagedAttr>() &&
      (vd->getType()->isCUDADeviceBuiltinSurfaceType() ||
       vd->getType()->isCUDADeviceBuiltinTextureType());

  if (getLangOpts().CUDA &&
      (isCudaSharedVar || isCudaShadowVar || isCudaDeviceShadowVar))
    init = cir::UndefAttr::get(convertType(vd->getType()));
  else if (vd->hasAttr<LoaderUninitializedAttr>())
    init = cir::UndefAttr::get(convertType(vd->getType()));
  else if (!initExpr) {
    // This is a tentative definition; tentative definitions are
    // implicitly initialized with { 0 }.
    //
    // Note that tentative definitions are only emitted at the end of
    // a translation unit, so they should never have incomplete
    // type. In addition, EmitTentativeDefinition makes sure that we
    // never attempt to emit a tentative definition if a real one
    // exists. A use may still exists, however, so we still may need
    // to do a RAUW.
    assert(!vd->getType()->isIncompleteType() && "Unexpected incomplete type");
    init = emitNullConstantAttr(vd->getType());
  } else {
    emitter.emplace(*this);
    mlir::Attribute initializer = emitter->tryEmitForInitializer(*initDecl);
    if (!initializer) {
      QualType qt = initExpr->getType();
      if (vd->getType()->isReferenceType())
        qt = vd->getType();

      if (getLangOpts().CPlusPlus) {
        if (initDecl->hasFlexibleArrayInit(astContext)) {
          errorNYI(vd->getSourceRange(), "flexible array initializer");
        }
        init = emitNullConstantAttr(qt);
        if (!isDefinitionAvailableExternally)
          needsGlobalCtor = true;
      } else {
        errorNYI(vd->getSourceRange(), "static initializer");
        return;
      }
    } else {
      init = initializer;
      // We don't need an initializer, so remove the entry for the delayed
      // initializer position (just in case this entry was delayed) if we
      // also don't need to register a destructor.
      if (getLangOpts().CPlusPlus && !needsGlobalDtor)
        delayedCXXInitPosition.erase(vd);
    }
  }

  mlir::Type initType;
  if (mlir::isa<mlir::SymbolRefAttr>(init)) {
    errorNYI(vd->getSourceRange(), "global initializer is a symbol reference");
    return;
  } else {
    assert(mlir::isa<mlir::TypedAttr>(init) && "This should have a type");
    auto typedInitAttr = mlir::cast<mlir::TypedAttr>(init);
    initType = typedInitAttr.getType();
  }
  assert(!mlir::isa<mlir::NoneType>(initType) && "Should have a type by now");

  cir::GlobalOp gv =
      getOrCreateCIRGlobal(vd, initType, ForDefinition_t(!isTentative));
  // TODO(cir): Strip off pointer casts from Entry if we get them?

  if (!gv || gv.getSymType() != initType) {
    errorNYI(vd->getSourceRange(), "global initializer with type mismatch");
    return;
  }

  maybeHandleStaticInExternC(vd, gv.getSymName());

  if (vd->hasAttr<AnnotateAttr>())
    addGlobalAnnotations(vd, gv);

  // Set CIR's linkage type as appropriate.
  cir::GlobalLinkageKind linkage =
      getCIRLinkageVarDefinition(vd, /*IsConstant=*/false);

  // On Darwin, the thread-wrapper function is only defined alongside the
  // variable, not also alongside callers. All accesses go through the
  // thread-wrapper, so the underlying variable can be internal. Exceptions:
  // constinit variables (can be referenced directly) and non-external linkage
  // (de-duplication semantics must be preserved).
  if (vd->getTLSKind() == VarDecl::TLS_Dynamic &&
      linkage == cir::GlobalLinkageKind::ExternalLinkage &&
      astContext.getTargetInfo().getTriple().isOSDarwin() &&
      !vd->hasAttr<ConstInitAttr>())
    linkage = cir::GlobalLinkageKind::InternalLinkage;

  // CUDA B.2.1 "The __device__ qualifier declares a variable that resides on
  // the device. [...]"
  // CUDA B.2.2 "The __constant__ qualifier, optionally used together with
  // __device__, declares a variable that: [...]
  if (langOpts.CUDA) {
    if (langOpts.CUDAIsDevice) {
      // __shared__ variables is not marked as externally initialized,
      // because they must not be initialized.
      if (linkage != cir::GlobalLinkageKind::InternalLinkage &&
          (vd->hasAttr<CUDADeviceAttr>() || vd->hasAttr<CUDAConstantAttr>() ||
           vd->getType()->isCUDADeviceBuiltinSurfaceType() ||
           vd->getType()->isCUDADeviceBuiltinTextureType())) {
        gv->setAttr(cir::CUDAExternallyInitializedAttr::getMnemonic(),
                    cir::CUDAExternallyInitializedAttr::get(&getMLIRContext()));
      }
    } else
      getCUDARuntime().internalizeDeviceSideVar(vd, linkage);

    getCUDARuntime().handleVarRegistration(vd, gv);
  }

  // Decorate CUDA shadow variables with the cu.shadow_name attribute so we know
  // how to register them when lowering.
  if (langOpts.CUDA && !langOpts.CUDAIsDevice) {
    bool isShadow = false;
    if (vd->hasAttr<CUDAConstantAttr>() || vd->hasAttr<CUDADeviceAttr>())
      isShadow = (!vd->hasExternalStorage() && !vd->isInline()) ||
                 getASTContext().CUDADeviceVarODRUsedByHost.contains(vd) ||
                 vd->hasAttr<HIPManagedAttr>();
    else if (vd->getType()->isCUDADeviceBuiltinSurfaceType() ||
             vd->getType()->isCUDADeviceBuiltinTextureType())
      // Builtin surfaces and textures are registered too, under the same
      // device-side name; handleVarRegistration skipped them if they have
      // external storage, so skip them here as well.
      isShadow = !vd->hasExternalStorage();

    if (isShadow) {
      auto shadowName = cudaRuntime->getDeviceSideName(cast<NamedDecl>(vd));
      auto attr = cir::CUDAShadowNameAttr::get(&getMLIRContext(), shadowName);
      gv->setAttr(cir::CUDAShadowNameAttr::getMnemonic(), attr);
    }
  }

  // Set initializer and finalize emission
  CIRGenModule::setInitializer(gv, init);
  if (emitter)
    emitter->finalize(gv);

  // If it is safe to mark the global 'constant', do so now.
  // Use the same logic as classic codegen EmitGlobalVarDefinition.
  gv.setConstant((vd->hasAttr<CUDAConstantAttr>() && langOpts.CUDAIsDevice) ||
                 (!needsGlobalCtor && !needsGlobalDtor &&
                  vd->getType().isConstantStorage(astContext,
                                                  /*ExcludeCtor=*/true,
                                                  /*ExcludeDtor=*/true)));
  // Set section attribute if the declaration has one.
  if (const auto *sa = vd->getAttr<SectionAttr>()) {
    gv.setSectionAttr(getStringAttrCached(sa->getName()));
    // If it is in a read-only section, mark it 'constant'.
    const ASTContext::SectionInfo &si =
        astContext.SectionInfos[sa->getName()];
    if ((si.SectionFlags & ASTContext::PSF_Write) == 0)
      gv.setConstant(true);
  }

  // Set CIR linkage and DLL storage class.
  gv.setLinkage(linkage);
  // FIXME(cir): setLinkage should likely set MLIR's visibility automatically.
  gv.setVisibility(getMLIRVisibilityFromCIRLinkage(linkage));
  // OGCG also sets DLL storage class here. DLL import/export is Windows-only.
  if (linkage == cir::GlobalLinkageKind::CommonLinkage) {
    // common vars aren't constant even if declared const.
    gv.setConstant(false);
    // Tentative definition of global variables may be initialized with
    // non-zero null pointers. In this case they should have weak linkage
    // since common linkage must have zero initializer and must not have
    // explicit section therefore cannot have non-zero initial value.
    std::optional<mlir::Attribute> initializer = gv.getInitialValue();
    if (initializer && !getBuilder().isNullValue(*initializer))
      gv.setLinkage(cir::GlobalLinkageKind::WeakAnyLinkage);
  }

  setNonAliasAttributes(vd, gv);

  // Re-derive alignment now that the type is complete. The initial alignment
  // was computed in getOrCreateCIRGlobal and may be stale if the type was
  // incomplete at declaration time (e.g. tentative definition of a
  // forward-declared struct).
  CharUnits alignVal = astContext.getDeclAlign(vd);
  gv.setAlignment(alignVal.getQuantity());

  maybeSetTrivialComdat(*vd, gv);

  // Emit the initializer function if necessary.
  if (needsGlobalCtor || needsGlobalDtor)
    emitCXXGlobalVarDeclInitFunc(vd, gv, needsGlobalCtor);

  // Register the global with sanitizer metadata consumers. CIR carries the
  // relevant global sanitizer attrs and TypeSan subset so direct LLVM lowering
  // can reconstruct OGCG-visible metadata.
  maybeRecordAddressSanitizerGlobalMetadata(vd, needsGlobalCtor, gv);
  maybeRecordTypeSanGlobalMetadata(vd, gv);
  // OGCG emits debug info for the global variable via
  // DI->EmitGlobalVariable() (CodeGenModule.cpp:6271-6273).
  assert(!cir::MissingFeatures::debugInfoGlobalVar());
}

namespace {
// Check if this function calls another decl that, because of the asm attribute
// or the other decl being a builtin, ends up pointing to itself.
struct FunctionIsDirectlyRecursive
    : public ConstStmtVisitor<FunctionIsDirectlyRecursive, bool> {
  const StringRef Name;
  const Builtin::Context &BI;
  FunctionIsDirectlyRecursive(StringRef N, const Builtin::Context &C)
      : Name(N), BI(C) {}

  bool VisitCallExpr(const CallExpr *E) {
    const FunctionDecl *FD = E->getDirectCallee();
    if (!FD)
      return false;
    AsmLabelAttr *Attr = FD->getAttr<AsmLabelAttr>();
    if (Attr && Name == Attr->getLabel())
      return true;
    unsigned BuiltinID = FD->getBuiltinID();
    if (!BuiltinID || !BI.isLibFunction(BuiltinID))
      return false;
    std::string BuiltinNameStr = BI.getName(BuiltinID);
    StringRef BuiltinName = BuiltinNameStr;
    return BuiltinName.consume_front("__builtin_") && Name == BuiltinName;
  }

  bool VisitStmt(const Stmt *S) {
    for (const Stmt *Child : S->children())
      if (Child && this->Visit(Child))
        return true;
    return false;
  }
};
} // namespace

// isTriviallyRecursive - Check if this function calls another
// decl that, because of the asm attribute or the other decl being a builtin,
// ends up pointing to itself.
bool CIRGenModule::isTriviallyRecursive(const FunctionDecl *fd) {
  StringRef name;
  if (getCXXABI().getMangleContext().shouldMangleDeclName(fd)) {
    // asm labels are a special kind of mangling we have to support.
    AsmLabelAttr *attr = fd->getAttr<AsmLabelAttr>();
    if (!attr)
      return false;
    name = attr->getLabel();
  } else {
    name = fd->getName();
  }

  FunctionIsDirectlyRecursive walker(name, astContext.BuiltinInfo);
  const Stmt *body = fd->getBody();
  return body ? walker.Visit(body) : false;
}

bool CIRGenModule::shouldEmitFunction(GlobalDecl gd) {
  if (getFunctionLinkage(gd) !=
      cir::GlobalLinkageKind::AvailableExternallyLinkage)
    return true;

  const auto *fd = cast<FunctionDecl>(gd.getDecl());
  // Inline builtins declaration must be emitted. They often are fortified
  // functions.
  if (fd->isInlineBuiltinDeclaration())
    return true;

  if (codeGenOpts.OptimizationLevel == 0 && !fd->hasAttr<AlwaysInlineAttr>())
    return false;

  if (fd->hasAttr<NoInlineAttr>())
    return false;

  // OGCG checks if the function belongs to a different C++20 named module
  // unit and avoids importing function bodies across module boundaries to
  // maintain ABI compatibility.
  assert(!cir::MissingFeatures::cxx20ModuleBoundaryCheck());

  // PR9614. Avoid cases where the source code is lying to us. An available
  // externally function should have an equivalent function somewhere else,
  // but a function that calls itself through asm label/`__builtin_` trickery is
  // clearly not equivalent to the real implementation.
  // This happens in glibc's btowc and in some configure checks.
  return !isTriviallyRecursive(fd);
}

void CIRGenModule::emitGlobalDefinition(clang::GlobalDecl gd,
                                        mlir::Operation *op) {
  const auto *decl = cast<ValueDecl>(gd.getDecl());
  if (const auto *fd = dyn_cast<FunctionDecl>(decl)) {
    // At -O0, don't generate CIR for functions with available_externally
    // linkage.
    if (!shouldEmitFunction(gd))
      return;

    if (const auto *method = dyn_cast<CXXMethodDecl>(decl)) {
      // Make sure to emit the definition(s) before we emit the thunks. This is
      // necessary for the generation of certain thunks.
      if (isa<CXXConstructorDecl>(method) || isa<CXXDestructorDecl>(method))
        abi->emitCXXStructor(gd);
      else if (fd->isMultiVersion())
        emitMultiVersionFunctionDefinition(gd, op);
      else
        emitGlobalFunctionDefinition(gd, op);

      if (method->isVirtual())
        getVTables().emitThunks(gd);

      return;
    }

    if (fd->isMultiVersion()) {
      emitMultiVersionFunctionDefinition(gd, op);
      return;
    }
    emitGlobalFunctionDefinition(gd, op);
    return;
  }

  if (const auto *vd = dyn_cast<VarDecl>(decl))
    return emitGlobalVarDefinition(vd, !vd->hasDefinition());

  llvm_unreachable("Invalid argument to CIRGenModule::emitGlobalDefinition");
}

void CIRGenModule::emitAliasDefinition(GlobalDecl gd) {
  const auto *d = cast<ValueDecl>(gd.getDecl());
  const AliasAttr *aa = d->getAttr<AliasAttr>();
  assert(aa && "Not an alias?");

  StringRef mangledName = getMangledName(gd);

  // If there is a definition in the module, then it wins over the alias.
  // This is dubious, but allow it to be safe.  Just ignore the alias.
  mlir::Operation *entry = getGlobalValue(mangledName);
  if (entry) {
    if (auto gv = dyn_cast<cir::CIRGlobalValueInterface>(entry)) {
      if (!gv.isDeclaration())
        return;
    } else {
      // AliasOp and other non-CIRGlobalValueInterface ops are always
      // definitions.
      return;
    }
  }

  // Match OGCG's ConvertTypeForMem(D->getType()) for aliases. In particular,
  // aliases to declarations with incomplete function signatures must use the
  // same empty-struct placeholder that OGCG materializes in LLVM IR instead of
  // CIR's internal !cir.func<()> placeholder.
  mlir::Type aliasType = getTypes().convertTypeForMem(d->getType());
  if (const auto *fd = dyn_cast<FunctionDecl>(d)) {
    if (const auto *fnTy = fd->getType()->getAs<FunctionType>();
        fnTy && !getTypes().isFuncTypeConvertible(fnTy)) {
      aliasType = cir::RecordType::get(&getMLIRContext(), /*members=*/{},
                                       /*packed=*/false, /*padded=*/false,
                                       cir::RecordType::Struct);
    }
  }

  // Determine linkage.
  cir::GlobalLinkageKind linkage;
  if (isa<FunctionDecl>(d))
    linkage = getFunctionLinkage(gd);
  else if (const auto *vd = dyn_cast<VarDecl>(d))
    linkage = getCIRLinkageVarDefinition(vd, /*IsConstant=*/false);
  else
    linkage = cir::GlobalLinkageKind::ExternalLinkage;

  // Check for weak attribute — override linkage.
  bool isWeak = d->hasAttr<WeakAttr>() || d->hasAttr<WeakRefAttr>() ||
                d->isWeakImported();

  // Compute visibility for the alias from the declaration.
  cir::VisibilityKind visKind = cir::VisibilityKind::Default;
  if (const auto *nd = dyn_cast<NamedDecl>(d)) {
    LinkageInfo lv = nd->getLinkageAndVisibility();
    // Internal definitions always have default visibility (matching OGCG).
    if (linkage != cir::GlobalLinkageKind::InternalLinkage &&
        linkage != cir::GlobalLinkageKind::PrivateLinkage) {
      if (lv.isVisibilityExplicit() || getLangOpts().SetVisibilityForExternDecls)
        visKind = getCIRVisibilityKind(lv.getVisibility());
    }
  }

  // Compute dso_local for the alias. This mirrors the logic in
  // shouldAssumeDSOLocal(), adapted for aliases which are always definitions
  // (never declarations for the linker). OGCG does this via
  // SetCommonAttributes → setGVProperties → setDSOLocal.
  bool dsoLocal = false;
  {
    bool isLocalLinkage =
        linkage == cir::GlobalLinkageKind::InternalLinkage ||
        linkage == cir::GlobalLinkageKind::PrivateLinkage;
    bool isWeakLinkage = isWeak;
    if (isLocalLinkage) {
      dsoLocal = true;
    } else if (visKind != cir::VisibilityKind::Default && !isWeakLinkage) {
      // Non-default visibility (hidden/protected) implies dso_local,
      // unless extern_weak.
      dsoLocal = true;
    } else {
      const llvm::Triple &tt = getTriple();
      if (tt.isOSBinFormatCOFF() ||
          (tt.isOSWindows() && tt.isOSBinFormatMachO())) {
        dsoLocal = !isWeakLinkage;
      } else if (tt.isOSBinFormatELF()) {
        llvm::Reloc::Model rm = getCodeGenOpts().RelocationModel;
        if (rm == llvm::Reloc::Static || getLangOpts().PIE) {
          // An alias is always a definition, so under static relocation
          // or PIE it is always dso_local.
          dsoLocal = true;
        }
      }
    }
  }

  // Create a reference to the aliasee. This ensures that it is emitted
  // if a deferred decl (matching OGCG's GetOrCreateLLVMFunction call).
  auto ddi = deferredDecls.find(aa->getAliasee());
  if (ddi != deferredDecls.end()) {
    addDeferredDeclToEmit(ddi->second);
    deferredDecls.erase(ddi);
  }

  // If there is a declaration in the module, then we had an extern followed
  // by the alias, as in:
  //   extern int test6();
  //   ...
  //   int test6() __attribute__((alias("test7")));
  //
  // For function aliases, convert the existing FuncOp declaration into an
  // alias by setting the aliasee attribute. In OGCG this works transparently
  // because llvm::GlobalAlias isa llvm::GlobalValue. In CIR, cir.get_global
  // only accepts cir.global or cir.func, so we must keep the FuncOp.
  if (entry) {
    if (auto fn = dyn_cast<cir::FuncOp>(entry)) {
      fn.setAliasee(aa->getAliasee());
      fn.setLinkage(isWeak ? cir::GlobalLinkageKind::WeakAnyLinkage : linkage);
      fn.setGlobalVisibility(visKind);
      fn.setDsoLocal(dsoLocal);
      return;
    }
    // For variable aliases, erase the declaration and create an AliasOp.
    eraseGlobalSymbol(entry);
    entry->erase();
  }

  // Create the alias op at module level.
  {
    mlir::OpBuilder::InsertionGuard guard(builder);
    if (lastGlobalOp)
      builder.setInsertionPointAfter(lastGlobalOp);
    else
      builder.setInsertionPointToStart(getModule().getBody());

    auto aliaseeRef =
        mlir::FlatSymbolRefAttr::get(&getMLIRContext(), aa->getAliasee());
    auto typeAttr = mlir::TypeAttr::get(aliasType);
    auto linkageAttr =
        cir::GlobalLinkageKindAttr::get(&getMLIRContext(), linkage);
    auto nameAttr = getStringAttrCached(mangledName);
    mlir::UnitAttr weakAttr =
        isWeak ? mlir::UnitAttr::get(&getMLIRContext()) : nullptr;
    auto visAttr = cir::VisibilityAttr::get(&getMLIRContext(), visKind);
    mlir::UnitAttr dsoLocalAttr =
        dsoLocal ? mlir::UnitAttr::get(&getMLIRContext()) : nullptr;

    // Determine TLS model for variable aliases (OGCG CodeGenModule.cpp:6711-6713).
    cir::TLS_ModelAttr tlsModelAttr;
    if (const auto *vd = dyn_cast<VarDecl>(d)) {
      if (vd->getTLSKind()) {
        cir::TLS_Model tlm = getDefaultCIRTLSModel();
        if (const TLSModelAttr *attr = vd->getAttr<TLSModelAttr>())
          tlm = llvm::StringSwitch<cir::TLS_Model>(attr->getModel())
                    .Case("global-dynamic", cir::TLS_Model::GeneralDynamic)
                    .Case("local-dynamic", cir::TLS_Model::LocalDynamic)
                    .Case("initial-exec", cir::TLS_Model::InitialExec)
                    .Case("local-exec", cir::TLS_Model::LocalExec);
        tlsModelAttr = cir::TLS_ModelAttr::get(&getMLIRContext(), tlm);
      }
    }

    auto aliasOp = cir::AliasOp::create(
        builder, getLoc(d->getLocation()), nameAttr, aliaseeRef, typeAttr,
        linkageAttr, visAttr, dsoLocalAttr, weakAttr,
        /*is_ifunc=*/nullptr, tlsModelAttr);
    insertGlobalSymbol(aliasOp);
    lastGlobalOp = aliasOp;
  }
}

mlir::Attribute
CIRGenModule::getConstantArrayFromStringLiteral(const StringLiteral *e) {
  assert(!e->getType()->isPointerType() && "Strings are always arrays");

  // Don't emit it as the address of the string, emit the string data itself
  // as an inline array.
  if (e->getCharByteWidth() == 1) {
    SmallString<64> str(e->getString());

    // Resize the string to the right size, which is indicated by its type.
    const ConstantArrayType *cat =
        astContext.getAsConstantArrayType(e->getType());
    uint64_t finalSize = cat->getZExtSize();
    str.resize(finalSize);

    mlir::Type eltTy = convertType(cat->getElementType());
    return builder.getString(str, eltTy, finalSize);
  }

  auto arrayTy = mlir::cast<cir::ArrayType>(convertType(e->getType()));

  auto arrayEltTy = mlir::cast<cir::IntType>(arrayTy.getElementType());

  uint64_t arraySize = arrayTy.getSize();
  uint64_t literalSize = e->getLength();
  uint64_t emittedSize = literalSize;
  if (emittedSize > arraySize)
    emittedSize = arraySize;

  // Check if the string is all null bytes before building the vector.
  // In most non-zero cases, this will break out on the first element.
  bool isAllZero = true;
  for (uint64_t i = 0; i < emittedSize; ++i) {
    if (e->getCodeUnit(i) != 0) {
      isAllZero = false;
      break;
    }
  }

  if (isAllZero)
    return cir::ZeroAttr::get(arrayTy);

  // Otherwise emit a constant array holding the characters.
  auto getCachedEltAttr = [&](uint64_t value) -> cir::IntAttr {
    if (arrayEltTy == builder.getUInt8Ty())
      return builder.getUInt8AttrLocalCached(static_cast<uint8_t>(value));
    if (arrayEltTy == builder.getSInt8Ty())
      return builder.getSInt8AttrLocalCached(static_cast<int8_t>(value));
    if (arrayEltTy == builder.getUInt16Ty())
      return builder.getUInt16AttrLocalCached(static_cast<uint16_t>(value));
    if (arrayEltTy == builder.getSInt16Ty())
      return builder.getSInt16AttrLocalCached(static_cast<int16_t>(value));
    if (arrayEltTy == builder.getUInt32Ty())
      return builder.getUInt32AttrLocalCached(static_cast<uint32_t>(value));
    if (arrayEltTy == builder.getSInt32Ty())
      return builder.getSInt32AttrLocalCached(static_cast<int32_t>(value));
    if (arrayEltTy == builder.getUInt64Ty())
      return builder.getUInt64AttrLocalCached(static_cast<uint64_t>(value));
    if (arrayEltTy == builder.getSInt64Ty())
      return builder.getSInt64AttrLocalCached(static_cast<int64_t>(value));
    return cir::IntAttr::get(arrayEltTy, value);
  };

  SmallVector<mlir::Attribute> elements;
  elements.reserve(arraySize);
  for (uint64_t i = 0; i < emittedSize; ++i)
    elements.push_back(getCachedEltAttr(e->getCodeUnit(i)));
  // Zero-fill the remaining elements (null terminator, padding, or truncation).
  cir::IntAttr zeroAttr = getCachedEltAttr(0);
  for (uint64_t i = emittedSize; i < arraySize; ++i)
    elements.push_back(zeroAttr);

  auto elementsAttr = mlir::ArrayAttr::get(&getMLIRContext(), elements);
  return builder.getConstArray(elementsAttr, arrayTy);
}

bool CIRGenModule::supportsCOMDAT() const {
  return getTriple().supportsCOMDAT();
}

static bool shouldBeInCOMDAT(CIRGenModule &cgm, const Decl &d) {
  if (!cgm.supportsCOMDAT())
    return false;

  if (d.hasAttr<SelectAnyAttr>())
    return true;

  GVALinkage linkage;
  if (auto *vd = dyn_cast<VarDecl>(&d))
    linkage = cgm.getASTContext().GetGVALinkageForVariable(vd);
  else
    linkage =
        cgm.getASTContext().GetGVALinkageForFunction(cast<FunctionDecl>(&d));

  switch (linkage) {
  case clang::GVA_Internal:
  case clang::GVA_AvailableExternally:
  case clang::GVA_StrongExternal:
    return false;
  case clang::GVA_DiscardableODR:
  case clang::GVA_StrongODR:
    return true;
  }
  llvm_unreachable("No such linkage");
}

void CIRGenModule::maybeSetTrivialComdat(const Decl &d, mlir::Operation *op) {
  if (!shouldBeInCOMDAT(*this, d))
    return;
  if (auto globalOp = dyn_cast_or_null<cir::GlobalOp>(op)) {
    globalOp.setComdat(true);
  } else if (auto funcOp = dyn_cast_or_null<cir::FuncOp>(op)) {
    funcOp.setComdat(true);
  }
}

void CIRGenModule::updateCompletedType(const TagDecl *td) {
  // Make sure that this type is translated.
  genTypes.updateCompletedType(td);
}

void CIRGenModule::addReplacement(StringRef name, StringRef replacementName) {
  replacements[name] = std::string(replacementName);
}

void CIRGenModule::noteFunctionEmissionComplete(cir::FuncOp fn) {
  completedFunctionEmissionOrder[fn.getOperation()] =
      ++completedFunctionEmissionCount;
  indexFunctionSymbolUsers(fn);
}

void CIRGenModule::indexFunctionSymbolUsers(cir::FuncOp fn) {
  llvm::SmallDenseSet<mlir::StringAttr, 8> referencedSymbols;
  fn.walk([&](mlir::Operation *op) {
    if (auto call = mlir::dyn_cast<cir::CallOp>(op)) {
      if (auto callee = call.getCalleeAttr())
        referencedSymbols.insert(callee.getAttr());
    } else if (auto getGlobal = mlir::dyn_cast<cir::GetGlobalOp>(op)) {
      referencedSymbols.insert(getGlobal.getNameAttr().getAttr());
    }
  });

  if (referencedSymbols.empty())
    return;

  auto fnNameAttr = fn.getSymNameAttr();
  for (mlir::StringAttr symbolNameAttr : referencedSymbols)
    completedFunctionSymbolUserIndex[symbolNameAttr.getValue()].push_back(
        fnNameAttr);
}

void CIRGenModule::forEachPotentialFunctionUserOfSymbol(
    mlir::StringAttr targetNameAttr,
    llvm::function_ref<void(cir::FuncOp)> visitor) {
  llvm::SmallPtrSet<mlir::Operation *, 8> visitedFunctions;

  if (auto indexedIt = completedFunctionSymbolUserIndex.find(
          targetNameAttr.getValue());
      indexedIt != completedFunctionSymbolUserIndex.end()) {
    for (mlir::StringAttr fnNameAttr : indexedIt->second) {
      auto fn = mlir::dyn_cast_or_null<cir::FuncOp>(
          getGlobalValue(fnNameAttr.getValue()));
      if (!fn || fn.isDeclaration())
        continue;
      if (visitedFunctions.insert(fn.getOperation()).second)
        visitor(fn);
    }
  }

  // Also scan currently-emitting helper/current functions that have bodies but
  // have not reached finishFunction() yet, since their earlier symbol users
  // still need immediate repair.
  for (auto fn : theModule.getOps<cir::FuncOp>()) {
    if (fn.isDeclaration())
      continue;
    if (completedFunctionEmissionOrder.contains(fn.getOperation()))
      continue;
    if (visitedFunctions.insert(fn.getOperation()).second)
      visitor(fn);
  }
}

void CIRGenModule::collectTopLevelGetGlobalUsers(
    mlir::StringAttr targetNameAttr,
    llvm::SmallVectorImpl<cir::GetGlobalOp> &getGlobalUsers) {
  for (mlir::Operation &op : theModule.getBody()->getOperations()) {
    if (isa<cir::FuncOp>(op))
      continue;
    op.walk([&](cir::GetGlobalOp getGlobal) {
      if (getGlobal.getNameAttr().getAttr() == targetNameAttr)
        getGlobalUsers.push_back(getGlobal);
    });
  }
}

void CIRGenModule::retypeGetGlobalUsers(mlir::StringAttr oldName,
                                        cir::PointerType newGlobalPtrTy) {
  auto retypeOne = [&](cir::GetGlobalOp ggo) {
    if (ggo.getNameAttr().getAttr() != oldName)
      return;
    mlir::Value useOpResultValue = ggo.getAddr();
    mlir::Type oldPtrTy = useOpResultValue.getType();
    if (oldPtrTy == newGlobalPtrTy)
      return;
    useOpResultValue.setType(newGlobalPtrTy);

    mlir::OpBuilder::InsertionGuard guard(builder);
    builder.setInsertionPointAfter(ggo);
    mlir::Value cast =
        builder.createBitcast(ggo->getLoc(), useOpResultValue, oldPtrTy);
    useOpResultValue.replaceAllUsesExcept(cast, cast.getDefiningOp());
  };

  // Function users via the symbol-user index (completed + currently-emitting
  // functions), instead of walking every op in the module per descriptor.
  forEachPotentialFunctionUserOfSymbol(
      oldName, [&](cir::FuncOp fn) { fn.walk(retypeOne); });
  // Users in non-function top-level ops (e.g. global initializer regions).
  llvm::SmallVector<cir::GetGlobalOp> topLevelUsers;
  collectTopLevelGetGlobalUsers(oldName, topLevelUsers);
  for (cir::GetGlobalOp ggo : topLevelUsers)
    retypeOne(ggo);
}

void CIRGenModule::replacePointerTypeArgs(cir::FuncOp oldF, cir::FuncOp newF,
                                          ArrayRef<mlir::Operation *> users) {
  llvm::SmallVector<cir::CallOp, 4> callUsers;
  llvm::SmallVector<cir::GetGlobalOp, 4> getGlobalUsers;
  callUsers.reserve(users.size());
  getGlobalUsers.reserve(users.size());
  for (mlir::Operation *user : users) {
    if (auto call = mlir::dyn_cast<cir::CallOp>(user))
      callUsers.push_back(call);
    else if (auto getGlobal = mlir::dyn_cast<cir::GetGlobalOp>(user))
      getGlobalUsers.push_back(getGlobal);
  }
  replacePointerTypeArgs(oldF, newF, callUsers, getGlobalUsers);
}

void CIRGenModule::replacePointerTypeArgs(
    cir::FuncOp oldF, cir::FuncOp newF, ArrayRef<cir::CallOp> callUsers,
    ArrayRef<cir::GetGlobalOp> getGlobalUsers) {
  // Collect GetGlobalOp users first to avoid iterator invalidation.
  llvm::SmallVector<cir::GetGlobalOp, 4> getGlobalOps;
  auto newFuncTy = newF.getFunctionType();
  auto funcArgTypes = newFuncTy.getInputs();
  auto newRetTy = newFuncTy.getReturnType();
  auto newPtrTy = cir::PointerType::get(newFuncTy);

  for (auto call : callUsers) {
    // CallTryOp only shows up after FlattenCFG.
    auto argOps = call.getArgs();
    // Only fix argument types when argument counts match. If they differ
    // (e.g. placeholder void() upgraded to real type), the call will be
    // replaced entirely by replaceUsesOfNonProtoTypeWithRealFunction.
    if (argOps.size() != funcArgTypes.size())
      continue;
    for (unsigned i = 0; i < funcArgTypes.size(); i++) {
      if (argOps[i].getType() == funcArgTypes[i])
        continue;

      mlir::OpBuilder::InsertionGuard guard(builder);
      builder.setInsertionPoint(call);
      auto argPointerTy = mlir::dyn_cast<cir::PointerType>(argOps[i].getType());
      auto funcArgPointerTy = mlir::dyn_cast<cir::PointerType>(funcArgTypes[i]);
      if (argPointerTy && funcArgPointerTy) {
        auto castedArg =
            builder.createBitcast(call.getLoc(), argOps[i], funcArgPointerTy);
        call.setArg(i, castedArg);
        continue;
      }

      // Member function pointer args can also carry stale placeholder
      // FuncType payloads. Rewrite to callee param type.
      auto argMethodTy = mlir::dyn_cast<cir::MethodType>(argOps[i].getType());
      auto funcArgMethodTy = mlir::dyn_cast<cir::MethodType>(funcArgTypes[i]);
      if (argMethodTy && funcArgMethodTy) {
        auto castedArg =
            builder.createBitcast(call.getLoc(), argOps[i], funcArgMethodTy);
        call.setArg(i, castedArg);
        continue;
      }
    }

    // Fix return type mismatch. When the callee's return type changes
    // (e.g., ptr<void> → ptr<s8i> when __builtin_memchr and
    // __builtin_char_memchr share the same @memchr symbol), existing
    // calls have the old return type. Update the call's result type and
    // insert a bitcast for users that expect the old type.
    if (call.getNumResults() > 0) {
      auto oldRetTy = call.getResult().getType();
      if (oldRetTy != newRetTy && mlir::isa<cir::PointerType>(oldRetTy) &&
          mlir::isa<cir::PointerType>(newRetTy)) {
        // Update the call's result type to match the new callee.
        call.getResult().setType(newRetTy);

        // Insert a bitcast after the call for users that expect the old type.
        if (!call.getResult().use_empty()) {
          mlir::OpBuilder::InsertionGuard guard(builder);
          builder.setInsertionPointAfter(call);
          auto castedResult =
              builder.createBitcast(call.getLoc(), call.getResult(), oldRetTy);
          call.getResult().replaceAllUsesExcept(castedResult,
                                                castedResult.getDefiningOp());
        }
      }
    }
  }

  // Handle GetGlobalOp: when the referenced function is replaced with one
  // that has a different type (e.g. destructor aliasing from Derived to
  // Base), the GetGlobalOp result type must be updated to match.
  for (auto getGlobal : getGlobalUsers) {
    if (getGlobal.getAddr().getType() != newPtrTy)
      getGlobalOps.push_back(getGlobal);
  }

  // Fix GetGlobalOp users: replace each with a new GetGlobalOp that has the
  // correct type for the replacement function, then bitcast to the old type
  // so downstream users (typically a bitcast to void*) remain valid.
  for (auto getGlobal : getGlobalOps) {
    auto oldPtrTy = getGlobal.getAddr().getType();

    mlir::OpBuilder::InsertionGuard guard(builder);
    builder.setInsertionPoint(getGlobal);

    // Create a new GetGlobalOp with the replacement function's symbol and type.
    auto newGetGlobal = cir::GetGlobalOp::create(
        builder, getGlobal.getLoc(), newPtrTy, newF.getSymName());

    // Bitcast from new type to old type for users that expect the old type.
    auto bitcast = builder.createBitcast(getGlobal.getLoc(),
                                         newGetGlobal.getAddr(), oldPtrTy);

    getGlobal.getAddr().replaceAllUsesWith(bitcast);
    getGlobal->erase();
  }
}

void CIRGenModule::applyReplacements() {
  if (replacements.empty())
    return;

  // Resolve all replacement chains and collect valid replacement pairs.
  // Also build a name map for the bulk symbol replacement pass.
  struct ReplacementPair {
    cir::FuncOp oldF;
    cir::FuncOp newF;
  };
  llvm::SmallVector<ReplacementPair> validReplacements;
  llvm::DenseMap<mlir::StringAttr, mlir::StringAttr> nameMap;
  llvm::DenseMap<StringRef, StringRef> resolvedReplacementCache;
  validReplacements.reserve(replacements.size());
  nameMap.reserve(replacements.size());
  resolvedReplacementCache.reserve(replacements.size());

  for (auto &i : replacements) {
    StringRef mangledName = i.getKey();
    StringRef replacementName = i.getValue();
    mlir::Operation *entry = getGlobalValue(mangledName);
    if (!entry)
      continue;

    StringRef finalReplacement = replacementName;
    auto resolvedIt = resolvedReplacementCache.find(replacementName);
    if (resolvedIt != resolvedReplacementCache.end()) {
      finalReplacement = resolvedIt->second;
    } else {
      // Follow transitive replacement chains.
      llvm::SmallPtrSet<const void *, 8> visited;
      llvm::SmallVector<StringRef, 8> chainNodes;
      while (true) {
        auto it = replacements.find(finalReplacement);
        if (it == replacements.end())
          break;
        // Record only newly visited nodes to avoid redundant cache writes
        // when replacement chains contain cycles.
        if (!visited.insert(it->getKey().data()).second)
          break; // cycle detection
        chainNodes.push_back(it->getKey());
        finalReplacement = it->getValue();
      }
      resolvedReplacementCache[replacementName] = finalReplacement;
      for (StringRef node : chainNodes)
        resolvedReplacementCache[node] = finalReplacement;
    }

    mlir::Operation *replacement = getGlobalValue(finalReplacement);
    if (!replacement)
      continue;

    auto oldF = dyn_cast<cir::FuncOp>(entry);
    auto newF = dyn_cast<cir::FuncOp>(replacement);

    if (!oldF || !newF) {
      errorNYI(replacement->getLoc(),
               "applyReplacements: non-function replacement");
      continue;
    }

    auto oldNameAttr = oldF.getSymNameAttr();
    nameMap[oldNameAttr] = newF.getSymNameAttr();
    validReplacements.push_back({oldF, newF});
  }

  llvm::SmallVector<ReplacementPair, 8> mismatchedReplacements;
  mismatchedReplacements.reserve(validReplacements.size());
  for (auto &rp : validReplacements) {
    if (rp.oldF.getFunctionType() != rp.newF.getFunctionType())
      mismatchedReplacements.push_back(rp);
  }

  // Build a symbol use map for replacement source symbols only when we need
  // pointer type fixups. Pure same-signature replacements only need symbol
  // rewrites, so skip this full walk entirely.
  struct SymbolUseBuckets {
    llvm::SmallVector<cir::CallOp, 4> callUsers;
    llvm::SmallVector<cir::GetGlobalOp, 4> getGlobalUsers;
  };

  // Collect all replacement target symbols and do a SINGLE module walk to
  // find all call/get_global users. This replaces per-symbol getSymbolUses()
  // calls, each of which walks the entire module's attributes — O(M*N) for
  // M replacements and N ops, causing 95M+ DenseMap allocation grows on
  // template-heavy C++ files like MoQSession.cpp.
  if (!mismatchedReplacements.empty()) {
    if (mismatchedReplacements.size() == 1) {
      // Common case: one mismatched replacement. Use a direct walk over
      // CallOp/GetGlobalOp instead of getSymbolUses() which materializes
      // full DictionaryAttrs and creates AttrTypeWalker instances per op.
      SymbolUseBuckets users;
      auto &rp = mismatchedReplacements.front();
      auto targetNameAttr = rp.oldF.getSymNameAttr();
      for (auto fn : theModule.getOps<cir::FuncOp>()) {
        if (fn.isDeclaration())
          continue;
        fn.walk([&](mlir::Operation *op) {
          if (auto call = mlir::dyn_cast<cir::CallOp>(op)) {
            if (auto callee = call.getCalleeAttr();
                callee && callee.getAttr() == targetNameAttr)
              users.callUsers.push_back(call);
          } else if (auto getGlobal = mlir::dyn_cast<cir::GetGlobalOp>(op)) {
            if (getGlobal.getNameAttr().getAttr() == targetNameAttr)
              users.getGlobalUsers.push_back(getGlobal);
          }
        });
      }
      if (!users.callUsers.empty() || !users.getGlobalUsers.empty()) {
        replacePointerTypeArgs(rp.oldF, rp.newF, users.callUsers,
                               users.getGlobalUsers);
      }
    } else if (mismatchedReplacements.size() <= 3) {
      llvm::SmallVector<SymbolUseBuckets, 3> replacementUsers;
      llvm::SmallVector<unsigned, 3> nonEmptyReplacementIndices;
      llvm::SmallVector<bool, 3> replacementHasUsers;
      replacementUsers.resize(mismatchedReplacements.size());
      nonEmptyReplacementIndices.reserve(mismatchedReplacements.size());
      replacementHasUsers.assign(mismatchedReplacements.size(), false);
      auto sym0 = mismatchedReplacements[0].oldF.getSymNameAttr();
      auto sym1 = mismatchedReplacements[1].oldF.getSymNameAttr();
      mlir::StringAttr sym2 = nullptr;
      bool hasThird = mismatchedReplacements.size() == 3;
      if (hasThird)
        sym2 = mismatchedReplacements[2].oldF.getSymNameAttr();
      // All symbols need direct walk resolution — skip getSymbolUses()
      // which materializes full DictionaryAttrs per op.
      bool missing0 = true;
      bool missing1 = true;
      bool missing2 = hasThird;

      auto markBucketNonEmpty = [&](unsigned idx) {
        if (!replacementHasUsers[idx]) {
          replacementHasUsers[idx] = true;
          nonEmptyReplacementIndices.push_back(idx);
        }
      };

      if (missing0 || missing1 || missing2) {
        unsigned missingCount = static_cast<unsigned>(missing0) +
                                static_cast<unsigned>(missing1) +
                                static_cast<unsigned>(missing2);
        unsigned singleMissingIdx = 0;
        mlir::StringAttr singleMissingSym = nullptr;
        if (missingCount == 1) {
          if (missing0) {
            singleMissingIdx = 0;
            singleMissingSym = sym0;
          } else if (missing1) {
            singleMissingIdx = 1;
            singleMissingSym = sym1;
          } else {
            singleMissingIdx = 2;
            singleMissingSym = sym2;
          }
        }

        // Cache most-recent symbol lookups across the full module walk.
        mlir::StringAttr lastCallCalleeAttr;
        bool lastCallCalleeIsReplacement = false;
        unsigned lastCallReplacementIdx = 0;
        mlir::StringAttr lastGetGlobalNameAttr;
        bool lastGetGlobalIsReplacement = false;
        unsigned lastGetGlobalReplacementIdx = 0;
        unsigned missingPairIdx0 = 0;
        unsigned missingPairIdx1 = 0;
        mlir::StringAttr missingPairSym0 = nullptr;
        mlir::StringAttr missingPairSym1 = nullptr;
        if (missingCount == 2) {
          if (missing0) {
            missingPairIdx0 = 0;
            missingPairSym0 = sym0;
            if (missing1) {
              missingPairIdx1 = 1;
              missingPairSym1 = sym1;
            } else {
              missingPairIdx1 = 2;
              missingPairSym1 = sym2;
            }
          } else {
            missingPairIdx0 = 1;
            missingPairSym0 = sym1;
            missingPairIdx1 = 2;
            missingPairSym1 = sym2;
          }
        }
        for (auto fn : theModule.getOps<cir::FuncOp>()) {
          if (fn.isDeclaration())
            continue;
          if (missingCount == 1) {
            fn.walk([&](mlir::Operation *op) {
              if (auto call = mlir::dyn_cast<cir::CallOp>(op)) {
                if (auto callee = call.getCalleeAttr()) {
                  auto calleeAttr = callee.getAttr();
                  if (calleeAttr == lastCallCalleeAttr) {
                    if (lastCallCalleeIsReplacement)
                      replacementUsers[lastCallReplacementIdx]
                          .callUsers.push_back(call);
                  } else {
                    lastCallCalleeAttr = calleeAttr;
                    if (calleeAttr == singleMissingSym) {
                      markBucketNonEmpty(singleMissingIdx);
                      lastCallCalleeIsReplacement = true;
                      lastCallReplacementIdx = singleMissingIdx;
                      replacementUsers[singleMissingIdx].callUsers.push_back(
                          call);
                    } else {
                      lastCallCalleeIsReplacement = false;
                    }
                  }
                }
              } else if (auto getGlobal =
                             mlir::dyn_cast<cir::GetGlobalOp>(op)) {
                auto nameAttr = getGlobal.getNameAttr().getAttr();
                if (nameAttr == lastGetGlobalNameAttr) {
                  if (lastGetGlobalIsReplacement)
                    replacementUsers[lastGetGlobalReplacementIdx]
                        .getGlobalUsers.push_back(getGlobal);
                } else {
                  lastGetGlobalNameAttr = nameAttr;
                  if (nameAttr == singleMissingSym) {
                    markBucketNonEmpty(singleMissingIdx);
                    lastGetGlobalIsReplacement = true;
                    lastGetGlobalReplacementIdx = singleMissingIdx;
                    replacementUsers[singleMissingIdx].getGlobalUsers.push_back(
                        getGlobal);
                  } else {
                    lastGetGlobalIsReplacement = false;
                  }
                }
              }
            });
          } else if (missingCount == 2) {
            fn.walk([&](mlir::Operation *op) {
              if (auto call = mlir::dyn_cast<cir::CallOp>(op)) {
                if (auto callee = call.getCalleeAttr()) {
                  auto calleeAttr = callee.getAttr();
                  if (calleeAttr == lastCallCalleeAttr) {
                    if (lastCallCalleeIsReplacement)
                      replacementUsers[lastCallReplacementIdx]
                          .callUsers.push_back(call);
                  } else {
                    lastCallCalleeAttr = calleeAttr;
                    lastCallCalleeIsReplacement = false;
                    if (calleeAttr == missingPairSym0) {
                      markBucketNonEmpty(missingPairIdx0);
                      lastCallCalleeIsReplacement = true;
                      lastCallReplacementIdx = missingPairIdx0;
                      replacementUsers[missingPairIdx0].callUsers.push_back(
                          call);
                    } else if (calleeAttr == missingPairSym1) {
                      markBucketNonEmpty(missingPairIdx1);
                      lastCallCalleeIsReplacement = true;
                      lastCallReplacementIdx = missingPairIdx1;
                      replacementUsers[missingPairIdx1].callUsers.push_back(
                          call);
                    }
                  }
                }
              } else if (auto getGlobal =
                             mlir::dyn_cast<cir::GetGlobalOp>(op)) {
                auto nameAttr = getGlobal.getNameAttr().getAttr();
                if (nameAttr == lastGetGlobalNameAttr) {
                  if (lastGetGlobalIsReplacement)
                    replacementUsers[lastGetGlobalReplacementIdx]
                        .getGlobalUsers.push_back(getGlobal);
                } else {
                  lastGetGlobalNameAttr = nameAttr;
                  lastGetGlobalIsReplacement = false;
                  if (nameAttr == missingPairSym0) {
                    markBucketNonEmpty(missingPairIdx0);
                    lastGetGlobalIsReplacement = true;
                    lastGetGlobalReplacementIdx = missingPairIdx0;
                    replacementUsers[missingPairIdx0].getGlobalUsers.push_back(
                        getGlobal);
                  } else if (nameAttr == missingPairSym1) {
                    markBucketNonEmpty(missingPairIdx1);
                    lastGetGlobalIsReplacement = true;
                    lastGetGlobalReplacementIdx = missingPairIdx1;
                    replacementUsers[missingPairIdx1].getGlobalUsers.push_back(
                        getGlobal);
                  }
                }
              }
            });
          } else {
            fn.walk([&](mlir::Operation *op) {
              if (auto call = mlir::dyn_cast<cir::CallOp>(op)) {
                if (auto callee = call.getCalleeAttr()) {
                  auto calleeAttr = callee.getAttr();
                  if (calleeAttr == lastCallCalleeAttr) {
                    if (lastCallCalleeIsReplacement)
                      replacementUsers[lastCallReplacementIdx]
                          .callUsers.push_back(call);
                  } else {
                    lastCallCalleeAttr = calleeAttr;
                    lastCallCalleeIsReplacement = false;
                    if (missing0 && calleeAttr == sym0) {
                      markBucketNonEmpty(0);
                      lastCallCalleeIsReplacement = true;
                      lastCallReplacementIdx = 0;
                      replacementUsers[0].callUsers.push_back(call);
                    } else if (missing1 && calleeAttr == sym1) {
                      markBucketNonEmpty(1);
                      lastCallCalleeIsReplacement = true;
                      lastCallReplacementIdx = 1;
                      replacementUsers[1].callUsers.push_back(call);
                    } else if (missing2 && calleeAttr == sym2) {
                      markBucketNonEmpty(2);
                      lastCallCalleeIsReplacement = true;
                      lastCallReplacementIdx = 2;
                      replacementUsers[2].callUsers.push_back(call);
                    }
                  }
                }
              } else if (auto getGlobal =
                             mlir::dyn_cast<cir::GetGlobalOp>(op)) {
                auto nameAttr = getGlobal.getNameAttr().getAttr();
                if (nameAttr == lastGetGlobalNameAttr) {
                  if (lastGetGlobalIsReplacement)
                    replacementUsers[lastGetGlobalReplacementIdx]
                        .getGlobalUsers.push_back(getGlobal);
                } else {
                  lastGetGlobalNameAttr = nameAttr;
                  lastGetGlobalIsReplacement = false;
                  if (missing0 && nameAttr == sym0) {
                    markBucketNonEmpty(0);
                    lastGetGlobalIsReplacement = true;
                    lastGetGlobalReplacementIdx = 0;
                    replacementUsers[0].getGlobalUsers.push_back(getGlobal);
                  } else if (missing1 && nameAttr == sym1) {
                    markBucketNonEmpty(1);
                    lastGetGlobalIsReplacement = true;
                    lastGetGlobalReplacementIdx = 1;
                    replacementUsers[1].getGlobalUsers.push_back(getGlobal);
                  } else if (missing2 && nameAttr == sym2) {
                    markBucketNonEmpty(2);
                    lastGetGlobalIsReplacement = true;
                    lastGetGlobalReplacementIdx = 2;
                    replacementUsers[2].getGlobalUsers.push_back(getGlobal);
                  }
                }
              }
            });
          }
        }
      }

      for (unsigned idx : nonEmptyReplacementIndices) {
        auto &users = replacementUsers[idx];
        auto &rp = mismatchedReplacements[idx];
        replacePointerTypeArgs(rp.oldF, rp.newF, users.callUsers,
                               users.getGlobalUsers);
      }
    } else {
      llvm::SmallVector<SymbolUseBuckets, 8> replacementUsers;
      llvm::SmallVector<unsigned, 8> nonEmptyReplacementIndices;
      llvm::SmallVector<bool, 8> replacementHasUsers;
      llvm::DenseMap<mlir::StringAttr, unsigned> unresolvedIndexByName;
      replacementUsers.resize(mismatchedReplacements.size());
      nonEmptyReplacementIndices.reserve(mismatchedReplacements.size());
      replacementHasUsers.resize(mismatchedReplacements.size(), false);
      unresolvedIndexByName.reserve(mismatchedReplacements.size());
      auto markReplacementHasUsers = [&](unsigned idx) {
        if (!replacementHasUsers[idx]) {
          replacementHasUsers[idx] = true;
          nonEmptyReplacementIndices.push_back(idx);
        }
      };
      for (unsigned idx = 0; idx < mismatchedReplacements.size(); ++idx) {
        auto &rp = mismatchedReplacements[idx];
        auto oldNameAttr = rp.oldF.getSymNameAttr();
        // Skip getSymbolUses() which materializes full DictionaryAttrs per op.
        // Always use the direct walk via unresolvedIndexByName.
        unresolvedIndexByName[oldNameAttr] = idx;
      }

      if (!unresolvedIndexByName.empty()) {
        // Cache most-recent symbol lookups across the fallback module walk.
        // Calls to helper wrappers around the same replacement target
        // frequently cluster across neighboring functions in template-heavy
        // TUs.
        mlir::StringAttr lastCallCalleeAttr;
        bool lastCallCalleeIsReplacement = false;
        unsigned lastCallReplacementIdx = 0;
        mlir::StringAttr lastGetGlobalNameAttr;
        bool lastGetGlobalIsReplacement = false;
        unsigned lastGetGlobalReplacementIdx = 0;
        bool useSingleUnresolvedDispatch = unresolvedIndexByName.size() == 1;
        bool useTwoUnresolvedDispatch = unresolvedIndexByName.size() == 2;
        bool useThreeUnresolvedDispatch = unresolvedIndexByName.size() == 3;
        mlir::StringAttr unresolvedSym0;
        mlir::StringAttr unresolvedSym1;
        mlir::StringAttr unresolvedSym2;
        unsigned unresolvedIdx0 = 0;
        unsigned unresolvedIdx1 = 0;
        unsigned unresolvedIdx2 = 0;
        if (useThreeUnresolvedDispatch) {
          unsigned unresolvedCount = 0;
          for (auto &entry : unresolvedIndexByName) {
            if (unresolvedCount == 0) {
              unresolvedSym0 = entry.first;
              unresolvedIdx0 = entry.second;
            } else if (unresolvedCount == 1) {
              unresolvedSym1 = entry.first;
              unresolvedIdx1 = entry.second;
            } else {
              unresolvedSym2 = entry.first;
              unresolvedIdx2 = entry.second;
            }
            ++unresolvedCount;
          }
        }
        for (auto fn : theModule.getOps<cir::FuncOp>()) {
          if (fn.isDeclaration())
            continue;
          fn.walk([&](mlir::Operation *op) {
            if (auto call = mlir::dyn_cast<cir::CallOp>(op)) {
              if (auto callee = call.getCalleeAttr()) {
                auto calleeAttr = callee.getAttr();
                if (calleeAttr == lastCallCalleeAttr) {
                  if (lastCallCalleeIsReplacement)
                    replacementUsers[lastCallReplacementIdx].callUsers.push_back(
                        call);
                } else {
                  lastCallCalleeAttr = calleeAttr;
                  bool matched = false;
                  if (useSingleUnresolvedDispatch) {
                    if (calleeAttr == unresolvedSym0) {
                      lastCallCalleeIsReplacement = true;
                      lastCallReplacementIdx = unresolvedIdx0;
                      matched = true;
                    }
                  } else if (useTwoUnresolvedDispatch) {
                    if (calleeAttr == unresolvedSym0) {
                      lastCallCalleeIsReplacement = true;
                      lastCallReplacementIdx = unresolvedIdx0;
                      matched = true;
                    } else if (calleeAttr == unresolvedSym1) {
                      lastCallCalleeIsReplacement = true;
                      lastCallReplacementIdx = unresolvedIdx1;
                      matched = true;
                    }
                  } else if (useThreeUnresolvedDispatch) {
                    if (calleeAttr == unresolvedSym0) {
                      lastCallCalleeIsReplacement = true;
                      lastCallReplacementIdx = unresolvedIdx0;
                      matched = true;
                    } else if (calleeAttr == unresolvedSym1) {
                      lastCallCalleeIsReplacement = true;
                      lastCallReplacementIdx = unresolvedIdx1;
                      matched = true;
                    } else if (calleeAttr == unresolvedSym2) {
                      lastCallCalleeIsReplacement = true;
                      lastCallReplacementIdx = unresolvedIdx2;
                      matched = true;
                    }
                  } else if (auto replacementIt =
                                 unresolvedIndexByName.find(calleeAttr);
                             replacementIt != unresolvedIndexByName.end()) {
                    lastCallCalleeIsReplacement = true;
                    lastCallReplacementIdx = replacementIt->second;
                    matched = true;
                  }
                  if (matched) {
                    markReplacementHasUsers(lastCallReplacementIdx);
                    replacementUsers[lastCallReplacementIdx].callUsers.push_back(
                        call);
                  } else {
                    lastCallCalleeIsReplacement = false;
                  }
                }
              }
            } else if (auto getGlobal = mlir::dyn_cast<cir::GetGlobalOp>(op)) {
              auto nameAttr = getGlobal.getNameAttr().getAttr();
              if (nameAttr == lastGetGlobalNameAttr) {
                if (lastGetGlobalIsReplacement)
                  replacementUsers[lastGetGlobalReplacementIdx]
                      .getGlobalUsers.push_back(getGlobal);
              } else {
                lastGetGlobalNameAttr = nameAttr;
                bool matched = false;
                if (useSingleUnresolvedDispatch) {
                  if (nameAttr == unresolvedSym0) {
                    lastGetGlobalIsReplacement = true;
                    lastGetGlobalReplacementIdx = unresolvedIdx0;
                    matched = true;
                  }
                } else if (useTwoUnresolvedDispatch) {
                  if (nameAttr == unresolvedSym0) {
                    lastGetGlobalIsReplacement = true;
                    lastGetGlobalReplacementIdx = unresolvedIdx0;
                    matched = true;
                  } else if (nameAttr == unresolvedSym1) {
                    lastGetGlobalIsReplacement = true;
                    lastGetGlobalReplacementIdx = unresolvedIdx1;
                    matched = true;
                  }
                } else if (useThreeUnresolvedDispatch) {
                  if (nameAttr == unresolvedSym0) {
                    lastGetGlobalIsReplacement = true;
                    lastGetGlobalReplacementIdx = unresolvedIdx0;
                    matched = true;
                  } else if (nameAttr == unresolvedSym1) {
                    lastGetGlobalIsReplacement = true;
                    lastGetGlobalReplacementIdx = unresolvedIdx1;
                    matched = true;
                  } else if (nameAttr == unresolvedSym2) {
                    lastGetGlobalIsReplacement = true;
                    lastGetGlobalReplacementIdx = unresolvedIdx2;
                    matched = true;
                  }
                } else if (auto replacementIt =
                               unresolvedIndexByName.find(nameAttr);
                           replacementIt != unresolvedIndexByName.end()) {
                  lastGetGlobalIsReplacement = true;
                  lastGetGlobalReplacementIdx = replacementIt->second;
                  matched = true;
                }
                if (matched) {
                  markReplacementHasUsers(lastGetGlobalReplacementIdx);
                  replacementUsers[lastGetGlobalReplacementIdx]
                      .getGlobalUsers.push_back(getGlobal);
                } else {
                  lastGetGlobalIsReplacement = false;
                }
              }
            }
          });
        }
      }

      for (unsigned idx : nonEmptyReplacementIndices) {
        auto &users = replacementUsers[idx];
        auto &rp = mismatchedReplacements[idx];
        replacePointerTypeArgs(rp.oldF, rp.newF, users.callUsers,
                               users.getGlobalUsers);
      }
    }
  }


  // Bulk-replace ALL symbol references in a single module walk using
  // AttrTypeReplacer. This handles CallOp callees, GetGlobalOp names,
  // FuncOp aliasee attrs, cir.throw dtor refs, vtable entries, and any
  // other op attribute containing a FlatSymbolRefAttr. Total cost: O(N)
  // instead of O(R*N) from per-replacement replaceAllSymbolUses.
  if (!nameMap.empty()) {
    mlir::AttrTypeReplacer replacer;
    if (nameMap.size() == 1) {
      auto only = *nameMap.begin();
      mlir::StringAttr onlyRootRef = only.first;
      mlir::StringAttr onlyReplacementRef = only.second;
      mlir::StringAttr lastRootRef;
      bool lastRootWasReplaced = false;
      replacer.addReplacement(
          [&](mlir::SymbolRefAttr attr)
              -> std::optional<std::pair<mlir::Attribute, mlir::WalkResult>> {
            if (!attr.getNestedReferences().empty())
              return std::nullopt; // only handle flat (non-nested) symbol refs
            auto rootRef = attr.getRootReference();
            if (rootRef == lastRootRef) {
              if (lastRootWasReplaced)
                return std::make_pair(
                    mlir::Attribute(
                        mlir::FlatSymbolRefAttr::get(onlyReplacementRef)),
                    mlir::WalkResult::skip());
              return std::nullopt;
            }
            lastRootRef = rootRef;
            if (rootRef == onlyRootRef) {
              lastRootWasReplaced = true;
              return std::make_pair(
                  mlir::Attribute(
                      mlir::FlatSymbolRefAttr::get(onlyReplacementRef)),
                  mlir::WalkResult::skip());
            }
            lastRootWasReplaced = false;
            return std::nullopt;
          });
      // Invoke the lambda while its by-reference captures (lastRootRef,
      // lastRootWasReplaced, onlyRootRef, onlyReplacementRef) are still in
      // scope. See the matching note above the orphan call site that used to
      // live below the if/else chain.
      replacer.recursivelyReplaceElementsIn(theModule);
    } else if (nameMap.size() == 2) {
      auto it0 = nameMap.begin();
      auto it1 = std::next(it0);
      mlir::StringAttr rootRef0 = it0->first;
      mlir::StringAttr replacementRef0 = it0->second;
      mlir::StringAttr rootRef1 = it1->first;
      mlir::StringAttr replacementRef1 = it1->second;
      mlir::StringAttr lastRootRef;
      mlir::StringAttr lastReplacementRef;
      bool lastRootWasReplaced = false;
      replacer.addReplacement(
          [&](mlir::SymbolRefAttr attr)
              -> std::optional<std::pair<mlir::Attribute, mlir::WalkResult>> {
            if (!attr.getNestedReferences().empty())
              return std::nullopt; // only handle flat (non-nested) symbol refs
            auto rootRef = attr.getRootReference();
            if (rootRef == lastRootRef) {
              if (lastRootWasReplaced)
                return std::make_pair(
                    mlir::Attribute(
                        mlir::FlatSymbolRefAttr::get(lastReplacementRef)),
                    mlir::WalkResult::skip());
              return std::nullopt;
            }
            lastRootRef = rootRef;
            if (rootRef == rootRef0) {
              lastRootWasReplaced = true;
              lastReplacementRef = replacementRef0;
              return std::make_pair(
                  mlir::Attribute(
                      mlir::FlatSymbolRefAttr::get(replacementRef0)),
                  mlir::WalkResult::skip());
            }
            if (rootRef == rootRef1) {
              lastRootWasReplaced = true;
              lastReplacementRef = replacementRef1;
              return std::make_pair(
                  mlir::Attribute(
                      mlir::FlatSymbolRefAttr::get(replacementRef1)),
                  mlir::WalkResult::skip());
            }
            lastRootWasReplaced = false;
            return std::nullopt;
          });
      // Invoke while captures are still live — see note above branch 1.
      replacer.recursivelyReplaceElementsIn(theModule);
    } else if (nameMap.size() == 3) {
      auto it0 = nameMap.begin();
      auto it1 = std::next(it0);
      auto it2 = std::next(it1);
      mlir::StringAttr rootRef0 = it0->first;
      mlir::StringAttr replacementRef0 = it0->second;
      mlir::StringAttr rootRef1 = it1->first;
      mlir::StringAttr replacementRef1 = it1->second;
      mlir::StringAttr rootRef2 = it2->first;
      mlir::StringAttr replacementRef2 = it2->second;
      mlir::StringAttr lastRootRef;
      mlir::StringAttr lastReplacementRef;
      bool lastRootWasReplaced = false;
      replacer.addReplacement(
          [&](mlir::SymbolRefAttr attr)
              -> std::optional<std::pair<mlir::Attribute, mlir::WalkResult>> {
            if (!attr.getNestedReferences().empty())
              return std::nullopt; // only handle flat (non-nested) symbol refs
            auto rootRef = attr.getRootReference();
            if (rootRef == lastRootRef) {
              if (lastRootWasReplaced)
                return std::make_pair(
                    mlir::Attribute(
                        mlir::FlatSymbolRefAttr::get(lastReplacementRef)),
                    mlir::WalkResult::skip());
              return std::nullopt;
            }
            lastRootRef = rootRef;
            if (rootRef == rootRef0) {
              lastRootWasReplaced = true;
              lastReplacementRef = replacementRef0;
              return std::make_pair(
                  mlir::Attribute(
                      mlir::FlatSymbolRefAttr::get(replacementRef0)),
                  mlir::WalkResult::skip());
            }
            if (rootRef == rootRef1) {
              lastRootWasReplaced = true;
              lastReplacementRef = replacementRef1;
              return std::make_pair(
                  mlir::Attribute(
                      mlir::FlatSymbolRefAttr::get(replacementRef1)),
                  mlir::WalkResult::skip());
            }
            if (rootRef == rootRef2) {
              lastRootWasReplaced = true;
              lastReplacementRef = replacementRef2;
              return std::make_pair(
                  mlir::Attribute(
                      mlir::FlatSymbolRefAttr::get(replacementRef2)),
                  mlir::WalkResult::skip());
            }
            lastRootWasReplaced = false;
            return std::nullopt;
          });
      // Invoke while captures are still live — see note above branch 1.
      replacer.recursivelyReplaceElementsIn(theModule);
    } else if (nameMap.size() == 4) {
      auto it0 = nameMap.begin();
      auto it1 = std::next(it0);
      auto it2 = std::next(it1);
      auto it3 = std::next(it2);
      mlir::StringAttr rootRef0 = it0->first;
      mlir::StringAttr replacementRef0 = it0->second;
      mlir::StringAttr rootRef1 = it1->first;
      mlir::StringAttr replacementRef1 = it1->second;
      mlir::StringAttr rootRef2 = it2->first;
      mlir::StringAttr replacementRef2 = it2->second;
      mlir::StringAttr rootRef3 = it3->first;
      mlir::StringAttr replacementRef3 = it3->second;
      mlir::StringAttr lastRootRef;
      mlir::StringAttr lastReplacementRef;
      bool lastRootWasReplaced = false;
      replacer.addReplacement(
          [&](mlir::SymbolRefAttr attr)
              -> std::optional<std::pair<mlir::Attribute, mlir::WalkResult>> {
            if (!attr.getNestedReferences().empty())
              return std::nullopt; // only handle flat (non-nested) symbol refs
            auto rootRef = attr.getRootReference();
            if (rootRef == lastRootRef) {
              if (lastRootWasReplaced)
                return std::make_pair(
                    mlir::Attribute(
                        mlir::FlatSymbolRefAttr::get(lastReplacementRef)),
                    mlir::WalkResult::skip());
              return std::nullopt;
            }
            lastRootRef = rootRef;
            if (rootRef == rootRef0) {
              lastRootWasReplaced = true;
              lastReplacementRef = replacementRef0;
              return std::make_pair(
                  mlir::Attribute(
                      mlir::FlatSymbolRefAttr::get(replacementRef0)),
                  mlir::WalkResult::skip());
            }
            if (rootRef == rootRef1) {
              lastRootWasReplaced = true;
              lastReplacementRef = replacementRef1;
              return std::make_pair(
                  mlir::Attribute(
                      mlir::FlatSymbolRefAttr::get(replacementRef1)),
                  mlir::WalkResult::skip());
            }
            if (rootRef == rootRef2) {
              lastRootWasReplaced = true;
              lastReplacementRef = replacementRef2;
              return std::make_pair(
                  mlir::Attribute(
                      mlir::FlatSymbolRefAttr::get(replacementRef2)),
                  mlir::WalkResult::skip());
            }
            if (rootRef == rootRef3) {
              lastRootWasReplaced = true;
              lastReplacementRef = replacementRef3;
              return std::make_pair(
                  mlir::Attribute(
                      mlir::FlatSymbolRefAttr::get(replacementRef3)),
                  mlir::WalkResult::skip());
            }
            lastRootWasReplaced = false;
            return std::nullopt;
          });
      // Invoke while captures are still live — see note above branch 1.
      replacer.recursivelyReplaceElementsIn(theModule);
    } else {
      mlir::StringAttr lastRootRef0;
      mlir::StringAttr lastReplacementRef0;
      bool lastRootWasReplaced0 = false;
      mlir::StringAttr lastRootRef1;
      mlir::StringAttr lastReplacementRef1;
      bool lastRootWasReplaced1 = false;
      replacer.addReplacement(
          [&](mlir::SymbolRefAttr attr)
              -> std::optional<std::pair<mlir::Attribute, mlir::WalkResult>> {
            if (!attr.getNestedReferences().empty())
              return std::nullopt; // only handle flat (non-nested) symbol refs
            auto rootRef = attr.getRootReference();
            if (rootRef == lastRootRef0) {
              if (lastRootWasReplaced0)
                return std::make_pair(
                    mlir::Attribute(
                        mlir::FlatSymbolRefAttr::get(lastReplacementRef0)),
                    mlir::WalkResult::skip());
              return std::nullopt;
            }
            if (rootRef == lastRootRef1) {
              if (lastRootWasReplaced1)
                return std::make_pair(
                    mlir::Attribute(
                        mlir::FlatSymbolRefAttr::get(lastReplacementRef1)),
                    mlir::WalkResult::skip());
              return std::nullopt;
            }

            auto it = nameMap.find(rootRef);
            if (it != nameMap.end()) {
              lastRootRef1 = lastRootRef0;
              lastReplacementRef1 = lastReplacementRef0;
              lastRootWasReplaced1 = lastRootWasReplaced0;
              lastRootRef0 = rootRef;
              lastRootWasReplaced0 = true;
              lastReplacementRef0 = it->second;
              return std::make_pair(
                  mlir::Attribute(mlir::FlatSymbolRefAttr::get(it->second)),
                  mlir::WalkResult::skip());
            }
            lastRootRef1 = lastRootRef0;
            lastReplacementRef1 = lastReplacementRef0;
            lastRootWasReplaced1 = lastRootWasReplaced0;
            lastRootRef0 = rootRef;
            lastRootWasReplaced0 = false;
            return std::nullopt;
          });
      // Invoke while captures are still live — see note above branch 1.
      replacer.recursivelyReplaceElementsIn(theModule);
    }
  }

  // Erase old functions and update caches.
  for (auto &rp : validReplacements) {
    rp.newF->moveBefore(rp.oldF);
    eraseGlobalSymbol(rp.oldF);
    rp.oldF.erase();
  }
}

cir::GlobalOp CIRGenModule::createOrReplaceCXXRuntimeVariable(
    mlir::Location loc, StringRef name, mlir::Type ty,
    cir::GlobalLinkageKind linkage, clang::CharUnits alignment,
    mlir::ptr::MemorySpaceAttrInterface addrSpace) {
  auto gv = mlir::dyn_cast_or_null<cir::GlobalOp>(getGlobalValue(name));

  if (gv) {
    // Check if the variable has the right type.
    if (gv.getSymType() == ty && gv.getAddrSpaceAttr() == addrSpace)
      return gv;

    // Because of C++ name mangling, the only way we can end up with an already
    // existing global with the same name is if it has been declared extern
    // "C".
    assert(gv.isDeclaration() && "Declaration has wrong type!");

    // Replace the old declaration with a new variable of the correct type.
    // This mirrors OGCG's CreateOrReplaceCXXRuntimeVariable which creates
    // a new global, replaces uses, and erases the old one.
    eraseGlobalSymbol(gv);
    gv.erase();
  }

  // Create a new variable.
  gv = createGlobalOp(*this, loc, name, ty, /*isConstant=*/true, addrSpace);

  // Set up extra information and add to the module
  gv.setLinkageAttr(
      cir::GlobalLinkageKindAttr::get(&getMLIRContext(), linkage));
  mlir::SymbolTable::setSymbolVisibility(gv,
                                         CIRGenModule::getMLIRVisibility(gv));

  if (supportsCOMDAT() && cir::isWeakForLinker(linkage) &&
      !gv.hasAvailableExternallyLinkage()) {
    gv.setComdat(true);
  }

  // Only set explicit alignment when non-zero. Zero alignment means
  // "use default" and should be omitted to match OGCG behavior for
  // external declarations.
  if (!alignment.isZero())
    gv.setAlignmentAttr(getSize(alignment));
  setDSOLocal(static_cast<mlir::Operation *>(gv));
  return gv;
}

// TODO(CIR): this could be a common method between LLVM codegen.
static bool isVarDeclStrongDefinition(const ASTContext &astContext,
                                      CIRGenModule &cgm, const VarDecl *vd,
                                      bool noCommon) {
  // Don't give variables common linkage if -fno-common was specified unless it
  // was overridden by a NoCommon attribute.
  if ((noCommon || vd->hasAttr<NoCommonAttr>()) && !vd->hasAttr<CommonAttr>())
    return true;

  // C11 6.9.2/2:
  //   A declaration of an identifier for an object that has file scope without
  //   an initializer, and without a storage-class specifier or with the
  //   storage-class specifier static, constitutes a tentative definition.
  if (vd->getInit() || vd->hasExternalStorage())
    return true;

  // A variable cannot be both common and exist in a section.
  if (vd->hasAttr<SectionAttr>())
    return true;

  // A variable cannot be both common and exist in a section.
  // We don't try to determine which is the right section in the front-end.
  // If no specialized section name is applicable, it will resort to default.
  if (vd->hasAttr<PragmaClangBSSSectionAttr>() ||
      vd->hasAttr<PragmaClangDataSectionAttr>() ||
      vd->hasAttr<PragmaClangRelroSectionAttr>() ||
      vd->hasAttr<PragmaClangRodataSectionAttr>())
    return true;

  // Thread local vars aren't considered common linkage.
  if (vd->getTLSKind())
    return true;

  // Tentative definitions marked with WeakImportAttr are true definitions.
  if (vd->hasAttr<WeakImportAttr>())
    return true;

  // A variable cannot be both common and exist in a comdat.
  if (shouldBeInCOMDAT(cgm, *vd))
    return true;

  // Declarations with a required alignment do not have common linkage in MSVC
  // mode.
  if (astContext.getTargetInfo().getCXXABI().isMicrosoft()) {
    if (vd->hasAttr<AlignedAttr>())
      return true;
    QualType varType = vd->getType();
    if (astContext.isAlignmentRequired(varType))
      return true;

    if (const auto *rd = varType->getAsRecordDecl()) {
      for (const FieldDecl *fd : rd->fields()) {
        if (fd->isBitField())
          continue;
        if (fd->hasAttr<AlignedAttr>())
          return true;
        if (astContext.isAlignmentRequired(fd->getType()))
          return true;
      }
    }
  }

  // Microsoft's link.exe doesn't support alignments greater than 32 bytes for
  // common symbols, so symbols with greater alignment requirements cannot be
  // common.
  // Other COFF linkers (ld.bfd and LLD) support arbitrary power-of-two
  // alignments for common symbols via the aligncomm directive, so this
  // restriction only applies to MSVC environments.
  if (astContext.getTargetInfo().getTriple().isKnownWindowsMSVCEnvironment() &&
      astContext.getTypeAlignIfKnown(vd->getType()) >
          astContext.toBits(CharUnits::fromQuantity(32)))
    return true;

  return false;
}

cir::GlobalLinkageKind CIRGenModule::getCIRLinkageForDeclarator(
    const DeclaratorDecl *dd, GVALinkage linkage, bool isConstantVariable) {
  if (linkage == GVA_Internal)
    return cir::GlobalLinkageKind::InternalLinkage;

  if (dd->hasAttr<WeakAttr>())
    return cir::GlobalLinkageKind::WeakAnyLinkage;

  if (const auto *fd = dd->getAsFunction())
    if (fd->isMultiVersion() && linkage == GVA_AvailableExternally)
      return cir::GlobalLinkageKind::LinkOnceAnyLinkage;

  // We are guaranteed to have a strong definition somewhere else,
  // so we can use available_externally linkage.
  if (linkage == GVA_AvailableExternally)
    return cir::GlobalLinkageKind::AvailableExternallyLinkage;

  // Note that Apple's kernel linker doesn't support symbol
  // coalescing, so we need to avoid linkonce and weak linkages there.
  // Normally, this means we just map to internal, but for explicit
  // instantiations we'll map to external.

  // In C++, the compiler has to emit a definition in every translation unit
  // that references the function.  We should use linkonce_odr because
  // a) if all references in this translation unit are optimized away, we
  // don't need to codegen it.  b) if the function persists, it needs to be
  // merged with other definitions. c) C++ has the ODR, so we know the
  // definition is dependable.
  if (linkage == GVA_DiscardableODR)
    return !astContext.getLangOpts().AppleKext
               ? cir::GlobalLinkageKind::LinkOnceODRLinkage
               : cir::GlobalLinkageKind::InternalLinkage;

  // An explicit instantiation of a template has weak linkage, since
  // explicit instantiations can occur in multiple translation units
  // and must all be equivalent. However, we are not allowed to
  // throw away these explicit instantiations.
  //
  // CUDA/HIP: For -fno-gpu-rdc case, device code is limited to one TU,
  // so say that CUDA templates are either external (for kernels) or internal.
  // This lets llvm perform aggressive inter-procedural optimizations. For
  // -fgpu-rdc case, device function calls across multiple TU's are allowed,
  // therefore we need to follow the normal linkage paradigm.
  if (linkage == GVA_StrongODR) {
    if (getLangOpts().AppleKext)
      return cir::GlobalLinkageKind::ExternalLinkage;
    if (getLangOpts().CUDA && getLangOpts().CUDAIsDevice &&
        !getLangOpts().GPURelocatableDeviceCode)
      return dd->hasAttr<CUDAGlobalAttr>()
                 ? cir::GlobalLinkageKind::ExternalLinkage
                 : cir::GlobalLinkageKind::InternalLinkage;
    return cir::GlobalLinkageKind::WeakODRLinkage;
  }

  // C++ doesn't have tentative definitions and thus cannot have common
  // linkage.
  if (!getLangOpts().CPlusPlus && isa<VarDecl>(dd) &&
      !isVarDeclStrongDefinition(astContext, *this, cast<VarDecl>(dd),
                                 getCodeGenOpts().NoCommon))
    return cir::GlobalLinkageKind::CommonLinkage;

  // selectany symbols are externally visible, so use weak instead of
  // linkonce.  MSVC optimizes away references to const selectany globals, so
  // all definitions should be the same and ODR linkage should be used.
  // http://msdn.microsoft.com/en-us/library/5tkz6s71.aspx
  if (dd->hasAttr<SelectAnyAttr>())
    return cir::GlobalLinkageKind::WeakODRLinkage;

  // Otherwise, we have strong external linkage.
  assert(linkage == GVA_StrongExternal);
  return cir::GlobalLinkageKind::ExternalLinkage;
}

/// This function is called when we implement a function with no prototype, e.g.
/// "int foo() {}". If there are existing call uses of the old function in the
/// module, this adjusts them to call the new function directly.
///
/// This is not just a cleanup: the always_inline pass requires direct calls to
/// functions to be able to inline them.  If there is a bitcast in the way, it
/// won't inline them. Instcombine normally deletes these calls, but it isn't
/// run at -O0.
void CIRGenModule::replaceUsesOfNonProtoTypeWithRealFunction(
    mlir::Operation *old, cir::FuncOp newFn) {
  // If we're redefining a global as a function, don't transform it.
  auto oldFn = mlir::dyn_cast<cir::FuncOp>(old);
  if (!oldFn)
    return;

  // TODO(cir): this RAUW ignores some OGCG-only forwarding details.
  // OGCG also forwards operand bundles here, but operand bundles are an
  // LLVM IR concept (funclet/deopt/GC). CIR uses MLIR operations instead.
  if (oldFn->getAttrs().size() <= 1) {
    errorNYI(old->getLoc(),
             "replaceUsesOfNonProtoTypeWithRealFunction: Attribute forwarding");
  }

  // Mark new function as originated from a no-proto declaration.
  newFn.setNoProto(oldFn.getNoProto());

  // Collect typed users first to avoid iterator invalidation when erasing
  // CallOps. Keep this SymbolTable-independent because replacement-heavy
  // modules may transiently have non-unique symbols.
  llvm::SmallVector<cir::CallOp, 8> callUsers;
  llvm::SmallVector<cir::GetGlobalOp, 8> getGlobalUsersNeedingRetype;
  // Indirect calls through get_global → bitcast → call that need rewriting.
  llvm::SmallVector<cir::CallOp, 8> indirectCallUsers;
  // Pre-reserve small expected capacities to reduce reallocation churn in
  // common no-proto rewrite cases with multiple call users.
  callUsers.reserve(16);
  getGlobalUsersNeedingRetype.reserve(16);
  auto targetNameAttr = oldFn.getSymNameAttr();
  auto newFuncTy = newFn.getFunctionType();
  auto newFuncPtrTy = cir::PointerType::get(newFuncTy);
  auto newFuncArgCount = newFuncTy.getInputs().size();
  // Re-scan only finished functions known to reference this symbol, plus any
  // currently-emitting function bodies that are not indexed yet.
  forEachPotentialFunctionUserOfSymbol(
      targetNameAttr, [&](cir::FuncOp fn) {
        fn.walk([&](mlir::Operation *op) {
          if (auto call = mlir::dyn_cast<cir::CallOp>(op)) {
            if (auto callee = call.getCalleeAttr();
                callee && callee.getAttr() == targetNameAttr &&
                call.getArgs().size() >= newFuncArgCount)
              callUsers.push_back(call);
          } else if (auto getGlobal = mlir::dyn_cast<cir::GetGlobalOp>(op)) {
            if (getGlobal.getNameAttr().getAttr() == targetNameAttr) {
              if (getGlobal.getAddr().getType() != newFuncPtrTy)
                getGlobalUsersNeedingRetype.push_back(getGlobal);
              // Also find indirect calls: get_global -> bitcast -> indirect
              // call. These occur when a no-proto function is called with
              // extra arguments that don't match the definition's signature.
              for (auto *user : getGlobal.getAddr().getUsers()) {
                auto castOp = mlir::dyn_cast<cir::CastOp>(user);
                if (!castOp || castOp.getKind() != cir::CastKind::bitcast)
                  continue;
                for (auto *castUser : castOp.getResult().getUsers()) {
                  auto indirectCall = mlir::dyn_cast<cir::CallOp>(castUser);
                  if (!indirectCall || indirectCall.getCalleeAttr())
                    continue;
                  auto indirectArgOps = indirectCall.getArgOperands();
                  if (indirectArgOps.size() < newFuncArgCount)
                    continue;
                  // Check argument type compatibility — only rewrite if all
                  // corresponding argument types match exactly or are
                  // pointer-coercible. This mirrors OGCG's
                  // replaceUsesOfNonProtoConstant type check.
                  auto defArgTypes = newFuncTy.getInputs();
                  bool typesMatch = true;
                  for (unsigned i = 0; i < newFuncArgCount; ++i) {
                    mlir::Type callArgTy = indirectArgOps[i].getType();
                    mlir::Type defArgTy = defArgTypes[i];
                    if (callArgTy == defArgTy)
                      continue;
                    // Pointer-to-pointer coercion is always valid.
                    if (mlir::isa<cir::PointerType>(callArgTy) &&
                        mlir::isa<cir::PointerType>(defArgTy))
                      continue;
                    typesMatch = false;
                    break;
                  }
                  if (typesMatch)
                    indirectCallUsers.push_back(indirectCall);
                }
              }
            }
          }
        });
      });
  llvm::SmallVector<cir::GetGlobalOp, 4> topLevelGetGlobalUsers;
  collectTopLevelGetGlobalUsers(targetNameAttr, topLevelGetGlobalUsers);
  for (auto getGlobal : topLevelGetGlobalUsers)
    if (getGlobal.getAddr().getType() != newFuncPtrTy)
      getGlobalUsersNeedingRetype.push_back(getGlobal);

  for (auto getGlobal : getGlobalUsersNeedingRetype)
    getGlobal.getAddr().setType(newFuncPtrTy);
  if (callUsers.empty() && indirectCallUsers.empty())
    return;

  auto funcArgTypes = newFuncTy.getInputs();
  auto classifyCIRTypeKind = [](mlir::Type t) -> uint8_t {
    if (mlir::isa<cir::PointerType>(t))
      return 1;
    if (mlir::isa<cir::MethodType>(t))
      return 2;
    return 0;
  };
  llvm::SmallVector<uint8_t, 16> funcArgKinds;
  funcArgKinds.reserve(funcArgTypes.size());
  for (mlir::Type funcArgTy : funcArgTypes)
    funcArgKinds.push_back(classifyCIRTypeKind(funcArgTy));
  llvm::SmallVector<unsigned, 16> coercibleArgIndices;
  coercibleArgIndices.reserve(funcArgKinds.size());
  for (unsigned i = 0, e = funcArgKinds.size(); i != e; ++i) {
    if (funcArgKinds[i] != 0)
      coercibleArgIndices.push_back(i);
  }
  const bool hasCoercibleParams = !coercibleArgIndices.empty();

  for (auto noProtoCallOp : callUsers) {
    auto argOps = noProtoCallOp.getArgs();

    assert(argOps.size() >= funcArgTypes.size() &&
           "non-rewritable no-proto calls should be filtered during scan");

    // Insert new ops only when we actually rewrite this call. Non-rewritable
    // calls (e.g. too-few args) skip insertion-point setup entirely.
    bool hasInsertionPoint = false;
    auto ensureInsertionPoint = [&]() {
      if (!hasInsertionPoint) {
        builder.setInsertionPoint(noProtoCallOp);
        hasInsertionPoint = true;
      }
    };

    // Coerce argument pointer/method types to match the real function
    // signature. Build the replacement operand list lazily only after seeing
    // the first mismatch so fully-matching calls avoid both extra scans and
    // temporary operand vector materialization.
    SmallVector<mlir::Value> coercedArgs;
    bool hasCoercions = false;

    if (hasCoercibleParams) {
      // Only pointer/method-typed callee parameters can need runtime bitcast
      // coercions in this path. Skip non-coercible parameter positions
      // entirely to avoid repeated type-kind classification work in
      // call-heavy modules.
      for (unsigned i : coercibleArgIndices) {
        if (i >= argOps.size())
          break;
        mlir::Type argTy = argOps[i].getType();
        mlir::Type funcArgTy = funcArgTypes[i];
        if (argTy == funcArgTy)
          continue;

        uint8_t argKind = classifyCIRTypeKind(argTy);
        uint8_t funcArgKind = funcArgKinds[i];

        if (argKind == 1 && funcArgKind == 1) {
          if (auto funcArgPtrTy = mlir::dyn_cast<cir::PointerType>(funcArgTy)) {
            if (!hasCoercions) {
              hasCoercions = true;
              // Materialize a mutable operand list only when we actually emit
              // at least one coercion. Pure signature retargets can reuse
              // argOps.
              coercedArgs.append(noProtoCallOp.getOperands().begin(),
                                 noProtoCallOp.getOperands().end());
            }
            ensureInsertionPoint();
            coercedArgs[i] = builder.createBitcast(noProtoCallOp.getLoc(),
                                                   argOps[i], funcArgPtrTy);
            continue;
          }
        }

        if (argKind == 2 && funcArgKind == 2) {
          if (auto funcArgMethodTy = mlir::dyn_cast<cir::MethodType>(funcArgTy)) {
            if (!hasCoercions) {
              hasCoercions = true;
              // Materialize a mutable operand list only when we actually emit
              // at least one coercion. Pure signature retargets can reuse
              // argOps.
              coercedArgs.append(noProtoCallOp.getOperands().begin(),
                                 noProtoCallOp.getOperands().end());
            }
            ensureInsertionPoint();
            // Reconcile stale placeholder member-function-pointer payload types
            // to the final callee signature when replacing an old declaration
            // call with the real function call.
            coercedArgs[i] = builder.createBitcast(noProtoCallOp.getLoc(),
                                                   argOps[i], funcArgMethodTy);
          }
        }
      }
    }

    auto createRewrittenCall = [&](mlir::ValueRange callArgs) {
      ensureInsertionPoint();

      cir::CallOp newCall;
      if (noProtoCallOp.getExceptionAttr()) {
        // Keep exceptional call semantics when rewriting no-proto call sites.
        newCall = builder.createTryCallOp(
            noProtoCallOp.getLoc(), newFn, callArgs,
            noProtoCallOp.getCallingConv(), noProtoCallOp.getSideEffect());
        if (auto &oldCleanup = noProtoCallOp.getCleanup();
            !oldCleanup.empty())
          newCall.getCleanup().takeBody(oldCleanup);
        if (auto extraAttrs = noProtoCallOp.getExtraAttrsAttr())
          newCall->setAttr("extra_attrs", extraAttrs);
      } else {
        newCall = builder.createCallOp(
            noProtoCallOp.getLoc(), newFn, callArgs,
            noProtoCallOp.getCallingConv(), noProtoCallOp.getSideEffect(),
            noProtoCallOp.getExtraAttrsAttr());
      }

      if (noProtoCallOp.getNothrowAttr())
        newCall->setAttr("nothrow", mlir::UnitAttr::get(&getMLIRContext()));
      if (noProtoCallOp.getTailAttr())
        newCall->setAttr("tail", mlir::UnitAttr::get(&getMLIRContext()));
      if (noProtoCallOp.getMustTailAttr())
        newCall->setAttr("must_tail", mlir::UnitAttr::get(&getMLIRContext()));
      if (noProtoCallOp.getNoTailAttr())
        newCall->setAttr("no_tail", mlir::UnitAttr::get(&getMLIRContext()));

      return newCall;
    };

    // Patch call type with the real function type.
    // Preserve calling-convention/side-effect/exception and call flags.
    // If extra arguments were passed (UB in C), silently drop them to match
    // the definition's signature — this mirrors OGCG's
    // replaceUsesOfNonProtoConstant.
    mlir::ValueRange callArgs =
        hasCoercions ? mlir::ValueRange(coercedArgs) : argOps;
    callArgs = callArgs.take_front(newFuncArgCount);
    cir::CallOp realCallOp = createRewrittenCall(callArgs);

    // Replace old no proto call with fixed call.
    noProtoCallOp.replaceAllUsesWith(realCallOp);
    noProtoCallOp.erase();
  }

  // Rewrite indirect calls through get_global → bitcast → call to be direct
  // calls to the real function, dropping extra arguments. This mirrors OGCG's
  // replaceUsesOfNonProtoConstant which handles bitcasted callee users.
  for (auto indirectCall : indirectCallUsers) {
    builder.setInsertionPoint(indirectCall);

    // Take only the arguments the definition expects, excluding the indirect
    // callee pointer (getArgOperands() strips it for indirect calls).
    // Coerce pointer types to match the definition's signature.
    auto argOperands = indirectCall.getArgOperands();
    auto funcArgTypes = newFuncTy.getInputs();
    SmallVector<mlir::Value> callArgs;
    callArgs.reserve(newFuncArgCount);
    for (unsigned i = 0; i < newFuncArgCount; ++i) {
      mlir::Value arg = argOperands[i];
      if (arg.getType() != funcArgTypes[i] &&
          mlir::isa<cir::PointerType>(arg.getType()) &&
          mlir::isa<cir::PointerType>(funcArgTypes[i])) {
        arg = builder.createBitcast(indirectCall.getLoc(), arg, funcArgTypes[i]);
      }
      callArgs.push_back(arg);
    }

    cir::CallOp newCall;
    if (indirectCall.getExceptionAttr()) {
      newCall = builder.createTryCallOp(
          indirectCall.getLoc(), newFn, callArgs,
          indirectCall.getCallingConv(), indirectCall.getSideEffect());
      if (auto &oldCleanup = indirectCall.getCleanup(); !oldCleanup.empty())
        newCall.getCleanup().takeBody(oldCleanup);
      if (auto extraAttrs = indirectCall.getExtraAttrsAttr())
        newCall->setAttr("extra_attrs", extraAttrs);
    } else {
      newCall = builder.createCallOp(
          indirectCall.getLoc(), newFn, callArgs,
          indirectCall.getCallingConv(), indirectCall.getSideEffect(),
          indirectCall.getExtraAttrsAttr());
    }

    if (indirectCall.getNothrowAttr())
      newCall->setAttr("nothrow", mlir::UnitAttr::get(&getMLIRContext()));

    indirectCall.replaceAllUsesWith(newCall);
    indirectCall.erase();
  }

  // Clean up orphaned bitcast + get_global ops left after indirect call
  // rewriting. Collect first, then erase to avoid iterator invalidation.
  SmallVector<cir::CastOp> deadCasts;
  SmallVector<cir::GetGlobalOp> deadGetGlobals;
  for (auto fn : theModule.getOps<cir::FuncOp>()) {
    if (fn.isDeclaration())
      continue;
    fn.walk([&](cir::GetGlobalOp getGlobal) {
      if (getGlobal.getNameAttr().getAttr() != targetNameAttr)
        return;
      for (auto *user : getGlobal.getAddr().getUsers()) {
        auto castOp = mlir::dyn_cast<cir::CastOp>(user);
        if (castOp && castOp.getResult().use_empty())
          deadCasts.push_back(castOp);
      }
    });
  }
  for (auto castOp : deadCasts)
    castOp.erase();
  for (auto fn : theModule.getOps<cir::FuncOp>()) {
    if (fn.isDeclaration())
      continue;
    fn.walk([&](cir::GetGlobalOp getGlobal) {
      if (getGlobal.getNameAttr().getAttr() == targetNameAttr &&
          getGlobal.getAddr().use_empty())
        deadGetGlobals.push_back(getGlobal);
    });
  }
  for (auto getGlobal : deadGetGlobals)
    getGlobal.erase();
}

cir::GlobalLinkageKind
CIRGenModule::getCIRLinkageVarDefinition(const VarDecl *vd, bool isConstant) {
  GVALinkage linkage = astContext.GetGVALinkageForVariable(vd);
  return getCIRLinkageForDeclarator(vd, linkage, isConstant);
}

cir::GlobalLinkageKind CIRGenModule::getFunctionLinkage(GlobalDecl gd) {
  const auto *d = cast<FunctionDecl>(gd.getDecl());

  GVALinkage linkage = astContext.GetGVALinkageForFunction(d);

  if (const auto *dtor = dyn_cast<CXXDestructorDecl>(d))
    return getCXXABI().getCXXDestructorLinkage(linkage, dtor, gd.getDtorType());

  return getCIRLinkageForDeclarator(d, linkage, /*isConstantVariable=*/false);
}

static cir::GlobalOp
generateStringLiteral(mlir::Location loc, mlir::TypedAttr c,
                      cir::GlobalLinkageKind lt, CIRGenModule &cgm,
                      StringRef globalName, CharUnits alignment) {
  mlir::ptr::MemorySpaceAttrInterface addrSpace =
      cir::toCIRLangAddressSpaceAttr(&cgm.getMLIRContext(),
                                     cgm.getGlobalConstantAddressSpace());

  // Create a global variable for this string
  // FIXME(cir): check for insertion point in module level.
  cir::GlobalOp gv = CIRGenModule::createGlobalOp(
      cgm, loc, globalName, c.getType(), !cgm.getLangOpts().WritableStrings,
      addrSpace);

  // Set up extra information and add to the module
  gv.setAlignmentAttr(cgm.getSize(alignment));
  gv.setLinkageAttr(
      cir::GlobalLinkageKindAttr::get(cgm.getBuilder().getContext(), lt));
  gv->setAttr("unnamed_addr",
              mlir::UnitAttr::get(cgm.getBuilder().getContext()));
  CIRGenModule::setInitializer(gv, c);
  if (gv.isWeakForLinker()) {
    assert(cgm.supportsCOMDAT() && "Only COFF uses weak string literals");
    gv.setComdat(true);
  }
  cgm.setDSOLocal(static_cast<mlir::Operation *>(gv));
  return gv;
}

static std::string mangleStringLiteralName(CIRGenModule &cgm,
                                           const StringLiteral *s) {
  SmallString<256> mangledName;
  llvm::raw_svector_ostream out(mangledName);
  cgm.getCXXABI().getMangleContext().mangleStringLiteral(s, out);
  return std::string(mangledName);
}

// LLVM IR automatically uniques names when new llvm::GlobalVariables are
// created. This is handy, for example, when creating globals for string
// literals. Since we don't do that when creating cir::GlobalOp's, we need
// a mechanism to generate a unique name in advance.
//
// For now, this mechanism is only used in cases where we know that the
// name is compiler-generated, so we don't use the MLIR symbol table for
// the lookup.
std::string CIRGenModule::getUniqueGlobalName(const std::string &baseName) {
  // If this is the first time we've generated a name for this basename, try
  // to use it as-is first (matching LLVM behavior). If that name already
  // exists in the module (e.g. user-provided symbol/asm label), fall back to
  // suffixed variants.
  unsigned &nextSuffix = cgGlobalNames[baseName];
  if (nextSuffix == 0) {
    if (!getGlobalValue(baseName)) {
      nextSuffix = 1;
      return baseName;
    }
    nextSuffix = 1;
  }

  std::string result;
  do {
    result = baseName + "." + std::to_string(nextSuffix++);
  } while (getGlobalValue(result));

  return result;
}

std::string CIRGenModule::getUniqueBlockName(const BlockExpr *blockExpr,
                                             llvm::StringRef baseName) {
  auto it = uniqueBlockNames.find(blockExpr);
  if (it != uniqueBlockNames.end())
    return it->second;

  std::string uniqueName = getUniqueGlobalName(baseName.str());
  uniqueBlockNames.try_emplace(blockExpr, uniqueName);
  return uniqueName;
}

bool CIRGenModule::blockFunctionUsesStret(const FunctionProtoType *blockProto,
                                          const FunctionArgList &params,
                                          cir::FuncType invokeTy) {
  if (!getTargetCIRGenInfo().doesReturnSlotInterfereWithArgs())
    return false;

  // CIRGen function types are ABI-agnostic, so reuse target-lowering's
  // function classifier to decide whether this block return is indirect.
  mlir::PatternRewriter rewriter(&getMLIRContext());
  auto lowerModule = cir::createLowerModule(getModule(), rewriter);
  if (!lowerModule)
    return false;

  llvm::SmallVector<TypeProvenance, 8> argProvenances;
  argProvenances.reserve(params.size());
  for (const VarDecl *param : params)
    argProvenances.push_back(getTypeProvenance(param->getType()));

  TypeProvenance resultProvenance =
      getTypeProvenance(blockProto->getReturnType());
  const auto &fnInfo = lowerModule->getTypes().arrangeFreeFunctionType(
      invokeTy, resultProvenance, argProvenances,
      getTypes().ClangCallConvToCIRCallConv(blockProto->getCallConv()));
  return fnInfo.getReturnInfo().isIndirect();
}

/// Return a pointer to a constant array for the given string literal.
cir::GlobalOp CIRGenModule::getGlobalForStringLiteral(const StringLiteral *s,
                                                      StringRef name) {
  CharUnits alignment =
      astContext.getAlignOfGlobalVarInChars(s->getType(), /*VD=*/nullptr);

  mlir::Attribute c = getConstantArrayFromStringLiteral(s);

  cir::GlobalOp gv;
  if (!getLangOpts().WritableStrings && constantStringMap.count(c)) {
    gv = constantStringMap[c];
    // The bigger alignment always wins.
    if (!gv.getAlignment() ||
        uint64_t(alignment.getQuantity()) > *gv.getAlignment())
      gv.setAlignmentAttr(getSize(alignment));
  } else {
    // Mangle the string literal if that's how the ABI merges duplicate strings.
    // Don't do it if they are writable, since we don't want writes in one TU to
    // affect strings in another.
    bool shouldMangle = getCXXABI().getMangleContext().shouldMangleStringLiteral(s) &&
                        !getLangOpts().WritableStrings;
    cir::GlobalLinkageKind linkage = cir::GlobalLinkageKind::PrivateLinkage;
    std::string globalName;
    if (shouldMangle) {
      linkage = cir::GlobalLinkageKind::LinkOnceODRLinkage;
      globalName = mangleStringLiteralName(*this, s);
    } else {
      // Unlike LLVM IR, CIR doesn't automatically unique names for globals, so
      // we need to do that explicitly.
      globalName = getUniqueGlobalName(name.str());
    }

    // Synthetic string literals (e.g., from SourceLocExpr) may not have valid
    // source locations. Use unknown location in those cases.
    mlir::Location loc = s->getBeginLoc().isValid()
                             ? getLoc(s->getSourceRange())
                             : builder.getUnknownLoc();
    auto typedC = llvm::cast<mlir::TypedAttr>(c);
    gv = generateStringLiteral(loc, typedC, linkage, *this, globalName,
                               alignment);
    mlir::SymbolTable::setSymbolVisibility(gv, getMLIRVisibility(gv));
    setDSOLocal(static_cast<mlir::Operation *>(gv));
    constantStringMap[c] = gv;
    SourceLocation strLoc = s->getStrTokenLoc(0);
    if (!strLoc.isValid())
      strLoc = s->getBeginLoc();
    maybeRecordAddressSanitizerGlobalMetadata(
        strLoc, "<string literal>", QualType(), SanitizerMask(),
        /*isDynInit=*/false, gv);
    maybeRecordTypeSanGlobalMetadata(strLoc, "<string literal>", QualType(),
                                     SanitizerMask(), gv);
  }
  return gv;
}

cir::GlobalOp CIRGenModule::getGlobalForConstantCString(mlir::Location loc,
                                                        StringRef str,
                                                        StringRef name) {
  SmallString<64> strWithNull(str);
  strWithNull.push_back('\0');

  CharUnits alignment =
      astContext.getAlignOfGlobalVarInChars(astContext.CharTy, /*VD=*/nullptr);
  mlir::Attribute c =
      builder.getString(strWithNull, convertType(astContext.CharTy),
                        strWithNull.size());

  cir::GlobalOp gv;
  if (!getLangOpts().WritableStrings && constantStringMap.count(c)) {
    gv = constantStringMap[c];
    if (!gv.getAlignment() ||
        uint64_t(alignment.getQuantity()) > *gv.getAlignment())
      gv.setAlignmentAttr(getSize(alignment));
  } else {
    std::string globalName = getUniqueGlobalName(name.str());
    gv = generateStringLiteral(loc, llvm::cast<mlir::TypedAttr>(c),
                               cir::GlobalLinkageKind::PrivateLinkage, *this,
                               globalName, alignment);
    mlir::SymbolTable::setSymbolVisibility(gv, getMLIRVisibility(gv));
    setDSOLocal(static_cast<mlir::Operation *>(gv));
    constantStringMap[c] = gv;
  }
  return gv;
}

cir::GlobalOp CIRGenModule::getGlobalForObjCEncodeExpr(const ObjCEncodeExpr *e,
                                                       StringRef name) {
  std::string str;
  getASTContext().getObjCEncodingForType(e->getEncodedType(), str);

  mlir::Location loc = e->getBeginLoc().isValid()
                           ? getLoc(e->getSourceRange())
                           : builder.getUnknownLoc();
  return getGlobalForConstantCString(loc, str, name);
}

/// Return a pointer to a constant array for the given string literal.
cir::GlobalViewAttr
CIRGenModule::getAddrOfConstantStringFromLiteral(const StringLiteral *s,
                                                 StringRef name) {
  cir::GlobalOp gv = getGlobalForStringLiteral(s, name);
  auto arrayTy = mlir::dyn_cast<cir::ArrayType>(gv.getSymType());
  assert(arrayTy && "String literal must be array");
  cir::PointerType ptrTy = getBuilder().getPointerTo(arrayTy.getElementType(),
                                                     gv.getAddrSpaceAttr());

  return builder.getGlobalViewAttr(ptrTy, gv);
}

cir::GlobalViewAttr
CIRGenModule::getAddrOfConstantStringFromObjCEncode(const ObjCEncodeExpr *e,
                                                    StringRef name) {
  cir::GlobalOp gv = getGlobalForObjCEncodeExpr(e, name);
  auto arrayTy = mlir::dyn_cast<cir::ArrayType>(gv.getSymType());
  assert(arrayTy && "ObjC @encode literal must be array");
  cir::PointerType ptrTy = getBuilder().getPointerTo(arrayTy.getElementType(),
                                                     gv.getAddrSpaceAttr());

  return builder.getGlobalViewAttr(ptrTy, gv);
}

enum GNUstep2SectionKind : unsigned {
  GNUstep2SelectorSection,
  GNUstep2ClassSection,
  GNUstep2ClassReferenceSection,
  GNUstep2CategorySection,
  GNUstep2ProtocolSection,
  GNUstep2ProtocolReferenceSection,
  GNUstep2ClassAliasSection,
  GNUstep2ConstantStringSection,
};

static constexpr llvm::StringLiteral GNUstep2ObjCSectionsBaseNames[] = {
    "__objc_selectors",     "__objc_classes",      "__objc_class_refs",
    "__objc_cats",          "__objc_protocols",    "__objc_protocol_refs",
    "__objc_class_aliases", "__objc_constant_string"};

static constexpr llvm::StringLiteral GNUstep2ObjCPECOFFSectionsBaseNames[] = {
    ".objcrt$SEL", ".objcrt$CLS", ".objcrt$CLR", ".objcrt$CAT",
    ".objcrt$PCL", ".objcrt$PCR", ".objcrt$CAL", ".objcrt$STR"};

static llvm::ArrayRef<llvm::StringLiteral>
getGNUstep2ObjCSectionBaseNames(const CIRGenModule &cgm) {
  return cgm.getTriple().isOSBinFormatCOFF()
             ? llvm::ArrayRef(GNUstep2ObjCPECOFFSectionsBaseNames)
             : llvm::ArrayRef(GNUstep2ObjCSectionsBaseNames);
}

static llvm::StringRef getGNUstep2ObjCSectionBaseName(
    const CIRGenModule &cgm, GNUstep2SectionKind kind) {
  return getGNUstep2ObjCSectionBaseNames(cgm)[static_cast<unsigned>(kind)];
}

static std::string getGNUstep2ObjCSectionName(const CIRGenModule &cgm,
                                              GNUstep2SectionKind kind) {
  std::string name = getGNUstep2ObjCSectionBaseName(cgm, kind).str();
  if (cgm.getTriple().isOSBinFormatCOFF())
    name += "$m";
  return name;
}

static const ObjCInterfaceDecl *lookupObjCInterfaceDecl(CIRGenModule &cgm,
                                                        llvm::StringRef name);
static void setGNUstep2ClassDLLStorageClass(CIRGenModule &cgm,
                                            cir::GlobalOp gv,
                                            const ObjCInterfaceDecl *id);

mlir::TypedAttr
CIRGenModule::getAddrOfConstantObjCString(const StringLiteral *literal) {
  const ObjCRuntime runtime = getLangOpts().ObjCRuntime;
  const bool isLegacyGNU =
      runtime.getKind() == ObjCRuntime::GCC ||
      runtime.getKind() == ObjCRuntime::ObjFW ||
      (runtime.getKind() == ObjCRuntime::GNUstep &&
       runtime.getVersion() < VersionTuple(2));
  const bool isGNUstep2 = runtime.getKind() == ObjCRuntime::GNUstep &&
                          runtime.getVersion() >= VersionTuple(2);
  if (!isLegacyGNU && !isGNUstep2)
    return getAddrOfConstantCFString(literal);

  mlir::Location loc = literal->getBeginLoc().isValid()
                           ? getLoc(literal->getSourceRange())
                           : builder.getUnknownLoc();
  StringRef bytes = literal->getString();

  if (isGNUstep2) {
    if (auto it = gnustep2ConstantStringMap.find(bytes);
        it != gnustep2ConstantStringMap.end())
      return it->second;

    const bool isCOFF = getTriple().isOSBinFormatCOFF();
    if (!literal->containsNonAscii() &&
        getTarget().getPointerWidth(LangAS::Default) == 64 &&
        literal->getLength() < 9) {
      uint64_t taggedString = 0;
      for (unsigned i = 0; i < literal->getLength(); ++i)
        taggedString |= static_cast<uint64_t>(literal->getCodeUnit(i))
                        << ((64 - 4 - 3) - (i * 7));
      taggedString |= static_cast<uint64_t>(literal->getLength()) << 3;
      taggedString |= 4;

      auto taggedValue = mlir::IntegerAttr::get(
          mlir::IntegerType::get(&getMLIRContext(), 64),
          llvm::APInt(64, taggedString));
      auto taggedAttr = cir::ConstPtrAttr::get(voidPtrTy, taggedValue);
      gnustep2ConstantStringMap[bytes] = taggedAttr;
      return taggedAttr;
    }

    StringRef stringClass = getLangOpts().ObjCConstantStringClass;
    if (stringClass.empty())
      stringClass = "NSConstantString";

    std::string classSymbolName =
        (llvm::Twine(isCOFF ? "$_" : "._") + "OBJC_CLASS_" + stringClass)
            .str();
    cir::GlobalOp classGV = createOrReplaceCXXRuntimeVariable(
        loc, classSymbolName, voidPtrTy,
        cir::GlobalLinkageKind::ExternalLinkage, CharUnits::Zero());
    classGV.setConstant(false);
    const ObjCInterfaceDecl *stringClassDecl =
        lookupObjCInterfaceDecl(*this, stringClass);
    setGNUstep2ClassDLLStorageClass(*this, classGV, stringClassDecl);
    if (isCOFF && !stringClassDecl)
      classGV->setAttr("cir.dll_storage_class",
                       getStringAttrCached("dllimport"));

    auto i32Ty = builder.getSInt32Ty();
    cir::GlobalOp stringGV;
    unsigned stringLength = 0;
    unsigned stringSize = 0;
    unsigned stringFlags = 0;

    if (literal->containsNonAscii()) {
      SmallVector<llvm::UTF16, 128> utf16(bytes.size() + 1);
      const llvm::UTF8 *from =
          reinterpret_cast<const llvm::UTF8 *>(bytes.data());
      llvm::UTF16 *to = utf16.data();
      (void)llvm::ConvertUTF8toUTF16(&from, from + bytes.size(), &to,
                                     utf16.data() + bytes.size(),
                                     llvm::strictConversion);
      stringLength = static_cast<unsigned>(to - utf16.data());
      *to = 0;
      stringSize = stringLength * sizeof(llvm::UTF16);
      stringFlags = 2;

      llvm::SmallVector<mlir::Attribute, 64> utf16Elems;
      utf16Elems.reserve(stringLength + 1);
      for (llvm::UTF16 codeUnit : llvm::ArrayRef(utf16.data(), to + 1))
        utf16Elems.push_back(cir::IntAttr::get(uInt16Ty, codeUnit));

      auto stringArrayTy = cir::ArrayType::get(uInt16Ty, utf16Elems.size());
      auto stringInit = cir::ConstArrayAttr::get(
          stringArrayTy, mlir::ArrayAttr::get(&getMLIRContext(), utf16Elems));
      stringGV = createGlobalOp(*this, loc, getUniqueGlobalName(".str"),
                                stringArrayTy, /*isConstant=*/true);
      stringGV.setLinkage(cir::GlobalLinkageKind::PrivateLinkage);
      mlir::SymbolTable::setSymbolVisibility(
          stringGV, mlir::SymbolTable::Visibility::Private);
      stringGV.setAlignmentAttr(
          getSize(astContext.getTypeAlignInChars(astContext.ShortTy)));
      stringGV->setAttr("unnamed_addr",
                        mlir::UnitAttr::get(&getMLIRContext()));
      setInitializer(stringGV, stringInit);
      setDSOLocal(static_cast<mlir::Operation *>(stringGV));
    } else {
      stringLength = bytes.size();
      stringSize = bytes.size();
      stringGV = getGlobalForConstantCString(loc, bytes);
    }

    auto constantStringTy = builder.getAnonRecordTy(
        {voidPtrTy, i32Ty, i32Ty, i32Ty, i32Ty, voidPtrTy},
        /*packed=*/false, /*padded=*/false);
    mlir::Attribute stringClassAttr =
        isCOFF ? builder.getConstPtrAttr(voidPtrTy, 0)
               : mlir::Attribute(builder.getGlobalViewAttr(voidPtrTy, classGV));
    auto constantStringInit = builder.getAnonConstRecord(
        builder.getArrayAttr(
            {stringClassAttr,
             cir::IntAttr::get(i32Ty, stringFlags),
             cir::IntAttr::get(i32Ty, stringLength),
             cir::IntAttr::get(i32Ty, stringSize),
             cir::IntAttr::get(i32Ty, 0),
             builder.getGlobalViewAttr(voidPtrTy, stringGV)}),
        /*packed=*/false, /*padded=*/false, constantStringTy);

    bool isNamed = !literal->containsNonAscii();
    std::string constantStringName = ".objc_str_";
    if (isNamed) {
      for (unsigned char c : bytes) {
        bool isAlnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9');
        if (isAlnum) {
          constantStringName.push_back(c);
          continue;
        }
        if (c == ' ') {
          constantStringName.push_back('_');
          continue;
        }
        isNamed = false;
        break;
      }
    }

    cir::GlobalOp stringObjGV = createGlobalOp(
        *this, loc,
        isNamed ? constantStringName : getUniqueGlobalName(".objc_string"),
        constantStringTy, /*isConstant=*/false);
    if (isNamed) {
      stringObjGV.setLinkage(cir::GlobalLinkageKind::LinkOnceODRLinkage);
      stringObjGV.setGlobalVisibility(cir::VisibilityKind::Hidden);
      mlir::SymbolTable::setSymbolVisibility(stringObjGV,
                                             getMLIRVisibility(stringObjGV));
      if (supportsCOMDAT())
        stringObjGV.setComdat(true);
    } else {
      stringObjGV.setLinkage(cir::GlobalLinkageKind::PrivateLinkage);
      mlir::SymbolTable::setSymbolVisibility(
          stringObjGV, mlir::SymbolTable::Visibility::Private);
      setDSOLocal(static_cast<mlir::Operation *>(stringObjGV));
    }
    stringObjGV.setAlignmentAttr(getSize(getPointerAlign()));
    stringObjGV.setSectionAttr(getStringAttrCached(
        getGNUstep2ObjCSectionName(*this, GNUstep2ConstantStringSection)));
    setInitializer(stringObjGV, constantStringInit);
    if (isCOFF)
      gnustep2COFFEarlyInitEntries.push_back({classGV, stringObjGV, 0});

    auto stringAttr = builder.getGlobalViewAttr(
        builder.getPointerTo(stringObjGV.getSymType(), stringObjGV.getAddrSpaceAttr()),
        stringObjGV);
    gnustep2ConstantStringMap[bytes] = stringAttr;
    return stringAttr;
  }

  if (auto it = legacyGNUConstantStringMap.find(bytes);
      it != legacyGNUConstantStringMap.end()) {
    cir::GlobalOp cached = it->second;
    return builder.getGlobalViewAttr(
        builder.getPointerTo(cached.getSymType(), cached.getAddrSpaceAttr()),
        cached);
  }

  auto intTy = mlir::cast<cir::IntType>(convertType(astContext.IntTy));

  StringRef stringClass = getLangOpts().ObjCConstantStringClass;
  if (stringClass.empty())
    stringClass = "NSConstantString";

  std::string classSymbolName = (llvm::Twine("_OBJC_CLASS_") + stringClass).str();
  cir::GlobalOp classGV = createOrReplaceCXXRuntimeVariable(
      loc, classSymbolName, voidPtrTy,
      cir::GlobalLinkageKind::ExternalWeakLinkage, CharUnits::Zero());
  classGV.setConstant(false);
  classGV.setComdat(false);

  cir::GlobalOp stringGV = getGlobalForConstantCString(loc, bytes);
  auto stringTy = builder.getAnonRecordTy({voidPtrTy, voidPtrTy, intTy},
                                          /*packed=*/false,
                                          /*padded=*/false);
  auto stringInit = builder.getAnonConstRecord(
      builder.getArrayAttr(
          {builder.getGlobalViewAttr(voidPtrTy, classGV),
           builder.getGlobalViewAttr(voidPtrTy, stringGV),
           cir::IntAttr::get(intTy, bytes.size())}),
      /*packed=*/false, /*padded=*/false, stringTy);
  cir::GlobalOp stringObjGV =
      createGlobalOp(*this, loc, getUniqueGlobalName(".objc_str"), stringTy,
                     /*isConstant=*/false);
  stringObjGV.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
  mlir::SymbolTable::setSymbolVisibility(
      stringObjGV, mlir::SymbolTable::Visibility::Private);
  stringObjGV.setAlignmentAttr(getSize(getPointerAlign()));
  setInitializer(stringObjGV, stringInit);
  setDSOLocal(static_cast<mlir::Operation *>(stringObjGV));

  legacyGNUConstantStringMap[bytes] = stringObjGV;
  legacyGNUConstantStringGlobals.push_back(stringObjGV);
  return builder.getGlobalViewAttr(builder.getPointerTo(stringObjGV.getSymType(),
                                                        stringObjGV.getAddrSpaceAttr()),
                                   stringObjGV);
}

static void setObjCRuntimeDLLStorageClass(CIRGenModule &cgm, cir::GlobalOp gv,
                                          llvm::StringRef name) {
  if (!cgm.getTriple().isOSBinFormatCOFF())
    return;

  IdentifierInfo &ii = cgm.getASTContext().Idents.get(name);
  DeclarationName declName(&ii);
  TranslationUnitDecl *tu = cgm.getASTContext().getTranslationUnitDecl();
  const VarDecl *vd = nullptr;
  for (const auto *result : tu->lookup(declName)) {
    if ((vd = dyn_cast<VarDecl>(result)))
      break;
  }

  if (!vd || vd->hasAttr<DLLImportAttr>()) {
    gv->setAttr("cir.dll_storage_class", cgm.getStringAttrCached("dllimport"));
  } else if (vd->hasAttr<DLLExportAttr>()) {
    gv->setAttr("cir.dll_storage_class", cgm.getStringAttrCached("dllexport"));
  }
  cgm.setDSOLocal(static_cast<mlir::Operation *>(gv));
}

static const ObjCInterfaceDecl *lookupObjCInterfaceDecl(CIRGenModule &cgm,
                                                        llvm::StringRef name) {
  IdentifierInfo &ii = cgm.getASTContext().Idents.get(name);
  DeclarationName declName(&ii);
  TranslationUnitDecl *tu = cgm.getASTContext().getTranslationUnitDecl();
  const ObjCInterfaceDecl *id = nullptr;
  for (const auto *result : tu->lookup(declName)) {
    if ((id = dyn_cast<ObjCInterfaceDecl>(result)))
      break;
  }

  if (!id)
    return nullptr;
  if (const ObjCInterfaceDecl *def = id->getDefinition())
    id = def;
  return id;
}

static void setGNUstep2ClassDLLStorageClass(CIRGenModule &cgm,
                                            cir::GlobalOp gv,
                                            const ObjCInterfaceDecl *id) {
  if (!cgm.getTriple().isOSBinFormatCOFF())
    return;

  gv->removeAttr("cir.dll_storage_class");
  gv->removeAttr("dso_local");
  if (!id)
    return;

  if (id->hasAttr<DLLImportAttr>()) {
    gv->setAttr("cir.dll_storage_class", cgm.getStringAttrCached("dllimport"));
    return;
  }

  if (id->hasAttr<DLLExportAttr>())
    gv->setAttr("cir.dll_storage_class", cgm.getStringAttrCached("dllexport"));
}

static void setObjCClassSymbolDLLStorageClass(CIRGenModule &cgm,
                                              cir::GlobalOp gv,
                                              const ObjCInterfaceDecl *id) {
  if (!cgm.getTriple().isOSBinFormatCOFF())
    return;

  gv->removeAttr("cir.dll_storage_class");
  if (id->hasAttr<DLLImportAttr>()) {
    gv->setAttr("cir.dll_storage_class", cgm.getStringAttrCached("dllimport"));
    cgm.setDSOLocal(static_cast<mlir::Operation *>(gv));
    return;
  }

  if (id->hasAttr<DLLExportAttr>()) {
    gv->setAttr("cir.dll_storage_class", cgm.getStringAttrCached("dllexport"));
    return;
  }

  if (gv.isDeclaration())
    gv->removeAttr("dso_local");
}

cir::GlobalViewAttr
CIRGenModule::getAddrOfConstantCFString(const StringLiteral *literal) {
  StringRef bytes = literal->getBytes();
  unsigned numBytes = bytes.size();
  unsigned stringLength = 0;
  bool isUTF16 = false;

  std::string cacheKeyStorage;
  StringRef cacheKey;
  if (!literal->containsNonAsciiOrNull()) {
    cacheKeyStorage.assign(bytes.begin(), bytes.end());
    cacheKey = cacheKeyStorage;
    stringLength = numBytes;
  } else {
    isUTF16 = true;
    SmallVector<llvm::UTF16, 128> utf16(numBytes + 1);
    const llvm::UTF8 *from = reinterpret_cast<const llvm::UTF8 *>(bytes.data());
    llvm::UTF16 *to = utf16.data();
    (void)llvm::ConvertUTF8toUTF16(&from, from + numBytes, &to,
                                   utf16.data() + numBytes,
                                   llvm::strictConversion);
    stringLength = static_cast<unsigned>(to - utf16.data());
    *to = 0;
    cacheKeyStorage.assign(reinterpret_cast<const char *>(utf16.data()),
                           (stringLength + 1) * sizeof(llvm::UTF16));
    cacheKey = cacheKeyStorage;
  }

  if (auto it = constantCFStringMap.find(cacheKey); it != constantCFStringMap.end()) {
    cir::GlobalOp cached = it->second;
    return builder.getGlobalViewAttr(
        builder.getPointerTo(cached.getSymType(), cached.getAddrSpaceAttr()),
        cached);
  }

  mlir::Location loc = literal->getBeginLoc().isValid()
                           ? getLoc(literal->getSourceRange())
                           : builder.getUnknownLoc();

  const ASTContext &ctx = getASTContext();
  const llvm::Triple &triple = getTriple();
  auto cfruntime = getLangOpts().CFRuntime;
  const bool isSwiftABI =
      static_cast<unsigned>(cfruntime) >=
      static_cast<unsigned>(LangOptions::CoreFoundationABI::Swift);
  const bool isSwift4_1 =
      cfruntime == LangOptions::CoreFoundationABI::Swift4_1;

  const char *classSym = "__CFConstantStringClassReference";
  mlir::Type classTy = cir::ArrayType::get(sInt32Ty, 0);
  if (isSwiftABI) {
    switch (cfruntime) {
    default:
    case LangOptions::CoreFoundationABI::Swift:
    case LangOptions::CoreFoundationABI::Swift5_0:
      classSym =
          triple.isOSDarwin() ? "$s15SwiftFoundation19_NSCFConstantStringCN"
                              : "$s10Foundation19_NSCFConstantStringCN";
      break;
    case LangOptions::CoreFoundationABI::Swift4_2:
      classSym =
          triple.isOSDarwin() ? "$S15SwiftFoundation19_NSCFConstantStringCN"
                              : "$S10Foundation19_NSCFConstantStringCN";
      break;
    case LangOptions::CoreFoundationABI::Swift4_1:
      classSym =
          triple.isOSDarwin() ? "__T015SwiftFoundation19_NSCFConstantStringCN"
                              : "__T010Foundation19_NSCFConstantStringCN";
      break;
    }
    classTy = uIntPtrTy;
  }

  cir::GlobalOp classRefGV =
      mlir::dyn_cast_or_null<cir::GlobalOp>(getGlobalValue(classSym));
  if (!classRefGV || (classRefGV.getSymType() != classTy &&
                      classRefGV.isDeclaration())) {
    classRefGV = createOrReplaceCXXRuntimeVariable(
        loc, classSym, classTy, cir::GlobalLinkageKind::ExternalLinkage,
        CharUnits::Zero());
    classRefGV.setConstant(false);
    setObjCRuntimeDLLStorageClass(*this, classRefGV, classSym);
  }
  cFConstantStringClassRef = classRefGV;

  mlir::Attribute stringInit;
  mlir::Type stringArrayTy;
  CharUnits stringAlign;
  if (isUTF16) {
    llvm::SmallVector<mlir::Attribute, 64> utf16Elems;
    utf16Elems.reserve(cacheKey.size() / sizeof(llvm::UTF16));
    for (size_t i = 0; i < cacheKey.size(); i += sizeof(llvm::UTF16)) {
      uint16_t v = (static_cast<unsigned char>(cacheKey[i])) |
                   (static_cast<uint16_t>(static_cast<unsigned char>(cacheKey[i + 1])) << 8);
      utf16Elems.push_back(cir::IntAttr::get(uInt16Ty, v));
    }
    stringArrayTy = cir::ArrayType::get(uInt16Ty, utf16Elems.size());
    stringInit = cir::ConstArrayAttr::get(
        mlir::cast<cir::ArrayType>(stringArrayTy),
        mlir::ArrayAttr::get(&getMLIRContext(), utf16Elems));
    stringAlign = ctx.getTypeAlignInChars(ctx.ShortTy);
  } else {
    std::string utf8(cacheKey.begin(), cacheKey.end());
    utf8.push_back('\0');
    stringInit = builder.getString(utf8, uCharTy, utf8.size());
    stringArrayTy = mlir::cast<mlir::TypedAttr>(stringInit).getType();
    stringAlign = ctx.getTypeAlignInChars(ctx.CharTy);
  }

  std::string strName = getUniqueGlobalName(".str");
  cir::GlobalOp strGV = createGlobalOp(*this, loc, strName, stringArrayTy,
                                       /*isConstant=*/true);
  strGV.setLinkage(cir::GlobalLinkageKind::PrivateLinkage);
  mlir::SymbolTable::setSymbolVisibility(strGV,
                                         mlir::SymbolTable::Visibility::Private);
  strGV.setAlignmentAttr(getSize(stringAlign));
  strGV->setAttr("unnamed_addr", mlir::UnitAttr::get(&getMLIRContext()));
  if (triple.isOSBinFormatMachO())
    strGV.setSectionAttr(getStringAttrCached(
        isUTF16 ? "__TEXT,__ustring" : "__TEXT,__cstring,cstring_literals"));
  else if (triple.isOSBinFormatELF())
    strGV.setSectionAttr(getStringAttrCached(".rodata"));
  setInitializer(strGV, stringInit);
  setDSOLocal(static_cast<mlir::Operation *>(strGV));

  QualType cfTy = ctx.getCFConstantStringType();
  mlir::Type cfRecTy = getTypes().convertTypeForMem(cfTy);
  auto cfRecordTy = mlir::cast<cir::RecordType>(cfRecTy);
  auto memberTypes = cfRecordTy.getMembers();
  assert((memberTypes.size() == 4 || memberTypes.size() == 5) &&
         "unexpected CF constant string record layout");

  SmallVector<mlir::Attribute, 6> fields;
  auto classSymbol = mlir::FlatSymbolRefAttr::get(classRefGV.getSymNameAttr());
  unsigned fieldIndex = 0;

  // Class reference (pointer or pointer-to-int, depending on ABI layout).
  fields.push_back(cir::GlobalViewAttr::get(memberTypes[fieldIndex++],
                                            classSymbol));

  if (isSwiftABI) {
    fields.push_back(cir::IntAttr::get(
        mlir::cast<cir::IntType>(memberTypes[fieldIndex++]),
        isSwift4_1 ? 0x05 : 0x01));
    fields.push_back(cir::IntAttr::get(
        mlir::cast<cir::IntType>(memberTypes[fieldIndex++]),
        isUTF16 ? 0x07d0 : 0x07c8));
  } else {
    fields.push_back(cir::IntAttr::get(
        mlir::cast<cir::IntType>(memberTypes[fieldIndex++]),
        isUTF16 ? 0x07d0 : 0x07c8));
  }

  // String pointer (allow typed cast from backing array element type).
  fields.push_back(cir::GlobalViewAttr::get(
      memberTypes[fieldIndex++],
      mlir::FlatSymbolRefAttr::get(strGV.getSymNameAttr())));

  // String length.
  fields.push_back(cir::IntAttr::get(
      mlir::cast<cir::IntType>(memberTypes[fieldIndex++]), stringLength));

  auto cfInit = cir::ConstRecordAttr::get(
      cfRecordTy, mlir::ArrayAttr::get(&getMLIRContext(), fields));

  std::string cfName = getUniqueGlobalName("_unnamed_cfstring_");
  cir::GlobalOp cfGV =
      createGlobalOp(*this, loc, cfName, cfRecordTy, /*isConstant=*/false);
  cfGV.setLinkage(cir::GlobalLinkageKind::PrivateLinkage);
  mlir::SymbolTable::setSymbolVisibility(cfGV,
                                         mlir::SymbolTable::Visibility::Private);
  CharUnits cfAlign = isSwiftABI ? ctx.toCharUnitsFromBits(64) : getPointerAlign();
  cfGV.setAlignmentAttr(getSize(cfAlign));
  if (triple.isOSBinFormatMachO())
    cfGV.setSectionAttr(getStringAttrCached("__DATA,__cfstring"));
  else
    cfGV.setSectionAttr(getStringAttrCached("cfstring"));
  cfGV->setAttr("cir.objc_arc_inert", mlir::UnitAttr::get(&getMLIRContext()));
  setInitializer(cfGV, cfInit);
  setDSOLocal(static_cast<mlir::Operation *>(cfGV));

  constantCFStringMap[cacheKeyStorage] = cfGV;
  return builder.getGlobalViewAttr(
      builder.getPointerTo(cfGV.getSymType(), cfGV.getAddrSpaceAttr()), cfGV);
}

cir::GlobalViewAttr
CIRGenModule::getAddrOfGlobalBlockLiteral(mlir::Location loc, GlobalDecl gd,
                                          const BlockExpr *e,
                                          const VarDecl *initializedVarDecl) {
  const BlockDecl *blockDecl = e->getBlockDecl();
  assert(!blockDecl->hasCaptures() &&
         "expected constant-emittable global block literal");

  auto &moduleBuilder = getBuilder();
  auto voidPtrTy = moduleBuilder.getVoidPtrTy();
  auto int32Ty = moduleBuilder.getSInt32Ty();
  auto ulongTy =
      mlir::cast<cir::IntType>(convertType(getASTContext().UnsignedLongTy));
  auto i8PtrTy = moduleBuilder.getPointerTo(moduleBuilder.getSInt8Ty());

  const std::string blockName = getUniqueBlockName(
      e, getBlockMangledName(*this, gd, blockDecl, initializedVarDecl));
  const std::string invokeName = blockName + ".invoke";
  const std::string sigName = blockName + ".sig";
  const std::string descName = blockName + ".descriptor";
  const std::string literalName = blockName + ".literal";

  const FunctionProtoType *blockProto = e->getFunctionType();
  const IdentifierInfo *ii = &getASTContext().Idents.get(".block_descriptor");
  ImplicitParamDecl selfDecl(getASTContext(), const_cast<BlockDecl *>(blockDecl),
                             SourceLocation(), ii, getASTContext().VoidPtrTy,
                             ImplicitParamKind::ObjCSelf);
  FunctionArgList params;
  params.push_back(&selfDecl);
  params.append(blockDecl->param_begin(), blockDecl->param_end());

  cir::FuncOp invokeFn = lookupFuncByName(*this, invokeName);
  if (!invokeFn) {
    const CIRGenFunctionInfo &invokeInfo =
        getTypes().arrangeBlockFunctionDeclaration(blockProto, params);
    cir::FuncType invokeTy = getTypes().getFunctionType(invokeInfo);
    invokeFn = createCIRFunction(loc, invokeName, invokeTy, nullptr);
    maybeSetDisableTailCallsForBlockInvoke(invokeFn, blockDecl);
    setInternalFunctionAttributes(GlobalDecl(blockDecl), invokeFn, invokeInfo);
    invokeFn.setSymVisibility("private");

    CIRGenFunction blockCGF(*this, moduleBuilder);
    {
      mlir::OpBuilder::InsertionGuard guard(moduleBuilder);
      blockCGF.generateBlockFunctionCode(e, invokeFn, invokeTy);
    }
  }
  bool usesStret = blockFunctionUsesStret(blockProto, params,
                                          invokeFn.getFunctionType());

  cir::GlobalOp isaGlobal = lookupGlobalByName(*this, "_NSConcreteGlobalBlock");
  if (!isaGlobal) {
    isaGlobal = CIRGenModule::createGlobalOp(*this, loc, "_NSConcreteGlobalBlock",
                                             voidPtrTy, /*isConstant=*/false);
    isaGlobal.setLinkage(cir::GlobalLinkageKind::ExternalLinkage);
    isaGlobal.setSymVisibility("private");
  }

  mlir::TypedAttr sigPtrAttr;
  if (!getCodeGenOpts().DisableBlockSignatureString) {
    cir::GlobalOp sigGlobal = lookupGlobalByName(*this, sigName);
    if (!sigGlobal) {
      std::string sig = getASTContext().getObjCEncodingForBlock(e);
      sig.push_back('\0');
      auto sigAttr = mlir::cast<mlir::TypedAttr>(
          moduleBuilder.getString(sig, moduleBuilder.getSInt8Ty(), sig.size()));
      sigGlobal = CIRGenModule::createGlobalOp(*this, loc, sigName,
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

  SmallVector<mlir::Type, 4> descMembers{ulongTy, ulongTy, i8PtrTy, i8PtrTy};
  SmallVector<mlir::Attribute, 4> descValues{
      cir::IntAttr::get(ulongTy, 0), cir::IntAttr::get(ulongTy, 0), sigPtrAttr,
      moduleBuilder.getConstPtrAttr(i8PtrTy, 0)};
  auto descTy = moduleBuilder.getAnonRecordTy(descMembers);

  SmallVector<mlir::Type, 5> literalMembers{
      moduleBuilder.getPointerTo(isaGlobal.getSymType()), int32Ty, int32Ty,
      voidPtrTy, moduleBuilder.getPointerTo(descTy)};
  auto literalTy = moduleBuilder.getAnonRecordTy(literalMembers);

  cir::GlobalOp descGlobal = lookupGlobalByName(*this, descName);
  if (!descGlobal) {
    uint64_t blockSize =
        getDataLayout().getTypeAllocSize(literalTy).getFixedValue();
    descValues[1] = cir::IntAttr::get(ulongTy, blockSize);
    auto descInit = moduleBuilder.getAnonConstRecord(
        mlir::ArrayAttr::get(&getMLIRContext(), descValues));
    descGlobal = CIRGenModule::createGlobalOp(*this, loc, descName, descTy,
                                              /*isConstant=*/true);
    descGlobal.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
    descGlobal.setSymVisibility("private");
    descGlobal.setAlignment(getPointerAlign().getAsAlign().value());
    CIRGenModule::setInitializer(descGlobal, descInit);
  }

  cir::GlobalOp literalGlobal = lookupGlobalByName(*this, literalName);
  if (!literalGlobal) {
    uint64_t flags = 1u << 28;
    if (!getCodeGenOpts().DisableBlockSignatureString)
      flags |= 1u << 30;
    if (usesStret)
      flags |= 1u << 29;

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
    literalGlobal = CIRGenModule::createGlobalOp(*this, loc, literalName,
                                                 literalTy,
                                                 /*isConstant=*/true);
    literalGlobal.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
    literalGlobal.setSymVisibility("private");
    literalGlobal.setAlignment(getPointerAlign().getAsAlign().value());
    CIRGenModule::setInitializer(literalGlobal, literalInit);
    llvm::SmallVector<mlir::Attribute, 2> ptrauthFixups;
    auto fieldOffset = [&](unsigned fieldIndex) {
      CharUnits offset;
      for (unsigned i = 0; i < fieldIndex; ++i) {
        offset = offset.alignTo(CharUnits::fromQuantity(
            getDataLayout().getABITypeAlign(literalMembers[i]).value()));
        offset += CharUnits::fromQuantity(
            getDataLayout().getTypeAllocSize(literalMembers[i]).getFixedValue());
      }
      return offset.alignTo(CharUnits::fromQuantity(
          getDataLayout().getABITypeAlign(literalMembers[fieldIndex]).value()));
    };
    auto addPtrauthFixup = [&](unsigned fieldIndex,
                               const PointerAuthSchema &schema, GlobalDecl decl,
                               QualType ty) {
      if (!schema || !shouldSignPointer(schema))
        return;
      ptrauthFixups.push_back(moduleBuilder.getArrayAttr({
          moduleBuilder.getI64IntegerAttr(fieldOffset(fieldIndex).getQuantity()),
          moduleBuilder.getI32IntegerAttr(schema.getKey()),
          moduleBuilder.getI64IntegerAttr(
              getPointerAuthOtherDiscriminator(schema, decl, ty)),
          moduleBuilder.getBoolAttr(schema.isAddressDiscriminated()),
      }));
    };
    addPtrauthFixup(
        /*fieldIndex=*/3,
        getCodeGenOpts().PointerAuth.BlockInvocationFunctionPointers,
        GlobalDecl(),
        e->getType()->castAs<BlockPointerType>()->getPointeeType());
    addPtrauthFixup(/*fieldIndex=*/4,
                    getCodeGenOpts().PointerAuth.BlockDescriptorPointers,
                    GlobalDecl(), QualType());
    if (!ptrauthFixups.empty())
      literalGlobal->setAttr("cir.global_const_ptrauth",
                             moduleBuilder.getArrayAttr(ptrauthFixups));
    literalGlobal->setAttr("cir.objc_arc_inert",
                           mlir::UnitAttr::get(&getMLIRContext()));
  }

  return moduleBuilder.getGlobalViewAttr(moduleBuilder.getPointerTo(literalTy),
                                         literalGlobal);
}

// Mirrors CGObjCNonFragileABIMac::isClassLayoutKnownStatically: a class's
// layout is statically known iff we can see the @implementation of it and every
// superclass up to (and including) NSObject, whose layout is guaranteed.
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

static bool usesDarwinNonFragileObjCRuntime(const CIRGenModule &cgm) {
  const ObjCRuntime runtime = cgm.getLangOpts().ObjCRuntime;
  return runtime.isNeXTFamily() && runtime.isNonFragile();
}

static bool usesDarwinFragileObjCRuntime(const CIRGenModule &cgm) {
  const ObjCRuntime runtime = cgm.getLangOpts().ObjCRuntime;
  return runtime.isNeXTFamily() && !runtime.isNonFragile() &&
         cgm.getTriple().isOSDarwin() && cgm.getTriple().isOSBinFormatMachO();
}

static bool usesDarwinObjCRuntime(const CIRGenModule &cgm) {
  return usesDarwinNonFragileObjCRuntime(cgm) ||
         usesDarwinFragileObjCRuntime(cgm);
}

static bool usesDarwinObjCMachOSections(const CIRGenModule &cgm) {
  return usesDarwinNonFragileObjCRuntime(cgm) &&
         cgm.getTriple().isOSBinFormatMachO();
}

static cir::GlobalLinkageKind
getObjCMetadataLinkage(const CIRGenModule &cgm, llvm::StringRef sectionName) {
  if (cgm.getTriple().isOSBinFormatMachO() &&
      (sectionName.empty() || sectionName.starts_with("__DATA")))
    return cir::GlobalLinkageKind::InternalLinkage;
  return cir::GlobalLinkageKind::PrivateLinkage;
}

static std::string getObjCSectionName(const CIRGenModule &cgm,
                                      llvm::StringRef section,
                                      llvm::StringRef machOAttributes) {
  switch (cgm.getTriple().getObjectFormat()) {
  case llvm::Triple::UnknownObjectFormat:
    llvm_unreachable("unexpected object file format");
  case llvm::Triple::MachO:
    if (machOAttributes.empty())
      return (llvm::Twine("__DATA,") + section).str();
    return (llvm::Twine("__DATA,") + section + "," + machOAttributes).str();
  case llvm::Triple::ELF:
    assert(section.starts_with("__") && "expected the name to begin with __");
    return section.substr(2).str();
  case llvm::Triple::COFF:
    assert(section.starts_with("__") && "expected the name to begin with __");
    return (llvm::Twine(".") + section.substr(2) + "$B").str();
  case llvm::Triple::Wasm:
  case llvm::Triple::GOFF:
  case llvm::Triple::SPIRV:
  case llvm::Triple::XCOFF:
  case llvm::Triple::DXContainer:
    llvm_unreachable(
        "Objective-C support is unimplemented for object file format");
  }

  llvm_unreachable("Unhandled llvm::Triple::ObjectFormatType enum");
}

enum ObjCNonFragileABIClassFlags : uint32_t {
  ObjCNonFragileABIClassMeta = 0x00001,
  ObjCNonFragileABIClassRoot = 0x00002,
  ObjCNonFragileABIClassHasCXXStructors = 0x00004,
  ObjCNonFragileABIClassHidden = 0x00010,
  ObjCNonFragileABIClassException = 0x00020,
  ObjCNonFragileABIClassCompiledByARC = 0x00080,
  ObjCNonFragileABIClassHasCXXDestructorOnly = 0x00100,
};

static bool hasObjCExceptionAttribute(const ObjCInterfaceDecl *oid) {
  if (oid->hasAttr<ObjCExceptionAttr>())
    return true;
  if (const ObjCInterfaceDecl *super = oid->getSuperClass())
    return hasObjCExceptionAttribute(super);
  return false;
}
static bool usesGNUstep2ObjCRuntime(const CIRGenModule &cgm) {
  const ObjCRuntime runtime = cgm.getLangOpts().ObjCRuntime;
  return runtime.getKind() == ObjCRuntime::GNUstep &&
         runtime.getVersion() >= VersionTuple(2);
}

static bool usesGNUObjCRuntime(const CIRGenModule &cgm) {
  const ObjCRuntime runtime = cgm.getLangOpts().ObjCRuntime;
  return runtime.getKind() == ObjCRuntime::GCC ||
         runtime.getKind() == ObjCRuntime::GNUstep ||
         runtime.getKind() == ObjCRuntime::ObjFW;
}

static bool usesLegacyGNUObjCRuntime(const CIRGenModule &cgm) {
  const ObjCRuntime runtime = cgm.getLangOpts().ObjCRuntime;
  return runtime.getKind() == ObjCRuntime::GCC ||
         runtime.getKind() == ObjCRuntime::ObjFW ||
         (runtime.getKind() == ObjCRuntime::GNUstep &&
          runtime.getVersion() < VersionTuple(2));
}

static unsigned getLegacyGNUObjCProtocolVersion(const CIRGenModule &cgm) {
  assert(usesLegacyGNUObjCRuntime(cgm) &&
         "legacy GNU protocol version requires a legacy GNU runtime");
  return cgm.getLangOpts().ObjCRuntime.getKind() == ObjCRuntime::GCC ? 2 : 3;
}

enum DarwinNonFragileObjCClassFlags : uint32_t {
  DarwinNonFragileObjCClassMeta = 0x00001,
  DarwinNonFragileObjCClassRoot = 0x00002,
  DarwinNonFragileObjCClassHasCXXStructors = 0x00004,
  DarwinNonFragileObjCClassHidden = 0x00010,
  DarwinNonFragileObjCClassException = 0x00020,
  DarwinNonFragileObjCClassCompiledByARC = 0x00080,
  DarwinNonFragileObjCClassHasCXXDestructorOnly = 0x00100,
};

enum DarwinFragileObjCClassFlags : uint32_t {
  DarwinFragileObjCClassFactory = 0x00001,
  DarwinFragileObjCClassMeta = 0x00002,
  DarwinFragileObjCClassHasCXXStructors = 0x02000,
  DarwinFragileObjCClassHidden = 0x20000,
  DarwinFragileObjCClassCompiledByARC = 0x04000000,
};
static const ObjCProtocolDecl *
getCanonicalObjCProtocolDecl(const ObjCProtocolDecl *pd) {
  if (const auto *definition = pd->getDefinition())
    return definition;
  return pd;
}

static void appendFirstImpliedRuntimeProtocols(
    const ObjCProtocolDecl *pd,
    llvm::SmallVectorImpl<const ObjCProtocolDecl *> &protocols,
    llvm::DenseSet<const ObjCProtocolDecl *> &seenProtocols) {
  pd = getCanonicalObjCProtocolDecl(pd);
  if (!pd->isNonRuntimeProtocol()) {
    if (seenProtocols.insert(pd).second)
      protocols.push_back(pd);
    return;
  }

  for (const auto *parent : pd->protocols())
    appendFirstImpliedRuntimeProtocols(parent, protocols, seenProtocols);
}

template <typename IteratorT>
static llvm::SmallVector<const ObjCProtocolDecl *, 8>
getRuntimeProtocolList(IteratorT begin, IteratorT end) {
  llvm::SmallVector<const ObjCProtocolDecl *, 8> runtimeProtocols;
  llvm::DenseSet<const ObjCProtocolDecl *> nonRuntimeProtocols;

  for (; begin != end; ++begin) {
    const auto *pd = getCanonicalObjCProtocolDecl(*begin);
    if (pd->isNonRuntimeProtocol())
      nonRuntimeProtocols.insert(pd);
    else
      runtimeProtocols.push_back(pd);
  }

  if (nonRuntimeProtocols.empty())
    return runtimeProtocols;

  llvm::SmallVector<const ObjCProtocolDecl *, 8> firstImpliedProtocols;
  llvm::DenseSet<const ObjCProtocolDecl *> seenFirstImpliedProtocols;
  for (const auto *pd : nonRuntimeProtocols)
    appendFirstImpliedRuntimeProtocols(pd, firstImpliedProtocols,
                                       seenFirstImpliedProtocols);

  llvm::DenseSet<const ObjCProtocolDecl *> allImpliedProtocols;
  for (const auto *pd : runtimeProtocols) {
    allImpliedProtocols.insert(pd);
    pd->getImpliedProtocols(allImpliedProtocols);
  }

  for (const auto *pd : firstImpliedProtocols)
    pd->getImpliedProtocols(allImpliedProtocols);

  for (const auto *pd : firstImpliedProtocols) {
    if (!allImpliedProtocols.contains(pd))
      runtimeProtocols.push_back(pd);
  }

  return runtimeProtocols;
}

static std::string mangleGNUObjCPublicSymbol(const CIRGenModule &cgm,
                                             llvm::StringRef prefix,
                                             llvm::StringRef name) {
  return (llvm::Twine(cgm.getTriple().isOSBinFormatCOFF() ? "$_" : "._") +
          prefix + name)
      .str();
}

static std::string
mangleGNUObjCTypeEncodingForSymbol(const CIRGenModule &cgm, QualType ty) {
  std::string typeEncoding;
  cgm.getASTContext().getObjCEncodingForType(ty, typeEncoding);
  if (cgm.getTriple().isOSBinFormatELF())
    llvm::replace(typeEncoding, '@', '\1');
  if (cgm.getTriple().isOSWindows())
    llvm::replace(typeEncoding, '=', '\2');
  return typeEncoding;
}

static std::string
mangleGNUObjCTypeEncodingForSymbol(const CIRGenModule &cgm,
                                   llvm::StringRef typeEncoding) {
  std::string mangledTypeEncoding = typeEncoding.str();
  if (cgm.getTriple().isOSBinFormatELF())
    llvm::replace(mangledTypeEncoding, '@', '\1');
  if (cgm.getTriple().isOSWindows())
    llvm::replace(mangledTypeEncoding, '=', '\2');
  return mangledTypeEncoding;
}

static std::string getGNUObjCSelectorNameSymbol(Selector sel) {
  return (llvm::Twine(".objc_sel_name_") + sel.getAsString()).str();
}

static std::string
getGNUObjCSelectorTypeSymbol(const CIRGenModule &cgm,
                             llvm::StringRef typeEncoding) {
  return (llvm::Twine(".objc_sel_types_") +
          mangleGNUObjCTypeEncodingForSymbol(cgm, typeEncoding))
      .str();
}

static std::string getGNUObjCSelectorSymbol(const CIRGenModule &cgm,
                                            Selector sel,
                                            llvm::StringRef typeEncoding) {
  return (llvm::Twine(".objc_selector_") + sel.getAsString() + "_" +
          mangleGNUObjCTypeEncodingForSymbol(cgm, typeEncoding))
      .str();
}

cir::GlobalOp CIRGenModule::getObjCSelectorNameGlobal(Selector sel) {
  std::string selectorName = sel.getAsString();
  if (auto it = objcSelectorNameGlobals.find(selectorName);
      it != objcSelectorNameGlobals.end())
    return it->second;

  const ObjCRuntime runtime = getLangOpts().ObjCRuntime;
  mlir::Location loc = builder.getUnknownLoc();
  std::string bytes = selectorName;
  bytes.push_back('\0');
  mlir::Attribute init = builder.getString(bytes, uCharTy, bytes.size());
  mlir::Type arrayTy = mlir::cast<mlir::TypedAttr>(init).getType();

  cir::GlobalOp gv;
  if (runtime.isNeXTFamily()) {
    gv = createGlobalOp(*this, loc, getUniqueGlobalName("OBJC_METH_VAR_NAME_"),
                        arrayTy, /*isConstant=*/true);
    gv.setLinkage(cir::GlobalLinkageKind::PrivateLinkage);
    mlir::SymbolTable::setSymbolVisibility(
        gv, mlir::SymbolTable::Visibility::Private);
    gv.setAlignmentAttr(getSize(CharUnits::One()));
    gv->setAttr("unnamed_addr", mlir::UnitAttr::get(&getMLIRContext()));
    if (getTriple().isOSDarwin() && getTriple().isOSBinFormatMachO()) {
      const bool isNonFragile = usesDarwinNonFragileObjCRuntime(*this);
      gv.setSectionAttr(
          getStringAttrCached(isNonFragile
                                  ? "__TEXT,__objc_methname,cstring_literals"
                                  : "__TEXT,__cstring,cstring_literals"));
    }
    setInitializer(gv, init);
    setDSOLocal(static_cast<mlir::Operation *>(gv));
    addCompilerUsedGlobal(gv);
  } else if (runtime.getKind() == ObjCRuntime::GCC ||
             runtime.getKind() == ObjCRuntime::GNUstep ||
             runtime.getKind() == ObjCRuntime::ObjFW) {
    if (usesGNUstep2ObjCRuntime(*this)) {
      gv = createGlobalOp(*this, loc, getGNUObjCSelectorNameSymbol(sel), arrayTy,
                          /*isConstant=*/true);
      gv.setLinkage(cir::GlobalLinkageKind::LinkOnceODRLinkage);
      gv.setGlobalVisibility(cir::VisibilityKind::Hidden);
      mlir::SymbolTable::setSymbolVisibility(gv, getMLIRVisibility(gv));
      if (supportsCOMDAT())
        gv.setComdat(true);
    } else {
      gv = createGlobalOp(*this, loc, getUniqueGlobalName(".objc_sel_name"),
                          arrayTy, /*isConstant=*/true);
      gv.setLinkage(cir::GlobalLinkageKind::PrivateLinkage);
      mlir::SymbolTable::setSymbolVisibility(
          gv, mlir::SymbolTable::Visibility::Private);
      gv->setAttr("unnamed_addr", mlir::UnitAttr::get(&getMLIRContext()));
      setDSOLocal(static_cast<mlir::Operation *>(gv));
    }
    gv.setAlignmentAttr(getSize(CharUnits::One()));
    setInitializer(gv, init);
  } else {
    llvm_unreachable("ObjC selector globals on unsupported runtime");
  }

  objcSelectorNameGlobals[selectorName] = gv;
  return gv;
}

cir::GlobalOp CIRGenModule::getObjCSelectorRefGlobal(Selector sel) {
  assert(usesDarwinObjCRuntime(*this) &&
         "ObjC selector refs currently require a Darwin Objective-C runtime");

  std::string selectorName = sel.getAsString();
  if (auto it = objcSelectorRefGlobals.find(selectorName);
      it != objcSelectorRefGlobals.end())
    return it->second;

  cir::GlobalOp selNameGV = getObjCSelectorNameGlobal(sel);
  mlir::Location loc = builder.getUnknownLoc();
  cir::PointerType selPtrTy =
      builder.getPointerTo(uInt8Ty, selNameGV.getAddrSpaceAttr());

  cir::GlobalOp gv = createGlobalOp(
      *this, loc, getUniqueGlobalName("OBJC_SELECTOR_REFERENCES_"), selPtrTy,
      /*isConstant=*/false);
  std::string sectionName =
      usesDarwinNonFragileObjCRuntime(*this)
          ? getObjCSectionName(*this, "__objc_selrefs",
                               "literal_pointers,no_dead_strip")
          : "__OBJC,__message_refs,literal_pointers,no_dead_strip";
  gv.setLinkage(getObjCMetadataLinkage(*this, sectionName));
  mlir::SymbolTable::setSymbolVisibility(gv,
                                         mlir::SymbolTable::Visibility::Private);
  gv.setAlignmentAttr(getSize(getPointerAlign()));
  gv.setSectionAttr(getStringAttrCached(sectionName));
  gv->setAttr(cir::CUDAExternallyInitializedAttr::getMnemonic(),
              cir::CUDAExternallyInitializedAttr::get(&getMLIRContext()));
  setInitializer(
      gv, cir::GlobalViewAttr::get(
              selPtrTy, mlir::FlatSymbolRefAttr::get(selNameGV.getSymNameAttr())));
  setDSOLocal(static_cast<mlir::Operation *>(gv));
  addCompilerUsedGlobal(gv);

  objcSelectorRefGlobals[selectorName] = gv;
  return gv;
}

cir::GlobalOp
CIRGenModule::getObjCGNUSelectorGlobal(Selector sel,
                                       const ObjCMethodDecl *method) {
  const ObjCRuntime runtime = getLangOpts().ObjCRuntime;
  assert((runtime.getKind() == ObjCRuntime::GCC ||
          runtime.getKind() == ObjCRuntime::GNUstep ||
          runtime.getKind() == ObjCRuntime::ObjFW) &&
         "ObjC GNU selector globals require a GNU-family runtime");

  std::string typeEncodingStorage;
  if (method)
    typeEncodingStorage = getASTContext().getObjCEncodingForMethodDecl(method);
  llvm::StringRef typeEncoding = typeEncodingStorage;

  std::string cacheKey = sel.getAsString();
  cacheKey.push_back('\0');
  cacheKey += typeEncoding;

  if (auto it = objcSelectorGlobals.find(cacheKey);
      it != objcSelectorGlobals.end())
    return it->second;

  mlir::Location loc = builder.getUnknownLoc();
  cir::GlobalOp selectorNameGV = getObjCSelectorNameGlobal(sel);
  auto i8PtrTy = builder.getPointerTo(uInt8Ty);
  mlir::Attribute typesField = builder.getConstPtrAttr(i8PtrTy, 0);
  const bool useSharedGNUstepSelectors = usesGNUstep2ObjCRuntime(*this);
  if (!typeEncoding.empty()) {
    cir::GlobalOp selectorTypesGV;
    if (auto it = objcSelectorTypeGlobals.find(typeEncoding);
        it != objcSelectorTypeGlobals.end()) {
      selectorTypesGV = it->second;
    } else {
      std::string bytes = typeEncoding.str();
      bytes.push_back('\0');
      auto typeInit = mlir::cast<mlir::TypedAttr>(
          builder.getString(bytes, uCharTy, bytes.size()));
      selectorTypesGV = createGlobalOp(
          *this, loc,
          useSharedGNUstepSelectors
              ? getGNUObjCSelectorTypeSymbol(*this, typeEncoding)
              : getUniqueGlobalName(".objc_sel_types"),
          typeInit.getType(), /*isConstant=*/true);
      if (useSharedGNUstepSelectors) {
        selectorTypesGV.setLinkage(cir::GlobalLinkageKind::LinkOnceODRLinkage);
        selectorTypesGV.setGlobalVisibility(cir::VisibilityKind::Hidden);
        mlir::SymbolTable::setSymbolVisibility(selectorTypesGV,
                                               getMLIRVisibility(selectorTypesGV));
        if (supportsCOMDAT())
          selectorTypesGV.setComdat(true);
      } else {
        selectorTypesGV.setLinkage(cir::GlobalLinkageKind::PrivateLinkage);
        mlir::SymbolTable::setSymbolVisibility(
            selectorTypesGV, mlir::SymbolTable::Visibility::Private);
        selectorTypesGV->setAttr("unnamed_addr",
                                 mlir::UnitAttr::get(&getMLIRContext()));
        setDSOLocal(static_cast<mlir::Operation *>(selectorTypesGV));
      }
      selectorTypesGV.setAlignmentAttr(getSize(CharUnits::One()));
      setInitializer(selectorTypesGV, typeInit);
      objcSelectorTypeGlobals[typeEncoding] = selectorTypesGV;
    }

    typesField = builder.getGlobalViewAttr(i8PtrTy, selectorTypesGV);
  }

  auto selectorInit = builder.getAnonConstRecord(mlir::ArrayAttr::get(
      &getMLIRContext(),
      {builder.getGlobalViewAttr(i8PtrTy, selectorNameGV), typesField}));
  cir::GlobalOp selectorGV = createGlobalOp(
      *this, loc,
      useSharedGNUstepSelectors
          ? getGNUObjCSelectorSymbol(*this, sel, typeEncoding)
          : getUniqueGlobalName(".objc_selector"),
      selectorInit.getType(),
      /*isConstant=*/false);
  if (useSharedGNUstepSelectors) {
    selectorGV.setLinkage(cir::GlobalLinkageKind::LinkOnceODRLinkage);
    selectorGV.setGlobalVisibility(cir::VisibilityKind::Hidden);
    mlir::SymbolTable::setSymbolVisibility(selectorGV,
                                           getMLIRVisibility(selectorGV));
    if (supportsCOMDAT())
      selectorGV.setComdat(true);
  } else {
    selectorGV.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
    mlir::SymbolTable::setSymbolVisibility(selectorGV,
                                           mlir::SymbolTable::Visibility::Private);
    setDSOLocal(static_cast<mlir::Operation *>(selectorGV));
  }
  selectorGV.setAlignmentAttr(getSize(getPointerAlign()));
  selectorGV.setSectionAttr(getStringAttrCached(
      getGNUstep2ObjCSectionName(*this, GNUstep2SelectorSection)));
  setInitializer(selectorGV, selectorInit);
  objcSelectorGlobals[cacheKey] = selectorGV;
  return selectorGV;
}

  mlir::Value CIRGenModule::getObjCSelector(CIRGenFunction &cgf, Selector sel,
                                          mlir::Location loc,
                                          const ObjCMethodDecl *method) {
  const ObjCRuntime runtime = getLangOpts().ObjCRuntime;
  if (usesDarwinObjCRuntime(*this)) {
    cir::GlobalOp selRefGV = getObjCSelectorRefGlobal(sel);
    mlir::Value selRefAddr = builder.createGetGlobal(loc, selRefGV);
    cir::LoadOp load = builder.createLoad(
        loc, Address(selRefAddr, selRefGV.getSymType(), getPointerAlign()));
    if (usesDarwinNonFragileObjCRuntime(*this))
      load->setAttr("cir.invariant_load",
                    mlir::UnitAttr::get(&getMLIRContext()));
    return load.getResult();
  }

  assert((runtime.getKind() == ObjCRuntime::GCC ||
          runtime.getKind() == ObjCRuntime::GNUstep ||
          runtime.getKind() == ObjCRuntime::ObjFW) &&
         "ObjC selectors currently require Darwin or GNU runtime support");

  cir::GlobalOp selectorGV = getObjCGNUSelectorGlobal(sel, method);

  auto loweredSelTy =
      mlir::cast<cir::PointerType>(convertType(getASTContext().getObjCSelType()));
  auto selectorPtrTy =
      builder.getPointerTo(selectorGV.getSymType(), selectorGV.getAddrSpaceAttr());
  mlir::Value selector =
      builder.getConstant(loc, builder.getGlobalViewAttr(selectorPtrTy, selectorGV));
  if (selector.getType() != loweredSelTy)
    selector = builder.createBitcast(loc, selector, loweredSelTy);
  return selector;
}

cir::GlobalOp CIRGenModule::getObjCClassNameGlobal(llvm::StringRef className) {
  assert(usesDarwinObjCRuntime(*this) &&
         "ObjC class-name globals currently require a Darwin Objective-C "
         "runtime");

  if (auto it = objcClassNameGlobals.find(className);
      it != objcClassNameGlobals.end())
    return it->second;

  mlir::Location loc = builder.getUnknownLoc();
  std::string bytes = className.str();
  bytes.push_back('\0');
  auto init =
      mlir::cast<mlir::TypedAttr>(builder.getString(bytes, uCharTy, bytes.size()));

  cir::GlobalOp gv =
      createGlobalOp(*this, loc, getUniqueGlobalName("OBJC_CLASS_NAME_"),
                     init.getType(), /*isConstant=*/true);
  gv.setLinkage(cir::GlobalLinkageKind::PrivateLinkage);
  mlir::SymbolTable::setSymbolVisibility(gv,
                                         mlir::SymbolTable::Visibility::Private);
  gv.setAlignmentAttr(getSize(CharUnits::One()));
  gv->setAttr("unnamed_addr", mlir::UnitAttr::get(&getMLIRContext()));
  if (usesDarwinNonFragileObjCRuntime(*this))
    gv.setSectionAttr(
        getStringAttrCached("__TEXT,__objc_classname,cstring_literals"));
  else
    gv.setSectionAttr(getStringAttrCached("__TEXT,__cstring,cstring_literals"));
  setInitializer(gv, init);
  setDSOLocal(static_cast<mlir::Operation *>(gv));
  addCompilerUsedGlobal(gv);

  objcClassNameGlobals[className] = gv;
  return gv;
}

cir::GlobalOp
CIRGenModule::getObjCMethodTypeGlobal(llvm::StringRef typeEncoding) {
  assert(usesDarwinObjCRuntime(*this) &&
         "ObjC method-type globals currently require a Darwin Objective-C "
         "runtime");

  if (auto it = objcMethodTypeGlobals.find(typeEncoding);
      it != objcMethodTypeGlobals.end())
    return it->second;

  mlir::Location loc = builder.getUnknownLoc();
  std::string bytes = typeEncoding.str();
  bytes.push_back('\0');
  auto init =
      mlir::cast<mlir::TypedAttr>(builder.getString(bytes, uCharTy, bytes.size()));

  cir::GlobalOp gv =
      createGlobalOp(*this, loc, getUniqueGlobalName("OBJC_METH_VAR_TYPE_"),
                     init.getType(), /*isConstant=*/true);
  gv.setLinkage(cir::GlobalLinkageKind::PrivateLinkage);
  mlir::SymbolTable::setSymbolVisibility(gv,
                                         mlir::SymbolTable::Visibility::Private);
  gv.setAlignmentAttr(getSize(CharUnits::One()));
  gv->setAttr("unnamed_addr", mlir::UnitAttr::get(&getMLIRContext()));
  if (getTriple().isOSDarwin() && getTriple().isOSBinFormatMachO())
    gv.setSectionAttr(getStringAttrCached(
        usesDarwinNonFragileObjCRuntime(*this)
            ? "__TEXT,__objc_methtype,cstring_literals"
            : "__TEXT,__cstring,cstring_literals"));
  setInitializer(gv, init);
  setDSOLocal(static_cast<mlir::Operation *>(gv));
  addCompilerUsedGlobal(gv);

  objcMethodTypeGlobals[typeEncoding] = gv;
  return gv;
}

void CIRGenModule::mangleObjCMethodName(const ObjCMethodDecl *method,
                                        llvm::raw_ostream &os,
                                        bool includePrefixByte) {
  getCXXABI().getMangleContext().mangleObjCMethodName(
      method, os, includePrefixByte,
      /*includeCategory=*/!method->isDirectMethod());
}

cir::GlobalOp
CIRGenModule::getObjCPropertyNameGlobal(llvm::StringRef value) {
  assert(usesDarwinObjCRuntime(*this) &&
         "ObjC property metadata strings currently require a Darwin "
         "Objective-C runtime");

  if (auto it = objcPropertyNameGlobals.find(value);
      it != objcPropertyNameGlobals.end())
    return it->second;

  mlir::Location loc = builder.getUnknownLoc();
  std::string bytes = value.str();
  bytes.push_back('\0');
  auto init =
      mlir::cast<mlir::TypedAttr>(builder.getString(bytes, uCharTy, bytes.size()));

  cir::GlobalOp gv =
      createGlobalOp(*this, loc, getUniqueGlobalName("OBJC_PROP_NAME_ATTR_"),
                     init.getType(), /*isConstant=*/true);
  gv.setLinkage(cir::GlobalLinkageKind::PrivateLinkage);
  mlir::SymbolTable::setSymbolVisibility(gv,
                                         mlir::SymbolTable::Visibility::Private);
  gv.setAlignmentAttr(getSize(CharUnits::One()));
  gv->setAttr("unnamed_addr", mlir::UnitAttr::get(&getMLIRContext()));
  if (getTriple().isOSDarwin() && getTriple().isOSBinFormatMachO())
    gv.setSectionAttr(getStringAttrCached(
        usesDarwinNonFragileObjCRuntime(*this)
            ? "__TEXT,__objc_methname,cstring_literals"
            : "__TEXT,__cstring,cstring_literals"));
  setInitializer(gv, init);
  setDSOLocal(static_cast<mlir::Operation *>(gv));
  addCompilerUsedGlobal(gv);

  objcPropertyNameGlobals[value] = gv;
  return gv;
}

cir::GlobalOp CIRGenModule::getObjCProtocolGlobal(mlir::Location loc,
                                                  const ObjCProtocolDecl *pd) {
  if (usesDarwinFragileObjCRuntime(*this)) {
    assert(pd && !pd->isNonRuntimeProtocol() &&
           "expected runtime Objective-C protocol");

    pd = getCanonicalObjCProtocolDecl(pd);
    std::string runtimeName = pd->getObjCRuntimeNameAsString().str();
    auto i32Ty = builder.getUInt32Ty();
    auto longTy = mlir::cast<cir::IntType>(convertType(astContext.LongTy));
    mlir::Attribute nullPtr = builder.getConstPtrAttr(voidPtrTy, 0);
    uint64_t pointerSize = getPointerAlign().getQuantity();
    uint64_t protocolExtensionSize =
        llvm::alignTo(uint64_t(4), pointerSize) + 5 * pointerSize;
    auto methodDescTy = builder.getAnonRecordTy(
        {voidPtrTy, voidPtrTy}, /*packed=*/false, /*padded=*/false);
    auto propertyEntryTy = builder.getAnonRecordTy(
        {voidPtrTy, voidPtrTy}, /*packed=*/false, /*padded=*/false);
    auto protocolExtTy = builder.getAnonRecordTy(
        {i32Ty, voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy},
        /*packed=*/false, /*padded=*/false);
    auto protocolTy = builder.getAnonRecordTy(
        {voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy},
        /*packed=*/false, /*padded=*/false);
    std::string protocolName = "OBJC_PROTOCOL_" + pd->getNameAsString();

    cir::GlobalOp gv;
    if (auto it = objcProtocolGlobals.find(pd); it != objcProtocolGlobals.end()) {
      gv = it->second;
      assert(gv.getSymType() == protocolTy &&
             "Darwin fragile protocol global has unexpected type");
      if (!pd->isThisDeclarationADefinition() || !gv.isDeclaration())
        return gv;
    } else {
      gv = mlir::dyn_cast_or_null<cir::GlobalOp>(getGlobalValue(protocolName));
      if (!gv)
        gv = createGlobalOp(*this, loc, protocolName, protocolTy,
                            /*isConstant=*/false);
      else
        assert(gv.getSymType() == protocolTy &&
               "Darwin fragile protocol global has unexpected type");
      objcProtocolGlobals[pd] = gv;
    }

    gv.setConstant(false);
    gv.setLinkage(cir::GlobalLinkageKind::PrivateLinkage);
    mlir::SymbolTable::setSymbolVisibility(gv,
                                           mlir::SymbolTable::Visibility::Private);
    gv.setAlignmentAttr(getSize(getPointerAlign()));
    gv.setSectionAttr(
        getStringAttrCached("__OBJC,__protocol,regular,no_dead_strip"));
    setDSOLocal(static_cast<mlir::Operation *>(gv));
    if (!pd->isThisDeclarationADefinition())
      return gv;

    auto createFragileMetadataGlobal =
        [&](llvm::StringRef name, mlir::Type type, mlir::Attribute init,
            llvm::StringRef section, cir::GlobalLinkageKind linkage)
        -> cir::GlobalOp {
      cir::GlobalOp global =
          createGlobalOp(*this, loc, name, type, /*isConstant=*/false);
      global.setLinkage(linkage);
      mlir::SymbolTable::setSymbolVisibility(
          global, linkage == cir::GlobalLinkageKind::InternalLinkage
                      ? mlir::SymbolTable::Visibility::Private
                      : mlir::SymbolTable::Visibility::Private);
      global.setAlignmentAttr(getSize(getPointerAlign()));
      if (!section.empty())
        global.setSectionAttr(getStringAttrCached(section));
      setInitializer(global, init);
      setDSOLocal(static_cast<mlir::Operation *>(global));
      addCompilerUsedGlobal(global);
      return global;
    };

    auto emitMethodList =
        [&](llvm::StringRef symbolName, llvm::StringRef sectionName,
            llvm::ArrayRef<const ObjCMethodDecl *> methods) -> mlir::Attribute {
      if (methods.empty())
        return nullPtr;

      llvm::SmallVector<mlir::Attribute, 8> methodAttrs;
      methodAttrs.reserve(methods.size());
      for (const auto *method : methods) {
        cir::GlobalOp methodNameGV =
            getObjCSelectorNameGlobal(method->getSelector());
        std::string typeEncoding =
            astContext.getObjCEncodingForMethodDecl(method, /*Extended=*/false);
        cir::GlobalOp methodTypeGV = getObjCMethodTypeGlobal(typeEncoding);
        methodAttrs.push_back(builder.getAnonConstRecord(
            builder.getArrayAttr(
                {builder.getGlobalViewAttr(voidPtrTy, methodNameGV),
                 builder.getGlobalViewAttr(voidPtrTy, methodTypeGV)}),
            /*packed=*/false, /*padded=*/false, methodDescTy));
      }

      auto methodArrayTy = cir::ArrayType::get(methodDescTy, methodAttrs.size());
      auto methodArrayInit = cir::ConstArrayAttr::get(
          methodArrayTy, builder.getArrayAttr(methodAttrs));
      auto methodListTy = builder.getAnonRecordTy(
          {i32Ty, methodArrayTy}, /*packed=*/false, /*padded=*/false);
      auto methodListInit = builder.getAnonConstRecord(
          builder.getArrayAttr(
              {cir::IntAttr::get(i32Ty, methodAttrs.size()), methodArrayInit}),
          /*packed=*/false, /*padded=*/false, methodListTy);
      cir::GlobalOp methodListGV = createFragileMetadataGlobal(
          symbolName, methodListTy, methodListInit, sectionName,
          cir::GlobalLinkageKind::PrivateLinkage);
      return builder.getGlobalViewAttr(voidPtrTy, methodListGV);
    };

    auto emitPropertyList = [&](llvm::StringRef symbolName,
                                bool isClassProperty) -> mlir::Attribute {
      if (isClassProperty) {
        const llvm::Triple &triple = getTriple();
        if ((triple.isMacOSX() && triple.isMacOSXVersionLT(10, 11)) ||
            (triple.isiOS() && triple.isOSVersionLT(9)))
          return nullPtr;
      }

      llvm::SmallVector<const ObjCPropertyDecl *, 8> properties;
      for (const auto *property : pd->properties()) {
        if (property->isClassProperty() != isClassProperty ||
            property->isDirectProperty())
          continue;
        properties.push_back(property);
      }
      if (properties.empty())
        return nullPtr;

      llvm::SmallVector<mlir::Attribute, 8> propertyAttrs;
      propertyAttrs.reserve(properties.size());
      for (const auto *property : properties) {
        cir::GlobalOp propertyNameGV =
            getObjCPropertyNameGlobal(property->getIdentifier()->getName());
        std::string propertyType =
            astContext.getObjCEncodingForPropertyDecl(property, nullptr);
        cir::GlobalOp propertyTypeGV = getObjCPropertyNameGlobal(propertyType);
        propertyAttrs.push_back(builder.getAnonConstRecord(
            builder.getArrayAttr(
                {builder.getGlobalViewAttr(voidPtrTy, propertyNameGV),
                 builder.getGlobalViewAttr(voidPtrTy, propertyTypeGV)}),
            /*packed=*/false, /*padded=*/false, propertyEntryTy));
      }

      auto propertyArrayTy =
          cir::ArrayType::get(propertyEntryTy, propertyAttrs.size());
      auto propertyArrayInit = cir::ConstArrayAttr::get(
          propertyArrayTy, builder.getArrayAttr(propertyAttrs));
      auto propertyListTy = builder.getAnonRecordTy(
          {i32Ty, i32Ty, propertyArrayTy}, /*packed=*/false, /*padded=*/false);
      auto propertyListInit = builder.getAnonConstRecord(
          builder.getArrayAttr(
              {cir::IntAttr::get(i32Ty, 2 * pointerSize),
               cir::IntAttr::get(i32Ty, propertyAttrs.size()), propertyArrayInit}),
          /*packed=*/false, /*padded=*/false, propertyListTy);
      cir::GlobalOp propertyListGV = createFragileMetadataGlobal(
          symbolName, propertyListTy, propertyListInit,
          "__OBJC,__property,regular,no_dead_strip",
          cir::GlobalLinkageKind::PrivateLinkage);
      return builder.getGlobalViewAttr(voidPtrTy, propertyListGV);
    };

    auto emitProtocolList =
        [&](llvm::StringRef symbolName,
            ObjCProtocolDecl::protocol_iterator begin,
            ObjCProtocolDecl::protocol_iterator end) -> mlir::Attribute {
      llvm::SmallVector<mlir::Attribute, 8> protocolAttrs;
      for (const auto *inherited : getRuntimeProtocolList(begin, end)) {
        protocolAttrs.push_back(builder.getGlobalViewAttr(
            voidPtrTy, getObjCProtocolGlobal(loc, inherited)));
      }

      if (protocolAttrs.empty())
        return nullPtr;

      protocolAttrs.push_back(nullPtr);
      auto protocolArrayTy = cir::ArrayType::get(voidPtrTy, protocolAttrs.size());
      auto protocolArrayInit = cir::ConstArrayAttr::get(
          protocolArrayTy, builder.getArrayAttr(protocolAttrs));
      auto protocolListTy = builder.getAnonRecordTy(
          {voidPtrTy, longTy, protocolArrayTy}, /*packed=*/false,
          /*padded=*/false);
      auto protocolListInit = builder.getAnonConstRecord(
          builder.getArrayAttr(
              {nullPtr,
               cir::IntAttr::get(longTy, protocolAttrs.size() - 1),
               protocolArrayInit}),
          /*packed=*/false, /*padded=*/false, protocolListTy);
      cir::GlobalOp protocolListGV = createFragileMetadataGlobal(
          symbolName, protocolListTy, protocolListInit,
          "__OBJC,__cat_cls_meth,regular,no_dead_strip",
          cir::GlobalLinkageKind::PrivateLinkage);
      return builder.getGlobalViewAttr(voidPtrTy, protocolListGV);
    };

    llvm::SmallVector<const ObjCMethodDecl *, 4> requiredInstanceMethods;
    llvm::SmallVector<const ObjCMethodDecl *, 4> requiredClassMethods;
    llvm::SmallVector<const ObjCMethodDecl *, 4> optionalInstanceMethods;
    llvm::SmallVector<const ObjCMethodDecl *, 4> optionalClassMethods;
    for (const auto *method : pd->instance_methods()) {
      (method->isOptional() ? optionalInstanceMethods : requiredInstanceMethods)
          .push_back(method);
    }
    for (const auto *method : pd->class_methods()) {
      (method->isOptional() ? optionalClassMethods : requiredClassMethods)
          .push_back(method);
    }

    llvm::SmallVector<mlir::Attribute, 8> extendedMethodTypes;
    auto collectExtendedTypes = [&](llvm::ArrayRef<const ObjCMethodDecl *> methods) {
      for (const auto *method : methods) {
        std::string typeEncoding =
            astContext.getObjCEncodingForMethodDecl(method, /*Extended=*/true);
        cir::GlobalOp methodTypeGV = getObjCMethodTypeGlobal(typeEncoding);
        extendedMethodTypes.push_back(
            builder.getGlobalViewAttr(voidPtrTy, methodTypeGV));
      }
    };
    collectExtendedTypes(requiredInstanceMethods);
    collectExtendedTypes(requiredClassMethods);
    collectExtendedTypes(optionalInstanceMethods);
    collectExtendedTypes(optionalClassMethods);

    mlir::Attribute extendedMethodTypesField = nullPtr;
    if (!extendedMethodTypes.empty()) {
      auto methodTypesArrayTy =
          cir::ArrayType::get(voidPtrTy, extendedMethodTypes.size());
      auto methodTypesInit = cir::ConstArrayAttr::get(
          methodTypesArrayTy, builder.getArrayAttr(extendedMethodTypes));
      cir::GlobalOp methodTypesGV = createFragileMetadataGlobal(
          "OBJC_PROTOCOL_METHOD_TYPES_" + pd->getNameAsString(),
          methodTypesArrayTy, methodTypesInit, StringRef(),
          cir::GlobalLinkageKind::InternalLinkage);
      extendedMethodTypesField =
          builder.getGlobalViewAttr(voidPtrTy, methodTypesGV);
    }

    mlir::Attribute optInstanceMethodsField = emitMethodList(
        "OBJC_PROTOCOL_INSTANCE_METHODS_OPT_" + pd->getNameAsString(),
        "__OBJC,__cat_inst_meth,regular,no_dead_strip",
        optionalInstanceMethods);
    mlir::Attribute optClassMethodsField = emitMethodList(
        "OBJC_PROTOCOL_CLASS_METHODS_OPT_" + pd->getNameAsString(),
        "__OBJC,__cat_cls_meth,regular,no_dead_strip", optionalClassMethods);
    mlir::Attribute instancePropertiesField = emitPropertyList(
        "OBJC_$_PROP_PROTO_LIST_" + pd->getNameAsString(),
        /*isClassProperty=*/false);
    mlir::Attribute classPropertiesField = emitPropertyList(
        "OBJC_$_CLASS_PROP_PROTO_LIST_" + pd->getNameAsString(),
        /*isClassProperty=*/true);

    mlir::Attribute protocolExtField = nullPtr;
    if (optInstanceMethodsField != nullPtr || optClassMethodsField != nullPtr ||
        instancePropertiesField != nullPtr || classPropertiesField != nullPtr ||
        extendedMethodTypesField != nullPtr) {
      auto protocolExtInit = builder.getAnonConstRecord(
          builder.getArrayAttr(
              {cir::IntAttr::get(i32Ty, protocolExtensionSize),
               optInstanceMethodsField, optClassMethodsField,
               instancePropertiesField, extendedMethodTypesField,
               classPropertiesField}),
          /*packed=*/false, /*padded=*/false, protocolExtTy);
      cir::GlobalOp protocolExtGV = createFragileMetadataGlobal(
          "_OBJC_PROTOCOLEXT_" + pd->getNameAsString(), protocolExtTy,
          protocolExtInit, StringRef(),
          cir::GlobalLinkageKind::InternalLinkage);
      protocolExtField = builder.getGlobalViewAttr(voidPtrTy, protocolExtGV);
    }

    auto protocolInit = builder.getAnonConstRecord(
        builder.getArrayAttr(
            {protocolExtField,
             builder.getGlobalViewAttr(voidPtrTy,
                                       getObjCClassNameGlobal(runtimeName)),
             emitProtocolList("OBJC_PROTOCOL_REFS_" + pd->getNameAsString(),
                              pd->protocol_begin(), pd->protocol_end()),
             emitMethodList("OBJC_PROTOCOL_INSTANCE_METHODS_" +
                                pd->getNameAsString(),
                            "__OBJC,__cat_inst_meth,regular,no_dead_strip",
                            requiredInstanceMethods),
             emitMethodList("OBJC_PROTOCOL_CLASS_METHODS_" +
                                pd->getNameAsString(),
                            "__OBJC,__cat_cls_meth,regular,no_dead_strip",
                            requiredClassMethods)}),
        /*packed=*/false, /*padded=*/false, protocolTy);
    setInitializer(gv, protocolInit);
    addCompilerUsedGlobal(gv);
    return gv;
  }

  if (usesLegacyGNUObjCRuntime(*this)) {
    assert(pd && !pd->isNonRuntimeProtocol() &&
           "expected runtime Objective-C protocol");

    pd = getCanonicalObjCProtocolDecl(pd);
    if (auto it = objcProtocolGlobals.find(pd); it != objcProtocolGlobals.end())
      return it->second;

    auto i8Ty = builder.getUInt8Ty();
    auto i32Ty = builder.getUInt32Ty();
    auto longTy = mlir::cast<cir::IntType>(convertType(astContext.LongTy));
    mlir::Attribute nullPtr = builder.getConstPtrAttr(voidPtrTy, 0);

    auto createCStringAttr = [&](llvm::StringRef prefix,
                                 llvm::StringRef value) -> mlir::Attribute {
      return builder.getGlobalViewAttr(
          voidPtrTy, getGlobalForConstantCString(loc, value, prefix));
    };

    auto createPropertyNameAttr = [&](const ObjCPropertyDecl *property)
        -> mlir::Attribute {
      const ObjCRuntime runtime = getLangOpts().ObjCRuntime;
      if (runtime.getKind() == ObjCRuntime::GNUstep &&
          runtime.getVersion() >= VersionTuple(1, 6)) {
        std::string encoding =
            astContext.getObjCEncodingForPropertyDecl(property, nullptr);
        std::string nameAndAttrs;
        nameAndAttrs.push_back('\0');
        nameAndAttrs.push_back(static_cast<char>(encoding.size() + 3));
        nameAndAttrs += encoding;
        nameAndAttrs.push_back('\0');
        nameAndAttrs += property->getNameAsString();
        return createCStringAttr(
            ".objc_property_name",
            llvm::StringRef(nameAndAttrs.data(), nameAndAttrs.size()));
      }
      return createCStringAttr(".objc_property_name", property->getName());
    };

    auto emitMethodList =
        [&](llvm::ArrayRef<const ObjCMethodDecl *> methods) -> mlir::Attribute {
      auto methodTy =
          builder.getAnonRecordTy({voidPtrTy, voidPtrTy}, /*packed=*/false,
                                  /*padded=*/false);
      auto methodArrayTy = cir::ArrayType::get(methodTy, methods.size());
      auto methodListTy =
          builder.getAnonRecordTy({i32Ty, methodArrayTy}, /*packed=*/false,
                                  /*padded=*/false);
      cir::GlobalOp methodListGV = createGlobalOp(
          *this, loc, getUniqueGlobalName(".objc_method_list"), methodListTy,
          /*isConstant=*/false);
      methodListGV.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
      mlir::SymbolTable::setSymbolVisibility(
          methodListGV, mlir::SymbolTable::Visibility::Private);
      methodListGV.setAlignmentAttr(getSize(getPointerAlign()));
      setDSOLocal(static_cast<mlir::Operation *>(methodListGV));

      if (methods.empty()) {
        setInitializer(methodListGV, builder.getZeroInitAttr(methodListTy));
        return builder.getGlobalViewAttr(voidPtrTy, methodListGV);
      }

      llvm::SmallVector<mlir::Attribute, 8> methodAttrs;
      methodAttrs.reserve(methods.size());
      for (const auto *method : methods) {
        methodAttrs.push_back(builder.getAnonConstRecord(
            builder.getArrayAttr(
                {createCStringAttr(".objc_sel_name",
                                   method->getSelector().getAsString()),
                 createCStringAttr(
                     ".objc_method_type",
                     astContext.getObjCEncodingForMethodDecl(method))}),
            /*packed=*/false, /*padded=*/false, methodTy));
      }

      auto methodArrayInit = cir::ConstArrayAttr::get(
          methodArrayTy, builder.getArrayAttr(methodAttrs));
      auto methodListInit = builder.getAnonConstRecord(
          builder.getArrayAttr(
              {cir::IntAttr::get(i32Ty, methodAttrs.size()), methodArrayInit}),
          /*packed=*/false, /*padded=*/false, methodListTy);
      setInitializer(methodListGV, methodListInit);
      return builder.getGlobalViewAttr(voidPtrTy, methodListGV);
    };

    auto emitPropertyList = [&](bool isOptional) -> mlir::Attribute {
      llvm::SmallVector<const ObjCPropertyDecl *, 8> properties;
      for (const auto *property : pd->properties()) {
        if (property->isClassProperty() || property->isOptional() != isOptional)
          continue;
        properties.push_back(property);
      }

      if (properties.empty())
        return nullPtr;

      auto propertyTy =
          builder.getAnonRecordTy({voidPtrTy, i8Ty, i8Ty, i8Ty, i8Ty, voidPtrTy,
                                   voidPtrTy, voidPtrTy, voidPtrTy},
                                  /*packed=*/false, /*padded=*/false);
      llvm::SmallVector<mlir::Attribute, 8> propertyAttrs;
      propertyAttrs.reserve(properties.size());
      for (const auto *property : properties) {
        unsigned attrs = property->getPropertyAttributes();
        if (attrs & ObjCPropertyAttribute::kind_readonly) {
          attrs &= ~ObjCPropertyAttribute::kind_copy;
          attrs &= ~ObjCPropertyAttribute::kind_retain;
          attrs &= ~ObjCPropertyAttribute::kind_weak;
          attrs &= ~ObjCPropertyAttribute::kind_strong;
        }
        unsigned attrByte0 = attrs & 0xff;
        attrs >>= 8;
        attrs <<= 2;

        auto getAccessorNameAndType = [&](const ObjCMethodDecl *accessor) {
          std::pair<mlir::Attribute, mlir::Attribute> result{nullPtr, nullPtr};
          if (!accessor)
            return result;
          result.first = createCStringAttr(".objc_sel_name",
                                           accessor->getSelector().getAsString());
          result.second = createCStringAttr(
              ".objc_method_type",
              astContext.getObjCEncodingForMethodDecl(accessor));
          return result;
        };
        auto getterFields =
            getAccessorNameAndType(property->getGetterMethodDecl());
        auto setterFields =
            getAccessorNameAndType(property->getSetterMethodDecl());

        propertyAttrs.push_back(builder.getAnonConstRecord(
            builder.getArrayAttr(
                {createPropertyNameAttr(property),
                 cir::IntAttr::get(i8Ty, attrByte0),
                 cir::IntAttr::get(i8Ty, attrs & 0xff),
                 cir::IntAttr::get(i8Ty, 0),
                 cir::IntAttr::get(i8Ty, 0), getterFields.first,
                 getterFields.second, setterFields.first, setterFields.second}),
            /*packed=*/false, /*padded=*/false, propertyTy));
      }

      auto propertyArrayTy =
          cir::ArrayType::get(propertyTy, propertyAttrs.size());
      auto propertyArrayInit = cir::ConstArrayAttr::get(
          propertyArrayTy, builder.getArrayAttr(propertyAttrs));
      auto propertyListTy =
          builder.getAnonRecordTy({i32Ty, voidPtrTy, propertyArrayTy},
                                  /*packed=*/false, /*padded=*/false);
      auto propertyListInit = builder.getAnonConstRecord(
          builder.getArrayAttr(
              {cir::IntAttr::get(i32Ty, propertyAttrs.size()), nullPtr,
               propertyArrayInit}),
          /*packed=*/false, /*padded=*/false, propertyListTy);
      cir::GlobalOp propertyListGV = createGlobalOp(
          *this, loc, getUniqueGlobalName(".objc_property_list"), propertyListTy,
          /*isConstant=*/false);
      propertyListGV.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
      mlir::SymbolTable::setSymbolVisibility(
          propertyListGV, mlir::SymbolTable::Visibility::Private);
      propertyListGV.setAlignmentAttr(getSize(getPointerAlign()));
      setInitializer(propertyListGV, propertyListInit);
      setDSOLocal(static_cast<mlir::Operation *>(propertyListGV));
      return builder.getGlobalViewAttr(voidPtrTy, propertyListGV);
    };

    auto emitProtocolList = [&](ObjCProtocolDecl::protocol_iterator begin,
                                ObjCProtocolDecl::protocol_iterator end)
        -> mlir::Attribute {
      llvm::SmallVector<mlir::Attribute, 4> protocolAttrs;
      for (const auto *inherited : getRuntimeProtocolList(begin, end)) {
        protocolAttrs.push_back(
            builder.getGlobalViewAttr(voidPtrTy,
                                      getObjCProtocolGlobal(loc, inherited)));
      }

      auto protocolArrayTy =
          cir::ArrayType::get(voidPtrTy, protocolAttrs.size());
      auto protocolListTy = builder.getAnonRecordTy(
          {voidPtrTy, longTy, protocolArrayTy}, /*packed=*/false,
          /*padded=*/false);
      cir::GlobalOp protocolListGV = createGlobalOp(
          *this, loc, getUniqueGlobalName(".objc_protocol_list"), protocolListTy,
          /*isConstant=*/false);
      protocolListGV.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
      mlir::SymbolTable::setSymbolVisibility(
          protocolListGV, mlir::SymbolTable::Visibility::Private);
      protocolListGV.setAlignmentAttr(getSize(getPointerAlign()));
      setDSOLocal(static_cast<mlir::Operation *>(protocolListGV));

      if (protocolAttrs.empty()) {
        setInitializer(protocolListGV, builder.getZeroInitAttr(protocolListTy));
        return builder.getGlobalViewAttr(voidPtrTy, protocolListGV);
      }

      auto protocolArrayInit = cir::ConstArrayAttr::get(
          protocolArrayTy, builder.getArrayAttr(protocolAttrs));
      auto protocolListInit = builder.getAnonConstRecord(
          builder.getArrayAttr(
              {nullPtr, cir::IntAttr::get(longTy, protocolAttrs.size()),
               protocolArrayInit}),
          /*packed=*/false, /*padded=*/false, protocolListTy);
      setInitializer(protocolListGV, protocolListInit);
      return builder.getGlobalViewAttr(voidPtrTy, protocolListGV);
    };

    llvm::SmallVector<const ObjCMethodDecl *, 8> requiredInstanceMethods;
    llvm::SmallVector<const ObjCMethodDecl *, 8> optionalInstanceMethods;
    for (const auto *method : pd->instance_methods()) {
      (method->isOptional() ? optionalInstanceMethods : requiredInstanceMethods)
          .push_back(method);
    }
    llvm::SmallVector<const ObjCMethodDecl *, 8> requiredClassMethods;
    llvm::SmallVector<const ObjCMethodDecl *, 8> optionalClassMethods;
    for (const auto *method : pd->class_methods()) {
      (method->isOptional() ? optionalClassMethods : requiredClassMethods)
          .push_back(method);
    }

    auto protocolTy = builder.getAnonRecordTy(
        {voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy,
         voidPtrTy, voidPtrTy, voidPtrTy},
        /*packed=*/false, /*padded=*/false);
    cir::GlobalOp gv = createGlobalOp(
        *this, loc, getUniqueGlobalName(".objc_protocol"), protocolTy,
        /*isConstant=*/false);
    gv.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
    mlir::SymbolTable::setSymbolVisibility(gv,
                                           mlir::SymbolTable::Visibility::Private);
    gv.setAlignmentAttr(getSize(getPointerAlign()));
    setInitializer(
        gv, builder.getAnonConstRecord(
                builder.getArrayAttr(
                    {builder.getConstPtrAttr(
                         voidPtrTy, getLegacyGNUObjCProtocolVersion(*this)),
                     createCStringAttr(".objc_protocol_name", pd->getName()),
                     emitProtocolList(pd->protocol_begin(), pd->protocol_end()),
                     emitMethodList(requiredInstanceMethods),
                     emitMethodList(requiredClassMethods),
                     emitMethodList(optionalInstanceMethods),
                     emitMethodList(optionalClassMethods),
                     emitPropertyList(/*isOptional=*/false),
                     emitPropertyList(/*isOptional=*/true)}),
                /*packed=*/false, /*padded=*/false, protocolTy));
    setDSOLocal(static_cast<mlir::Operation *>(gv));

    objcProtocolGlobals[pd] = gv;
    if (pd->isThisDeclarationADefinition())
      legacyGNUProtocols.push_back(pd);
    return gv;
  }

  if (usesGNUstep2ObjCRuntime(*this)) {
    assert(pd && !pd->isNonRuntimeProtocol() &&
           "expected runtime Objective-C protocol");

    pd = getCanonicalObjCProtocolDecl(pd);
    if (auto it = objcProtocolGlobals.find(pd); it != objcProtocolGlobals.end())
      return it->second;

    std::string runtimeName = pd->getObjCRuntimeNameAsString().str();
    std::string protocolName =
        mangleGNUObjCPublicSymbol(*this, "OBJC_PROTOCOL_", runtimeName);

    llvm::SmallVector<mlir::Type, 11> protocolFields(11, voidPtrTy);
    auto protocolTy =
        builder.getAnonRecordTy(protocolFields, /*packed=*/false,
                                /*padded=*/false);

    cir::GlobalOp gv =
        mlir::dyn_cast_or_null<cir::GlobalOp>(getGlobalValue(protocolName));
    if (!gv)
      gv = createGlobalOp(*this, loc, protocolName, protocolTy,
                          /*isConstant=*/false);
    else
      assert(gv.getSymType() == protocolTy &&
             "GNUstep2 protocol global has unexpected type");

    gv.setConstant(false);
    gv.setLinkage(cir::GlobalLinkageKind::ExternalLinkage);
    mlir::SymbolTable::setSymbolVisibility(gv, getMLIRVisibility(gv));
    gv.setAlignmentAttr(getSize(getPointerAlign()));

    if (!pd->isThisDeclarationADefinition()) {
      objcProtocolGlobals[pd] = gv;
      return gv;
    }

    auto i32Ty = builder.getSInt32Ty();
    auto sizeTy = mlir::cast<cir::IntType>(convertType(astContext.getSizeType()));
    mlir::Attribute nullPtr = builder.getConstPtrAttr(voidPtrTy, 0);

    auto createPrivateCStringAttr = [&](llvm::StringRef prefix,
                                        llvm::StringRef value) -> mlir::Attribute {
      std::string bytes = value.str();
      bytes.push_back('\0');
      auto init = mlir::cast<mlir::TypedAttr>(
          builder.getString(bytes, uCharTy, bytes.size()));
      cir::GlobalOp stringGV = createGlobalOp(
          *this, loc, getUniqueGlobalName(prefix.str()), init.getType(),
          /*isConstant=*/true);
      stringGV.setLinkage(cir::GlobalLinkageKind::PrivateLinkage);
      mlir::SymbolTable::setSymbolVisibility(
          stringGV, mlir::SymbolTable::Visibility::Private);
      stringGV.setAlignmentAttr(getSize(CharUnits::One()));
      stringGV->setAttr("unnamed_addr", mlir::UnitAttr::get(&getMLIRContext()));
      setInitializer(stringGV, init);
      setDSOLocal(static_cast<mlir::Operation *>(stringGV));
      return builder.getGlobalViewAttr(voidPtrTy, stringGV);
    };

    auto emitMethodList =
        [&](llvm::ArrayRef<const ObjCMethodDecl *> methods) -> mlir::Attribute {
      auto methodDescTy =
          builder.getAnonRecordTy({voidPtrTy, voidPtrTy}, /*packed=*/false,
                                  /*padded=*/false);
      llvm::SmallVector<mlir::Attribute, 8> methodAttrs;
      methodAttrs.reserve(methods.size());
      for (const auto *method : methods) {
        cir::GlobalOp selectorGV =
            getObjCGNUSelectorGlobal(method->getSelector(), method);
        std::string typeEncoding =
            astContext.getObjCEncodingForMethodDecl(method, /*Extended=*/true);
        methodAttrs.push_back(builder.getAnonConstRecord(
            builder.getArrayAttr(
                {builder.getGlobalViewAttr(voidPtrTy, selectorGV),
                 createPrivateCStringAttr(".objc_protocol_method_type",
                                          typeEncoding)}),
            /*packed=*/false, /*padded=*/false, methodDescTy));
      }

      auto methodArrayTy = cir::ArrayType::get(methodDescTy, methodAttrs.size());
      auto methodArrayInit = cir::ConstArrayAttr::get(
          methodArrayTy, builder.getArrayAttr(methodAttrs));
      auto methodListTy =
          builder.getAnonRecordTy({i32Ty, i32Ty, methodArrayTy},
                                  /*packed=*/false, /*padded=*/false);
      auto methodListInit = builder.getAnonConstRecord(
          builder.getArrayAttr(
              {cir::IntAttr::get(i32Ty, methodAttrs.size()),
               cir::IntAttr::get(
                   i32Ty, getDataLayout().getTypeAllocSize(methodDescTy)
                              .getFixedValue()),
               methodArrayInit}),
          /*packed=*/false, /*padded=*/false, methodListTy);
      cir::GlobalOp methodListGV = createGlobalOp(
          *this, loc, getUniqueGlobalName(".objc_protocol_method_list"),
          methodListTy, /*isConstant=*/false);
      methodListGV.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
      mlir::SymbolTable::setSymbolVisibility(
          methodListGV, mlir::SymbolTable::Visibility::Private);
      methodListGV.setAlignmentAttr(getSize(getPointerAlign()));
      setInitializer(methodListGV, methodListInit);
      setDSOLocal(static_cast<mlir::Operation *>(methodListGV));
      return builder.getGlobalViewAttr(voidPtrTy, methodListGV);
    };

    auto emitPropertyList =
        [&](bool isClassProperty, bool isOptional) -> mlir::Attribute {
      llvm::SmallVector<const ObjCPropertyDecl *, 8> properties;
      for (const auto *property : pd->properties()) {
        if (property->isClassProperty() != isClassProperty ||
            property->isOptional() != isOptional)
          continue;
        properties.push_back(property);
      }

      if (properties.empty())
        return nullPtr;

      auto propertyTy =
          builder.getAnonRecordTy({voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy,
                                   voidPtrTy},
                                  /*packed=*/false, /*padded=*/false);
      llvm::SmallVector<mlir::Attribute, 8> propertyAttrs;
      propertyAttrs.reserve(properties.size());
      for (const auto *property : properties) {
        std::string propertyAttrsStr =
            astContext.getObjCEncodingForPropertyDecl(property, nullptr);
        std::string propertyTypeStr;
        astContext.getObjCEncodingForType(property->getType(), propertyTypeStr);

        auto getAccessorSelectorAttr = [&](const ObjCMethodDecl *accessor) {
          if (!accessor)
            return nullPtr;
          return mlir::Attribute(builder.getGlobalViewAttr(
              voidPtrTy,
              getObjCGNUSelectorGlobal(accessor->getSelector(), accessor)));
        };

        propertyAttrs.push_back(builder.getAnonConstRecord(
            builder.getArrayAttr(
                {createPrivateCStringAttr(".objc_property_name",
                                         property->getName()),
                 createPrivateCStringAttr(".objc_property_attrs",
                                         propertyAttrsStr),
                 createPrivateCStringAttr(".objc_property_type",
                                         propertyTypeStr),
                 getAccessorSelectorAttr(property->getGetterMethodDecl()),
                 getAccessorSelectorAttr(property->getSetterMethodDecl())}),
            /*packed=*/false, /*padded=*/false, propertyTy));
      }

      auto propertyArrayTy =
          cir::ArrayType::get(propertyTy, propertyAttrs.size());
      auto propertyArrayInit = cir::ConstArrayAttr::get(
          propertyArrayTy, builder.getArrayAttr(propertyAttrs));
      auto propertyListTy =
          builder.getAnonRecordTy({i32Ty, i32Ty, voidPtrTy, propertyArrayTy},
                                  /*packed=*/false, /*padded=*/false);
      auto propertyListInit = builder.getAnonConstRecord(
          builder.getArrayAttr(
              {cir::IntAttr::get(i32Ty, propertyAttrs.size()),
               cir::IntAttr::get(
                   i32Ty, getDataLayout().getTypeAllocSize(propertyTy)
                              .getFixedValue()),
               nullPtr, propertyArrayInit}),
          /*packed=*/false, /*padded=*/false, propertyListTy);
      cir::GlobalOp propertyListGV = createGlobalOp(
          *this, loc, getUniqueGlobalName(".objc_property_list"), propertyListTy,
          /*isConstant=*/false);
      propertyListGV.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
      mlir::SymbolTable::setSymbolVisibility(
          propertyListGV, mlir::SymbolTable::Visibility::Private);
      propertyListGV.setAlignmentAttr(getSize(getPointerAlign()));
      setInitializer(propertyListGV, propertyListInit);
      setDSOLocal(static_cast<mlir::Operation *>(propertyListGV));
      return builder.getGlobalViewAttr(voidPtrTy, propertyListGV);
    };

    auto emitProtocolList = [&]() -> mlir::Attribute {
      llvm::SmallVector<mlir::Attribute, 4> protocols;
      for (const auto *inherited : pd->protocols()) {
        if (inherited->isNonRuntimeProtocol())
          continue;
        protocols.push_back(
            builder.getGlobalViewAttr(voidPtrTy, getObjCProtocolGlobal(loc, inherited)));
      }

      auto protocolArrayTy = cir::ArrayType::get(voidPtrTy, protocols.size());
      auto protocolArrayInit = cir::ConstArrayAttr::get(
          protocolArrayTy, builder.getArrayAttr(protocols));
      auto protocolListTy =
          builder.getAnonRecordTy({voidPtrTy, sizeTy, protocolArrayTy},
                                  /*packed=*/false, /*padded=*/false);
      auto protocolListInit = builder.getAnonConstRecord(
          builder.getArrayAttr(
              {nullPtr,
               cir::IntAttr::get(sizeTy, protocols.size()), protocolArrayInit}),
          /*packed=*/false, /*padded=*/false, protocolListTy);
      cir::GlobalOp protocolListGV = createGlobalOp(
          *this, loc, getUniqueGlobalName(".objc_protocol_list"), protocolListTy,
          /*isConstant=*/false);
      protocolListGV.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
      mlir::SymbolTable::setSymbolVisibility(
          protocolListGV, mlir::SymbolTable::Visibility::Private);
      protocolListGV.setAlignmentAttr(getSize(getPointerAlign()));
      setInitializer(protocolListGV, protocolListInit);
      setDSOLocal(static_cast<mlir::Operation *>(protocolListGV));
      return builder.getGlobalViewAttr(voidPtrTy, protocolListGV);
    };

    llvm::SmallVector<const ObjCMethodDecl *, 8> requiredInstanceMethods;
    llvm::SmallVector<const ObjCMethodDecl *, 8> optionalInstanceMethods;
    for (const auto *method : pd->instance_methods()) {
      (method->isOptional() ? optionalInstanceMethods : requiredInstanceMethods)
          .push_back(method);
    }
    llvm::SmallVector<const ObjCMethodDecl *, 8> requiredClassMethods;
    llvm::SmallVector<const ObjCMethodDecl *, 8> optionalClassMethods;
    for (const auto *method : pd->class_methods()) {
      (method->isOptional() ? optionalClassMethods : requiredClassMethods)
          .push_back(method);
    }

    auto protocolInit = builder.getAnonConstRecord(
        builder.getArrayAttr(
            {builder.getConstPtrAttr(voidPtrTy, 4),
             createPrivateCStringAttr(".objc_protocol_name", runtimeName),
             emitProtocolList(), emitMethodList(requiredInstanceMethods),
             emitMethodList(requiredClassMethods),
             emitMethodList(optionalInstanceMethods),
             emitMethodList(optionalClassMethods),
             emitPropertyList(/*isClassProperty=*/false, /*isOptional=*/false),
             emitPropertyList(/*isClassProperty=*/false, /*isOptional=*/true),
             emitPropertyList(/*isClassProperty=*/true, /*isOptional=*/false),
             emitPropertyList(/*isClassProperty=*/true, /*isOptional=*/true)}),
        /*packed=*/false, /*padded=*/false, protocolTy);
    gv.setSectionAttr(getStringAttrCached(
        getGNUstep2ObjCSectionName(*this, GNUstep2ProtocolSection)));
    if (supportsCOMDAT())
      gv.setComdat(true);
    setInitializer(gv, protocolInit);

    objcProtocolGlobals[pd] = gv;
    return gv;
  }

  assert(usesDarwinNonFragileObjCRuntime(*this) &&
         "ObjC protocols currently require the Next non-fragile runtime");
  assert(pd && !pd->isNonRuntimeProtocol() &&
         "expected runtime Objective-C protocol");

  pd = getCanonicalObjCProtocolDecl(pd);
  std::string runtimeName = pd->getObjCRuntimeNameAsString().str();
  if (auto it = objcProtocolGlobals.find(pd); it != objcProtocolGlobals.end())
    if (!pd->isThisDeclarationADefinition() || !it->second.isDeclaration())
      return it->second;

  if (!pd->isThisDeclarationADefinition()) {
    std::string protocolName =
        (llvm::Twine("_OBJC_PROTOCOL_$_") + runtimeName).str();
    auto i32Ty = builder.getUInt32Ty();
    auto protocolTy = builder.getAnonRecordTy(
        {voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy,
         voidPtrTy, voidPtrTy, i32Ty, i32Ty, voidPtrTy, voidPtrTy, voidPtrTy},
        /*packed=*/false, /*padded=*/false);
    cir::GlobalOp gv = createOrReplaceCXXRuntimeVariable(
        loc, protocolName, protocolTy, cir::GlobalLinkageKind::ExternalLinkage,
        CharUnits::Zero());
    gv.setConstant(false);
    gv.setGlobalVisibility(cir::VisibilityKind::Default);
    mlir::SymbolTable::setSymbolVisibility(gv, getMLIRVisibility(gv));
    objcProtocolGlobals[pd] = gv;
    return gv;
  }

  auto i32Ty = builder.getUInt32Ty();
  auto intptrTy = mlir::cast<cir::IntType>(convertType(astContext.getUIntPtrType()));
  mlir::Attribute nullPtr = builder.getConstPtrAttr(voidPtrTy, 0);
  auto methodEntryTy = builder.getAnonRecordTy(
      {voidPtrTy, voidPtrTy, voidPtrTy}, /*packed=*/false, /*padded=*/false);
  auto propertyEntryTy = builder.getAnonRecordTy(
      {voidPtrTy, voidPtrTy}, /*packed=*/false, /*padded=*/false);

  auto createObjCConstGlobal = [&](llvm::StringRef name, mlir::Type type,
                                   mlir::Attribute init,
                                   bool addToCompilerUsed = true)
      -> cir::GlobalOp {
    std::string sectionName =
        usesDarwinObjCMachOSections(*this) ? "__DATA, __objc_const" : "";
    cir::GlobalOp gv =
        createGlobalOp(*this, loc, name, type, /*isConstant=*/false);
    gv.setLinkage(getObjCMetadataLinkage(*this, sectionName));
    mlir::SymbolTable::setSymbolVisibility(
        gv, mlir::SymbolTable::Visibility::Private);
    gv.setAlignmentAttr(getSize(getPointerAlign()));
    if (!sectionName.empty())
      gv.setSectionAttr(getStringAttrCached(sectionName));
    setInitializer(gv, init);
    if (addToCompilerUsed)
      addCompilerUsedGlobal(gv);
    return gv;
  };

  auto emitMethodList =
      [&](llvm::StringRef symbolName,
          llvm::ArrayRef<const ObjCMethodDecl *> methods) -> mlir::Attribute {
    if (methods.empty())
      return nullPtr;

    llvm::SmallVector<mlir::Attribute, 8> methodAttrs;
    methodAttrs.reserve(methods.size());
    for (const auto *method : methods) {
      cir::GlobalOp methodNameGV = getObjCSelectorNameGlobal(method->getSelector());
      std::string typeEncoding =
          astContext.getObjCEncodingForMethodDecl(method, /*Extended=*/false);
      cir::GlobalOp methodTypeGV = getObjCMethodTypeGlobal(typeEncoding);
      methodAttrs.push_back(builder.getAnonConstRecord(
          builder.getArrayAttr({builder.getGlobalViewAttr(voidPtrTy, methodNameGV),
                                builder.getGlobalViewAttr(voidPtrTy, methodTypeGV),
                                nullPtr}),
          /*packed=*/false, /*padded=*/false, methodEntryTy));
    }

    auto methodArrayTy = cir::ArrayType::get(methodEntryTy, methodAttrs.size());
    auto methodArrayInit =
        cir::ConstArrayAttr::get(methodArrayTy, builder.getArrayAttr(methodAttrs));
    auto methodListTy = builder.getAnonRecordTy(
        {i32Ty, i32Ty, methodArrayTy}, /*packed=*/false, /*padded=*/false);
    auto methodListInit = builder.getAnonConstRecord(
        builder.getArrayAttr(
            {cir::IntAttr::get(
                 i32Ty,
                 getDataLayout().getTypeAllocSize(methodEntryTy).getFixedValue()),
             cir::IntAttr::get(i32Ty, methodAttrs.size()), methodArrayInit}),
        /*packed=*/false, /*padded=*/false, methodListTy);
    cir::GlobalOp gv =
        createObjCConstGlobal(symbolName, methodListTy, methodListInit);
    return builder.getGlobalViewAttr(voidPtrTy, gv);
  };

  auto emitPropertyList = [&](llvm::StringRef symbolName,
                              bool isClassProperty) -> mlir::Attribute {
    if (isClassProperty) {
      const llvm::Triple &triple = getTriple();
      if ((triple.isMacOSX() && triple.isMacOSXVersionLT(10, 11)) ||
          (triple.isiOS() && triple.isOSVersionLT(9)))
        return nullPtr;
    }

    llvm::SmallVector<const ObjCPropertyDecl *, 8> properties;
    for (const auto *property : pd->properties()) {
      if (property->isClassProperty() != isClassProperty ||
          property->isDirectProperty())
        continue;
      properties.push_back(property);
    }

    if (properties.empty())
      return nullPtr;

    llvm::SmallVector<mlir::Attribute, 8> propertyAttrs;
    propertyAttrs.reserve(properties.size());
    for (const auto *property : properties) {
      cir::GlobalOp propertyNameGV =
          getObjCPropertyNameGlobal(property->getIdentifier()->getName());
      std::string propertyType =
          astContext.getObjCEncodingForPropertyDecl(property, nullptr);
      cir::GlobalOp propertyTypeGV = getObjCPropertyNameGlobal(propertyType);
      propertyAttrs.push_back(builder.getAnonConstRecord(
          builder.getArrayAttr(
              {builder.getGlobalViewAttr(voidPtrTy, propertyNameGV),
               builder.getGlobalViewAttr(voidPtrTy, propertyTypeGV)}),
          /*packed=*/false, /*padded=*/false, propertyEntryTy));
    }

    auto propertyArrayTy =
        cir::ArrayType::get(propertyEntryTy, propertyAttrs.size());
    auto propertyArrayInit = cir::ConstArrayAttr::get(
        propertyArrayTy, builder.getArrayAttr(propertyAttrs));
    auto propertyListTy = builder.getAnonRecordTy(
        {i32Ty, i32Ty, propertyArrayTy}, /*packed=*/false, /*padded=*/false);
    auto propertyListInit = builder.getAnonConstRecord(
        builder.getArrayAttr(
            {cir::IntAttr::get(
                 i32Ty, getDataLayout().getTypeAllocSize(propertyEntryTy)
                            .getFixedValue()),
             cir::IntAttr::get(i32Ty, propertyAttrs.size()), propertyArrayInit}),
        /*packed=*/false, /*padded=*/false, propertyListTy);
    cir::GlobalOp gv =
        createObjCConstGlobal(symbolName, propertyListTy, propertyListInit);
    return builder.getGlobalViewAttr(voidPtrTy, gv);
  };

  auto emitProtocolList = [&]() -> mlir::Attribute {
    llvm::SmallVector<mlir::Attribute, 8> protocolAttrs;
    for (auto it = pd->protocol_begin(); it != pd->protocol_end(); ++it) {
      const ObjCProtocolDecl *inherited = *it;
      if (inherited->isNonRuntimeProtocol())
        continue;
      protocolAttrs.push_back(builder.getGlobalViewAttr(
          voidPtrTy, getObjCProtocolGlobal(loc, inherited)));
    }

    if (protocolAttrs.empty())
      return nullPtr;

    protocolAttrs.push_back(nullPtr);
    auto protocolArrayTy = cir::ArrayType::get(voidPtrTy, protocolAttrs.size());
    auto protocolArrayInit = cir::ConstArrayAttr::get(
        protocolArrayTy, builder.getArrayAttr(protocolAttrs));
    auto protocolListTy = builder.getAnonRecordTy(
        {intptrTy, protocolArrayTy}, /*packed=*/false, /*padded=*/false);
    auto protocolListInit = builder.getAnonConstRecord(
        builder.getArrayAttr(
            {cir::IntAttr::get(intptrTy, protocolAttrs.size() - 1),
             protocolArrayInit}),
        /*packed=*/false, /*padded=*/false, protocolListTy);
    std::string name =
        (llvm::Twine("_OBJC_$_PROTOCOL_REFS_") + runtimeName).str();
    cir::GlobalOp gv =
        createObjCConstGlobal(name, protocolListTy, protocolListInit);
    return builder.getGlobalViewAttr(voidPtrTy, gv);
  };

  llvm::SmallVector<const ObjCMethodDecl *, 4> methodLists[4];
  for (const auto *method : pd->methods()) {
    size_t index =
        (2 * size_t(method->isOptional())) + size_t(method->isClassMethod());
    methodLists[index].push_back(method);
  }

  auto requiredInstanceMethods = emitMethodList(
      (llvm::Twine("_OBJC_$_PROTOCOL_INSTANCE_METHODS_") + runtimeName).str(),
      methodLists[0]);
  auto requiredClassMethods = emitMethodList(
      (llvm::Twine("_OBJC_$_PROTOCOL_CLASS_METHODS_") + runtimeName).str(),
      methodLists[1]);
  auto optionalInstanceMethods = emitMethodList(
      (llvm::Twine("_OBJC_$_PROTOCOL_INSTANCE_METHODS_OPT_") + runtimeName)
          .str(),
      methodLists[2]);
  auto optionalClassMethods = emitMethodList(
      (llvm::Twine("_OBJC_$_PROTOCOL_CLASS_METHODS_OPT_") + runtimeName).str(),
      methodLists[3]);

  llvm::SmallVector<mlir::Attribute, 8> extendedMethodTypes;
  for (const auto &methods : methodLists) {
    for (const auto *method : methods) {
      std::string typeEncoding =
          astContext.getObjCEncodingForMethodDecl(method, /*Extended=*/true);
      cir::GlobalOp methodTypeGV = getObjCMethodTypeGlobal(typeEncoding);
      extendedMethodTypes.push_back(
          builder.getGlobalViewAttr(voidPtrTy, methodTypeGV));
    }
  }

  mlir::Attribute extendedMethodTypesField = nullPtr;
  if (!extendedMethodTypes.empty()) {
    auto methodTypesArrayTy =
        cir::ArrayType::get(voidPtrTy, extendedMethodTypes.size());
    auto methodTypesInit = cir::ConstArrayAttr::get(
        methodTypesArrayTy, builder.getArrayAttr(extendedMethodTypes));
    auto methodTypesGV = createObjCConstGlobal(
        (llvm::Twine("_OBJC_$_PROTOCOL_METHOD_TYPES_") + runtimeName).str(),
        methodTypesArrayTy, methodTypesInit);
    extendedMethodTypesField = builder.getGlobalViewAttr(voidPtrTy, methodTypesGV);
  }

  auto protocolTy = builder.getAnonRecordTy(
      {voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy,
       voidPtrTy, voidPtrTy, i32Ty, i32Ty, voidPtrTy, voidPtrTy, voidPtrTy},
      /*packed=*/false, /*padded=*/false);
  std::string protocolName =
      (llvm::Twine("_OBJC_PROTOCOL_$_") + runtimeName).str();
  cir::GlobalOp gv =
      mlir::dyn_cast_or_null<cir::GlobalOp>(getGlobalValue(protocolName));
  if (!gv)
    gv = createGlobalOp(*this, loc, protocolName, protocolTy,
                        /*isConstant=*/false);
  else
    assert(gv.getSymType() == protocolTy &&
           "Darwin protocol global has unexpected type");

  gv.setConstant(false);
  gv.setAlignmentAttr(getSize(getPointerAlign()));
  if (!pd->isThisDeclarationADefinition()) {
    gv.setLinkage(cir::GlobalLinkageKind::ExternalLinkage);
    gv.setGlobalVisibility(cir::VisibilityKind::Default);
    mlir::SymbolTable::setSymbolVisibility(gv, getMLIRVisibility(gv));
    objcProtocolGlobals[pd] = gv;
    return gv;
  }

  auto protocolInit = builder.getAnonConstRecord(
      builder.getArrayAttr(
          {nullPtr,
           builder.getGlobalViewAttr(voidPtrTy, getObjCClassNameGlobal(runtimeName)),
           emitProtocolList(), requiredInstanceMethods, requiredClassMethods,
           optionalInstanceMethods, optionalClassMethods,
           emitPropertyList((llvm::Twine("_OBJC_$_PROP_LIST_") + runtimeName)
                                .str(),
                            /*isClassProperty=*/false),
           cir::IntAttr::get(
               i32Ty, getDataLayout().getTypeAllocSize(protocolTy).getFixedValue()),
           cir::IntAttr::get(i32Ty, 0), extendedMethodTypesField, nullPtr,
           emitPropertyList((llvm::Twine("_OBJC_$_CLASS_PROP_LIST_") + runtimeName)
                                .str(),
                            /*isClassProperty=*/true)}),
      /*packed=*/false, /*padded=*/false, protocolTy);
  gv.setLinkage(cir::GlobalLinkageKind::WeakAnyLinkage);
  gv.setGlobalVisibility(cir::VisibilityKind::Hidden);
  mlir::SymbolTable::setSymbolVisibility(gv, getMLIRVisibility(gv));
  gv.setAlignmentAttr(getSize(getPointerAlign()));
  gv.setAlignmentAttr(getSize(getPointerAlign()));
  if (supportsCOMDAT() && !getTriple().isOSBinFormatMachO())
    gv.setComdat(true);
  setInitializer(gv, protocolInit);
  addUsedGlobal(gv);

  objcProtocolGlobals[pd] = gv;
  return gv;
}

cir::GlobalOp
CIRGenModule::getObjCProtocolLabelGlobal(mlir::Location loc,
                                         const ObjCProtocolDecl *pd) {
  assert(usesDarwinNonFragileObjCRuntime(*this) &&
         "ObjC protocol labels currently require the Next non-fragile runtime");
  assert(pd && "expected Objective-C protocol");

  pd = getCanonicalObjCProtocolDecl(pd);
  if (auto it = objcProtocolLabelGlobals.find(pd);
      it != objcProtocolLabelGlobals.end())
    return it->second;

  cir::GlobalOp protocolGV = getObjCProtocolGlobal(loc, pd);
  std::string labelName =
      (llvm::Twine("_OBJC_LABEL_PROTOCOL_$_") +
       pd->getObjCRuntimeNameAsString())
          .str();
  cir::GlobalOp gv =
      createGlobalOp(*this, loc, labelName, voidPtrTy, /*isConstant=*/false);
  gv.setLinkage(cir::GlobalLinkageKind::WeakAnyLinkage);
  gv.setGlobalVisibility(cir::VisibilityKind::Hidden);
  mlir::SymbolTable::setSymbolVisibility(gv, getMLIRVisibility(gv));
  gv.setAlignmentAttr(getSize(getPointerAlign()));
  std::string sectionName =
      getObjCSectionName(*this, "__objc_protolist", "coalesced,no_dead_strip");
  gv.setSectionAttr(getStringAttrCached(sectionName));
  if (supportsCOMDAT() && !getTriple().isOSBinFormatMachO())
    gv.setComdat(true);
  setInitializer(gv, builder.getGlobalViewAttr(voidPtrTy, protocolGV));
  addUsedGlobal(gv);

  objcProtocolLabelGlobals[pd] = gv;
  return gv;
}

cir::GlobalOp CIRGenModule::getObjCProtocolRefGlobal(mlir::Location loc,
                                                     const ObjCProtocolDecl *pd) {
  if (usesGNUstep2ObjCRuntime(*this)) {
    assert(pd && !pd->isNonRuntimeProtocol() &&
           "expected runtime Objective-C protocol");

    pd = getCanonicalObjCProtocolDecl(pd);
    if (auto it = objcProtocolRefGlobals.find(pd);
        it != objcProtocolRefGlobals.end())
      return it->second;

    cir::GlobalOp protocolGV = getObjCProtocolGlobal(loc, pd);
    cir::PointerType protocolPtrTy =
        builder.getPointerTo(protocolGV.getSymType(), protocolGV.getAddrSpaceAttr());
    std::string refName =
        mangleGNUObjCPublicSymbol(*this, "OBJC_REF_PROTOCOL_",
                                  pd->getObjCRuntimeNameAsString());
    cir::GlobalOp gv =
        mlir::dyn_cast_or_null<cir::GlobalOp>(getGlobalValue(refName));
    if (!gv)
      gv = createGlobalOp(*this, loc, refName, protocolPtrTy,
                          /*isConstant=*/false);
    else
      assert(gv.getSymType() == protocolPtrTy &&
             "GNUstep2 protocol ref global has unexpected type");

    gv.setConstant(false);
    gv.setLinkage(cir::GlobalLinkageKind::LinkOnceODRLinkage);
    mlir::SymbolTable::setSymbolVisibility(gv, getMLIRVisibility(gv));
    gv.setAlignmentAttr(getSize(getPointerAlign()));
    gv.setSectionAttr(getStringAttrCached(
        getGNUstep2ObjCSectionName(*this, GNUstep2ProtocolReferenceSection)));
    if (supportsCOMDAT())
      gv.setComdat(true);
    setInitializer(gv, builder.getGlobalViewAttr(protocolPtrTy, protocolGV));

    objcProtocolRefGlobals[pd] = gv;
    return gv;
  }

  assert(usesDarwinNonFragileObjCRuntime(*this) &&
         "ObjC protocol refs currently require the Next non-fragile runtime");
  assert(pd && "expected Objective-C protocol");

  pd = getCanonicalObjCProtocolDecl(pd);
  if (auto it = objcProtocolRefGlobals.find(pd);
      it != objcProtocolRefGlobals.end())
    return it->second;

  cir::GlobalOp protocolGV = getObjCProtocolGlobal(loc, pd);
  (void)getObjCProtocolLabelGlobal(loc, pd);

  std::string refName =
      (llvm::Twine("_OBJC_PROTOCOL_REFERENCE_$_") +
       pd->getObjCRuntimeNameAsString())
          .str();
  cir::GlobalOp gv =
      createGlobalOp(*this, loc, refName, voidPtrTy, /*isConstant=*/false);
  gv.setLinkage(cir::GlobalLinkageKind::WeakAnyLinkage);
  gv.setGlobalVisibility(cir::VisibilityKind::Hidden);
  mlir::SymbolTable::setSymbolVisibility(gv, getMLIRVisibility(gv));
  gv.setAlignmentAttr(getSize(getPointerAlign()));
  std::string sectionName =
      getObjCSectionName(*this, "__objc_protorefs", "coalesced,no_dead_strip");
  gv.setSectionAttr(getStringAttrCached(sectionName));
  if (supportsCOMDAT() && !getTriple().isOSBinFormatMachO())
    gv.setComdat(true);
  setInitializer(gv, builder.getGlobalViewAttr(voidPtrTy, protocolGV));
  addUsedGlobal(gv);

  objcProtocolRefGlobals[pd] = gv;
  return gv;
}

mlir::Value CIRGenModule::getObjCProtocol(CIRGenFunction &cgf,
                                          const ObjCProtocolDecl *pd,
                                          mlir::Location loc) {
  if (usesLegacyGNUObjCRuntime(*this)) {
    cir::GlobalOp protocolGV = getObjCProtocolGlobal(loc, pd);
    return builder.createGetGlobal(loc, protocolGV);
  }

  cir::GlobalOp protocolRefGV = getObjCProtocolRefGlobal(loc, pd);
  mlir::Value protocolRefAddr = builder.createGetGlobal(loc, protocolRefGV);
  return builder.createLoad(
      loc, Address(protocolRefAddr, protocolRefGV.getSymType(),
                   getPointerAlign()));
}

cir::IntType CIRGenModule::getObjCDarwinIvarOffsetVarType() {
  // arm64 uses signed 32-bit `int` ivar offsets; all other targets use signed
  // `long`. This mirrors OGCG's ObjCCommonTypesHelper::IvarOffsetVarTy (see
  // CGObjCMac.cpp), whose access path (EmitIvarOffset) sign-extends the value,
  // confirming the canonical interpretation is signed.
  if (getTriple().getArch() == llvm::Triple::aarch64)
    return builder.getSInt32Ty();
  return mlir::cast<cir::IntType>(convertType(astContext.LongTy));
}

cir::GlobalOp CIRGenModule::getObjCDarwinClassGlobal(
    mlir::Location loc, const ObjCImplementationDecl *oid) {
  assert(usesDarwinNonFragileObjCRuntime(*this) &&
         "Darwin ObjC class metadata requires the Next non-fragile runtime");
  assert(oid && "expected Objective-C implementation");

  const ObjCInterfaceDecl *classDecl = oid->getClassInterface();
  assert(classDecl && "expected Objective-C class interface");

  std::string runtimeName = classDecl->getObjCRuntimeNameAsString().str();
  std::string className = (llvm::Twine("OBJC_CLASS_$_") + runtimeName).str();
  if (auto existing = mlir::dyn_cast_or_null<cir::GlobalOp>(getGlobalValue(className));
      existing && !existing.isDeclaration())
    return existing;

  auto i32Ty = builder.getUInt32Ty();
  auto intptrTy =
      mlir::cast<cir::IntType>(convertType(astContext.getUIntPtrType()));
  // Element type of the OBJC_IVAR_$_ offset global. Must match the ivar-access
  // path (getOrCreateObjCIvarOffsetVariable) exactly — same width AND
  // signedness — or createOrReplaceCXXRuntimeVariable below asserts and the
  // cir.get_global pointee in accessors mismatches.
  auto intOrLongTy = getObjCDarwinIvarOffsetVarType();
  mlir::Attribute nullPtr = builder.getConstPtrAttr(voidPtrTy, 0);

  auto getTypePathByteOffset =
      [&](mlir::Type type, llvm::ArrayRef<unsigned> path) -> uint64_t {
    uint64_t offset = 0;
    for (unsigned index : path) {
      if (auto recordTy = mlir::dyn_cast<cir::RecordType>(type)) {
        offset += recordTy.getElementOffset(getDataLayout().layout, index);
        type = recordTy.getMembers()[index];
        continue;
      }
      if (auto arrayTy = mlir::dyn_cast<cir::ArrayType>(type)) {
        offset += index *
                  getDataLayout().getTypeAllocSize(arrayTy.getElementType())
                      .getFixedValue();
        type = arrayTy.getElementType();
        continue;
      }
      llvm_unreachable("unexpected ObjC metadata type path");
    }
    return offset;
  };

  auto createObjCConstGlobal = [&](llvm::StringRef name, mlir::Type type,
                                   mlir::Attribute init,
                                   bool addToCompilerUsed = true)
      -> cir::GlobalOp {
    std::string sectionName =
        usesDarwinObjCMachOSections(*this) ? "__DATA, __objc_const" : "";
    cir::GlobalOp gv =
        createGlobalOp(*this, loc, name, type, /*isConstant=*/false);
    gv.setLinkage(getObjCMetadataLinkage(*this, sectionName));
    mlir::SymbolTable::setSymbolVisibility(gv,
                                           mlir::SymbolTable::Visibility::Private);
    gv.setAlignmentAttr(getSize(getPointerAlign()));
    if (!sectionName.empty())
      gv.setSectionAttr(getStringAttrCached(sectionName));
    setInitializer(gv, init);
    setDSOLocal(static_cast<mlir::Operation *>(gv));
    if (addToCompilerUsed)
      addCompilerUsedGlobal(gv);
    return gv;
  };

  auto addConstantPtrauthFixup =
      [&](llvm::SmallVectorImpl<mlir::Attribute> &fixups, uint64_t offset,
          const PointerAuthSchema &schema) {
        if (!schema || !shouldSignPointer(schema))
          return;
        fixups.push_back(builder.getArrayAttr(
            {builder.getI64IntegerAttr(offset),
             builder.getI32IntegerAttr(schema.getKey()),
             builder.getI64IntegerAttr(
                 getPointerAuthOtherDiscriminator(schema, GlobalDecl(),
                                                 QualType())),
             builder.getBoolAttr(schema.isAddressDiscriminated())}));
      };

  auto setConstantPtrauthFixups =
      [&](cir::GlobalOp gv, llvm::ArrayRef<mlir::Attribute> fixups) {
        if (!fixups.empty())
          gv->setAttr("cir.global_const_ptrauth", builder.getArrayAttr(fixups));
      };

  auto emitMethodList =
      [&](llvm::StringRef symbolName,
          llvm::ArrayRef<const ObjCMethodDecl *> methods) -> mlir::Attribute {
    if (methods.empty())
      return nullPtr;

    auto methodEntryTy = builder.getAnonRecordTy(
        {voidPtrTy, voidPtrTy, voidPtrTy}, /*packed=*/false, /*padded=*/false);
    llvm::SmallVector<mlir::Attribute, 8> methodAttrs;
    methodAttrs.reserve(methods.size());
    for (const auto *method : methods) {
      GlobalDecl gd(method);
      const CIRGenFunctionInfo &fi =
          getTypes().arrangeObjCMethodDeclaration(method);
      cir::FuncType fnTy = getTypes().getFunctionType(fi);
      SmallString<256> nameBuf;
      llvm::raw_svector_ostream os(nameBuf);
      mangleObjCMethodName(method, os);
      cir::FuncOp fn = getOrCreateCIRFunction(
          nameBuf.str(), fnTy, gd, /*forVTable=*/false,
          /*dontDefer=*/true, /*isThunk=*/false, NotForDefinition);
      fn.setLinkage(cir::GlobalLinkageKind::InternalLinkage);

      cir::GlobalOp selectorGV = getObjCSelectorNameGlobal(method->getSelector());
      std::string typeEncoding =
          astContext.getObjCEncodingForMethodDecl(method, /*Extended=*/false);
      cir::GlobalOp methodTypeGV = getObjCMethodTypeGlobal(typeEncoding);
      methodAttrs.push_back(builder.getAnonConstRecord(
          builder.getArrayAttr(
              {builder.getGlobalViewAttr(voidPtrTy, selectorGV),
               builder.getGlobalViewAttr(voidPtrTy, methodTypeGV),
               cir::GlobalViewAttr::get(
                   voidPtrTy, mlir::FlatSymbolRefAttr::get(fn.getSymNameAttr()))}),
          /*packed=*/false, /*padded=*/false, methodEntryTy));
    }

    auto methodArrayTy = cir::ArrayType::get(methodEntryTy, methodAttrs.size());
    auto methodArrayInit = cir::ConstArrayAttr::get(
        methodArrayTy, builder.getArrayAttr(methodAttrs));
    auto methodListTy = builder.getAnonRecordTy(
        {i32Ty, i32Ty, methodArrayTy}, /*packed=*/false, /*padded=*/false);
    auto methodListInit = builder.getAnonConstRecord(
        builder.getArrayAttr(
            {cir::IntAttr::get(
                 i32Ty,
                 getDataLayout().getTypeAllocSize(methodEntryTy).getFixedValue()),
             cir::IntAttr::get(i32Ty, methodAttrs.size()), methodArrayInit}),
        /*packed=*/false, /*padded=*/false, methodListTy);
    cir::GlobalOp methodListGV =
        createObjCConstGlobal(symbolName, methodListTy, methodListInit);

    llvm::SmallVector<mlir::Attribute, 8> ptrauthFixups;
    for (unsigned i = 0; i < methods.size(); ++i)
      addConstantPtrauthFixup(
          ptrauthFixups,
          getTypePathByteOffset(methodListTy, {2, i, 2}),
          getCodeGenOpts().PointerAuth.ObjCMethodListFunctionPointers);
    setConstantPtrauthFixups(methodListGV, ptrauthFixups);
    return builder.getGlobalViewAttr(voidPtrTy, methodListGV);
  };

  auto emitPropertyList = [&](llvm::StringRef symbolName,
                              bool isClassProperty) -> mlir::Attribute {
    if (isClassProperty) {
      const llvm::Triple &triple = getTriple();
      if ((triple.isMacOSX() && triple.isMacOSXVersionLT(10, 11)) ||
          (triple.isiOS() && triple.isOSVersionLT(9)))
        return nullPtr;
    }

    llvm::SmallVector<const ObjCPropertyDecl *, 8> properties;
    llvm::SmallPtrSet<const IdentifierInfo *, 8> seenProperties;

    std::function<void(const ObjCProtocolDecl *)> collectProtocolProperties;
    collectProtocolProperties = [&](const ObjCProtocolDecl *protocol) {
      for (const auto *inherited : protocol->protocols())
        collectProtocolProperties(inherited);
      for (const auto *property : protocol->properties()) {
        if (property->isClassProperty() != isClassProperty ||
            property->isDirectProperty())
          continue;
        if (!seenProperties.insert(property->getIdentifier()).second)
          continue;
        properties.push_back(property);
      }
    };

    for (const ObjCCategoryDecl *classExt : classDecl->known_extensions())
      for (const auto *property : classExt->properties()) {
        if (property->isClassProperty() != isClassProperty ||
            property->isDirectProperty())
          continue;
        if (!seenProperties.insert(property->getIdentifier()).second)
          continue;
        properties.push_back(property);
      }

    for (const auto *property : classDecl->properties()) {
      if (property->isClassProperty() != isClassProperty ||
          property->isDirectProperty())
        continue;
      if (!seenProperties.insert(property->getIdentifier()).second)
        continue;
      properties.push_back(property);
    }

    for (const auto *protocol : classDecl->all_referenced_protocols())
      collectProtocolProperties(protocol);

    if (properties.empty())
      return nullPtr;

    auto propertyEntryTy = builder.getAnonRecordTy(
        {voidPtrTy, voidPtrTy}, /*packed=*/false, /*padded=*/false);
    llvm::SmallVector<mlir::Attribute, 8> propertyAttrs;
    propertyAttrs.reserve(properties.size());
    for (const auto *property : properties) {
      cir::GlobalOp propertyNameGV =
          getObjCPropertyNameGlobal(property->getIdentifier()->getName());
      std::string propertyType =
          astContext.getObjCEncodingForPropertyDecl(property, oid);
      cir::GlobalOp propertyTypeGV = getObjCPropertyNameGlobal(propertyType);
      propertyAttrs.push_back(builder.getAnonConstRecord(
          builder.getArrayAttr(
              {builder.getGlobalViewAttr(voidPtrTy, propertyNameGV),
               builder.getGlobalViewAttr(voidPtrTy, propertyTypeGV)}),
          /*packed=*/false, /*padded=*/false, propertyEntryTy));
    }

    auto propertyArrayTy =
        cir::ArrayType::get(propertyEntryTy, propertyAttrs.size());
    auto propertyArrayInit = cir::ConstArrayAttr::get(
        propertyArrayTy, builder.getArrayAttr(propertyAttrs));
    auto propertyListTy = builder.getAnonRecordTy(
        {i32Ty, i32Ty, propertyArrayTy}, /*packed=*/false, /*padded=*/false);
    auto propertyListInit = builder.getAnonConstRecord(
        builder.getArrayAttr(
            {cir::IntAttr::get(
                 i32Ty, getDataLayout().getTypeAllocSize(propertyEntryTy)
                            .getFixedValue()),
             cir::IntAttr::get(i32Ty, propertyAttrs.size()), propertyArrayInit}),
        /*packed=*/false, /*padded=*/false, propertyListTy);
    cir::GlobalOp propertyListGV =
        createObjCConstGlobal(symbolName, propertyListTy, propertyListInit);
    return builder.getGlobalViewAttr(voidPtrTy, propertyListGV);
  };

  auto emitProtocolList = [&]() -> mlir::Attribute {
    llvm::SmallVector<mlir::Attribute, 8> protocolAttrs;
    for (const auto *protocol : classDecl->all_referenced_protocols()) {
      if (protocol->isNonRuntimeProtocol())
        continue;
      protocolAttrs.push_back(
          builder.getGlobalViewAttr(voidPtrTy, getObjCProtocolGlobal(loc, protocol)));
    }

    if (protocolAttrs.empty())
      return nullPtr;

    protocolAttrs.push_back(nullPtr);
    auto protocolArrayTy = cir::ArrayType::get(voidPtrTy, protocolAttrs.size());
    auto protocolArrayInit = cir::ConstArrayAttr::get(
        protocolArrayTy, builder.getArrayAttr(protocolAttrs));
    auto protocolListTy = builder.getAnonRecordTy(
        {intptrTy, protocolArrayTy}, /*packed=*/false, /*padded=*/false);
    auto protocolListInit = builder.getAnonConstRecord(
        builder.getArrayAttr(
            {cir::IntAttr::get(intptrTy, protocolAttrs.size() - 1),
             protocolArrayInit}),
        /*packed=*/false, /*padded=*/false, protocolListTy);
    cir::GlobalOp protocolListGV = createObjCConstGlobal(
        (llvm::Twine("_OBJC_CLASS_PROTOCOLS_$_") + runtimeName).str(),
        protocolListTy, protocolListInit);
    return builder.getGlobalViewAttr(voidPtrTy, protocolListGV);
  };

  auto emitIvarList = [&]() -> mlir::Attribute {
    llvm::SmallVector<const ObjCIvarDecl *, 8> ivars;
    for (const ObjCIvarDecl *ivar = classDecl->all_declared_ivar_begin(); ivar;
         ivar = ivar->getNextIvar()) {
      if (ivar->getDeclName())
        ivars.push_back(ivar);
    }

    if (ivars.empty())
      return nullPtr;

    auto ivarEntryTy = builder.getAnonRecordTy(
        {voidPtrTy, voidPtrTy, voidPtrTy, i32Ty, i32Ty}, /*packed=*/false,
        /*padded=*/false);
    llvm::SmallVector<mlir::Attribute, 8> ivarAttrs;
    ivarAttrs.reserve(ivars.size());
    CharUnits ivarOffsetAlign =
        getTriple().getArch() == llvm::Triple::aarch64
            ? astContext.getTypeAlignInChars(astContext.IntTy)
            : astContext.getTypeAlignInChars(astContext.LongTy);
    for (const ObjCIvarDecl *ivar : ivars) {
      std::string ivarTypeEncoding;
      astContext.getObjCEncodingForType(ivar->getType(), ivarTypeEncoding,
                                        ivar);

      std::string offsetName =
          (llvm::Twine("OBJC_IVAR_$_") + runtimeName + "." +
           ivar->getNameAsString())
              .str();
      cir::GlobalOp offsetGV = createOrReplaceCXXRuntimeVariable(
          loc, offsetName, intOrLongTy, cir::GlobalLinkageKind::ExternalLinkage,
          ivarOffsetAlign);
      // If the class layout is statically known, mark the offset global
      // constant: the runtime never patches it, so treat any attempt to as a
      // crash. Otherwise it stays writable so the runtime can fix it up.
      // Mirrors CGObjCNonFragileABIMac::EmitIvarOffsetVar.
      offsetGV.setConstant(isObjCClassLayoutKnownStatically(classDecl));
      // The ObjC runtime scans the __objc_ivar section to patch offsets of
      // runtime-adjustable ivars at load time; the section is emitted for every
      // MachO offset global (constant or not), matching classic CodeGen.
      if (usesDarwinObjCMachOSections(*this))
        offsetGV.setSectionAttr(getStringAttrCached("__DATA, __objc_ivar"));
      if (getTriple().isOSBinFormatCOFF()) {
        const bool isPrivateOrPackage =
            ivar->getAccessControl() == ObjCIvarDecl::Private ||
            ivar->getAccessControl() == ObjCIvarDecl::Package;
        if (classDecl->hasAttr<DLLImportAttr>()) {
          offsetGV->setAttr("cir.dll_storage_class",
                            getStringAttrCached("dllimport"));
        } else if (classDecl->hasAttr<DLLExportAttr>() &&
                   !isPrivateOrPackage) {
          offsetGV->setAttr("cir.dll_storage_class",
                            getStringAttrCached("dllexport"));
        }
        offsetGV->removeAttr("dso_local");
      }
      if (!getTriple().isOSBinFormatCOFF() &&
          (ivar->getAccessControl() == ObjCIvarDecl::Private ||
           ivar->getAccessControl() == ObjCIvarDecl::Package ||
           classDecl->getVisibility() == HiddenVisibility)) {
        offsetGV.setGlobalVisibility(cir::VisibilityKind::Hidden);
        mlir::SymbolTable::setSymbolVisibility(offsetGV,
                                               getMLIRVisibility(offsetGV));
      } else {
        offsetGV.setGlobalVisibility(cir::VisibilityKind::Default);
        mlir::SymbolTable::setSymbolVisibility(
            offsetGV, mlir::SymbolTable::Visibility::Public);
      }

      int64_t offset =
          astContext.lookupFieldBitOffset(classDecl, ivar) /
          astContext.getCharWidth();
      setInitializer(offsetGV, cir::IntAttr::get(intOrLongTy, offset));

      unsigned align =
          llvm::Log2_32(astContext.getPreferredTypeAlign(ivar->getType().getTypePtr()) >>
                        3);
      unsigned size =
          astContext.getTypeSizeInChars(ivar->getType()).getQuantity();

      cir::GlobalOp ivarNameGV =
          getObjCPropertyNameGlobal(ivar->getIdentifier()->getName());
      cir::GlobalOp ivarTypeGV = getObjCMethodTypeGlobal(ivarTypeEncoding);
      ivarAttrs.push_back(builder.getAnonConstRecord(
          builder.getArrayAttr(
              {builder.getGlobalViewAttr(voidPtrTy, offsetGV),
               builder.getGlobalViewAttr(voidPtrTy, ivarNameGV),
               builder.getGlobalViewAttr(voidPtrTy, ivarTypeGV),
               cir::IntAttr::get(i32Ty, align),
               cir::IntAttr::get(i32Ty, size)}),
          /*packed=*/false, /*padded=*/false, ivarEntryTy));
    }

    auto ivarArrayTy = cir::ArrayType::get(ivarEntryTy, ivarAttrs.size());
    auto ivarArrayInit =
        cir::ConstArrayAttr::get(ivarArrayTy, builder.getArrayAttr(ivarAttrs));
    auto ivarListTy = builder.getAnonRecordTy(
        {i32Ty, i32Ty, ivarArrayTy}, /*packed=*/false, /*padded=*/false);
    auto ivarListInit = builder.getAnonConstRecord(
        builder.getArrayAttr(
            {cir::IntAttr::get(
                 i32Ty, getDataLayout().getTypeAllocSize(ivarEntryTy)
                            .getFixedValue()),
             cir::IntAttr::get(i32Ty, ivarAttrs.size()), ivarArrayInit}),
        /*packed=*/false, /*padded=*/false, ivarListTy);
    cir::GlobalOp ivarListGV = createObjCConstGlobal(
        (llvm::Twine("_OBJC_$_INSTANCE_VARIABLES_") + runtimeName).str(),
        ivarListTy, ivarListInit);
    return builder.getGlobalViewAttr(voidPtrTy, ivarListGV);
  };

  llvm::SmallVector<const ObjCMethodDecl *, 8> instanceMethods;
  llvm::SmallPtrSet<const ObjCMethodDecl *, 8> seenInstanceMethods;
  auto addInstanceMethod = [&](const ObjCMethodDecl *method) {
    if (!method || method->isDirectMethod())
      return;
    if (seenInstanceMethods.insert(method).second)
      instanceMethods.push_back(method);
  };
  for (const auto *method : oid->instance_methods())
    addInstanceMethod(method);

  llvm::SmallVector<const ObjCMethodDecl *, 8> classMethods;
  llvm::SmallPtrSet<const ObjCMethodDecl *, 8> seenClassMethods;
  auto addClassMethod = [&](const ObjCMethodDecl *method) {
    if (!method || method->isDirectMethod())
      return;
    if (seenClassMethods.insert(method).second)
      classMethods.push_back(method);
  };
  for (const auto *method : oid->class_methods())
    addClassMethod(method);

  auto methodListAttr = emitMethodList(
      (llvm::Twine("_OBJC_$_INSTANCE_METHODS_") + runtimeName).str(),
      instanceMethods);
  auto classMethodListAttr = emitMethodList(
      (llvm::Twine("_OBJC_$_CLASS_METHODS_") + runtimeName).str(), classMethods);
  auto classNameAttr =
      builder.getGlobalViewAttr(voidPtrTy, getObjCClassNameGlobal(runtimeName));
  auto ivarListAttr = emitIvarList();
  auto protocolListAttr = emitProtocolList();
  auto propertyListAttr = emitPropertyList(
      (llvm::Twine("_OBJC_$_PROP_LIST_") + runtimeName).str(),
      /*isClassProperty=*/false);
  auto classPropertyListAttr = emitPropertyList(
      (llvm::Twine("_OBJC_$_CLASS_PROP_LIST_") + runtimeName).str(),
      /*isClassProperty=*/true);

  bool classIsHidden = classDecl->getVisibility() == HiddenVisibility;
  const bool classIsException = hasObjCExceptionAttribute(classDecl);
  auto getClassFlags = [&](bool isMeta) {
    uint32_t flags = isMeta ? ObjCNonFragileABIClassMeta : 0;
    if (!classDecl->getSuperClass())
      flags |= ObjCNonFragileABIClassRoot;
    if (classIsHidden)
      flags |= ObjCNonFragileABIClassHidden;
    if (oid->hasNonZeroConstructors() || oid->hasDestructors()) {
      flags |= ObjCNonFragileABIClassHasCXXStructors;
      if (!oid->hasNonZeroConstructors())
        flags |= ObjCNonFragileABIClassHasCXXDestructorOnly;
    }
    if (!isMeta && classIsException)
      flags |= ObjCNonFragileABIClassException;
    if (getLangOpts().ObjCAutoRefCount)
      flags |= ObjCNonFragileABIClassCompiledByARC;
    return flags;
  };

  auto classRoTy = builder.getAnonRecordTy(
      {i32Ty, i32Ty, i32Ty, voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy,
       voidPtrTy, voidPtrTy},
      /*packed=*/false, /*padded=*/false);
  auto buildClassRo = [&](llvm::StringRef symbolName, bool isMeta,
                          uint32_t instanceStart,
                          uint32_t instanceSize) -> cir::GlobalOp {
    mlir::Attribute methodsAttr =
        isMeta ? classMethodListAttr : methodListAttr;
    mlir::Attribute propertiesAttr =
        isMeta ? classPropertyListAttr : propertyListAttr;
    auto init = builder.getAnonConstRecord(
        builder.getArrayAttr(
            {cir::IntAttr::get(i32Ty, getClassFlags(isMeta)),
             cir::IntAttr::get(i32Ty, instanceStart),
             cir::IntAttr::get(i32Ty, instanceSize), nullPtr, classNameAttr,
             methodsAttr, protocolListAttr, isMeta ? nullPtr : ivarListAttr,
             nullPtr, propertiesAttr}),
        /*packed=*/false, /*padded=*/false, classRoTy);
    cir::GlobalOp roGV =
        createObjCConstGlobal(symbolName, classRoTy, init,
                              /*addToCompilerUsed=*/false);
    if (methodsAttr != nullPtr) {
      llvm::SmallVector<mlir::Attribute, 1> fixups;
      addConstantPtrauthFixup(
          fixups, getTypePathByteOffset(classRoTy, {5}),
          getCodeGenOpts().PointerAuth.ObjCMethodListPointer);
      setConstantPtrauthFixups(roGV, fixups);
    }
    return roGV;
  };

  auto classTy = builder.getAnonRecordTy(
      {voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy},
      /*packed=*/false, /*padded=*/false);
  uint32_t metaInstanceSize =
      getDataLayout().getTypeAllocSize(classTy).getFixedValue();
  uint32_t instanceStart = 0;
  uint32_t instanceSize = 0;
  const ASTRecordLayout &layout = astContext.getASTObjCInterfaceLayout(classDecl);
  instanceSize = layout.getDataSize().getQuantity();
  instanceStart =
      layout.getFieldCount() ? layout.getFieldOffset(0) / astContext.getCharWidth()
                             : instanceSize;

  cir::GlobalOp classGV = createOrReplaceCXXRuntimeVariable(
      loc, className, classTy, cir::GlobalLinkageKind::ExternalLinkage,
      CharUnits::Zero());
  classGV.setConstant(false);
  classGV.setAlignmentAttr(getSize(getPointerAlign()));
  if (usesDarwinObjCMachOSections(*this))
    classGV.setSectionAttr(getStringAttrCached("__DATA, __objc_data"));

  std::string metaclassName =
      (llvm::Twine("OBJC_METACLASS_$_") + runtimeName).str();
  cir::GlobalOp metaclassGV = createOrReplaceCXXRuntimeVariable(
      loc, metaclassName, classTy, cir::GlobalLinkageKind::ExternalLinkage,
      CharUnits::Zero());
  metaclassGV.setConstant(false);
  metaclassGV.setAlignmentAttr(getSize(getPointerAlign()));
  if (usesDarwinObjCMachOSections(*this))
    metaclassGV.setSectionAttr(getStringAttrCached("__DATA, __objc_data"));

  cir::GlobalOp classRoGV = buildClassRo(
      (llvm::Twine("_OBJC_CLASS_RO_$_") + runtimeName).str(),
      /*isMeta=*/false, instanceStart, instanceSize);
  cir::GlobalOp metaclassRoGV = buildClassRo(
      (llvm::Twine("_OBJC_METACLASS_RO_$_") + runtimeName).str(),
      /*isMeta=*/true, metaInstanceSize, metaInstanceSize);

  cir::GlobalOp emptyCacheGV = createOrReplaceCXXRuntimeVariable(
      loc, "_objc_empty_cache", uInt8Ty, cir::GlobalLinkageKind::ExternalLinkage,
      CharUnits::Zero());
  emptyCacheGV.setConstant(false);
  setObjCRuntimeDLLStorageClass(*this, emptyCacheGV, "_objc_empty_cache");

  mlir::Attribute nullVTableAttr = nullPtr;
  if (getTriple().isMacOSX() && getTriple().isMacOSXVersionLT(10, 9)) {
    cir::GlobalOp emptyVTableGV = createOrReplaceCXXRuntimeVariable(
        loc, "_objc_empty_vtable", uInt8Ty,
        cir::GlobalLinkageKind::ExternalLinkage, CharUnits::Zero());
    emptyVTableGV.setConstant(false);
    setObjCRuntimeDLLStorageClass(*this, emptyVTableGV, "_objc_empty_vtable");
    nullVTableAttr = builder.getGlobalViewAttr(voidPtrTy, emptyVTableGV);
  }

  mlir::Attribute classSuperAttr = nullPtr;
  mlir::Attribute metaIsaAttr;
  mlir::Attribute metaSuperAttr;
  const ObjCInterfaceDecl *superClassDecl = classDecl->getSuperClass();
  if (!superClassDecl) {
    metaIsaAttr = builder.getGlobalViewAttr(voidPtrTy, metaclassGV);
    metaSuperAttr = builder.getGlobalViewAttr(voidPtrTy, classGV);
  } else {
    classSuperAttr =
        builder.getGlobalViewAttr(voidPtrTy, getObjCClassSymbol(loc, superClassDecl));
    metaSuperAttr = builder.getGlobalViewAttr(
        voidPtrTy, getObjCMetaClassSymbol(loc, superClassDecl));

    const ObjCInterfaceDecl *rootClassDecl = classDecl;
    while (const ObjCInterfaceDecl *super = rootClassDecl->getSuperClass())
      rootClassDecl = super;
    metaIsaAttr = builder.getGlobalViewAttr(
        voidPtrTy, getObjCMetaClassSymbol(loc, rootClassDecl));
  }

  auto buildClassObject = [&](cir::GlobalOp gv, mlir::Attribute isaAttr,
                              mlir::Attribute superAttr,
                              cir::GlobalOp roGV) {
    auto init = builder.getAnonConstRecord(
        builder.getArrayAttr(
            {isaAttr, superAttr, builder.getGlobalViewAttr(voidPtrTy, emptyCacheGV),
             nullVTableAttr, builder.getGlobalViewAttr(voidPtrTy, roGV)}),
        /*packed=*/false, /*padded=*/false, classTy);
    setInitializer(gv, init);

    llvm::SmallVector<mlir::Attribute, 3> fixups;
    if (isaAttr != nullPtr)
      addConstantPtrauthFixup(
          fixups, getTypePathByteOffset(classTy, {0}),
          getCodeGenOpts().PointerAuth.ObjCIsaPointers);
    if (superAttr != nullPtr)
      addConstantPtrauthFixup(
          fixups, getTypePathByteOffset(classTy, {1}),
          getCodeGenOpts().PointerAuth.ObjCSuperPointers);
    addConstantPtrauthFixup(
        fixups, getTypePathByteOffset(classTy, {4}),
        getCodeGenOpts().PointerAuth.ObjCClassROPointers);
    setConstantPtrauthFixups(gv, fixups);
  };

  buildClassObject(metaclassGV, metaIsaAttr, metaSuperAttr, metaclassRoGV);
  buildClassObject(classGV, builder.getGlobalViewAttr(voidPtrTy, metaclassGV),
                   classSuperAttr, classRoGV);
  setGVProperties(static_cast<mlir::Operation *>(metaclassGV), classDecl);
  setGVProperties(static_cast<mlir::Operation *>(classGV), classDecl);
  objcDarwinDefinedClasses.push_back(classGV);
  if (isObjCImplementationNonLazy(oid))
    objcDarwinDefinedNonLazyClasses.push_back(classGV);
  if (classIsException)
    (void)getObjCDarwinEHTypeGlobal(loc, classDecl, /*forDefinition=*/true);

  return classGV;
}

cir::GlobalOp CIRGenModule::getObjCDarwinCategoryGlobal(
    mlir::Location loc, const ObjCCategoryImplDecl *cid) {
  assert(usesDarwinNonFragileObjCRuntime(*this) &&
         "Darwin ObjC category metadata requires the Next non-fragile "
         "runtime");
  assert(cid && "expected Objective-C category implementation");

  if (auto it = objcCategoryGlobals.find(cid); it != objcCategoryGlobals.end())
    return it->second;

  const ObjCInterfaceDecl *classDecl = cid->getClassInterface();
  const ObjCCategoryDecl *categoryDecl = cid->getCategoryDecl();
  assert(classDecl && "expected Objective-C category class");

  std::string runtimeName = classDecl->getObjCRuntimeNameAsString().str();
  std::string categoryName = cid->getNameAsString();
  std::string listName = runtimeName + "_$_" + categoryName;
  auto i32Ty = builder.getUInt32Ty();
  auto intptrTy =
      mlir::cast<cir::IntType>(convertType(astContext.getUIntPtrType()));
  mlir::Attribute nullPtr = builder.getConstPtrAttr(voidPtrTy, 0);

  auto getTypePathByteOffset =
      [&](mlir::Type type, llvm::ArrayRef<unsigned> path) -> uint64_t {
    uint64_t offset = 0;
    for (unsigned index : path) {
      if (auto recordTy = mlir::dyn_cast<cir::RecordType>(type)) {
        offset += recordTy.getElementOffset(getDataLayout().layout, index);
        type = recordTy.getMembers()[index];
        continue;
      }
      if (auto arrayTy = mlir::dyn_cast<cir::ArrayType>(type)) {
        offset += index *
                  getDataLayout().getTypeAllocSize(arrayTy.getElementType())
                      .getFixedValue();
        type = arrayTy.getElementType();
        continue;
      }
      llvm_unreachable("unexpected ObjC metadata type path");
    }
    return offset;
  };

  auto createObjCConstGlobal = [&](llvm::StringRef name, mlir::Type type,
                                   mlir::Attribute init) -> cir::GlobalOp {
    std::string sectionName =
        usesDarwinObjCMachOSections(*this) ? "__DATA, __objc_const" : "";
    cir::GlobalOp gv =
        createGlobalOp(*this, loc, name, type, /*isConstant=*/false);
    gv.setLinkage(getObjCMetadataLinkage(*this, sectionName));
    mlir::SymbolTable::setSymbolVisibility(gv,
                                           mlir::SymbolTable::Visibility::Private);
    gv.setAlignmentAttr(getSize(getPointerAlign()));
    if (!sectionName.empty())
      gv.setSectionAttr(getStringAttrCached(sectionName));
    setInitializer(gv, init);
    setDSOLocal(static_cast<mlir::Operation *>(gv));
    addCompilerUsedGlobal(gv);
    return gv;
  };

  auto addConstantPtrauthFixup =
      [&](llvm::SmallVectorImpl<mlir::Attribute> &fixups, uint64_t offset,
          const PointerAuthSchema &schema) {
        if (!schema || !shouldSignPointer(schema))
          return;
        fixups.push_back(builder.getArrayAttr(
            {builder.getI64IntegerAttr(offset),
             builder.getI32IntegerAttr(schema.getKey()),
             builder.getI64IntegerAttr(
                 getPointerAuthOtherDiscriminator(schema, GlobalDecl(),
                                                 QualType())),
             builder.getBoolAttr(schema.isAddressDiscriminated())}));
      };

  auto setConstantPtrauthFixups =
      [&](cir::GlobalOp gv, llvm::ArrayRef<mlir::Attribute> fixups) {
        if (!fixups.empty())
          gv->setAttr("cir.global_const_ptrauth", builder.getArrayAttr(fixups));
      };

  auto emitMethodList =
      [&](llvm::StringRef symbolName,
          llvm::ArrayRef<const ObjCMethodDecl *> methods) -> mlir::Attribute {
    if (methods.empty())
      return nullPtr;

    auto methodEntryTy = builder.getAnonRecordTy(
        {voidPtrTy, voidPtrTy, voidPtrTy}, /*packed=*/false, /*padded=*/false);
    llvm::SmallVector<mlir::Attribute, 8> methodAttrs;
    methodAttrs.reserve(methods.size());
    for (const auto *method : methods) {
      GlobalDecl gd(method);
      const CIRGenFunctionInfo &fi =
          getTypes().arrangeObjCMethodDeclaration(method);
      cir::FuncType fnTy = getTypes().getFunctionType(fi);
      SmallString<256> nameBuf;
      llvm::raw_svector_ostream os(nameBuf);
      mangleObjCMethodName(method, os);
      cir::FuncOp fn = getOrCreateCIRFunction(
          nameBuf.str(), fnTy, gd, /*forVTable=*/false,
          /*dontDefer=*/true, /*isThunk=*/false, NotForDefinition);
      fn.setLinkage(cir::GlobalLinkageKind::InternalLinkage);

      cir::GlobalOp selectorGV = getObjCSelectorNameGlobal(method->getSelector());
      std::string typeEncoding =
          astContext.getObjCEncodingForMethodDecl(method, /*Extended=*/false);
      cir::GlobalOp methodTypeGV = getObjCMethodTypeGlobal(typeEncoding);
      methodAttrs.push_back(builder.getAnonConstRecord(
          builder.getArrayAttr(
              {builder.getGlobalViewAttr(voidPtrTy, selectorGV),
               builder.getGlobalViewAttr(voidPtrTy, methodTypeGV),
               cir::GlobalViewAttr::get(
                   voidPtrTy, mlir::FlatSymbolRefAttr::get(fn.getSymNameAttr()))}),
          /*packed=*/false, /*padded=*/false, methodEntryTy));
    }

    auto methodArrayTy = cir::ArrayType::get(methodEntryTy, methodAttrs.size());
    auto methodArrayInit = cir::ConstArrayAttr::get(
        methodArrayTy, builder.getArrayAttr(methodAttrs));
    auto methodListTy = builder.getAnonRecordTy(
        {i32Ty, i32Ty, methodArrayTy}, /*packed=*/false, /*padded=*/false);
    auto methodListInit = builder.getAnonConstRecord(
        builder.getArrayAttr(
            {cir::IntAttr::get(
                 i32Ty,
                 getDataLayout().getTypeAllocSize(methodEntryTy).getFixedValue()),
             cir::IntAttr::get(i32Ty, methodAttrs.size()), methodArrayInit}),
        /*packed=*/false, /*padded=*/false, methodListTy);
    cir::GlobalOp methodListGV =
        createObjCConstGlobal(symbolName, methodListTy, methodListInit);

    llvm::SmallVector<mlir::Attribute, 8> ptrauthFixups;
    for (unsigned i = 0; i < methods.size(); ++i)
      addConstantPtrauthFixup(
          ptrauthFixups,
          getTypePathByteOffset(methodListTy, {2, i, 2}),
          getCodeGenOpts().PointerAuth.ObjCMethodListFunctionPointers);
    setConstantPtrauthFixups(methodListGV, ptrauthFixups);
    return builder.getGlobalViewAttr(voidPtrTy, methodListGV);
  };

  auto emitPropertyList = [&](llvm::StringRef symbolName,
                              bool isClassProperty) -> mlir::Attribute {
    if (!categoryDecl)
      return nullPtr;
    if (isClassProperty) {
      const llvm::Triple &triple = getTriple();
      if ((triple.isMacOSX() && triple.isMacOSXVersionLT(10, 11)) ||
          (triple.isiOS() && triple.isOSVersionLT(9)))
        return nullPtr;
    }

    llvm::SmallVector<const ObjCPropertyDecl *, 8> properties;
    llvm::SmallPtrSet<const IdentifierInfo *, 8> seenProperties;
    auto addProperty = [&](const ObjCPropertyDecl *property) {
      if (property->isClassProperty() != isClassProperty ||
          property->isDirectProperty())
        return;
      if (!seenProperties.insert(property->getIdentifier()).second)
        return;
      properties.push_back(property);
    };
    for (const auto *property : categoryDecl->properties())
      addProperty(property);

    std::function<void(const ObjCProtocolDecl *)> collectProtocolProperties;
    collectProtocolProperties = [&](const ObjCProtocolDecl *protocol) {
      for (const auto *inherited : protocol->protocols())
        collectProtocolProperties(inherited);
      for (const auto *property : protocol->properties())
        addProperty(property);
    };
    for (const auto *protocol : categoryDecl->protocols())
      collectProtocolProperties(protocol);

    if (properties.empty())
      return nullPtr;

    auto propertyEntryTy = builder.getAnonRecordTy(
        {voidPtrTy, voidPtrTy}, /*packed=*/false, /*padded=*/false);
    llvm::SmallVector<mlir::Attribute, 8> propertyAttrs;
    propertyAttrs.reserve(properties.size());
    for (const auto *property : properties) {
      cir::GlobalOp propertyNameGV =
          getObjCPropertyNameGlobal(property->getIdentifier()->getName());
      std::string propertyType =
          astContext.getObjCEncodingForPropertyDecl(property, cid);
      cir::GlobalOp propertyTypeGV = getObjCPropertyNameGlobal(propertyType);
      propertyAttrs.push_back(builder.getAnonConstRecord(
          builder.getArrayAttr(
              {builder.getGlobalViewAttr(voidPtrTy, propertyNameGV),
               builder.getGlobalViewAttr(voidPtrTy, propertyTypeGV)}),
          /*packed=*/false, /*padded=*/false, propertyEntryTy));
    }

    auto propertyArrayTy =
        cir::ArrayType::get(propertyEntryTy, propertyAttrs.size());
    auto propertyArrayInit = cir::ConstArrayAttr::get(
        propertyArrayTy, builder.getArrayAttr(propertyAttrs));
    auto propertyListTy = builder.getAnonRecordTy(
        {i32Ty, i32Ty, propertyArrayTy}, /*packed=*/false, /*padded=*/false);
    auto propertyListInit = builder.getAnonConstRecord(
        builder.getArrayAttr(
            {cir::IntAttr::get(
                 i32Ty, getDataLayout().getTypeAllocSize(propertyEntryTy)
                            .getFixedValue()),
             cir::IntAttr::get(i32Ty, propertyAttrs.size()), propertyArrayInit}),
        /*packed=*/false, /*padded=*/false, propertyListTy);
    cir::GlobalOp propertyListGV =
        createObjCConstGlobal(symbolName, propertyListTy, propertyListInit);
    return builder.getGlobalViewAttr(voidPtrTy, propertyListGV);
  };

  auto emitProtocolList = [&]() -> mlir::Attribute {
    if (!categoryDecl)
      return nullPtr;

    llvm::SmallVector<mlir::Attribute, 8> protocolAttrs;
    for (const auto *protocol : categoryDecl->protocols()) {
      if (protocol->isNonRuntimeProtocol())
        continue;
      protocolAttrs.push_back(
          builder.getGlobalViewAttr(voidPtrTy, getObjCProtocolGlobal(loc, protocol)));
    }

    if (protocolAttrs.empty())
      return nullPtr;

    protocolAttrs.push_back(nullPtr);
    auto protocolArrayTy = cir::ArrayType::get(voidPtrTy, protocolAttrs.size());
    auto protocolArrayInit = cir::ConstArrayAttr::get(
        protocolArrayTy, builder.getArrayAttr(protocolAttrs));
    auto protocolListTy = builder.getAnonRecordTy(
        {intptrTy, protocolArrayTy}, /*packed=*/false, /*padded=*/false);
    auto protocolListInit = builder.getAnonConstRecord(
        builder.getArrayAttr(
            {cir::IntAttr::get(intptrTy, protocolAttrs.size() - 1),
             protocolArrayInit}),
        /*packed=*/false, /*padded=*/false, protocolListTy);
    cir::GlobalOp protocolListGV = createObjCConstGlobal(
        (llvm::Twine("_OBJC_CATEGORY_PROTOCOLS_$_") + listName).str(),
        protocolListTy, protocolListInit);
    return builder.getGlobalViewAttr(voidPtrTy, protocolListGV);
  };

  llvm::SmallVector<const ObjCMethodDecl *, 16> instanceMethods;
  llvm::SmallVector<const ObjCMethodDecl *, 8> classMethods;
  for (const auto *method : cid->methods()) {
    if (method->isDirectMethod())
      continue;
    if (method->isInstanceMethod())
      instanceMethods.push_back(method);
    else
      classMethods.push_back(method);
  }

  mlir::Attribute instanceMethodListAttr = emitMethodList(
      (llvm::Twine("_OBJC_$_CATEGORY_INSTANCE_METHODS_") + listName).str(),
      instanceMethods);
  mlir::Attribute classMethodListAttr = emitMethodList(
      (llvm::Twine("_OBJC_$_CATEGORY_CLASS_METHODS_") + listName).str(),
      classMethods);
  mlir::Attribute protocolListAttr = emitProtocolList();
  mlir::Attribute propertyListAttr = emitPropertyList(
      (llvm::Twine("_OBJC_$_PROP_LIST_") + listName).str(),
      /*isClassProperty=*/false);
  mlir::Attribute classPropertyListAttr = emitPropertyList(
      (llvm::Twine("_OBJC_$_CLASS_PROP_LIST_") + listName).str(),
      /*isClassProperty=*/true);
  cir::GlobalOp categoryNameGV = getObjCClassNameGlobal(categoryName);
  cir::GlobalOp classSymbolGV = getObjCClassSymbol(loc, classDecl);

  bool isEmptyCategory =
      instanceMethodListAttr == nullPtr && classMethodListAttr == nullPtr &&
      protocolListAttr == nullPtr && propertyListAttr == nullPtr &&
      classPropertyListAttr == nullPtr;
  if (isEmptyCategory)
    return {};

  auto categoryTy = builder.getAnonRecordTy(
      {voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy,
       voidPtrTy, i32Ty},
      /*packed=*/false, /*padded=*/false);
  auto categoryInit = builder.getAnonConstRecord(
      builder.getArrayAttr(
          {builder.getGlobalViewAttr(voidPtrTy, categoryNameGV),
           builder.getGlobalViewAttr(voidPtrTy, classSymbolGV),
           instanceMethodListAttr, classMethodListAttr, protocolListAttr,
           propertyListAttr, classPropertyListAttr,
           cir::IntAttr::get(
               i32Ty, getDataLayout().getTypeAllocSize(categoryTy).getFixedValue())}),
      /*packed=*/false, /*padded=*/false, categoryTy);
  cir::GlobalOp categoryGV = createObjCConstGlobal(
      (llvm::Twine("_OBJC_$_CATEGORY_") + listName).str(), categoryTy,
      categoryInit);

  llvm::SmallVector<mlir::Attribute, 2> ptrauthFixups;
  if (instanceMethodListAttr != nullPtr)
    addConstantPtrauthFixup(
        ptrauthFixups, getTypePathByteOffset(categoryTy, {2}),
        getCodeGenOpts().PointerAuth.ObjCMethodListPointer);
  if (classMethodListAttr != nullPtr)
    addConstantPtrauthFixup(
        ptrauthFixups, getTypePathByteOffset(categoryTy, {3}),
        getCodeGenOpts().PointerAuth.ObjCMethodListPointer);
  setConstantPtrauthFixups(categoryGV, ptrauthFixups);

  objcCategoryGlobals[cid] = categoryGV;
  if (classDecl->hasAttr<ObjCClassStubAttr>())
    objcDarwinDefinedStubCategories.push_back(categoryGV);
  else
    objcDarwinDefinedCategories.push_back(categoryGV);
  if (isObjCImplementationNonLazy(cid))
    objcDarwinDefinedNonLazyCategories.push_back(categoryGV);
  return categoryGV;
}

cir::GlobalOp CIRGenModule::getObjCGNUCategoryGlobal(
    mlir::Location loc, const ObjCCategoryImplDecl *cid) {
  assert(usesGNUObjCRuntime(*this) &&
         "GNU category metadata currently requires a GNU runtime");
  assert(cid && "expected Objective-C category implementation");

  if (auto it = objcCategoryGlobals.find(cid); it != objcCategoryGlobals.end())
    return it->second;

  const ObjCInterfaceDecl *classDecl = cid->getClassInterface();
  const ObjCCategoryDecl *categoryDecl = cid->getCategoryDecl();
  assert(classDecl && categoryDecl && "expected Objective-C category context");

  std::string className = classDecl->getNameAsString();
  std::string categoryName = cid->getNameAsString();

  llvm::SmallVector<const ObjCMethodDecl *, 8> instanceMethods;
  for (auto it = cid->instmeth_begin(); it != cid->instmeth_end(); ++it)
    instanceMethods.push_back(*it);
  llvm::SmallVector<const ObjCMethodDecl *, 8> classMethods;
  for (auto it = cid->classmeth_begin(); it != cid->classmeth_end(); ++it)
    classMethods.push_back(*it);

  if (usesLegacyGNUObjCRuntime(*this)) {
    auto i32Ty = builder.getUInt32Ty();
    auto longTy = mlir::cast<cir::IntType>(convertType(astContext.LongTy));
    mlir::Attribute nullPtr = builder.getConstPtrAttr(voidPtrTy, 0);

    auto createCStringAttr = [&](llvm::StringRef prefix,
                                 llvm::StringRef value) -> mlir::Attribute {
      return builder.getGlobalViewAttr(
          voidPtrTy, getGlobalForConstantCString(loc, value, prefix));
    };

    auto emitMethodList =
        [&](llvm::ArrayRef<const ObjCMethodDecl *> methods) -> mlir::Attribute {
      if (methods.empty())
        return nullPtr;

      auto methodTy =
          builder.getAnonRecordTy({voidPtrTy, voidPtrTy, voidPtrTy},
                                  /*packed=*/false, /*padded=*/false);
      llvm::SmallVector<mlir::Attribute, 8> methodAttrs;
      methodAttrs.reserve(methods.size());
      for (const auto *method : methods) {
        GlobalDecl gd(method);
        const CIRGenFunctionInfo &fi =
            getTypes().arrangeObjCMethodDeclaration(method);
        cir::FuncType fnTy = getTypes().getFunctionType(fi);
        SmallString<256> nameBuf;
        llvm::raw_svector_ostream os(nameBuf);
        mangleObjCMethodName(method, os);
        cir::FuncOp fn = getOrCreateCIRFunction(
            nameBuf.str(), fnTy, gd, /*forVTable=*/false,
            /*dontDefer=*/true, /*isThunk=*/false, NotForDefinition);
        if (!method->isDirectMethod())
          fn.setLinkage(cir::GlobalLinkageKind::InternalLinkage);

        std::string typeEncoding =
            astContext.getObjCEncodingForMethodDecl(method);
        methodAttrs.push_back(builder.getAnonConstRecord(
            builder.getArrayAttr(
                {createCStringAttr(".objc_sel_name",
                                   method->getSelector().getAsString()),
                 createCStringAttr(".objc_method_type", typeEncoding),
                 cir::GlobalViewAttr::get(
                     voidPtrTy, mlir::FlatSymbolRefAttr::get(fn.getSymNameAttr()))}),
            /*packed=*/false, /*padded=*/false, methodTy));
      }

      auto methodArrayTy = cir::ArrayType::get(methodTy, methodAttrs.size());
      auto methodArrayInit = cir::ConstArrayAttr::get(
          methodArrayTy, builder.getArrayAttr(methodAttrs));
      auto methodListTy =
          builder.getAnonRecordTy({voidPtrTy, i32Ty, methodArrayTy},
                                  /*packed=*/false, /*padded=*/false);
      auto methodListInit = builder.getAnonConstRecord(
          builder.getArrayAttr(
              {nullPtr, cir::IntAttr::get(i32Ty, methodAttrs.size()),
               methodArrayInit}),
          /*packed=*/false, /*padded=*/false, methodListTy);
      cir::GlobalOp methodListGV = createGlobalOp(
          *this, loc, getUniqueGlobalName(".objc_method_list"), methodListTy,
          /*isConstant=*/false);
      methodListGV.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
      mlir::SymbolTable::setSymbolVisibility(
          methodListGV, mlir::SymbolTable::Visibility::Private);
      methodListGV.setAlignmentAttr(getSize(getPointerAlign()));
      setInitializer(methodListGV, methodListInit);
      setDSOLocal(static_cast<mlir::Operation *>(methodListGV));
      return builder.getGlobalViewAttr(voidPtrTy, methodListGV);
    };

    auto emitProtocolList = [&]() -> mlir::Attribute {
      llvm::SmallVector<mlir::Attribute, 4> protocols;
      const auto &referencedProtocols = categoryDecl->getReferencedProtocols();
      for (const auto *protocol :
           getRuntimeProtocolList(referencedProtocols.begin(),
                                  referencedProtocols.end())) {
        protocols.push_back(
            builder.getGlobalViewAttr(voidPtrTy,
                                      getObjCProtocolGlobal(loc, protocol)));
      }

      auto protocolArrayTy = cir::ArrayType::get(voidPtrTy, protocols.size());
      auto protocolListTy = builder.getAnonRecordTy(
          {voidPtrTy, longTy, protocolArrayTy}, /*packed=*/false,
          /*padded=*/false);
      cir::GlobalOp protocolListGV = createGlobalOp(
          *this, loc, getUniqueGlobalName(".objc_protocol_list"), protocolListTy,
          /*isConstant=*/false);
      protocolListGV.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
      mlir::SymbolTable::setSymbolVisibility(
          protocolListGV, mlir::SymbolTable::Visibility::Private);
      protocolListGV.setAlignmentAttr(getSize(getPointerAlign()));
      setDSOLocal(static_cast<mlir::Operation *>(protocolListGV));

      if (protocols.empty()) {
        setInitializer(protocolListGV, builder.getZeroInitAttr(protocolListTy));
        return builder.getGlobalViewAttr(voidPtrTy, protocolListGV);
      }

      auto protocolArrayInit = cir::ConstArrayAttr::get(
          protocolArrayTy, builder.getArrayAttr(protocols));
      auto protocolListInit = builder.getAnonConstRecord(
          builder.getArrayAttr(
              {nullPtr, cir::IntAttr::get(longTy, protocols.size()),
               protocolArrayInit}),
          /*packed=*/false, /*padded=*/false, protocolListTy);
      setInitializer(protocolListGV, protocolListInit);
      return builder.getGlobalViewAttr(voidPtrTy, protocolListGV);
    };

    auto categoryTy = builder.getAnonRecordTy(
        {voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy},
        /*packed=*/false, /*padded=*/false);
    auto categoryInit = builder.getAnonConstRecord(
        builder.getArrayAttr(
            {createCStringAttr(".objc_category_name", categoryName),
             createCStringAttr(".objc_category_class_name", className),
             emitMethodList(instanceMethods), emitMethodList(classMethods),
             emitProtocolList()}),
        /*packed=*/false, /*padded=*/false, categoryTy);

    cir::GlobalOp gv = createGlobalOp(
        *this, loc,
        getUniqueGlobalName(std::string(".objc_category_") + className +
                            categoryName),
        categoryTy, /*isConstant=*/false);
    gv.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
    mlir::SymbolTable::setSymbolVisibility(
        gv, mlir::SymbolTable::Visibility::Private);
    gv.setAlignmentAttr(getSize(getPointerAlign()));
    setInitializer(gv, categoryInit);
    setDSOLocal(static_cast<mlir::Operation *>(gv));

    objcCategoryGlobals[cid] = gv;
    legacyGNUCategoryGlobals.push_back(gv);
    return gv;
  }

  auto i32Ty = builder.getUInt32Ty();
  auto sizeTy = mlir::cast<cir::IntType>(convertType(astContext.getSizeType()));
  mlir::Attribute nullPtr = builder.getConstPtrAttr(voidPtrTy, 0);

  auto createCStringAttr = [&](llvm::StringRef prefix,
                               llvm::StringRef value) -> mlir::Attribute {
    cir::GlobalOp stringGV =
        getGlobalForConstantCString(loc, value, prefix);
    return builder.getGlobalViewAttr(voidPtrTy, stringGV);
  };

  auto emitMethodList =
      [&](llvm::ArrayRef<const ObjCMethodDecl *> methods) -> mlir::Attribute {
    if (methods.empty())
      return nullPtr;

    auto methodTy =
        builder.getAnonRecordTy({voidPtrTy, voidPtrTy, voidPtrTy},
                                /*packed=*/false, /*padded=*/false);
    llvm::SmallVector<mlir::Attribute, 8> methodAttrs;
    methodAttrs.reserve(methods.size());
    for (const auto *method : methods) {
      GlobalDecl gd(method);
      const CIRGenFunctionInfo &fi =
          getTypes().arrangeObjCMethodDeclaration(method);
      cir::FuncType fnTy = getTypes().getFunctionType(fi);
      SmallString<256> nameBuf;
      {
        llvm::raw_svector_ostream os(nameBuf);
        mangleObjCMethodName(method, os);
      }
      cir::FuncOp fn = getOrCreateCIRFunction(
          nameBuf.str(), fnTy, gd, /*forVTable=*/false,
          /*dontDefer=*/true, /*isThunk=*/false, NotForDefinition);
      if (!method->isDirectMethod())
        fn.setLinkage(cir::GlobalLinkageKind::InternalLinkage);

      cir::GlobalOp selectorGV =
          getObjCGNUSelectorGlobal(method->getSelector(), method);
      std::string extendedTypeEncoding =
          astContext.getObjCEncodingForMethodDecl(method, /*Extended=*/true);
      methodAttrs.push_back(builder.getAnonConstRecord(
          builder.getArrayAttr(
              {cir::GlobalViewAttr::get(
                   voidPtrTy, mlir::FlatSymbolRefAttr::get(fn.getSymNameAttr())),
               builder.getGlobalViewAttr(voidPtrTy, selectorGV),
               createCStringAttr(".objc_method_type", extendedTypeEncoding)}),
          /*packed=*/false, /*padded=*/false, methodTy));
    }

    auto methodArrayTy = cir::ArrayType::get(methodTy, methodAttrs.size());
    auto methodArrayInit = cir::ConstArrayAttr::get(
        methodArrayTy, builder.getArrayAttr(methodAttrs));
    auto methodListTy = builder.getAnonRecordTy(
        {voidPtrTy, i32Ty, sizeTy, methodArrayTy}, /*packed=*/false,
        /*padded=*/false);
    auto methodListInit = builder.getAnonConstRecord(
        builder.getArrayAttr(
            {nullPtr, cir::IntAttr::get(i32Ty, methodAttrs.size()),
             cir::IntAttr::get(
                 sizeTy,
                 getDataLayout().getTypeAllocSize(methodTy).getFixedValue()),
             methodArrayInit}),
        /*packed=*/false, /*padded=*/false, methodListTy);
    cir::GlobalOp methodListGV = createGlobalOp(
        *this, loc, getUniqueGlobalName(".objc_method_list"), methodListTy,
        /*isConstant=*/false);
    methodListGV.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
    mlir::SymbolTable::setSymbolVisibility(
        methodListGV, mlir::SymbolTable::Visibility::Private);
    methodListGV.setAlignmentAttr(getSize(getPointerAlign()));
    setInitializer(methodListGV, methodListInit);
    setDSOLocal(static_cast<mlir::Operation *>(methodListGV));
    return builder.getGlobalViewAttr(voidPtrTy, methodListGV);
  };

  auto emitPropertyList = [&](bool isClassProperty) -> mlir::Attribute {
    llvm::SmallVector<const ObjCPropertyDecl *, 8> properties;
    llvm::SmallPtrSet<const IdentifierInfo *, 8> seenProperties;

    for (const auto *property : categoryDecl->properties()) {
      if (property->isClassProperty() != isClassProperty)
        continue;
      if (!seenProperties.insert(property->getIdentifier()).second)
        continue;
      properties.push_back(property);
    }

    std::function<void(const ObjCProtocolDecl *)> collectProtocolProperties;
    collectProtocolProperties = [&](const ObjCProtocolDecl *protocol) {
      for (const auto *inherited : protocol->protocols())
        collectProtocolProperties(inherited);
      for (const auto *property : protocol->properties()) {
        if (property->isClassProperty() != isClassProperty)
          continue;
        if (!astContext.getObjCPropertyImplDeclForPropertyDecl(property, cid))
          continue;
        if (!seenProperties.insert(property->getIdentifier()).second)
          continue;
        properties.push_back(property);
      }
    };
    for (const auto *protocol : categoryDecl->protocols())
      collectProtocolProperties(protocol);

    if (properties.empty())
      return nullPtr;

    auto propertyTy =
        builder.getAnonRecordTy({voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy,
                                 voidPtrTy},
                                /*packed=*/false, /*padded=*/false);
    llvm::SmallVector<mlir::Attribute, 8> propertyAttrs;
    propertyAttrs.reserve(properties.size());
    for (const auto *property : properties) {
      std::string propertyAttrsStr =
          astContext.getObjCEncodingForPropertyDecl(property, cid);
      std::string propertyTypeStr;
      astContext.getObjCEncodingForType(property->getType(), propertyTypeStr);

      auto getAccessorSelectorAttr = [&](const ObjCMethodDecl *accessor) {
        if (!accessor)
          return nullPtr;
        return mlir::Attribute(builder.getGlobalViewAttr(
            voidPtrTy,
            getObjCGNUSelectorGlobal(accessor->getSelector(), accessor)));
      };

      propertyAttrs.push_back(builder.getAnonConstRecord(
          builder.getArrayAttr(
              {createCStringAttr(".objc_property_name", property->getName()),
               createCStringAttr(".objc_property_attrs", propertyAttrsStr),
               createCStringAttr(".objc_property_type", propertyTypeStr),
               getAccessorSelectorAttr(property->getGetterMethodDecl()),
               getAccessorSelectorAttr(property->getSetterMethodDecl())}),
          /*packed=*/false, /*padded=*/false, propertyTy));
    }

    auto propertyArrayTy = cir::ArrayType::get(propertyTy, propertyAttrs.size());
    auto propertyArrayInit = cir::ConstArrayAttr::get(
        propertyArrayTy, builder.getArrayAttr(propertyAttrs));
    auto propertyListTy = builder.getAnonRecordTy(
        {i32Ty, i32Ty, voidPtrTy, propertyArrayTy}, /*packed=*/false,
        /*padded=*/false);
    auto propertyListInit = builder.getAnonConstRecord(
        builder.getArrayAttr(
            {cir::IntAttr::get(i32Ty, propertyAttrs.size()),
             cir::IntAttr::get(
                 i32Ty,
                 getDataLayout().getTypeAllocSize(propertyTy).getFixedValue()),
             nullPtr, propertyArrayInit}),
        /*packed=*/false, /*padded=*/false, propertyListTy);
    cir::GlobalOp propertyListGV = createGlobalOp(
        *this, loc, getUniqueGlobalName(".objc_property_list"), propertyListTy,
        /*isConstant=*/false);
    propertyListGV.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
    mlir::SymbolTable::setSymbolVisibility(
        propertyListGV, mlir::SymbolTable::Visibility::Private);
    propertyListGV.setAlignmentAttr(getSize(getPointerAlign()));
    setInitializer(propertyListGV, propertyListInit);
    setDSOLocal(static_cast<mlir::Operation *>(propertyListGV));
    return builder.getGlobalViewAttr(voidPtrTy, propertyListGV);
  };

  auto emitProtocolList = [&]() -> mlir::Attribute {
    llvm::SmallVector<mlir::Attribute, 4> protocols;
    const auto &referencedProtocols = categoryDecl->getReferencedProtocols();
    for (const auto *protocol :
         getRuntimeProtocolList(referencedProtocols.begin(),
                                referencedProtocols.end())) {
      protocols.push_back(
          builder.getGlobalViewAttr(voidPtrTy, getObjCProtocolGlobal(loc, protocol)));
    }

    auto protocolArrayTy = cir::ArrayType::get(voidPtrTy, protocols.size());
    auto protocolArrayInit = cir::ConstArrayAttr::get(
        protocolArrayTy, builder.getArrayAttr(protocols));
    auto protocolListTy = builder.getAnonRecordTy(
        {voidPtrTy, sizeTy, protocolArrayTy}, /*packed=*/false,
        /*padded=*/false);
    auto protocolListInit = builder.getAnonConstRecord(
        builder.getArrayAttr(
            {nullPtr, cir::IntAttr::get(sizeTy, protocols.size()),
             protocolArrayInit}),
        /*packed=*/false, /*padded=*/false, protocolListTy);
    cir::GlobalOp protocolListGV = createGlobalOp(
        *this, loc, getUniqueGlobalName(".objc_protocol_list"), protocolListTy,
        /*isConstant=*/false);
    protocolListGV.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
    mlir::SymbolTable::setSymbolVisibility(
        protocolListGV, mlir::SymbolTable::Visibility::Private);
    protocolListGV.setAlignmentAttr(getSize(getPointerAlign()));
    setInitializer(protocolListGV, protocolListInit);
    setDSOLocal(static_cast<mlir::Operation *>(protocolListGV));
    return builder.getGlobalViewAttr(voidPtrTy, protocolListGV);
  };

  auto categoryTy = builder.getAnonRecordTy(
      {voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy,
       voidPtrTy},
      /*packed=*/false, /*padded=*/false);
  auto categoryInit = builder.getAnonConstRecord(
      builder.getArrayAttr(
          {createCStringAttr(".objc_category_name", categoryName),
           createCStringAttr(".objc_category_class_name", className),
           emitMethodList(instanceMethods), emitMethodList(classMethods),
           emitProtocolList(), emitPropertyList(/*isClassProperty=*/false),
           emitPropertyList(/*isClassProperty=*/true)}),
      /*packed=*/false, /*padded=*/false, categoryTy);

  cir::GlobalOp gv = createGlobalOp(
      *this, loc,
      getUniqueGlobalName(std::string(".objc_category_") + className +
                          categoryName),
      categoryTy, /*isConstant=*/false);
  gv.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
  mlir::SymbolTable::setSymbolVisibility(gv,
                                         mlir::SymbolTable::Visibility::Private);
  gv.setAlignmentAttr(getSize(getPointerAlign()));
  gv.setSectionAttr(getStringAttrCached(
      getGNUstep2ObjCSectionName(*this, GNUstep2CategorySection)));
  setInitializer(gv, categoryInit);
  setDSOLocal(static_cast<mlir::Operation *>(gv));
  addUsedGlobal(gv);

  objcCategoryGlobals[cid] = gv;
  return gv;
}

cir::GlobalOp CIRGenModule::getObjCDarwinFragileCategoryGlobal(
    mlir::Location loc, const ObjCCategoryImplDecl *cid) {
  assert(usesDarwinFragileObjCRuntime(*this) &&
         "Darwin category metadata currently requires fragile Mach-O");
  assert(cid && "expected Objective-C category implementation");

  if (auto it = objcCategoryGlobals.find(cid); it != objcCategoryGlobals.end())
    return it->second;

  const ObjCInterfaceDecl *classDecl = cid->getClassInterface();
  const ObjCCategoryDecl *categoryDecl = cid->getCategoryDecl();
  assert(classDecl && "expected Objective-C category class");

  std::string className = classDecl->getObjCRuntimeNameAsString().str();
  std::string categoryName = cid->getNameAsString();
  std::string extName = (llvm::Twine(classDecl->getName()) + "_" + categoryName).str();
  auto i32Ty = builder.getUInt32Ty();
  auto longTy = mlir::cast<cir::IntType>(convertType(astContext.LongTy));
  mlir::Attribute nullPtr = builder.getConstPtrAttr(voidPtrTy, 0);
  uint64_t pointerSize = getPointerAlign().getQuantity();
  auto methodEntryTy = builder.getAnonRecordTy(
      {voidPtrTy, voidPtrTy, voidPtrTy}, /*packed=*/false, /*padded=*/false);
  auto propertyEntryTy = builder.getAnonRecordTy(
      {voidPtrTy, voidPtrTy}, /*packed=*/false, /*padded=*/false);
  auto categoryTy = builder.getAnonRecordTy(
      {voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy, i32Ty, voidPtrTy,
       voidPtrTy},
      /*packed=*/false, /*padded=*/false);

  auto createFragileMetadataGlobal =
      [&](llvm::StringRef name, mlir::Type type, mlir::Attribute init,
          llvm::StringRef section) -> cir::GlobalOp {
    cir::GlobalOp global =
        createGlobalOp(*this, loc, name, type, /*isConstant=*/false);
    global.setLinkage(cir::GlobalLinkageKind::PrivateLinkage);
    mlir::SymbolTable::setSymbolVisibility(
        global, mlir::SymbolTable::Visibility::Private);
    global.setAlignmentAttr(getSize(getPointerAlign()));
    global.setSectionAttr(getStringAttrCached(section));
    setInitializer(global, init);
    setDSOLocal(static_cast<mlir::Operation *>(global));
    addCompilerUsedGlobal(global);
    return global;
  };

  auto emitMethodList =
      [&](llvm::StringRef symbolName, llvm::StringRef sectionName,
          llvm::ArrayRef<const ObjCMethodDecl *> methods) -> mlir::Attribute {
    if (methods.empty())
      return nullPtr;

    llvm::SmallVector<mlir::Attribute, 8> methodAttrs;
    methodAttrs.reserve(methods.size());
    for (const auto *method : methods) {
      cir::GlobalOp methodNameGV =
          getObjCSelectorNameGlobal(method->getSelector());
      std::string typeEncoding =
          astContext.getObjCEncodingForMethodDecl(method, /*Extended=*/false);
      cir::GlobalOp methodTypeGV = getObjCMethodTypeGlobal(typeEncoding);

      GlobalDecl gd(method);
      const CIRGenFunctionInfo &fi =
          getTypes().arrangeObjCMethodDeclaration(method);
      cir::FuncType fnTy = getTypes().getFunctionType(fi);
      SmallString<256> nameBuf;
      {
        llvm::raw_svector_ostream os(nameBuf);
        mangleObjCMethodName(method, os);
      }
      cir::FuncOp fn = getOrCreateCIRFunction(
          nameBuf.str(), fnTy, gd, /*forVTable=*/false,
          /*dontDefer=*/true, /*isThunk=*/false, NotForDefinition);
      if (!method->isDirectMethod())
        fn.setLinkage(cir::GlobalLinkageKind::InternalLinkage);

      methodAttrs.push_back(builder.getAnonConstRecord(
          builder.getArrayAttr(
              {builder.getGlobalViewAttr(voidPtrTy, methodNameGV),
               builder.getGlobalViewAttr(voidPtrTy, methodTypeGV),
               cir::GlobalViewAttr::get(
                   voidPtrTy, mlir::FlatSymbolRefAttr::get(fn.getSymNameAttr()))}),
          /*packed=*/false, /*padded=*/false, methodEntryTy));
    }

    auto methodArrayTy = cir::ArrayType::get(methodEntryTy, methodAttrs.size());
    auto methodArrayInit =
        cir::ConstArrayAttr::get(methodArrayTy, builder.getArrayAttr(methodAttrs));
    auto methodListTy = builder.getAnonRecordTy(
        {voidPtrTy, i32Ty, methodArrayTy}, /*packed=*/false, /*padded=*/false);
    auto methodListInit = builder.getAnonConstRecord(
        builder.getArrayAttr(
            {nullPtr, cir::IntAttr::get(i32Ty, methodAttrs.size()),
             methodArrayInit}),
        /*packed=*/false, /*padded=*/false, methodListTy);
    cir::GlobalOp methodListGV = createFragileMetadataGlobal(
        symbolName, methodListTy, methodListInit, sectionName);
    return builder.getGlobalViewAttr(voidPtrTy, methodListGV);
  };

  auto emitPropertyList = [&](bool isClassProperty) -> mlir::Attribute {
    if (!categoryDecl)
      return nullPtr;

    llvm::SmallVector<const ObjCPropertyDecl *, 8> properties;
    llvm::SmallPtrSet<const IdentifierInfo *, 8> seenProperties;
    auto addProperty = [&](const ObjCPropertyDecl *property) {
      if (property->isClassProperty() != isClassProperty ||
          property->isDirectProperty())
        return;
      if (!seenProperties.insert(property->getIdentifier()).second)
        return;
      properties.push_back(property);
    };
    for (const auto *property : categoryDecl->properties())
      addProperty(property);

    std::function<void(const ObjCProtocolDecl *)> collectProtocolProperties;
    collectProtocolProperties = [&](const ObjCProtocolDecl *protocol) {
      for (const auto *inherited : protocol->protocols())
        collectProtocolProperties(inherited);
      for (const auto *property : protocol->properties())
        addProperty(property);
    };
    for (const auto *protocol : categoryDecl->protocols())
      collectProtocolProperties(protocol);

    if (properties.empty())
      return nullPtr;

    llvm::SmallVector<mlir::Attribute, 8> propertyAttrs;
    propertyAttrs.reserve(properties.size());
    for (const auto *property : properties) {
      cir::GlobalOp propertyNameGV =
          getObjCPropertyNameGlobal(property->getIdentifier()->getName());
      std::string propertyType =
          astContext.getObjCEncodingForPropertyDecl(property, cid);
      cir::GlobalOp propertyTypeGV = getObjCPropertyNameGlobal(propertyType);
      propertyAttrs.push_back(builder.getAnonConstRecord(
          builder.getArrayAttr(
              {builder.getGlobalViewAttr(voidPtrTy, propertyNameGV),
               builder.getGlobalViewAttr(voidPtrTy, propertyTypeGV)}),
          /*packed=*/false, /*padded=*/false, propertyEntryTy));
    }

    auto propertyArrayTy =
        cir::ArrayType::get(propertyEntryTy, propertyAttrs.size());
    auto propertyArrayInit = cir::ConstArrayAttr::get(
        propertyArrayTy, builder.getArrayAttr(propertyAttrs));
    auto propertyListTy = builder.getAnonRecordTy(
        {i32Ty, i32Ty, propertyArrayTy}, /*packed=*/false, /*padded=*/false);
    auto propertyListInit = builder.getAnonConstRecord(
        builder.getArrayAttr(
            {cir::IntAttr::get(i32Ty, 2 * pointerSize),
             cir::IntAttr::get(i32Ty, propertyAttrs.size()), propertyArrayInit}),
        /*packed=*/false, /*padded=*/false, propertyListTy);
    cir::GlobalOp propertyListGV = createFragileMetadataGlobal(
        (llvm::Twine(isClassProperty ? "_OBJC_$_CLASS_PROP_LIST_"
                                     : "_OBJC_$_PROP_LIST_") +
         extName)
            .str(),
        propertyListTy, propertyListInit,
        "__OBJC,__property,regular,no_dead_strip");
    return builder.getGlobalViewAttr(voidPtrTy, propertyListGV);
  };

  auto emitProtocolList = [&]() -> mlir::Attribute {
    if (!categoryDecl)
      return nullPtr;

    llvm::SmallVector<mlir::Attribute, 8> protocolAttrs;
    for (const auto *protocol : categoryDecl->protocols()) {
      if (protocol->isNonRuntimeProtocol())
        continue;
      protocolAttrs.push_back(builder.getGlobalViewAttr(
          voidPtrTy, getObjCProtocolGlobal(loc, protocol)));
    }
    if (protocolAttrs.empty())
      return nullPtr;

    protocolAttrs.push_back(nullPtr);
    auto protocolArrayTy = cir::ArrayType::get(voidPtrTy, protocolAttrs.size());
    auto protocolArrayInit = cir::ConstArrayAttr::get(
        protocolArrayTy, builder.getArrayAttr(protocolAttrs));
    auto protocolListTy = builder.getAnonRecordTy(
        {voidPtrTy, longTy, protocolArrayTy}, /*packed=*/false,
        /*padded=*/false);
    auto protocolListInit = builder.getAnonConstRecord(
        builder.getArrayAttr(
            {nullPtr, cir::IntAttr::get(longTy, protocolAttrs.size() - 1),
             protocolArrayInit}),
        /*packed=*/false, /*padded=*/false, protocolListTy);
    cir::GlobalOp protocolListGV = createFragileMetadataGlobal(
        "OBJC_CATEGORY_PROTOCOLS_" + extName, protocolListTy, protocolListInit,
        "__OBJC,__cat_cls_meth,regular,no_dead_strip");
    return builder.getGlobalViewAttr(voidPtrTy, protocolListGV);
  };

  llvm::SmallVector<const ObjCMethodDecl *, 8> instanceMethods;
  for (auto it = cid->instmeth_begin(); it != cid->instmeth_end(); ++it)
    if (!(*it)->isDirectMethod())
      instanceMethods.push_back(*it);
  llvm::SmallVector<const ObjCMethodDecl *, 8> classMethods;
  for (auto it = cid->classmeth_begin(); it != cid->classmeth_end(); ++it)
    if (!(*it)->isDirectMethod())
      classMethods.push_back(*it);

  auto categoryInit = builder.getAnonConstRecord(
      builder.getArrayAttr(
          {builder.getGlobalViewAttr(voidPtrTy,
                                     getObjCClassNameGlobal(categoryName)),
           builder.getGlobalViewAttr(voidPtrTy, getObjCClassNameGlobal(className)),
           emitMethodList("OBJC_CATEGORY_INSTANCE_METHODS_" + extName,
                          "__OBJC,__cat_inst_meth,regular,no_dead_strip",
                          instanceMethods),
           emitMethodList("OBJC_CATEGORY_CLASS_METHODS_" + extName,
                          "__OBJC,__cat_cls_meth,regular,no_dead_strip",
                          classMethods),
           emitProtocolList(), cir::IntAttr::get(i32Ty, 8 * pointerSize),
           emitPropertyList(/*isClassProperty=*/false),
           emitPropertyList(/*isClassProperty=*/true)}),
      /*packed=*/false, /*padded=*/false, categoryTy);

  cir::GlobalOp gv = createFragileMetadataGlobal(
      "OBJC_CATEGORY_" + extName, categoryTy, categoryInit,
      "__OBJC,__category,regular,no_dead_strip");
  objcCategoryGlobals[cid] = gv;
  objcDarwinFragileDefinedCategories.push_back(gv);
  return gv;
}

void CIRGenModule::emitObjCPropertyImplementations(
    const ObjCImplementationDecl *oid) {
  assert(oid && "expected Objective-C implementation");

  for (const auto *pid : oid->property_impls()) {
    if (pid->getPropertyImplementation() != ObjCPropertyImplDecl::Synthesize)
      continue;

    ObjCPropertyDecl *property = pid->getPropertyDecl();
    ObjCMethodDecl *getter = pid->getGetterMethodDecl();
    if (!getter || getter->isSynthesizedAccessorStub()) {
      CIRGenFunction cgf(*this, builder);
      mlir::OpBuilder::InsertionGuard guard(builder);
      cgf.generateObjCPropertyGetter(oid, pid);
    }

    ObjCMethodDecl *setter = pid->getSetterMethodDecl();
    if (!property->isReadOnly() &&
        (!setter || setter->isSynthesizedAccessorStub())) {
      CIRGenFunction cgf(*this, builder);
      mlir::OpBuilder::InsertionGuard guard(builder);
      cgf.generateObjCPropertySetter(oid, pid);
    }
  }
}

static bool needsObjCDestructMethod(ObjCImplementationDecl *oid) {
  const ObjCInterfaceDecl *iface = oid->getClassInterface();
  for (const ObjCIvarDecl *ivar = iface->all_declared_ivar_begin(); ivar;
       ivar = ivar->getNextIvar())
    if (ivar->getType().isDestructedType())
      return true;
  return false;
}

static bool allTrivialObjCInitializers(CIRGenModule &cgm,
                                       ObjCImplementationDecl *oid) {
  CIRGenFunction cgf(cgm, cgm.getBuilder());
  for (const auto *ivarInit : oid->inits())
    if (!cgf.isTrivialInitializer(ivarInit->getInit()))
      return false;
  return true;
}

void CIRGenModule::emitObjCIvarInitializations(ObjCImplementationDecl *oid) {
  if (needsObjCDestructMethod(oid)) {
    const IdentifierInfo *ii = &astContext.Idents.get(".cxx_destruct");
    Selector cxxSelector = astContext.Selectors.getSelector(0, &ii);
    ObjCMethodDecl *dtorMethod = oid->getInstanceMethod(cxxSelector);
    if (!dtorMethod) {
      dtorMethod = ObjCMethodDecl::Create(
          astContext, oid->getLocation(), oid->getLocation(), cxxSelector,
          astContext.VoidTy, nullptr, oid,
          /*isInstance=*/true, /*isVariadic=*/false,
          /*isPropertyAccessor=*/true, /*isSynthesizedAccessorStub=*/false,
          /*isImplicitlyDeclared=*/true,
          /*isDefined=*/false, ObjCImplementationControl::Required);
      oid->addInstanceMethod(dtorMethod);
    }

    CIRGenFunction cgf(*this, builder);
    mlir::OpBuilder::InsertionGuard guard(builder);
    cgf.generateObjCDestructorMethod(oid, dtorMethod);
    oid->setHasDestructors(true);
  }

  if (oid->getNumIvarInitializers() == 0 || allTrivialObjCInitializers(*this, oid))
    return;

  const IdentifierInfo *ii = &astContext.Idents.get(".cxx_construct");
  Selector cxxSelector = astContext.Selectors.getSelector(0, &ii);
  ObjCMethodDecl *ctorMethod = oid->getInstanceMethod(cxxSelector);
  if (!ctorMethod) {
    ctorMethod = ObjCMethodDecl::Create(
        astContext, oid->getLocation(), oid->getLocation(), cxxSelector,
        astContext.getObjCIdType(), nullptr, oid,
        /*isInstance=*/true, /*isVariadic=*/false,
        /*isPropertyAccessor=*/true, /*isSynthesizedAccessorStub=*/false,
        /*isImplicitlyDeclared=*/true,
        /*isDefined=*/false, ObjCImplementationControl::Required);
    oid->addInstanceMethod(ctorMethod);
  }

  CIRGenFunction cgf(*this, builder);
  mlir::OpBuilder::InsertionGuard guard(builder);
  cgf.generateObjCConstructorMethod(oid, ctorMethod);
  oid->setHasNonZeroConstructors(true);
}

static bool isObjCImplementationNonLazy(const ObjCImplDecl *od) {
  ASTContext &ctx = od->getASTContext();
  Selector loadSel = ctx.Selectors.getNullarySelector(&ctx.Idents.get("load"));
  return od->getClassMethod(loadSel) != nullptr ||
         od->getClassInterface()->hasAttr<ObjCNonLazyClassAttr>() ||
         od->hasAttr<ObjCNonLazyClassAttr>();
}

void CIRGenModule::registerObjCCompatibleAlias(
    const ObjCCompatibleAliasDecl *oad) {
  assert(oad && "expected Objective-C compatibility alias");
  if (!usesLegacyGNUObjCRuntime(*this))
    return;

  const auto *classDecl = oad->getClassInterface();
  if (!classDecl)
    return;

  legacyGNUClassAliases.emplace_back(classDecl->getNameAsString(),
                                     oad->getNameAsString());
}

cir::GlobalOp CIRGenModule::getObjCGNUClassGlobal(
    mlir::Location loc, const ObjCImplementationDecl *oid) {
  assert(usesGNUObjCRuntime(*this) &&
         "GNU ObjC class metadata currently requires a GNU runtime");
  assert(oid && "expected Objective-C implementation");

  const ObjCInterfaceDecl *classDecl = oid->getClassInterface();
  assert(classDecl && "expected Objective-C class interface");
  std::string className = classDecl->getNameAsString();
  if (usesLegacyGNUObjCRuntime(*this)) {
    std::string classSymbolName = (llvm::Twine("_OBJC_CLASS_") + className).str();
    if (auto *existing = getGlobalValue(classSymbolName))
      return mlir::cast<cir::GlobalOp>(existing);

    auto i8Ty = builder.getUInt8Ty();
    auto i32Ty = builder.getUInt32Ty();
    auto longTy = mlir::cast<cir::IntType>(convertType(astContext.LongTy));
    auto intptrTy = mlir::cast<cir::IntType>(uIntPtrTy);
    mlir::Attribute nullPtr = builder.getConstPtrAttr(voidPtrTy, 0);

    auto createCStringAttr = [&](llvm::StringRef prefix,
                                 llvm::StringRef value) -> mlir::Attribute {
      return builder.getGlobalViewAttr(
          voidPtrTy, getGlobalForConstantCString(loc, value, prefix));
    };

    auto createPropertyNameAttr = [&](const ObjCPropertyDecl *property)
        -> mlir::Attribute {
      const ObjCRuntime runtime = getLangOpts().ObjCRuntime;
      if (runtime.getKind() == ObjCRuntime::GNUstep &&
          runtime.getVersion() >= VersionTuple(1, 6)) {
        std::string encoding =
            astContext.getObjCEncodingForPropertyDecl(property, oid);
        std::string nameAndAttrs;
        nameAndAttrs.push_back('\0');
        nameAndAttrs.push_back(static_cast<char>(encoding.size() + 3));
        nameAndAttrs += encoding;
        nameAndAttrs.push_back('\0');
        nameAndAttrs += property->getNameAsString();
        return createCStringAttr(
            ".objc_property_name",
            llvm::StringRef(nameAndAttrs.data(), nameAndAttrs.size()));
      }
      return createCStringAttr(".objc_property_name", property->getName());
    };

    auto makeLegacyGNUBitField = [&](llvm::ArrayRef<bool> bits) {
      uint64_t value = 1;
      for (size_t i = 0; i < bits.size() &&
                         i + 1 < static_cast<size_t>(intptrTy.getWidth());
           ++i)
        if (bits[i])
          value |= 1ULL << (i + 1);
      return cir::IntAttr::get(intptrTy, value);
    };

    auto emitMethodList =
        [&](llvm::ArrayRef<const ObjCMethodDecl *> methods) -> mlir::Attribute {
      if (methods.empty())
        return nullPtr;

      auto methodTy =
          builder.getAnonRecordTy({voidPtrTy, voidPtrTy, voidPtrTy},
                                  /*packed=*/false, /*padded=*/false);
      llvm::SmallVector<mlir::Attribute, 8> methodAttrs;
      methodAttrs.reserve(methods.size());
      for (const auto *method : methods) {
        GlobalDecl gd(method);
        const CIRGenFunctionInfo &fi =
            getTypes().arrangeObjCMethodDeclaration(method);
        cir::FuncType fnTy = getTypes().getFunctionType(fi);
        SmallString<256> nameBuf;
        llvm::raw_svector_ostream os(nameBuf);
        mangleObjCMethodName(method, os);
        cir::FuncOp fn = getOrCreateCIRFunction(
            nameBuf.str(), fnTy, gd, /*forVTable=*/false,
            /*dontDefer=*/true, /*isThunk=*/false, NotForDefinition);
        if (!method->isDirectMethod())
          fn.setLinkage(cir::GlobalLinkageKind::InternalLinkage);

        std::string typeEncoding =
            astContext.getObjCEncodingForMethodDecl(method);
        methodAttrs.push_back(builder.getAnonConstRecord(
            builder.getArrayAttr(
                {createCStringAttr(".objc_sel_name",
                                   method->getSelector().getAsString()),
                 createCStringAttr(".objc_method_type", typeEncoding),
                 cir::GlobalViewAttr::get(
                     voidPtrTy, mlir::FlatSymbolRefAttr::get(fn.getSymNameAttr()))}),
            /*packed=*/false, /*padded=*/false, methodTy));
      }

      auto methodArrayTy = cir::ArrayType::get(methodTy, methodAttrs.size());
      auto methodArrayInit = cir::ConstArrayAttr::get(
          methodArrayTy, builder.getArrayAttr(methodAttrs));
      auto methodListTy =
          builder.getAnonRecordTy({voidPtrTy, i32Ty, methodArrayTy},
                                  /*packed=*/false, /*padded=*/false);
      auto methodListInit = builder.getAnonConstRecord(
          builder.getArrayAttr(
              {nullPtr,
               cir::IntAttr::get(i32Ty, methodAttrs.size()),
               methodArrayInit}),
          /*packed=*/false, /*padded=*/false, methodListTy);
      cir::GlobalOp methodListGV = createGlobalOp(
          *this, loc, getUniqueGlobalName(".objc_method_list"), methodListTy,
          /*isConstant=*/false);
      methodListGV.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
      mlir::SymbolTable::setSymbolVisibility(
          methodListGV, mlir::SymbolTable::Visibility::Private);
      methodListGV.setAlignmentAttr(getSize(getPointerAlign()));
      setInitializer(methodListGV, methodListInit);
      setDSOLocal(static_cast<mlir::Operation *>(methodListGV));
      return builder.getGlobalViewAttr(voidPtrTy, methodListGV);
    };

    auto emitPropertyList = [&](bool isClassProperty) -> mlir::Attribute {
      llvm::SmallVector<const ObjCPropertyDecl *, 8> properties;
      llvm::SmallPtrSet<const IdentifierInfo *, 8> seenProperties;

      std::function<void(const ObjCProtocolDecl *)> collectProtocolProperties;
      collectProtocolProperties = [&](const ObjCProtocolDecl *protocol) {
        for (const auto *inherited : protocol->protocols())
          collectProtocolProperties(inherited);
        for (const auto *property : protocol->properties()) {
          if (property->isClassProperty() != isClassProperty)
            continue;
          if (!astContext.getObjCPropertyImplDeclForPropertyDecl(property, oid))
            continue;
          if (!seenProperties.insert(property->getIdentifier()).second)
            continue;
          properties.push_back(property);
        }
      };

      for (const ObjCCategoryDecl *classExt : classDecl->known_extensions())
        for (const auto *property : classExt->properties()) {
          if (property->isClassProperty() != isClassProperty)
            continue;
          if (!seenProperties.insert(property->getIdentifier()).second)
            continue;
          properties.push_back(property);
        }

      for (const auto *property : classDecl->properties()) {
        if (property->isClassProperty() != isClassProperty)
          continue;
        if (!seenProperties.insert(property->getIdentifier()).second)
          continue;
        properties.push_back(property);
      }

      for (const auto *protocol : classDecl->all_referenced_protocols())
        collectProtocolProperties(protocol);

      if (properties.empty())
        return nullPtr;

      auto propertyTy =
          builder.getAnonRecordTy({voidPtrTy, i8Ty, i8Ty, i8Ty, i8Ty, voidPtrTy,
                                   voidPtrTy, voidPtrTy, voidPtrTy},
                                  /*packed=*/false, /*padded=*/false);
      llvm::SmallVector<mlir::Attribute, 8> propertyAttrs;
      propertyAttrs.reserve(properties.size());
      for (const auto *property : properties) {
        bool isSynthesized = false;
        bool isDynamic = false;
        if (const auto *propertyImpl =
                astContext.getObjCPropertyImplDeclForPropertyDecl(property, oid)) {
          isSynthesized = propertyImpl->getPropertyImplementation() ==
                          ObjCPropertyImplDecl::Synthesize;
          isDynamic = propertyImpl->getPropertyImplementation() ==
                      ObjCPropertyImplDecl::Dynamic;
        }

        unsigned attrs = property->getPropertyAttributes();
        if (attrs & ObjCPropertyAttribute::kind_readonly) {
          attrs &= ~ObjCPropertyAttribute::kind_copy;
          attrs &= ~ObjCPropertyAttribute::kind_retain;
          attrs &= ~ObjCPropertyAttribute::kind_weak;
          attrs &= ~ObjCPropertyAttribute::kind_strong;
        }
        unsigned attrByte0 = attrs & 0xff;
        attrs >>= 8;
        attrs <<= 2;
        attrs |= isSynthesized ? 1U : 0U;
        attrs |= isDynamic ? 2U : 0U;

        auto getAccessorNameAndType = [&](const ObjCMethodDecl *accessor) {
          std::pair<mlir::Attribute, mlir::Attribute> result{nullPtr, nullPtr};
          if (!accessor)
            return result;
          result.first = createCStringAttr(".objc_sel_name",
                                           accessor->getSelector().getAsString());
          result.second = createCStringAttr(
              ".objc_method_type",
              astContext.getObjCEncodingForMethodDecl(accessor));
          return result;
        };
        auto getterFields =
            getAccessorNameAndType(property->getGetterMethodDecl());
        auto setterFields =
            getAccessorNameAndType(property->getSetterMethodDecl());

        propertyAttrs.push_back(builder.getAnonConstRecord(
            builder.getArrayAttr(
                {createPropertyNameAttr(property),
                 cir::IntAttr::get(i8Ty, attrByte0),
                 cir::IntAttr::get(i8Ty, attrs & 0xff),
                 cir::IntAttr::get(i8Ty, 0),
                 cir::IntAttr::get(i8Ty, 0), getterFields.first,
                 getterFields.second, setterFields.first, setterFields.second}),
            /*packed=*/false, /*padded=*/false, propertyTy));
      }

      auto propertyArrayTy = cir::ArrayType::get(propertyTy, propertyAttrs.size());
      auto propertyArrayInit = cir::ConstArrayAttr::get(
          propertyArrayTy, builder.getArrayAttr(propertyAttrs));
      auto propertyListTy =
          builder.getAnonRecordTy({i32Ty, voidPtrTy, propertyArrayTy},
                                  /*packed=*/false, /*padded=*/false);
      auto propertyListInit = builder.getAnonConstRecord(
          builder.getArrayAttr(
              {cir::IntAttr::get(i32Ty, propertyAttrs.size()), nullPtr,
               propertyArrayInit}),
          /*packed=*/false, /*padded=*/false, propertyListTy);
      cir::GlobalOp propertyListGV = createGlobalOp(
          *this, loc, getUniqueGlobalName(".objc_property_list"), propertyListTy,
          /*isConstant=*/false);
      propertyListGV.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
      mlir::SymbolTable::setSymbolVisibility(
          propertyListGV, mlir::SymbolTable::Visibility::Private);
      propertyListGV.setAlignmentAttr(getSize(getPointerAlign()));
      setInitializer(propertyListGV, propertyListInit);
      setDSOLocal(static_cast<mlir::Operation *>(propertyListGV));
      return builder.getGlobalViewAttr(voidPtrTy, propertyListGV);
    };

    mlir::Attribute ivarOffsetsAttr = nullPtr;
    mlir::Attribute strongBitmapAttr = makeLegacyGNUBitField({});
    mlir::Attribute weakBitmapAttr = makeLegacyGNUBitField({});
    auto emitIvarList = [&]() -> mlir::Attribute {
      llvm::SmallVector<const ObjCIvarDecl *, 8> ivars;
      llvm::SmallVector<mlir::Attribute, 8> ivarOffsetAttrs;
      llvm::SmallVector<bool, 8> strongIvars;
      llvm::SmallVector<bool, 8> weakIvars;
      for (const ObjCIvarDecl *ivar = classDecl->all_declared_ivar_begin(); ivar;
           ivar = ivar->getNextIvar())
        ivars.push_back(ivar);

      auto ivarOffsetArrayTy = cir::ArrayType::get(voidPtrTy, ivars.size());
      auto ivarOffsetArrayInit =
          cir::ConstArrayAttr::get(ivarOffsetArrayTy,
                                   builder.getArrayAttr(ivarOffsetAttrs));
      if (!ivars.empty()) {
        int64_t superInstanceSize = 0;
        if (const ObjCInterfaceDecl *superClassDecl = classDecl->getSuperClass())
          superInstanceSize = astContext.getASTObjCInterfaceLayout(superClassDecl)
                                  .getSize()
                                  .getQuantity();

        auto ivarTy = builder.getAnonRecordTy({voidPtrTy, voidPtrTy, i32Ty},
                                              /*packed=*/false,
                                              /*padded=*/false);
        llvm::SmallVector<mlir::Attribute, 8> ivarAttrs;
        ivarAttrs.reserve(ivars.size());
        ivarOffsetAttrs.reserve(ivars.size());

        for (const ObjCIvarDecl *ivar : ivars) {
          std::string ivarTypeEncoding;
          astContext.getObjCEncodingForType(ivar->getType(), ivarTypeEncoding,
                                            ivar);
          int64_t offset =
              astContext.lookupFieldBitOffset(classDecl, ivar) /
              astContext.getCharWidth();
          if (getLangOpts().ObjCRuntime.isNonFragile())
            offset -= superInstanceSize;

          std::string offsetValueName =
              "__objc_ivar_offset_value_" + className + "." +
              ivar->getNameAsString();
          cir::GlobalOp offsetValueGV =
              mlir::dyn_cast_or_null<cir::GlobalOp>(getGlobalValue(offsetValueName));
          if (!offsetValueGV)
            offsetValueGV = createGlobalOp(*this, loc, offsetValueName, i32Ty,
                                           /*isConstant=*/false);
          offsetValueGV.setLinkage(cir::GlobalLinkageKind::ExternalLinkage);
          mlir::SymbolTable::setSymbolVisibility(
              offsetValueGV, mlir::SymbolTable::Visibility::Public);
          offsetValueGV.setAlignmentAttr(
              getSize(astContext.getTypeSizeInChars(astContext.IntTy)));
          setInitializer(offsetValueGV, cir::IntAttr::get(i32Ty, offset));
          ivarOffsetAttrs.push_back(
              builder.getGlobalViewAttr(voidPtrTy, offsetValueGV));

          std::string offsetName =
              "__objc_ivar_offset_" + className + "." + ivar->getNameAsString();
          cir::GlobalOp offsetGV =
              mlir::dyn_cast_or_null<cir::GlobalOp>(getGlobalValue(offsetName));
          if (!offsetGV)
            offsetGV = createGlobalOp(
                *this, loc, offsetName, builder.getPointerTo(i32Ty),
                /*isConstant=*/false);
          offsetGV.setLinkage(cir::GlobalLinkageKind::ExternalLinkage);
          mlir::SymbolTable::setSymbolVisibility(
              offsetGV, mlir::SymbolTable::Visibility::Public);
          offsetGV.setAlignmentAttr(getSize(getPointerAlign()));
          setInitializer(offsetGV, builder.getGlobalViewAttr(
                                       builder.getPointerTo(i32Ty), offsetValueGV));

          Qualifiers::ObjCLifetime lifetime =
              ivar->getType().getQualifiers().getObjCLifetime();
          strongIvars.push_back(lifetime == Qualifiers::OCL_Strong);
          weakIvars.push_back(lifetime == Qualifiers::OCL_Weak);

          ivarAttrs.push_back(builder.getAnonConstRecord(
              builder.getArrayAttr(
                  {createCStringAttr(".objc_ivar_name", ivar->getName()),
                   createCStringAttr(".objc_ivar_type", ivarTypeEncoding),
                   cir::IntAttr::get(i32Ty, offset)}),
              /*packed=*/false, /*padded=*/false, ivarTy));
        }

        ivarOffsetArrayTy = cir::ArrayType::get(voidPtrTy, ivarOffsetAttrs.size());
        ivarOffsetArrayInit =
            cir::ConstArrayAttr::get(ivarOffsetArrayTy,
                                     builder.getArrayAttr(ivarOffsetAttrs));

        auto ivarArrayTy = cir::ArrayType::get(ivarTy, ivarAttrs.size());
        auto ivarArrayInit =
            cir::ConstArrayAttr::get(ivarArrayTy, builder.getArrayAttr(ivarAttrs));
        auto ivarListTy = builder.getAnonRecordTy({i32Ty, ivarArrayTy},
                                                  /*packed=*/false,
                                                  /*padded=*/false);
        auto ivarListInit = builder.getAnonConstRecord(
            builder.getArrayAttr(
                {cir::IntAttr::get(i32Ty, ivarAttrs.size()), ivarArrayInit}),
            /*packed=*/false, /*padded=*/false, ivarListTy);
        cir::GlobalOp ivarListGV = createGlobalOp(
            *this, loc, getUniqueGlobalName(".objc_ivar_list"), ivarListTy,
            /*isConstant=*/false);
        ivarListGV.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
        mlir::SymbolTable::setSymbolVisibility(
            ivarListGV, mlir::SymbolTable::Visibility::Private);
        ivarListGV.setAlignmentAttr(getSize(getPointerAlign()));
        setInitializer(ivarListGV, ivarListInit);
        setDSOLocal(static_cast<mlir::Operation *>(ivarListGV));

        auto getIvarOffsetPtrAttr = [&](unsigned index) {
          auto indices = builder.getArrayAttr(
              {builder.getI32IntegerAttr(1), builder.getI32IntegerAttr(index),
               builder.getI32IntegerAttr(2)});
          return builder.getGlobalViewAttr(builder.getPointerTo(i32Ty), ivarListGV,
                                           indices);
        };

        cir::GlobalOp ivarOffsetsGV = createGlobalOp(
            *this, loc, getUniqueGlobalName(".ivar.offsets"), ivarOffsetArrayTy,
            /*isConstant=*/false);
        ivarOffsetsGV.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
        mlir::SymbolTable::setSymbolVisibility(
            ivarOffsetsGV, mlir::SymbolTable::Visibility::Private);
        ivarOffsetsGV.setAlignmentAttr(getSize(getPointerAlign()));
        setInitializer(ivarOffsetsGV, ivarOffsetArrayInit);
        setDSOLocal(static_cast<mlir::Operation *>(ivarOffsetsGV));
        ivarOffsetsAttr = builder.getGlobalViewAttr(voidPtrTy, ivarOffsetsGV);

        // Legacy GNU accessors load through __objc_ivar_offset_*, which OGCG
        // points at the ivar-list offset slot rather than the offset-value
        // global stored in .ivar.offsets.
        for (auto [index, ivar] : llvm::enumerate(ivars)) {
          std::string offsetName =
              "__objc_ivar_offset_" + className + "." + ivar->getNameAsString();
          cir::GlobalOp offsetGV =
              mlir::cast<cir::GlobalOp>(getGlobalValue(offsetName));
          setInitializer(offsetGV, getIvarOffsetPtrAttr(index));
        }

        strongBitmapAttr = makeLegacyGNUBitField(strongIvars);
        weakBitmapAttr = makeLegacyGNUBitField(weakIvars);
        return builder.getGlobalViewAttr(voidPtrTy, ivarListGV);
      }

      cir::GlobalOp ivarOffsetsGV = createGlobalOp(
          *this, loc, getUniqueGlobalName(".ivar.offsets"), ivarOffsetArrayTy,
          /*isConstant=*/false);
      ivarOffsetsGV.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
      mlir::SymbolTable::setSymbolVisibility(
          ivarOffsetsGV, mlir::SymbolTable::Visibility::Private);
      ivarOffsetsGV.setAlignmentAttr(getSize(getPointerAlign()));
      setInitializer(ivarOffsetsGV, ivarOffsetArrayInit);
      setDSOLocal(static_cast<mlir::Operation *>(ivarOffsetsGV));
      ivarOffsetsAttr = builder.getGlobalViewAttr(voidPtrTy, ivarOffsetsGV);
      return nullPtr;
    };

    llvm::SmallVector<const ObjCMethodDecl *, 8> instanceMethods;
    llvm::SmallPtrSet<const ObjCMethodDecl *, 8> seenInstanceMethods;
    for (const auto *method : oid->instance_methods())
      if (seenInstanceMethods.insert(method).second)
        instanceMethods.push_back(method);

    llvm::SmallVector<const ObjCMethodDecl *, 8> classMethods;
    llvm::SmallPtrSet<const ObjCMethodDecl *, 8> seenClassMethods;
    for (const auto *method : oid->class_methods())
      if (seenClassMethods.insert(method).second)
        classMethods.push_back(method);

    for (const auto *pid : oid->property_impls()) {
      if (pid->getPropertyImplementation() != ObjCPropertyImplDecl::Synthesize)
        continue;
      auto addAccessor = [&](const ObjCMethodDecl *method) {
        if (!method)
          return;
        if (method->isClassMethod()) {
          if (seenClassMethods.insert(method).second)
            classMethods.push_back(method);
        } else if (seenInstanceMethods.insert(method).second) {
          instanceMethods.push_back(method);
        }
      };
      addAccessor(pid->getGetterMethodDecl());
      if (!pid->getPropertyDecl()->isReadOnly())
        addAccessor(pid->getSetterMethodDecl());
    }

    auto methodListAttr = emitMethodList(instanceMethods);
    auto classMethodListAttr = emitMethodList(classMethods);
    auto propertyListAttr = emitPropertyList(/*isClassProperty=*/false);
    auto classPropertyListAttr = emitPropertyList(/*isClassProperty=*/true);
    mlir::Attribute ivarListAttr = emitIvarList();
    auto emitProtocolList = [&]() -> mlir::Attribute {
      llvm::SmallVector<mlir::Attribute, 4> protocolAttrs;
      for (const auto *protocol :
           getRuntimeProtocolList(classDecl->protocol_begin(),
                                  classDecl->protocol_end())) {
        protocolAttrs.push_back(
            builder.getGlobalViewAttr(voidPtrTy,
                                      getObjCProtocolGlobal(loc, protocol)));
      }

      auto protocolArrayTy =
          cir::ArrayType::get(voidPtrTy, protocolAttrs.size());
      auto protocolListTy = builder.getAnonRecordTy(
          {voidPtrTy, longTy, protocolArrayTy}, /*packed=*/false,
          /*padded=*/false);
      cir::GlobalOp protocolListGV = createGlobalOp(
          *this, loc, getUniqueGlobalName(".objc_protocol_list"), protocolListTy,
          /*isConstant=*/false);
      protocolListGV.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
      mlir::SymbolTable::setSymbolVisibility(
          protocolListGV, mlir::SymbolTable::Visibility::Private);
      protocolListGV.setAlignmentAttr(getSize(getPointerAlign()));
      setDSOLocal(static_cast<mlir::Operation *>(protocolListGV));

      if (protocolAttrs.empty()) {
        setInitializer(protocolListGV, builder.getZeroInitAttr(protocolListTy));
        return builder.getGlobalViewAttr(voidPtrTy, protocolListGV);
      }

      auto protocolArrayInit = cir::ConstArrayAttr::get(
          protocolArrayTy, builder.getArrayAttr(protocolAttrs));
      auto protocolListInit = builder.getAnonConstRecord(
          builder.getArrayAttr(
              {nullPtr, cir::IntAttr::get(longTy, protocolAttrs.size()),
               protocolArrayInit}),
          /*packed=*/false, /*padded=*/false, protocolListTy);
      setInitializer(protocolListGV, protocolListInit);
      return builder.getGlobalViewAttr(voidPtrTy, protocolListGV);
    };
    mlir::Attribute protocolListAttr = emitProtocolList();

    std::string classNameSymbol = "__objc_class_name_" + className;
    cir::GlobalOp classNameSymbolGV =
        mlir::dyn_cast_or_null<cir::GlobalOp>(getGlobalValue(classNameSymbol));
    if (!classNameSymbolGV)
      classNameSymbolGV = createGlobalOp(*this, loc, classNameSymbol, longTy,
                                         /*isConstant=*/false);
    classNameSymbolGV.setLinkage(cir::GlobalLinkageKind::ExternalLinkage);
    mlir::SymbolTable::setSymbolVisibility(
        classNameSymbolGV, mlir::SymbolTable::Visibility::Public);
    classNameSymbolGV.setAlignmentAttr(
        getSize(astContext.getTypeSizeInChars(astContext.LongTy)));
    setInitializer(classNameSymbolGV, cir::IntAttr::get(longTy, 0));

    mlir::Attribute classNameAttr = createCStringAttr(".class_name", className);
    mlir::Attribute superClassAttr = nullPtr;
    int64_t superInstanceSize = 0;
    if (const ObjCInterfaceDecl *superClassDecl = classDecl->getSuperClass()) {
      superInstanceSize = astContext.getASTObjCInterfaceLayout(superClassDecl)
                              .getSize()
                              .getQuantity();
      superClassAttr =
          createCStringAttr(".super_class_name", superClassDecl->getName());
    }

    int64_t instanceSize =
        astContext.getASTObjCInterfaceLayout(classDecl).getSize().getQuantity();
    if (getLangOpts().ObjCRuntime.isNonFragile())
      instanceSize = 0 - (instanceSize - superInstanceSize);

    auto classTy = builder.getAnonRecordTy(
        {voidPtrTy, voidPtrTy, voidPtrTy, longTy, longTy, longTy, voidPtrTy,
         voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy,
         longTy, voidPtrTy, voidPtrTy, intptrTy, intptrTy},
        /*packed=*/false, /*padded=*/false);
    int64_t metaInstanceSize =
        getDataLayout().getTypeAllocSize(classTy).getFixedValue();

    mlir::Attribute emptyBitmap = cir::IntAttr::get(intptrTy, 0);
    auto metaclassInit = builder.getAnonConstRecord(
        builder.getArrayAttr(
            {nullPtr, nullPtr, classNameAttr, cir::IntAttr::get(longTy, 0),
             cir::IntAttr::get(longTy, 0x12),
             cir::IntAttr::get(longTy, metaInstanceSize), nullPtr,
             classMethodListAttr, nullPtr, nullPtr, nullPtr, nullPtr, nullPtr,
             cir::IntAttr::get(longTy, 1), nullPtr, classPropertyListAttr,
             emptyBitmap, emptyBitmap}),
        /*packed=*/false, /*padded=*/false, classTy);
    std::string metaclassName = (llvm::Twine("_OBJC_METACLASS_") + className).str();
    cir::GlobalOp metaclassGV = createGlobalOp(*this, loc, metaclassName, classTy,
                                               /*isConstant=*/false);
    metaclassGV.setLinkage(cir::GlobalLinkageKind::ExternalLinkage);
    mlir::SymbolTable::setSymbolVisibility(metaclassGV,
                                           mlir::SymbolTable::Visibility::Public);
    metaclassGV.setAlignmentAttr(getSize(getPointerAlign()));
    setInitializer(metaclassGV, metaclassInit);
    setDSOLocal(static_cast<mlir::Operation *>(metaclassGV));
    setObjCClassSymbolDLLStorageClass(*this, metaclassGV, classDecl);

    auto classInit = builder.getAnonConstRecord(
        builder.getArrayAttr(
            {builder.getGlobalViewAttr(voidPtrTy, metaclassGV), superClassAttr,
             classNameAttr, cir::IntAttr::get(longTy, 0),
             cir::IntAttr::get(longTy, 0x11),
             cir::IntAttr::get(longTy, instanceSize), ivarListAttr,
             methodListAttr, nullPtr, nullPtr, nullPtr,
             protocolListAttr, nullPtr,
             cir::IntAttr::get(longTy, 1), ivarOffsetsAttr, propertyListAttr,
             strongBitmapAttr, weakBitmapAttr}),
        /*packed=*/false, /*padded=*/false, classTy);
    cir::GlobalOp classGV =
        createGlobalOp(*this, loc, classSymbolName, classTy, /*isConstant=*/false);
    classGV.setLinkage(cir::GlobalLinkageKind::ExternalLinkage);
    mlir::SymbolTable::setSymbolVisibility(classGV,
                                           mlir::SymbolTable::Visibility::Public);
    classGV.setAlignmentAttr(getSize(getPointerAlign()));
    setInitializer(classGV, classInit);
    setDSOLocal(static_cast<mlir::Operation *>(classGV));
    setObjCClassSymbolDLLStorageClass(*this, classGV, classDecl);
    legacyGNUClassGlobals.push_back(classGV);
    return classGV;
  }

  std::string classSymbolName =
      mangleGNUObjCPublicSymbol(*this, "OBJC_CLASS_", className);
  if (auto *existing = getGlobalValue(classSymbolName))
    return mlir::cast<cir::GlobalOp>(existing);

  auto i32Ty = builder.getUInt32Ty();
  auto longTy = mlir::cast<cir::IntType>(convertType(astContext.LongTy));
  auto sizeTy = mlir::cast<cir::IntType>(convertType(astContext.getSizeType()));
  mlir::Attribute nullPtr = builder.getConstPtrAttr(voidPtrTy, 0);
  const bool isCOFF = getTriple().isOSBinFormatCOFF();

  auto createCStringAttr = [&](llvm::StringRef prefix,
                               llvm::StringRef value) -> mlir::Attribute {
    return builder.getGlobalViewAttr(
        voidPtrTy, getGlobalForConstantCString(loc, value, prefix));
  };

  auto emitMethodList =
      [&](llvm::ArrayRef<const ObjCMethodDecl *> methods) -> mlir::Attribute {
    if (methods.empty())
      return nullPtr;

    auto methodTy =
        builder.getAnonRecordTy({voidPtrTy, voidPtrTy, voidPtrTy},
                                /*packed=*/false, /*padded=*/false);
    llvm::SmallVector<mlir::Attribute, 8> methodAttrs;
    methodAttrs.reserve(methods.size());
    for (const auto *method : methods) {
      GlobalDecl gd(method);
      const CIRGenFunctionInfo &fi =
          getTypes().arrangeObjCMethodDeclaration(method);
      cir::FuncType fnTy = getTypes().getFunctionType(fi);
      SmallString<256> nameBuf;
      llvm::raw_svector_ostream os(nameBuf);
      mangleObjCMethodName(method, os);
      cir::FuncOp fn = getOrCreateCIRFunction(
          nameBuf.str(), fnTy, gd, /*forVTable=*/false,
          /*dontDefer=*/true, /*isThunk=*/false, NotForDefinition);
      if (!method->isDirectMethod())
        fn.setLinkage(cir::GlobalLinkageKind::InternalLinkage);

      cir::GlobalOp selectorGV =
          getObjCGNUSelectorGlobal(method->getSelector(), method);
      std::string extendedTypeEncoding =
          astContext.getObjCEncodingForMethodDecl(method, /*Extended=*/true);
      methodAttrs.push_back(builder.getAnonConstRecord(
          builder.getArrayAttr(
              {cir::GlobalViewAttr::get(
                   voidPtrTy, mlir::FlatSymbolRefAttr::get(fn.getSymNameAttr())),
               builder.getGlobalViewAttr(voidPtrTy, selectorGV),
               createCStringAttr(".objc_method_type", extendedTypeEncoding)}),
          /*packed=*/false, /*padded=*/false, methodTy));
    }

    auto methodArrayTy = cir::ArrayType::get(methodTy, methodAttrs.size());
    auto methodArrayInit = cir::ConstArrayAttr::get(
        methodArrayTy, builder.getArrayAttr(methodAttrs));
    auto methodListTy = builder.getAnonRecordTy(
        {voidPtrTy, i32Ty, sizeTy, methodArrayTy}, /*packed=*/false,
        /*padded=*/false);
    auto methodListInit = builder.getAnonConstRecord(
        builder.getArrayAttr(
            {nullPtr, cir::IntAttr::get(i32Ty, methodAttrs.size()),
             cir::IntAttr::get(
                 sizeTy,
                 getDataLayout().getTypeAllocSize(methodTy).getFixedValue()),
             methodArrayInit}),
        /*packed=*/false, /*padded=*/false, methodListTy);
    cir::GlobalOp methodListGV = createGlobalOp(
        *this, loc, getUniqueGlobalName(".objc_method_list"), methodListTy,
        /*isConstant=*/false);
    methodListGV.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
    mlir::SymbolTable::setSymbolVisibility(
        methodListGV, mlir::SymbolTable::Visibility::Private);
    methodListGV.setAlignmentAttr(getSize(getPointerAlign()));
    setInitializer(methodListGV, methodListInit);
    setDSOLocal(static_cast<mlir::Operation *>(methodListGV));
    return builder.getGlobalViewAttr(voidPtrTy, methodListGV);
  };

  auto emitPropertyList = [&](bool isClassProperty) -> mlir::Attribute {
    llvm::SmallVector<const ObjCPropertyDecl *, 8> properties;
    llvm::SmallPtrSet<const IdentifierInfo *, 8> seenProperties;

    std::function<void(const ObjCProtocolDecl *)> collectProtocolProperties;
    collectProtocolProperties = [&](const ObjCProtocolDecl *protocol) {
      for (const auto *inherited : protocol->protocols())
        collectProtocolProperties(inherited);
      for (const auto *property : protocol->properties()) {
        if (property->isClassProperty() != isClassProperty)
          continue;
        if (!astContext.getObjCPropertyImplDeclForPropertyDecl(property, oid))
          continue;
        if (!seenProperties.insert(property->getIdentifier()).second)
          continue;
        properties.push_back(property);
      }
    };

    for (const ObjCCategoryDecl *classExt : classDecl->known_extensions())
      for (const auto *property : classExt->properties()) {
        if (property->isClassProperty() != isClassProperty)
          continue;
        if (!seenProperties.insert(property->getIdentifier()).second)
          continue;
        properties.push_back(property);
      }

    for (const auto *property : classDecl->properties()) {
      if (property->isClassProperty() != isClassProperty)
        continue;
      if (!seenProperties.insert(property->getIdentifier()).second)
        continue;
      properties.push_back(property);
    }

    for (const auto *protocol : classDecl->all_referenced_protocols())
      collectProtocolProperties(protocol);

    if (properties.empty())
      return nullPtr;

    auto propertyTy =
        builder.getAnonRecordTy({voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy,
                                 voidPtrTy},
                                /*packed=*/false, /*padded=*/false);
    llvm::SmallVector<mlir::Attribute, 8> propertyAttrs;
    propertyAttrs.reserve(properties.size());
    for (const auto *property : properties) {
      std::string propertyAttrsStr =
          astContext.getObjCEncodingForPropertyDecl(property, oid);
      std::string propertyTypeStr;
      astContext.getObjCEncodingForType(property->getType(), propertyTypeStr);

      auto getAccessorSelectorAttr = [&](const ObjCMethodDecl *accessor) {
        if (!accessor)
          return nullPtr;
        return mlir::Attribute(builder.getGlobalViewAttr(
            voidPtrTy,
            getObjCGNUSelectorGlobal(accessor->getSelector(), accessor)));
      };

      propertyAttrs.push_back(builder.getAnonConstRecord(
          builder.getArrayAttr(
              {createCStringAttr(".objc_property_name", property->getName()),
               createCStringAttr(".objc_property_attrs", propertyAttrsStr),
               createCStringAttr(".objc_property_type", propertyTypeStr),
               getAccessorSelectorAttr(property->getGetterMethodDecl()),
               getAccessorSelectorAttr(property->getSetterMethodDecl())}),
          /*packed=*/false, /*padded=*/false, propertyTy));
    }

    auto propertyArrayTy = cir::ArrayType::get(propertyTy, propertyAttrs.size());
    auto propertyArrayInit = cir::ConstArrayAttr::get(
        propertyArrayTy, builder.getArrayAttr(propertyAttrs));
    auto propertyListTy = builder.getAnonRecordTy(
        {i32Ty, i32Ty, voidPtrTy, propertyArrayTy}, /*packed=*/false,
        /*padded=*/false);
    auto propertyListInit = builder.getAnonConstRecord(
        builder.getArrayAttr(
            {cir::IntAttr::get(i32Ty, propertyAttrs.size()),
             cir::IntAttr::get(
                 i32Ty,
                 getDataLayout().getTypeAllocSize(propertyTy).getFixedValue()),
             nullPtr, propertyArrayInit}),
        /*packed=*/false, /*padded=*/false, propertyListTy);
    cir::GlobalOp propertyListGV = createGlobalOp(
        *this, loc, getUniqueGlobalName(".objc_property_list"), propertyListTy,
        /*isConstant=*/false);
    propertyListGV.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
    mlir::SymbolTable::setSymbolVisibility(
        propertyListGV, mlir::SymbolTable::Visibility::Private);
    propertyListGV.setAlignmentAttr(getSize(getPointerAlign()));
    setInitializer(propertyListGV, propertyListInit);
    setDSOLocal(static_cast<mlir::Operation *>(propertyListGV));
    return builder.getGlobalViewAttr(voidPtrTy, propertyListGV);
  };

  auto emitProtocolList = [&]() -> mlir::Attribute {
    llvm::SmallVector<mlir::Attribute, 4> protocols;
    for (const auto *protocol :
         getRuntimeProtocolList(classDecl->all_referenced_protocol_begin(),
                                classDecl->all_referenced_protocol_end())) {
      protocols.push_back(
          builder.getGlobalViewAttr(voidPtrTy, getObjCProtocolGlobal(loc, protocol)));
    }
    if (protocols.empty())
      return nullPtr;

    auto protocolArrayTy = cir::ArrayType::get(voidPtrTy, protocols.size());
    auto protocolArrayInit = cir::ConstArrayAttr::get(
        protocolArrayTy, builder.getArrayAttr(protocols));
    auto protocolListTy = builder.getAnonRecordTy(
        {voidPtrTy, sizeTy, protocolArrayTy}, /*packed=*/false,
        /*padded=*/false);
    auto protocolListInit = builder.getAnonConstRecord(
        builder.getArrayAttr(
            {nullPtr, cir::IntAttr::get(sizeTy, protocols.size()),
             protocolArrayInit}),
        /*packed=*/false, /*padded=*/false, protocolListTy);
    cir::GlobalOp protocolListGV = createGlobalOp(
        *this, loc, getUniqueGlobalName(".objc_protocol_list"), protocolListTy,
        /*isConstant=*/false);
    protocolListGV.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
    mlir::SymbolTable::setSymbolVisibility(
        protocolListGV, mlir::SymbolTable::Visibility::Private);
    protocolListGV.setAlignmentAttr(getSize(getPointerAlign()));
    setInitializer(protocolListGV, protocolListInit);
    setDSOLocal(static_cast<mlir::Operation *>(protocolListGV));
    return builder.getGlobalViewAttr(voidPtrTy, protocolListGV);
  };

  auto emitIvarList = [&]() -> mlir::Attribute {
    llvm::SmallVector<const ObjCIvarDecl *, 8> ivars;
    for (const ObjCIvarDecl *ivar = classDecl->all_declared_ivar_begin(); ivar;
         ivar = ivar->getNextIvar())
      ivars.push_back(ivar);
    if (ivars.empty())
      return nullPtr;

    auto ivarTy = builder.getAnonRecordTy(
        {voidPtrTy, voidPtrTy, voidPtrTy, i32Ty, i32Ty}, /*packed=*/false,
        /*padded=*/false);
    llvm::SmallVector<mlir::Attribute, 8> ivarAttrs;
    ivarAttrs.reserve(ivars.size());

    int64_t superInstanceSize = 0;
    if (const ObjCInterfaceDecl *superClassDecl = classDecl->getSuperClass())
      superInstanceSize = astContext.getASTObjCInterfaceLayout(superClassDecl)
                              .getSize()
                              .getQuantity();

    auto flagsForOwnership = [](Qualifiers::ObjCLifetime ownership) {
      switch (ownership) {
      case Qualifiers::OCL_Strong:
        return 1;
      case Qualifiers::OCL_Weak:
        return 2;
      case Qualifiers::OCL_ExplicitNone:
        return 3;
      case Qualifiers::OCL_None:
      case Qualifiers::OCL_Autoreleasing:
        assert(ownership != Qualifiers::OCL_Autoreleasing);
        return 0;
      }
      llvm_unreachable("invalid Objective-C lifetime");
    };

    for (const ObjCIvarDecl *ivar : ivars) {
      std::string ivarTypeEncoding;
      astContext.getObjCEncodingForType(ivar->getType(), ivarTypeEncoding,
                                        ivar);
      int64_t baseOffset =
          astContext.lookupFieldBitOffset(classDecl, ivar) /
          astContext.getCharWidth();
      int64_t offset = baseOffset - superInstanceSize;
      std::string offsetName = "__objc_ivar_offset_" + className + "." +
                               ivar->getNameAsString() + "." +
                               mangleGNUObjCTypeEncodingForSymbol(*this,
                                                                  ivar->getType());
      cir::GlobalOp offsetGV =
          mlir::dyn_cast_or_null<cir::GlobalOp>(getGlobalValue(offsetName));
      if (!offsetGV)
        offsetGV = createGlobalOp(*this, loc, offsetName, i32Ty,
                                  /*isConstant=*/false);
      offsetGV.setLinkage(cir::GlobalLinkageKind::ExternalLinkage);
      const bool isPrivateOrPackage =
          ivar->getAccessControl() == ObjCIvarDecl::Private ||
          ivar->getAccessControl() == ObjCIvarDecl::Package;
      const bool isHidden =
          isPrivateOrPackage || classDecl->getVisibility() == HiddenVisibility;
      if (isHidden) {
        offsetGV.setGlobalVisibility(cir::VisibilityKind::Hidden);
        mlir::SymbolTable::setSymbolVisibility(offsetGV,
                                               getMLIRVisibility(offsetGV));
      } else {
        offsetGV.setGlobalVisibility(cir::VisibilityKind::Default);
        mlir::SymbolTable::setSymbolVisibility(
            offsetGV, mlir::SymbolTable::Visibility::Public);
      }
      if (isCOFF) {
        offsetGV->removeAttr("cir.dll_storage_class");
        offsetGV->removeAttr("dso_local");
        if (classDecl->hasAttr<DLLExportAttr>() && !isHidden) {
          offsetGV->setAttr("cir.dll_storage_class",
                            getStringAttrCached("dllexport"));
          setDSOLocal(static_cast<mlir::Operation *>(offsetGV));
        }
      }
      offsetGV.setAlignmentAttr(
          getSize(astContext.getTypeSizeInChars(astContext.IntTy)));
      setInitializer(offsetGV, cir::IntAttr::get(i32Ty, offset));

      unsigned align =
          llvm::Log2_32(astContext.getTypeAlignInChars(ivar->getType())
                            .getQuantity());
      unsigned flags =
          (align << 3) | (1U << 2) |
          flagsForOwnership(ivar->getType().getQualifiers().getObjCLifetime());

      ivarAttrs.push_back(builder.getAnonConstRecord(
          builder.getArrayAttr(
              {createCStringAttr(".objc_ivar_name", ivar->getName()),
               createCStringAttr(".objc_ivar_type", ivarTypeEncoding),
               builder.getGlobalViewAttr(voidPtrTy, offsetGV),
               cir::IntAttr::get(
                   i32Ty,
                   astContext.getTypeSizeInChars(ivar->getType()).getQuantity()),
               cir::IntAttr::get(i32Ty, flags)}),
          /*packed=*/false, /*padded=*/false, ivarTy));
    }

    auto ivarArrayTy = cir::ArrayType::get(ivarTy, ivarAttrs.size());
    auto ivarArrayInit =
        cir::ConstArrayAttr::get(ivarArrayTy, builder.getArrayAttr(ivarAttrs));
    auto ivarListTy = builder.getAnonRecordTy(
        {i32Ty, sizeTy, ivarArrayTy}, /*packed=*/false, /*padded=*/false);
    auto ivarListInit = builder.getAnonConstRecord(
        builder.getArrayAttr(
            {cir::IntAttr::get(i32Ty, ivarAttrs.size()),
             cir::IntAttr::get(
                 sizeTy, getDataLayout().getTypeAllocSize(ivarTy).getFixedValue()),
             ivarArrayInit}),
        /*packed=*/false, /*padded=*/false, ivarListTy);
    cir::GlobalOp ivarListGV = createGlobalOp(
        *this, loc, getUniqueGlobalName(".objc_ivar_list"), ivarListTy,
        /*isConstant=*/false);
    ivarListGV.setLinkage(cir::GlobalLinkageKind::PrivateLinkage);
    mlir::SymbolTable::setSymbolVisibility(ivarListGV,
                                           mlir::SymbolTable::Visibility::Private);
    ivarListGV.setAlignmentAttr(getSize(getPointerAlign()));
    setInitializer(ivarListGV, ivarListInit);
    setDSOLocal(static_cast<mlir::Operation *>(ivarListGV));
    return builder.getGlobalViewAttr(voidPtrTy, ivarListGV);
  };

  llvm::SmallVector<const ObjCMethodDecl *, 8> instanceMethods;
  llvm::SmallPtrSet<const ObjCMethodDecl *, 8> seenInstanceMethods;
  for (const auto *method : oid->instance_methods())
    if (seenInstanceMethods.insert(method).second)
      instanceMethods.push_back(method);

  llvm::SmallVector<const ObjCMethodDecl *, 8> classMethods;
  llvm::SmallPtrSet<const ObjCMethodDecl *, 8> seenClassMethods;
  for (const auto *method : oid->class_methods())
    if (seenClassMethods.insert(method).second)
      classMethods.push_back(method);

  for (const auto *pid : oid->property_impls()) {
    if (pid->getPropertyImplementation() != ObjCPropertyImplDecl::Synthesize)
      continue;
    auto addAccessor = [&](const ObjCMethodDecl *method) {
      if (!method)
        return;
      if (method->isClassMethod()) {
        if (seenClassMethods.insert(method).second)
          classMethods.push_back(method);
      } else if (seenInstanceMethods.insert(method).second) {
        instanceMethods.push_back(method);
      }
    };
    addAccessor(pid->getGetterMethodDecl());
    if (!pid->getPropertyDecl()->isReadOnly())
      addAccessor(pid->getSetterMethodDecl());
  }

  auto methodListAttr = emitMethodList(instanceMethods);
  auto classMethodListAttr = emitMethodList(classMethods);
  auto ivarListAttr = emitIvarList();
  auto protocolListAttr = emitProtocolList();
  auto propertyListAttr = emitPropertyList(/*isClassProperty=*/false);
  auto classPropertyListAttr = emitPropertyList(/*isClassProperty=*/true);
  auto classNameAttr = createCStringAttr(".objc_class_name", className);

  auto metaclassTy = builder.getAnonRecordTy(
      {voidPtrTy, voidPtrTy, voidPtrTy, longTy, longTy, longTy, voidPtrTy,
       voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy,
       voidPtrTy, voidPtrTy, longTy, voidPtrTy},
      /*packed=*/false, /*padded=*/false);
  auto metaclassInit = builder.getAnonConstRecord(
      builder.getArrayAttr(
          {nullPtr, nullPtr, classNameAttr, cir::IntAttr::get(longTy, 0),
           cir::IntAttr::get(longTy, 1), cir::IntAttr::get(longTy, 0),
           nullPtr, classMethodListAttr, nullPtr, nullPtr, nullPtr, nullPtr,
           nullPtr, nullPtr, nullPtr, cir::IntAttr::get(longTy, 0),
           classPropertyListAttr}),
      /*packed=*/false, /*padded=*/false, metaclassTy);
  std::string metaclassName =
      mangleGNUObjCPublicSymbol(*this, "OBJC_METACLASS_", className);
  cir::GlobalOp metaclassGV = createGlobalOp(*this, loc, metaclassName,
                                             metaclassTy, /*isConstant=*/false);
  metaclassGV.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
  mlir::SymbolTable::setSymbolVisibility(metaclassGV,
                                         mlir::SymbolTable::Visibility::Private);
  metaclassGV.setAlignmentAttr(getSize(getPointerAlign()));
  setInitializer(metaclassGV, metaclassInit);
  setDSOLocal(static_cast<mlir::Operation *>(metaclassGV));

  cir::GlobalOp superClassGV;
  mlir::Attribute superClassAttr = nullPtr;
  const ObjCInterfaceDecl *superClassDecl = classDecl->getSuperClass();
  if (superClassDecl) {
    std::string superClassName = mangleGNUObjCPublicSymbol(
        *this, "OBJC_CLASS_", superClassDecl->getNameAsString());
    superClassGV =
        mlir::dyn_cast_or_null<cir::GlobalOp>(getGlobalValue(superClassName));
    if (!superClassGV)
      superClassGV =
          createGlobalOp(*this, loc, superClassName, voidPtrTy,
                         /*isConstant=*/false);
    superClassGV.setLinkage(cir::GlobalLinkageKind::ExternalLinkage);
    mlir::SymbolTable::setSymbolVisibility(superClassGV,
                                           getMLIRVisibility(superClassGV));
    if (isCOFF) {
      setGNUstep2ClassDLLStorageClass(*this, superClassGV, superClassDecl);
    } else {
      superClassAttr = builder.getGlobalViewAttr(voidPtrTy, superClassGV);
    }
  }

  int64_t superInstanceSize = superClassDecl
                                  ? astContext.getASTObjCInterfaceLayout(
                                        superClassDecl)
                                        .getSize()
                                        .getQuantity()
                                  : 0;
  int64_t instanceSize =
      astContext.getASTObjCInterfaceLayout(classDecl).getSize().getQuantity();
  instanceSize = 0 - (instanceSize - superInstanceSize);

  auto classTy = metaclassTy;
  auto classInit = builder.getAnonConstRecord(
      builder.getArrayAttr(
          {builder.getGlobalViewAttr(voidPtrTy, metaclassGV), superClassAttr,
           classNameAttr, cir::IntAttr::get(longTy, 0),
           cir::IntAttr::get(longTy, 0),
           cir::IntAttr::get(longTy, instanceSize), ivarListAttr,
           methodListAttr, nullPtr, nullPtr, nullPtr, nullPtr, protocolListAttr,
           nullPtr, nullPtr, cir::IntAttr::get(longTy, 0), propertyListAttr}),
      /*packed=*/false, /*padded=*/false, classTy);
  cir::GlobalOp classGV =
      createGlobalOp(*this, loc, classSymbolName, classTy, /*isConstant=*/false);
  classGV.setLinkage(cir::GlobalLinkageKind::ExternalLinkage);
  mlir::SymbolTable::setSymbolVisibility(classGV,
                                         mlir::SymbolTable::Visibility::Public);
  classGV.setAlignmentAttr(getSize(getPointerAlign()));
  setInitializer(classGV, classInit);
  setDSOLocal(static_cast<mlir::Operation *>(classGV));
  if (isCOFF) {
    setGNUstep2ClassDLLStorageClass(*this, classGV, classDecl);
    if (superClassDecl)
      gnustep2COFFEarlyInitEntries.push_back({superClassGV, classGV, 1});
  }

  std::string classRefName =
      mangleGNUObjCPublicSymbol(*this, "OBJC_REF_CLASS_", className);
  cir::GlobalOp classRefGV = createOrReplaceCXXRuntimeVariable(
      loc, classRefName, voidPtrTy, cir::GlobalLinkageKind::ExternalLinkage,
      CharUnits::Zero());
  classRefGV.setConstant(false);
  classRefGV.setLinkage(cir::GlobalLinkageKind::ExternalLinkage);
  mlir::SymbolTable::setSymbolVisibility(classRefGV,
                                         mlir::SymbolTable::Visibility::Public);
  classRefGV.setAlignmentAttr(getSize(getPointerAlign()));
  classRefGV.setSectionAttr(getStringAttrCached(
      getGNUstep2ObjCSectionName(*this, GNUstep2ClassReferenceSection)));
  setInitializer(classRefGV, builder.getGlobalViewAttr(voidPtrTy, classGV));
  if (isCOFF)
    setGNUstep2ClassDLLStorageClass(*this, classRefGV, classDecl);
  objcClassRefGlobals[classDecl] = classRefGV;

  std::string initClassName =
      mangleGNUObjCPublicSymbol(*this, "OBJC_INIT_CLASS_", className);
  cir::GlobalOp initClassGV = createOrReplaceCXXRuntimeVariable(
      loc, initClassName, voidPtrTy, cir::GlobalLinkageKind::ExternalLinkage,
      CharUnits::Zero());
  initClassGV.setConstant(false);
  initClassGV.setLinkage(cir::GlobalLinkageKind::ExternalLinkage);
  mlir::SymbolTable::setSymbolVisibility(initClassGV,
                                         mlir::SymbolTable::Visibility::Public);
  initClassGV.setAlignmentAttr(getSize(getPointerAlign()));
  initClassGV.setSectionAttr(getStringAttrCached(
      getGNUstep2ObjCSectionName(*this, GNUstep2ClassSection)));
  setInitializer(initClassGV, builder.getGlobalViewAttr(voidPtrTy, classGV));
  if (isCOFF) {
    initClassGV->removeAttr("cir.dll_storage_class");
    initClassGV->removeAttr("dso_local");
  }
  addUsedGlobal(initClassGV);

  return classGV;
}

void CIRGenModule::emitObjCLegacyGNUModuleInit(mlir::Location loc) {
  assert(usesLegacyGNUObjCRuntime(*this) &&
         "legacy GNU / ObjFW ObjC module init requires an old-style runtime");
  if (getGlobalValue(".objc_load_function"))
    return;
  if (legacyGNUClassGlobals.empty() && legacyGNUCategoryGlobals.empty() &&
      objcSelectorGlobals.empty() && legacyGNUClassAliases.empty() &&
      legacyGNUProtocols.empty() &&
      legacyGNUConstantStringGlobals.empty())
    return;

  auto longTy = mlir::cast<cir::IntType>(convertType(astContext.LongTy));
  auto intTy = mlir::cast<cir::IntType>(convertType(astContext.IntTy));
  auto i16Ty = builder.getSInt16Ty();
  mlir::Attribute nullPtr = builder.getConstPtrAttr(voidPtrTy, 0);

  auto createPrivateGlobal =
      [&](llvm::StringRef name, mlir::Type type,
          mlir::Attribute init) -> cir::GlobalOp {
    cir::GlobalOp gv =
        createGlobalOp(*this, loc, name, type, /*isConstant=*/false);
    gv.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
    mlir::SymbolTable::setSymbolVisibility(
        gv, mlir::SymbolTable::Visibility::Private);
    gv.setAlignmentAttr(getSize(getPointerAlign()));
    setInitializer(gv, init);
    setDSOLocal(static_cast<mlir::Operation *>(gv));
    return gv;
  };

  auto createPrivateCString = [&](llvm::StringRef name,
                                  llvm::StringRef value) -> cir::GlobalOp {
    std::string bytes = value.str();
    bytes.push_back('\0');
    auto init = mlir::cast<mlir::TypedAttr>(
        builder.getString(bytes, uCharTy, bytes.size()));
    cir::GlobalOp gv =
        createGlobalOp(*this, loc, name, init.getType(), /*isConstant=*/true);
    gv.setLinkage(cir::GlobalLinkageKind::PrivateLinkage);
    mlir::SymbolTable::setSymbolVisibility(
        gv, mlir::SymbolTable::Visibility::Private);
    gv.setAlignmentAttr(getSize(CharUnits::One()));
    gv->setAttr("unnamed_addr", mlir::UnitAttr::get(&getMLIRContext()));
    setInitializer(gv, init);
    setDSOLocal(static_cast<mlir::Operation *>(gv));
    return gv;
  };

  llvm::SmallVector<mlir::Attribute, 8> protocolAttrs;
  llvm::SmallVector<const ObjCProtocolDecl *, 8> orderedProtocols(
      legacyGNUProtocols.begin(), legacyGNUProtocols.end());
  llvm::sort(orderedProtocols, [](const ObjCProtocolDecl *lhs,
                                  const ObjCProtocolDecl *rhs) {
    return lhs->getObjCRuntimeNameAsString() < rhs->getObjCRuntimeNameAsString();
  });
  protocolAttrs.reserve(orderedProtocols.size());
  for (const ObjCProtocolDecl *protocol : orderedProtocols)
    protocolAttrs.push_back(
        builder.getGlobalViewAttr(voidPtrTy, objcProtocolGlobals.find(protocol)->second));

  auto protocolArrayTy = cir::ArrayType::get(voidPtrTy, protocolAttrs.size());
  auto protocolListTy = builder.getAnonRecordTy(
      {voidPtrTy, longTy, protocolArrayTy}, /*packed=*/false,
      /*padded=*/false);
  mlir::Attribute protocolListInit = builder.getZeroInitAttr(protocolListTy);
  if (!protocolAttrs.empty()) {
    auto protocolArrayInit = cir::ConstArrayAttr::get(
        protocolArrayTy, builder.getArrayAttr(protocolAttrs));
    protocolListInit = builder.getAnonConstRecord(
        builder.getArrayAttr(
            {nullPtr, cir::IntAttr::get(longTy, protocolAttrs.size()),
             protocolArrayInit}),
        /*packed=*/false, /*padded=*/false, protocolListTy);
  }
  cir::GlobalOp protocolListGV =
      createPrivateGlobal(getUniqueGlobalName(".objc_protocol_list"),
                          protocolListTy, protocolListInit);

  cir::GlobalOp holderCategoryNameGV =
      getGlobalForConstantCString(loc, "AnotherHack", ".objc_category_name");
  cir::GlobalOp holderClassNameGV = getGlobalForConstantCString(
      loc, "__ObjC_Protocol_Holder_Ugly_Hack", ".objc_category_class_name");
  auto holderCategoryTy = builder.getAnonRecordTy(
      {voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy},
      /*packed=*/false, /*padded=*/false);
  auto holderCategoryInit = builder.getAnonConstRecord(
      builder.getArrayAttr(
          {builder.getGlobalViewAttr(voidPtrTy, holderCategoryNameGV),
           builder.getGlobalViewAttr(voidPtrTy, holderClassNameGV), nullPtr,
           nullPtr, builder.getGlobalViewAttr(voidPtrTy, protocolListGV)}),
      /*packed=*/false, /*padded=*/false, holderCategoryTy);
  cir::GlobalOp holderCategoryGV = createPrivateGlobal(
      ".objc_protocol_holder_category", holderCategoryTy, holderCategoryInit);

  auto selectorElemTy =
      builder.getAnonRecordTy({voidPtrTy, voidPtrTy}, /*packed=*/false,
                              /*padded=*/false);
  struct LegacyGNUSelectorEntry {
    std::string cacheKey;
    cir::GlobalOp placeholderGV;
    mlir::Attribute init;
  };

  llvm::SmallVector<LegacyGNUSelectorEntry, 8> selectors;
  selectors.reserve(objcSelectorGlobals.size());
  for (const auto &entry : objcSelectorGlobals) {
    cir::GlobalOp selectorGV = entry.getValue();
    std::optional<mlir::Attribute> init = selectorGV.getInitialValue();
    assert(init && "GNU selector global must have an initializer");
    selectors.push_back({entry.getKey().str(), selectorGV, *init});
  }
  llvm::sort(selectors, [](const auto &lhs, const auto &rhs) {
    auto lhsName = llvm::StringRef(lhs.cacheKey).split('\0').first;
    auto rhsName = llvm::StringRef(rhs.cacheKey).split('\0').first;
    if (lhsName != rhsName)
      return lhsName < rhsName;
    return lhs.placeholderGV->isBeforeInBlock(rhs.placeholderGV);
  });

  size_t selectorCount = selectors.size();
  mlir::Attribute selectorListInit;
  auto selectorArrayTy = cir::ArrayType::get(selectorElemTy, selectorCount + 1);
  if (selectors.empty()) {
    selectorListInit = builder.getZeroInitAttr(selectorArrayTy);
  } else {
    llvm::SmallVector<mlir::Attribute, 8> selectorInitAttrs;
    selectorInitAttrs.reserve(selectorCount + 1);
    for (const auto &selector : selectors)
      selectorInitAttrs.push_back(selector.init);
    selectorInitAttrs.push_back(builder.getZeroInitAttr(selectorElemTy));
    selectorListInit = cir::ConstArrayAttr::get(
        selectorArrayTy, builder.getArrayAttr(selectorInitAttrs));
  }
  cir::GlobalOp selectorListGV = createPrivateGlobal(
      ".objc_selector_list", selectorArrayTy, selectorListInit);
  if (!selectors.empty()) {
    mlir::FlatSymbolRefAttr selectorListSym =
        mlir::FlatSymbolRefAttr::get(selectorListGV.getSymNameAttr());
    llvm::StringMap<LegacyGNUSelectorReplacement> selectorReplacements;
    for (auto [index, selector] : llvm::enumerate(selectors)) {
      selectorReplacements.try_emplace(
          selector.placeholderGV.getSymName(),
          LegacyGNUSelectorReplacement{
              selectorListSym, static_cast<unsigned>(index)});
    }
    replaceLegacyGNUSelectorUsesInModule(*this, selectorReplacements);
    for (const auto &selector : selectors) {
      eraseGlobalSymbol(selector.placeholderGV);
      selector.placeholderGV->erase();
    }
    objcSelectorGlobals.clear();
  }

  mlir::Attribute statics = nullPtr;
  if (!legacyGNUConstantStringGlobals.empty()) {
    StringRef stringClass = getLangOpts().ObjCConstantStringClass;
    if (stringClass.empty())
      stringClass = "NXConstantString";

    cir::GlobalOp staticClassNameGV =
        createPrivateCString(".objc_static_class_name", stringClass);

    llvm::SmallVector<mlir::Attribute, 8> staticObjects;
    staticObjects.reserve(legacyGNUConstantStringGlobals.size() + 1);
    for (cir::GlobalOp stringGV : legacyGNUConstantStringGlobals)
      staticObjects.push_back(builder.getGlobalViewAttr(voidPtrTy, stringGV));
    staticObjects.push_back(nullPtr);

    auto staticObjectsTy = cir::ArrayType::get(voidPtrTy, staticObjects.size());
    auto staticsTy = builder.getAnonRecordTy({voidPtrTy, staticObjectsTy},
                                             /*packed=*/false,
                                             /*padded=*/false);
    auto staticsInit = builder.getAnonConstRecord(
        builder.getArrayAttr(
            {builder.getGlobalViewAttr(voidPtrTy, staticClassNameGV),
             cir::ConstArrayAttr::get(staticObjectsTy,
                                      builder.getArrayAttr(staticObjects))}),
        /*packed=*/false, /*padded=*/false, staticsTy);
    cir::GlobalOp staticsGV =
        createPrivateGlobal(".objc_statics", staticsTy, staticsInit);

    auto staticsPtrTy = cir::ArrayType::get(voidPtrTy, 2);
    auto staticsPtrInit = cir::ConstArrayAttr::get(
        staticsPtrTy,
        builder.getArrayAttr(
            {builder.getGlobalViewAttr(voidPtrTy, staticsGV), nullPtr}));
    cir::GlobalOp staticsPtrGV = createPrivateGlobal(".objc_statics_ptr",
                                                     staticsPtrTy,
                                                     staticsPtrInit);
    statics = builder.getGlobalViewAttr(voidPtrTy, staticsPtrGV);
  }

  llvm::SmallVector<mlir::Attribute, 8> defs;
  defs.reserve(legacyGNUClassGlobals.size() + legacyGNUCategoryGlobals.size() +
               3);
  for (cir::GlobalOp classGV : legacyGNUClassGlobals)
    defs.push_back(builder.getGlobalViewAttr(voidPtrTy, classGV));
  for (cir::GlobalOp categoryGV : legacyGNUCategoryGlobals)
    defs.push_back(builder.getGlobalViewAttr(voidPtrTy, categoryGV));
  defs.push_back(builder.getGlobalViewAttr(voidPtrTy, holderCategoryGV));
  defs.push_back(statics);
  defs.push_back(nullPtr);
  auto defsArrayTy = cir::ArrayType::get(voidPtrTy, defs.size());
  auto defsArrayInit =
      cir::ConstArrayAttr::get(defsArrayTy, builder.getArrayAttr(defs));

  auto symtabTy = builder.getAnonRecordTy(
      {longTy, voidPtrTy, i16Ty, i16Ty, defsArrayTy}, /*packed=*/false,
      /*padded=*/false);
  auto symtabInit = builder.getAnonConstRecord(
      builder.getArrayAttr(
          {cir::IntAttr::get(longTy, selectorCount),
           builder.getGlobalViewAttr(voidPtrTy, selectorListGV),
           cir::IntAttr::get(i16Ty, legacyGNUClassGlobals.size()),
           cir::IntAttr::get(i16Ty, legacyGNUCategoryGlobals.size() + 1),
           defsArrayInit}),
      /*packed=*/false, /*padded=*/false, symtabTy);
  cir::GlobalOp symtabGV =
      createPrivateGlobal(".objc_symtab", symtabTy, symtabInit);

  int runtimeVersion =
      (getLangOpts().ObjCRuntime.getKind() == ObjCRuntime::GNUstep ||
       getLangOpts().ObjCRuntime.getKind() == ObjCRuntime::ObjFW)
          ? 9
          : 8;
  const LangOptions &opts = getLangOpts();
  if (opts.getGC() != LangOptions::NonGC || opts.ObjCAutoRefCount)
    runtimeVersion = 10;

  auto &sourceManager = astContext.getSourceManager();
  std::string sourcePath;
  if (auto mainFile =
          sourceManager.getFileEntryRefForID(sourceManager.getMainFileID()))
    sourcePath = (mainFile->getDir().getName() + "/" + mainFile->getName()).str();
  cir::GlobalOp sourceFileGV =
      createPrivateCString(".objc_source_file_name", sourcePath);

  llvm::SmallVector<mlir::Type, 5> moduleFieldTypes = {longTy, longTy, voidPtrTy,
                                                       voidPtrTy, intTy};
  if (runtimeVersion < 10)
    moduleFieldTypes.pop_back();
  auto moduleTy = builder.getAnonRecordTy(moduleFieldTypes, /*packed=*/false,
                                          /*padded=*/false);

  llvm::SmallVector<mlir::Attribute, 5> moduleFields;
  moduleFields.push_back(cir::IntAttr::get(longTy, runtimeVersion));
  moduleFields.push_back(cir::IntAttr::get(
      longTy, getDataLayout().getTypeStoreSize(moduleTy).getFixedValue()));
  moduleFields.push_back(builder.getGlobalViewAttr(voidPtrTy, sourceFileGV));
  moduleFields.push_back(builder.getGlobalViewAttr(voidPtrTy, symtabGV));
  if (runtimeVersion >= 10) {
    int gcMode = 0;
    switch (opts.getGC()) {
    case LangOptions::GCOnly:
      gcMode = 2;
      break;
    case LangOptions::NonGC:
      gcMode = opts.ObjCAutoRefCount ? 1 : 0;
      break;
    case LangOptions::HybridGC:
      gcMode = 1;
      break;
    }
    moduleFields.push_back(cir::IntAttr::get(intTy, gcMode));
  }
  auto moduleInit = builder.getAnonConstRecord(builder.getArrayAttr(moduleFields),
                                               /*packed=*/false,
                                               /*padded=*/false, moduleTy);
  cir::GlobalOp moduleGV =
      createPrivateGlobal(".objc_module", moduleTy, moduleInit);

  auto fnTy = cir::FuncType::get({}, voidTy);
  cir::FuncOp loadFn =
      createCIRFunction(loc, ".objc_load_function", fnTy, /*funcDecl=*/nullptr);
  loadFn.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
  mlir::SymbolTable::setSymbolVisibility(loadFn,
                                         mlir::SymbolTable::Visibility::Private);
  setDSOLocal(loadFn.getOperation());
  if (loadFn.isDeclaration()) {
    mlir::Block *entry = loadFn.addEntryBlock();
    mlir::OpBuilder::InsertionGuard guard(builder);
    builder.setInsertionPointToStart(entry);

    auto modulePtrTy =
        builder.getPointerTo(moduleGV.getSymType(), moduleGV.getAddrSpaceAttr());
    cir::FuncType execClassTy =
        cir::FuncType::get({modulePtrTy}, voidTy, /*isVarArg=*/true);
    cir::FuncOp execClassFn =
        createRuntimeFunction(execClassTy, "__objc_exec_class");
    builder.createCallOp(loc, execClassFn, builder.createGetGlobal(loc, moduleGV));

    if (!legacyGNUClassAliases.empty()) {
      cir::FuncType registerAliasTy =
          cir::FuncType::get({voidPtrTy, voidPtrTy}, voidTy);
      cir::FuncOp registerAliasFn =
          createRuntimeFunction(registerAliasTy, "class_registerAlias_np");
      registerAliasFn.setLinkage(cir::GlobalLinkageKind::ExternalWeakLinkage);

      auto &body = loadFn.getBody();
      auto *aliasBlock = new mlir::Block();
      auto *noAliasBlock = new mlir::Block();
      body.push_back(aliasBlock);
      body.push_back(noAliasBlock);

      auto registerAliasFnPtrTy =
          builder.getPointerTo(registerAliasFn.getFunctionType());
      mlir::Value registerAliasAddr =
          cir::GetGlobalOp::create(builder, loc, registerAliasFnPtrTy,
                                   registerAliasFn.getSymName())
              .getAddr();
      mlir::Value hasRegisterAlias = cir::CmpOp::create(
          builder, loc, cir::CmpOpKind::ne, registerAliasAddr,
          builder.getNullPtr(registerAliasFnPtrTy, loc));
      cir::BrCondOp::create(builder, loc, hasRegisterAlias, aliasBlock,
                            noAliasBlock);

      builder.setInsertionPointToStart(aliasBlock);
      for (const auto &[className, aliasName] : legacyGNUClassAliases) {
        std::string classSymbolName = (llvm::Twine("_OBJC_CLASS_") + className).str();
        auto classGV =
            mlir::dyn_cast_or_null<cir::GlobalOp>(getGlobalValue(classSymbolName));
        if (!classGV)
          continue;

        mlir::Value classAddr =
            builder.createBitcast(loc, builder.createGetGlobal(loc, classGV),
                                  voidPtrTy);
        cir::GlobalOp aliasNameGV =
            getGlobalForConstantCString(loc, aliasName, ".objc_alias_name");
        mlir::Value aliasAddr = builder.createBitcast(
            loc, builder.createGetGlobal(loc, aliasNameGV), voidPtrTy);
        builder.createCallOp(loc, registerAliasFn,
                             mlir::ValueRange{classAddr, aliasAddr});
      }
      cir::BrOp::create(builder, loc, noAliasBlock);
      builder.setInsertionPointToStart(noAliasBlock);
    }

    cir::ReturnOp::create(builder, loc);
  }
  addGlobalCtor(loadFn);
}

cir::GlobalOp CIRGenModule::getObjCDarwinFragileClassGlobal(
    mlir::Location loc, const ObjCImplementationDecl *oid) {
  assert(usesDarwinFragileObjCRuntime(*this) &&
         "Darwin ObjC class metadata currently requires fragile Mach-O");
  assert(oid && "expected Objective-C implementation");

  const ObjCInterfaceDecl *classDecl = oid->getClassInterface();
  assert(classDecl && "expected Objective-C class interface");

  std::string className = classDecl->getNameAsString();
  std::string runtimeName = classDecl->getObjCRuntimeNameAsString().str();
  std::string classSymbolName = "OBJC_CLASS_" + className;
  if (auto *existing = getGlobalValue(classSymbolName)) {
    auto classGV = mlir::cast<cir::GlobalOp>(existing);
    if (!classGV.isDeclaration())
      return classGV;
  }

  auto i32Ty = builder.getUInt32Ty();
  auto longTy = mlir::cast<cir::IntType>(convertType(astContext.LongTy));
  mlir::Attribute nullPtr = builder.getConstPtrAttr(voidPtrTy, 0);
  uint64_t pointerSize = getPointerAlign().getQuantity();
  uint64_t longSize =
      astContext.getTypeSizeInChars(astContext.LongTy).getQuantity();
  uint64_t propertyEntrySize = 2 * pointerSize;
  uint64_t classExtensionSize =
      llvm::alignTo(uint64_t(4), pointerSize) + 2 * pointerSize;
  uint64_t classMetadataSize = 9 * pointerSize + 3 * longSize;
  auto methodEntryTy = builder.getAnonRecordTy(
      {voidPtrTy, voidPtrTy, voidPtrTy}, /*packed=*/false, /*padded=*/false);
  auto ivarEntryTy = builder.getAnonRecordTy(
      {voidPtrTy, voidPtrTy, i32Ty}, /*packed=*/false, /*padded=*/false);
  auto propertyEntryTy = builder.getAnonRecordTy(
      {voidPtrTy, voidPtrTy}, /*packed=*/false, /*padded=*/false);
  auto classExtensionTy = builder.getAnonRecordTy(
      {i32Ty, voidPtrTy, voidPtrTy}, /*packed=*/false, /*padded=*/false);
  auto classTy = builder.getAnonRecordTy(
      {voidPtrTy, voidPtrTy, voidPtrTy, longTy, longTy, longTy, voidPtrTy,
       voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy},
      /*packed=*/false, /*padded=*/false);

  auto createFragileMetadataGlobal =
      [&](llvm::StringRef name, mlir::Type type, mlir::Attribute init,
          llvm::StringRef section) -> cir::GlobalOp {
    cir::GlobalOp gv =
        createGlobalOp(*this, loc, name, type, /*isConstant=*/false);
    gv.setLinkage(cir::GlobalLinkageKind::PrivateLinkage);
    mlir::SymbolTable::setSymbolVisibility(
        gv, mlir::SymbolTable::Visibility::Private);
    gv.setAlignmentAttr(getSize(getPointerAlign()));
    gv.setSectionAttr(getStringAttrCached(section));
    setInitializer(gv, init);
    setDSOLocal(static_cast<mlir::Operation *>(gv));
    addCompilerUsedGlobal(gv);
    return gv;
  };

  auto createFragileCStringGlobal =
      [&](llvm::StringRef prefix, llvm::StringRef value) -> cir::GlobalOp {
    std::string bytes = value.str();
    bytes.push_back('\0');
    auto init = mlir::cast<mlir::TypedAttr>(
        builder.getString(bytes, uCharTy, bytes.size()));
    cir::GlobalOp gv =
        createGlobalOp(*this, loc, getUniqueGlobalName(prefix.str()),
                       init.getType(), /*isConstant=*/true);
    gv.setLinkage(cir::GlobalLinkageKind::PrivateLinkage);
    mlir::SymbolTable::setSymbolVisibility(
        gv, mlir::SymbolTable::Visibility::Private);
    gv.setAlignmentAttr(getSize(CharUnits::One()));
    gv->setAttr("unnamed_addr", mlir::UnitAttr::get(&getMLIRContext()));
    gv.setSectionAttr(
        getStringAttrCached("__TEXT,__cstring,cstring_literals"));
    setInitializer(gv, init);
    setDSOLocal(static_cast<mlir::Operation *>(gv));
    addCompilerUsedGlobal(gv);
    return gv;
  };

  auto getClassNameAttr = [&](const ObjCInterfaceDecl *iface) -> mlir::Attribute {
    if (!iface)
      return nullPtr;
    return builder.getGlobalViewAttr(
        voidPtrTy,
        getObjCClassNameGlobal(iface->getObjCRuntimeNameAsString()));
  };

  llvm::SmallVector<const ObjCMethodDecl *, 8> instanceMethods;
  llvm::SmallVector<const ObjCMethodDecl *, 8> classMethods;
  llvm::SmallPtrSet<const ObjCMethodDecl *, 8> seenMethods;
  auto addMethod = [&](const ObjCMethodDecl *method) {
    if (!method || method->isDirectMethod())
      return;
    const ObjCMethodDecl *canonical = method->getCanonicalDecl();
    if (!seenMethods.insert(canonical).second)
      return;
    (method->isClassMethod() ? classMethods : instanceMethods).push_back(method);
  };
  for (const auto *method : oid->methods())
    addMethod(method);
  for (const auto *propertyImpl : oid->property_impls()) {
    if (propertyImpl->getPropertyImplementation() !=
        ObjCPropertyImplDecl::Synthesize)
      continue;
    ObjCPropertyDecl *property = propertyImpl->getPropertyDecl();
    if (property->isDirectProperty())
      continue;
    addMethod(propertyImpl->getGetterMethodDecl());
    if (!property->isReadOnly())
      addMethod(propertyImpl->getSetterMethodDecl());
  }

  auto emitMethodList =
      [&](llvm::StringRef symbolName, llvm::StringRef sectionName,
          llvm::ArrayRef<const ObjCMethodDecl *> methods) -> mlir::Attribute {
    if (methods.empty())
      return nullPtr;

    llvm::SmallVector<mlir::Attribute, 8> methodAttrs;
    methodAttrs.reserve(methods.size());
    for (const auto *method : methods) {
      cir::GlobalOp methodNameGV =
          getObjCSelectorNameGlobal(method->getSelector());
      std::string typeEncoding =
          astContext.getObjCEncodingForMethodDecl(method, /*Extended=*/false);
      cir::GlobalOp methodTypeGV = getObjCMethodTypeGlobal(typeEncoding);

      GlobalDecl gd(method);
      const CIRGenFunctionInfo &fi =
          getTypes().arrangeObjCMethodDeclaration(method);
      cir::FuncType fnTy = getTypes().getFunctionType(fi);
      SmallString<256> nameBuf;
      {
        llvm::raw_svector_ostream os(nameBuf);
        mangleObjCMethodName(method, os);
      }
      cir::FuncOp fn = getOrCreateCIRFunction(
          nameBuf.str(), fnTy, gd, /*forVTable=*/false,
          /*dontDefer=*/true, /*isThunk=*/false, NotForDefinition);
      fn.setLinkage(cir::GlobalLinkageKind::InternalLinkage);

      methodAttrs.push_back(builder.getAnonConstRecord(
          builder.getArrayAttr(
              {builder.getGlobalViewAttr(voidPtrTy, methodNameGV),
               builder.getGlobalViewAttr(voidPtrTy, methodTypeGV),
               cir::GlobalViewAttr::get(
                   voidPtrTy, mlir::FlatSymbolRefAttr::get(fn.getSymNameAttr()))}),
          /*packed=*/false, /*padded=*/false, methodEntryTy));
    }

    auto methodArrayTy = cir::ArrayType::get(methodEntryTy, methodAttrs.size());
    auto methodArrayInit =
        cir::ConstArrayAttr::get(methodArrayTy, builder.getArrayAttr(methodAttrs));
    auto methodListTy = builder.getAnonRecordTy(
        {voidPtrTy, i32Ty, methodArrayTy}, /*packed=*/false, /*padded=*/false);
    auto methodListInit = builder.getAnonConstRecord(
        builder.getArrayAttr(
            {nullPtr, cir::IntAttr::get(i32Ty, methodAttrs.size()),
             methodArrayInit}),
        /*packed=*/false, /*padded=*/false, methodListTy);
    cir::GlobalOp gv = createFragileMetadataGlobal(symbolName, methodListTy,
                                                   methodListInit, sectionName);
    return builder.getGlobalViewAttr(voidPtrTy, gv);
  };

  auto emitIvarList = [&]() -> mlir::Attribute {
    llvm::SmallVector<const ObjCIvarDecl *, 8> ivars;
    for (const ObjCIvarDecl *ivar = classDecl->all_declared_ivar_begin(); ivar;
         ivar = ivar->getNextIvar())
      ivars.push_back(ivar);
    if (ivars.empty())
      return nullPtr;

    llvm::SmallVector<mlir::Attribute, 8> ivarAttrs;
    ivarAttrs.reserve(ivars.size());
    for (const ObjCIvarDecl *ivar : ivars) {
      std::string ivarTypeEncoding;
      astContext.getObjCEncodingForType(ivar->getType(), ivarTypeEncoding);
      uint64_t offset = astContext.lookupFieldBitOffset(classDecl, ivar) /
                        astContext.getCharWidth();
      cir::GlobalOp ivarNameGV =
          createFragileCStringGlobal("OBJC_METH_VAR_NAME_",
                                     ivar->getNameAsString());
      cir::GlobalOp ivarTypeGV = getObjCMethodTypeGlobal(ivarTypeEncoding);
      ivarAttrs.push_back(builder.getAnonConstRecord(
          builder.getArrayAttr(
              {builder.getGlobalViewAttr(voidPtrTy, ivarNameGV),
               builder.getGlobalViewAttr(voidPtrTy, ivarTypeGV),
               cir::IntAttr::get(i32Ty, offset)}),
          /*packed=*/false, /*padded=*/false, ivarEntryTy));
    }

    auto ivarArrayTy = cir::ArrayType::get(ivarEntryTy, ivarAttrs.size());
    auto ivarArrayInit =
        cir::ConstArrayAttr::get(ivarArrayTy, builder.getArrayAttr(ivarAttrs));
    auto ivarListTy = builder.getAnonRecordTy(
        {i32Ty, ivarArrayTy}, /*packed=*/false, /*padded=*/false);
    auto ivarListInit = builder.getAnonConstRecord(
        builder.getArrayAttr(
            {cir::IntAttr::get(i32Ty, ivarAttrs.size()), ivarArrayInit}),
        /*packed=*/false, /*padded=*/false, ivarListTy);
    cir::GlobalOp gv = createFragileMetadataGlobal(
        "OBJC_INSTANCE_VARIABLES_" + className, ivarListTy, ivarListInit,
        "__OBJC,__instance_vars,regular,no_dead_strip");
    return builder.getGlobalViewAttr(voidPtrTy, gv);
  };

  auto emitPropertyList = [&](bool isClassProperty) -> mlir::Attribute {
    if (isClassProperty) {
      const llvm::Triple &triple = getTriple();
      if ((triple.isMacOSX() && triple.isMacOSXVersionLT(10, 11)) ||
          (triple.isiOS() && triple.isOSVersionLT(9)))
        return nullPtr;
    }

    llvm::SmallVector<const ObjCPropertyDecl *, 8> properties;
    llvm::SmallPtrSet<const IdentifierInfo *, 8> seenProperties;
    auto addProperty = [&](const ObjCPropertyDecl *property) {
      if (property->isClassProperty() != isClassProperty ||
          property->isDirectProperty())
        return;
      if (!seenProperties.insert(property->getIdentifier()).second)
        return;
      properties.push_back(property);
    };

    for (const ObjCCategoryDecl *classExt : classDecl->known_extensions())
      for (const auto *property : classExt->properties())
        addProperty(property);
    for (const auto *property : classDecl->properties())
      addProperty(property);

    if (properties.empty())
      return nullPtr;

    llvm::SmallVector<mlir::Attribute, 8> propertyAttrs;
    propertyAttrs.reserve(properties.size());
    for (const auto *property : properties) {
      cir::GlobalOp propertyNameGV =
          getObjCPropertyNameGlobal(property->getIdentifier()->getName());
      std::string propertyType =
          astContext.getObjCEncodingForPropertyDecl(property, oid);
      cir::GlobalOp propertyTypeGV = getObjCPropertyNameGlobal(propertyType);
      propertyAttrs.push_back(builder.getAnonConstRecord(
          builder.getArrayAttr(
              {builder.getGlobalViewAttr(voidPtrTy, propertyNameGV),
               builder.getGlobalViewAttr(voidPtrTy, propertyTypeGV)}),
          /*packed=*/false, /*padded=*/false, propertyEntryTy));
    }

    auto propertyArrayTy =
        cir::ArrayType::get(propertyEntryTy, propertyAttrs.size());
    auto propertyArrayInit = cir::ConstArrayAttr::get(
        propertyArrayTy, builder.getArrayAttr(propertyAttrs));
    auto propertyListTy = builder.getAnonRecordTy(
        {i32Ty, i32Ty, propertyArrayTy}, /*packed=*/false, /*padded=*/false);
    auto propertyListInit = builder.getAnonConstRecord(
        builder.getArrayAttr(
            {cir::IntAttr::get(i32Ty, propertyEntrySize),
             cir::IntAttr::get(i32Ty, propertyAttrs.size()), propertyArrayInit}),
        /*packed=*/false, /*padded=*/false, propertyListTy);
    cir::GlobalOp gv = createFragileMetadataGlobal(
        (llvm::Twine(isClassProperty ? "_OBJC_$_CLASS_PROP_LIST_"
                                     : "_OBJC_$_PROP_LIST_") +
         className)
            .str(),
        propertyListTy, propertyListInit,
        "__OBJC,__property,regular,no_dead_strip");
    return builder.getGlobalViewAttr(voidPtrTy, gv);
  };

  auto emitProtocolList = [&]() -> mlir::Attribute {
    llvm::SmallVector<mlir::Attribute, 8> protocolAttrs;
    for (const auto *protocol : classDecl->all_referenced_protocols()) {
      if (protocol->isNonRuntimeProtocol())
        continue;
      protocolAttrs.push_back(
          builder.getGlobalViewAttr(voidPtrTy, getObjCProtocolGlobal(loc, protocol)));
    }

    if (protocolAttrs.empty())
      return nullPtr;

    protocolAttrs.push_back(nullPtr);
    auto protocolArrayTy = cir::ArrayType::get(voidPtrTy, protocolAttrs.size());
    auto protocolArrayInit = cir::ConstArrayAttr::get(
        protocolArrayTy, builder.getArrayAttr(protocolAttrs));
    auto protocolListTy = builder.getAnonRecordTy(
        {voidPtrTy, longTy, protocolArrayTy}, /*packed=*/false,
        /*padded=*/false);
    auto protocolListInit = builder.getAnonConstRecord(
        builder.getArrayAttr(
            {nullPtr,
             cir::IntAttr::get(longTy, protocolAttrs.size() - 1),
             protocolArrayInit}),
        /*packed=*/false, /*padded=*/false, protocolListTy);
    cir::GlobalOp gv = createFragileMetadataGlobal(
        "OBJC_CLASS_PROTOCOLS_" + className, protocolListTy, protocolListInit,
        "__OBJC,__cat_cls_meth,regular,no_dead_strip");
    return builder.getGlobalViewAttr(voidPtrTy, gv);
  };

  auto emitClassExtension = [&](bool isMetaclass) -> mlir::Attribute {
    mlir::Attribute propertyListAttr = emitPropertyList(isMetaclass);
    if (propertyListAttr == nullPtr)
      return nullPtr;

    auto classExtensionInit = builder.getAnonConstRecord(
        builder.getArrayAttr(
            {cir::IntAttr::get(i32Ty, classExtensionSize),
             nullPtr, propertyListAttr}),
        /*packed=*/false, /*padded=*/false, classExtensionTy);
    cir::GlobalOp gv = createFragileMetadataGlobal(
        "OBJC_CLASSEXT_" + className, classExtensionTy, classExtensionInit,
        "__OBJC,__class_ext,regular,no_dead_strip");
    return builder.getGlobalViewAttr(voidPtrTy, gv);
  };

  mlir::Attribute classNameAttr =
      builder.getGlobalViewAttr(voidPtrTy, getObjCClassNameGlobal(runtimeName));
  mlir::Attribute classMethodsAttr = emitMethodList(
      "OBJC_CLASS_METHODS_" + className,
      "__OBJC,__cls_meth,regular,no_dead_strip", classMethods);
  mlir::Attribute instanceMethodsAttr = emitMethodList(
      "OBJC_INSTANCE_METHODS_" + className,
      "__OBJC,__inst_meth,regular,no_dead_strip", instanceMethods);
  mlir::Attribute ivarsAttr = emitIvarList();
  mlir::Attribute protocolListAttr = emitProtocolList();
  mlir::Attribute classExtensionAttr = emitClassExtension(/*isMetaclass=*/false);
  mlir::Attribute metaclassExtensionAttr =
      emitClassExtension(/*isMetaclass=*/true);

  uint32_t classFlags = DarwinFragileObjCClassFactory;
  if (oid->hasNonZeroConstructors() || oid->hasDestructors())
    classFlags |= DarwinFragileObjCClassHasCXXStructors;
  if (classDecl->getVisibility() == HiddenVisibility)
    classFlags |= DarwinFragileObjCClassHidden;
  if (getLangOpts().ObjCAutoRefCount)
    classFlags |= DarwinFragileObjCClassCompiledByARC;

  uint32_t metaclassFlags = DarwinFragileObjCClassMeta;
  if (classDecl->getVisibility() == HiddenVisibility)
    metaclassFlags |= DarwinFragileObjCClassHidden;

  const ObjCInterfaceDecl *rootDecl = classDecl;
  while (const ObjCInterfaceDecl *super = rootDecl->getSuperClass())
    rootDecl = super;

  uint64_t classSize =
      astContext.getASTObjCInterfaceLayout(classDecl).getSize().getQuantity();
  uint64_t metaclassSize = classMetadataSize;

  cir::GlobalOp metaclassGV = createOrReplaceCXXRuntimeVariable(
      loc, "OBJC_METACLASS_" + className, classTy,
      cir::GlobalLinkageKind::PrivateLinkage, getPointerAlign());
  metaclassGV.setConstant(false);
  auto metaclassInit = builder.getAnonConstRecord(
      builder.getArrayAttr(
          {getClassNameAttr(rootDecl), getClassNameAttr(classDecl->getSuperClass()),
           classNameAttr, cir::IntAttr::get(longTy, 0),
           cir::IntAttr::get(longTy, metaclassFlags),
           cir::IntAttr::get(longTy, metaclassSize), nullPtr, classMethodsAttr,
           nullPtr, protocolListAttr, nullPtr, metaclassExtensionAttr}),
      /*packed=*/false, /*padded=*/false, classTy);
  setInitializer(metaclassGV, metaclassInit);
  metaclassGV.setSectionAttr(
      getStringAttrCached("__OBJC,__meta_class,regular,no_dead_strip"));
  metaclassGV.setAlignmentAttr(getSize(getPointerAlign()));
  addCompilerUsedGlobal(metaclassGV);

  cir::GlobalOp classGV = createOrReplaceCXXRuntimeVariable(
      loc, classSymbolName, classTy, cir::GlobalLinkageKind::PrivateLinkage,
      getPointerAlign());
  classGV.setConstant(false);
  auto classInit = builder.getAnonConstRecord(
      builder.getArrayAttr(
          {builder.getGlobalViewAttr(voidPtrTy, metaclassGV),
           getClassNameAttr(classDecl->getSuperClass()), classNameAttr,
           cir::IntAttr::get(longTy, 0), cir::IntAttr::get(longTy, classFlags),
           cir::IntAttr::get(longTy, classSize), ivarsAttr, instanceMethodsAttr,
           nullPtr, protocolListAttr, nullPtr, classExtensionAttr}),
      /*packed=*/false, /*padded=*/false, classTy);
  setInitializer(classGV, classInit);
  classGV.setSectionAttr(
      getStringAttrCached("__OBJC,__class,regular,no_dead_strip"));
  classGV.setAlignmentAttr(getSize(getPointerAlign()));
  addCompilerUsedGlobal(classGV);

  objcDarwinFragileDefinedClasses.push_back(classGV);
  return classGV;
}
cir::GlobalOp CIRGenModule::getObjCDarwinEHTypeGlobal(
    mlir::Location loc, const ObjCInterfaceDecl *id, bool forDefinition) {
  assert(usesDarwinNonFragileObjCRuntime(*this) &&
         "Darwin ObjC exception metadata currently requires non-fragile "
         "Mach-O");
  assert(id && "expected Objective-C interface");

  id = id->getCanonicalDecl();
  if (auto it = objcDarwinEHTypeGlobals.find(id);
      it != objcDarwinEHTypeGlobals.end()) {
    if (!forDefinition || !it->second.isDeclaration())
      return it->second;
  }

  std::string runtimeName = id->getObjCRuntimeNameAsString().str();
  std::string symbolName = "OBJC_EHTYPE_$_" + runtimeName;
  auto ehTypeTy = builder.getAnonRecordTy(
      {voidPtrTy, voidPtrTy, voidPtrTy}, /*packed=*/false, /*padded=*/false);

  bool isExceptionClass = hasObjCExceptionAttribute(id);
  if (!forDefinition && isExceptionClass) {
    cir::GlobalOp gv = createOrReplaceCXXRuntimeVariable(
        loc, symbolName, ehTypeTy, cir::GlobalLinkageKind::ExternalLinkage,
        CharUnits::Zero());
    gv.setConstant(false);
    setGVProperties(static_cast<mlir::Operation *>(gv), id);
    objcDarwinEHTypeGlobals[id] = gv;
    return gv;
  }

  cir::GlobalLinkageKind linkage =
      forDefinition ? cir::GlobalLinkageKind::ExternalLinkage
                    : cir::GlobalLinkageKind::WeakAnyLinkage;
  cir::GlobalOp gv = createOrReplaceCXXRuntimeVariable(
      loc, symbolName, ehTypeTy, linkage, getPointerAlign());
  gv.setConstant(false);
  gv.setLinkage(linkage);
  if (getTriple().isOSBinFormatCOFF() && !forDefinition)
    gv->removeAttr("dso_local");
  objcDarwinEHTypeGlobals[id] = gv;
  if (!gv.isDeclaration())
    return gv;

  cir::GlobalOp vtableGV = createOrReplaceCXXRuntimeVariable(
      loc, "objc_ehtype_vtable", voidPtrTy,
      cir::GlobalLinkageKind::ExternalLinkage, CharUnits::Zero());
  vtableGV.setConstant(false);
  setObjCRuntimeDLLStorageClass(*this, vtableGV, "objc_ehtype_vtable");

  auto vtableSym = mlir::FlatSymbolRefAttr::get(vtableGV.getSymNameAttr());
  auto vtableIndices = builder.getArrayAttr({builder.getI32IntegerAttr(2)});
  mlir::Attribute vtableAttr =
      cir::GlobalViewAttr::get(voidPtrTy, vtableSym, vtableIndices,
                               /*inbounds=*/true);
  mlir::Attribute classNameAttr =
      builder.getGlobalViewAttr(voidPtrTy, getObjCClassNameGlobal(runtimeName));
  mlir::Attribute classAttr =
      builder.getGlobalViewAttr(voidPtrTy, getObjCClassSymbol(loc, id));

  auto init = builder.getAnonConstRecord(
      builder.getArrayAttr({vtableAttr, classNameAttr, classAttr}),
      /*packed=*/false, /*padded=*/false, ehTypeTy);
  setInitializer(gv, init);
  if (!getTriple().isOSBinFormatCOFF() &&
      id->getVisibility() == HiddenVisibility)
    gv.setGlobalVisibility(cir::VisibilityKind::Hidden);
  if (const auto &schema = getCodeGenOpts().PointerAuth.CXXTypeInfoVTablePointer;
      schema && shouldSignPointer(schema)) {
    gv->setAttr(
        "cir.global_const_ptrauth",
        builder.getArrayAttr({builder.getArrayAttr(
            {builder.getI64IntegerAttr(0),
             builder.getI32IntegerAttr(schema.getKey()),
             builder.getI64IntegerAttr(
                 getPointerAuthOtherDiscriminator(schema, GlobalDecl(),
                                                 QualType())),
             builder.getBoolAttr(schema.isAddressDiscriminated())})}));
  }
  gv.setAlignmentAttr(getSize(getPointerAlign()));
  if (forDefinition && getTriple().isOSBinFormatMachO())
    gv.setSectionAttr(getStringAttrCached("__DATA,__objc_const"));
  if (isExceptionClass)
    setGVProperties(static_cast<mlir::Operation *>(gv), id);
  return gv;
}
cir::GlobalOp CIRGenModule::getObjCDarwinIdEHTypeGlobal(mlir::Location loc) {
  assert(usesDarwinNonFragileObjCRuntime(*this) &&
         "Darwin ObjC exception metadata currently requires non-fragile "
         "Mach-O");

  constexpr llvm::StringLiteral symbolName = "OBJC_EHTYPE_id";
  auto ehTypeTy = builder.getAnonRecordTy(
      {voidPtrTy, voidPtrTy, voidPtrTy}, /*packed=*/false, /*padded=*/false);

  cir::GlobalOp gv = createOrReplaceCXXRuntimeVariable(
      loc, symbolName, ehTypeTy, cir::GlobalLinkageKind::ExternalLinkage,
      CharUnits::Zero());
  gv.setConstant(false);
  setObjCRuntimeDLLStorageClass(*this, gv, symbolName);
  return gv;
}
void CIRGenModule::emitObjCDarwinFragileModuleInfo(mlir::Location loc) {
  assert(usesDarwinFragileObjCRuntime(*this) &&
         "Darwin ObjC module metadata currently requires fragile Mach-O");
  if ((objcDarwinFragileDefinedClasses.empty() &&
       objcDarwinFragileDefinedCategories.empty()) ||
      getGlobalValue("OBJC_MODULES"))
    return;

  auto i16Ty = builder.getUInt16Ty();
  auto longTy = mlir::cast<cir::IntType>(convertType(astContext.LongTy));
  mlir::Attribute nullPtr = builder.getConstPtrAttr(voidPtrTy, 0);
  uint64_t pointerSize = getPointerAlign().getQuantity();
  uint64_t longSize =
      astContext.getTypeSizeInChars(astContext.LongTy).getQuantity();
  uint64_t moduleSize = 2 * longSize + 2 * pointerSize;

  llvm::SmallVector<mlir::Attribute, 8> symbolAttrs;
  symbolAttrs.reserve(objcDarwinFragileDefinedClasses.size() +
                      objcDarwinFragileDefinedCategories.size());
  for (cir::GlobalOp classGV : objcDarwinFragileDefinedClasses)
    symbolAttrs.push_back(builder.getGlobalViewAttr(voidPtrTy, classGV));
  for (cir::GlobalOp categoryGV : objcDarwinFragileDefinedCategories)
    symbolAttrs.push_back(builder.getGlobalViewAttr(voidPtrTy, categoryGV));

  auto symbolArrayTy = cir::ArrayType::get(voidPtrTy, symbolAttrs.size());
  auto symbolArrayInit = cir::ConstArrayAttr::get(
      symbolArrayTy, builder.getArrayAttr(symbolAttrs));
  auto symtabTy = builder.getAnonRecordTy(
      {longTy, voidPtrTy, i16Ty, i16Ty, symbolArrayTy}, /*packed=*/false,
      /*padded=*/false);
  auto symtabInit = builder.getAnonConstRecord(
      builder.getArrayAttr(
          {cir::IntAttr::get(longTy, 0), nullPtr,
           cir::IntAttr::get(i16Ty, objcDarwinFragileDefinedClasses.size()),
           cir::IntAttr::get(i16Ty, objcDarwinFragileDefinedCategories.size()),
           symbolArrayInit}),
      /*packed=*/false, /*padded=*/false, symtabTy);
  cir::GlobalOp symtabGV =
      createGlobalOp(*this, loc, "OBJC_SYMBOLS", symtabTy,
                     /*isConstant=*/false);
  symtabGV.setLinkage(cir::GlobalLinkageKind::PrivateLinkage);
  mlir::SymbolTable::setSymbolVisibility(symtabGV,
                                         mlir::SymbolTable::Visibility::Private);
  symtabGV.setAlignmentAttr(getSize(getPointerAlign()));
  symtabGV.setSectionAttr(
      getStringAttrCached("__OBJC,__symbols,regular,no_dead_strip"));
  setInitializer(symtabGV, symtabInit);
  setDSOLocal(static_cast<mlir::Operation *>(symtabGV));
  addCompilerUsedGlobal(symtabGV);

  auto moduleTy = builder.getAnonRecordTy(
      {longTy, longTy, voidPtrTy, voidPtrTy}, /*packed=*/false,
      /*padded=*/false);
  auto moduleInit = builder.getAnonConstRecord(
      builder.getArrayAttr(
          {cir::IntAttr::get(longTy, 7),
           cir::IntAttr::get(longTy, moduleSize),
           builder.getGlobalViewAttr(voidPtrTy, getObjCClassNameGlobal("")),
           builder.getGlobalViewAttr(voidPtrTy, symtabGV)}),
      /*packed=*/false, /*padded=*/false, moduleTy);
  cir::GlobalOp moduleGV =
      createGlobalOp(*this, loc, "OBJC_MODULES", moduleTy,
                     /*isConstant=*/false);
  moduleGV.setLinkage(cir::GlobalLinkageKind::PrivateLinkage);
  mlir::SymbolTable::setSymbolVisibility(moduleGV,
                                         mlir::SymbolTable::Visibility::Private);
  moduleGV.setAlignmentAttr(getSize(getPointerAlign()));
  moduleGV.setSectionAttr(
      getStringAttrCached("__OBJC,__module_info,regular,no_dead_strip"));
  setInitializer(moduleGV, moduleInit);
  setDSOLocal(static_cast<mlir::Operation *>(moduleGV));
  addCompilerUsedGlobal(moduleGV);
}

void CIRGenModule::emitObjCDarwinModuleInfo(mlir::Location loc) {
  assert(usesDarwinNonFragileObjCRuntime(*this) &&
         "Darwin ObjC module metadata currently requires the NeXT "
         "non-fragile runtime");

  auto emitGlobalList =
      [&](llvm::StringRef symbolName, llvm::StringRef sectionName,
          llvm::ArrayRef<cir::GlobalOp> globals) {
        if (globals.empty())
          return;

        llvm::SmallVector<mlir::Attribute, 8> globalAttrs;
        globalAttrs.reserve(globals.size());
        for (cir::GlobalOp gv : globals)
          globalAttrs.push_back(builder.getGlobalViewAttr(voidPtrTy, gv));

        auto listTy = cir::ArrayType::get(voidPtrTy, globalAttrs.size());
        auto listInit = cir::ConstArrayAttr::get(
            listTy, builder.getArrayAttr(globalAttrs));
        cir::GlobalOp gv =
            createGlobalOp(*this, loc, symbolName, listTy, /*isConstant=*/false);
        gv.setLinkage(cir::GlobalLinkageKind::PrivateLinkage);
        mlir::SymbolTable::setSymbolVisibility(
            gv, mlir::SymbolTable::Visibility::Private);
        gv.setAlignmentAttr(getSize(getPointerAlign()));
        gv.setSectionAttr(getStringAttrCached(sectionName));
        setInitializer(gv, listInit);
        setDSOLocal(static_cast<mlir::Operation *>(gv));
        addCompilerUsedGlobal(gv);
      };

  if (!objcDarwinDefinedClasses.empty())
    emitGlobalList("OBJC_LABEL_CLASS_$",
                   getObjCSectionName(*this, "__objc_classlist",
                                      "regular,no_dead_strip"),
                   objcDarwinDefinedClasses);
  if (!objcDarwinDefinedNonLazyClasses.empty())
    emitGlobalList("OBJC_LABEL_NONLAZY_CLASS_$",
                   getObjCSectionName(*this, "__objc_nlclslist",
                                      "regular,no_dead_strip"),
                   objcDarwinDefinedNonLazyClasses);
  if (!objcDarwinDefinedCategories.empty())
    emitGlobalList("OBJC_LABEL_CATEGORY_$",
                   getObjCSectionName(*this, "__objc_catlist",
                                      "regular,no_dead_strip"),
                   objcDarwinDefinedCategories);
  if (!objcDarwinDefinedStubCategories.empty())
    emitGlobalList("OBJC_LABEL_STUB_CATEGORY_$",
                   getObjCSectionName(*this, "__objc_catlist2",
                                      "regular,no_dead_strip"),
                   objcDarwinDefinedStubCategories);
  if (!objcDarwinDefinedNonLazyCategories.empty())
    emitGlobalList("OBJC_LABEL_NONLAZY_CATEGORY_$",
                   getObjCSectionName(*this, "__objc_nlcatlist",
                                      "regular,no_dead_strip"),
                   objcDarwinDefinedNonLazyCategories);
}

void CIRGenModule::emitObjCGNUModuleInit(mlir::Location loc) {
  assert(usesGNUstep2ObjCRuntime(*this) &&
         "GNU ObjC module init currently requires GNUstep v2");
  if (getGlobalValue(".objc_init"))
    return;

  const bool isCOFF = getTriple().isOSBinFormatCOFF();

  auto hasGlobalInSection = [&](llvm::StringRef sectionName) {
    for (cir::GlobalOp gv : getModule().getOps<cir::GlobalOp>()) {
      if (auto section = gv.getSectionAttr();
          section && section.getValue() == sectionName)
        return true;
    }
    return false;
  };
  // Match OGCG: GNUstep2 always materializes the module bootstrap, even for
  // TUs that only use ObjC EH and do not otherwise contribute section payloads.

  auto i32Ty = builder.getSInt32Ty();
  auto initVersionTy = builder.getSInt64Ty();
  auto fnTy = cir::FuncType::get({}, voidTy);
  mlir::Attribute nullPtr = builder.getConstPtrAttr(voidPtrTy, 0);

  auto createHiddenExternalPtrGlobal = [&](llvm::StringRef name) {
    cir::GlobalOp gv =
        createGlobalOp(*this, loc, name, voidPtrTy, /*isConstant=*/false);
    gv.setLinkage(cir::GlobalLinkageKind::ExternalLinkage);
    gv.setGlobalVisibility(cir::VisibilityKind::Hidden);
    mlir::SymbolTable::setSymbolVisibility(gv, getMLIRVisibility(gv));
    return gv;
  };

  auto sentinelTy = builder.getAnonRecordTy({}, /*packed=*/false,
                                            /*padded=*/false);
  auto sentinelInit = builder.getZeroInitAttr(sentinelTy);
  auto createHiddenSectionSentinelGlobal = [&](llvm::StringRef name,
                                               llvm::StringRef sectionName) {
    cir::GlobalOp gv =
        createGlobalOp(*this, loc, name, sentinelTy, /*isConstant=*/false);
    gv.setLinkage(cir::GlobalLinkageKind::LinkOnceODRLinkage);
    gv.setGlobalVisibility(cir::VisibilityKind::Hidden);
    mlir::SymbolTable::setSymbolVisibility(gv, getMLIRVisibility(gv));
    gv.setAlignmentAttr(getSize(getPointerAlign()));
    gv.setSectionAttr(getStringAttrCached(sectionName));
    if (supportsCOMDAT())
      gv.setComdat(true);
    setInitializer(gv, sentinelInit);
    return gv;
  };

  llvm::SmallVector<mlir::Attribute, 17> initFields;
  initFields.push_back(cir::IntAttr::get(initVersionTy, 0));
  for (llvm::StringRef sectionBaseName : getGNUstep2ObjCSectionBaseNames(*this)) {
    if (isCOFF) {
      initFields.push_back(builder.getGlobalViewAttr(
          voidPtrTy,
          createHiddenSectionSentinelGlobal(
              (llvm::Twine("__start_") + sectionBaseName).str(),
              (llvm::Twine(sectionBaseName) + "$a").str())));
      initFields.push_back(builder.getGlobalViewAttr(
          voidPtrTy,
          createHiddenSectionSentinelGlobal(
              (llvm::Twine("__stop") + sectionBaseName).str(),
              (llvm::Twine(sectionBaseName) + "$z").str())));
      continue;
    }

    initFields.push_back(builder.getGlobalViewAttr(
        voidPtrTy, createHiddenExternalPtrGlobal(
                       (llvm::Twine("__start_") + sectionBaseName).str())));
    initFields.push_back(builder.getGlobalViewAttr(
        voidPtrTy, createHiddenExternalPtrGlobal(
                       (llvm::Twine("__stop_") + sectionBaseName).str())));
  }

  llvm::SmallVector<mlir::Type, 17> initFieldTypes(1, initVersionTy);
  initFieldTypes.resize(initFields.size(), voidPtrTy);
  initFieldTypes.front() = initVersionTy;
  auto initTy = builder.getAnonRecordTy(initFieldTypes, /*packed=*/false,
                                        /*padded=*/false);
  auto initAttr = builder.getAnonConstRecord(builder.getArrayAttr(initFields),
                                             /*packed=*/false,
                                             /*padded=*/false, initTy);
  cir::GlobalOp initGV =
      createGlobalOp(*this, loc, ".objc_init", initTy, /*isConstant=*/false);
  initGV.setLinkage(cir::GlobalLinkageKind::LinkOnceODRLinkage);
  initGV.setGlobalVisibility(cir::VisibilityKind::Hidden);
  mlir::SymbolTable::setSymbolVisibility(initGV, getMLIRVisibility(initGV));
  initGV.setAlignmentAttr(getSize(getPointerAlign()));
  if (supportsCOMDAT())
    initGV.setComdat(true);
  setInitializer(initGV, initAttr);

  cir::FuncOp loadFn = createCIRFunction(loc, ".objcv2_load_function", fnTy,
                                         /*funcDecl=*/nullptr);
  loadFn.setLinkage(cir::GlobalLinkageKind::LinkOnceODRLinkage);
  loadFn.setGlobalVisibility(cir::VisibilityKind::Hidden);
  mlir::SymbolTable::setSymbolVisibility(
      loadFn, getMLIRVisibilityFromCIRLinkage(loadFn.getLinkage()));
  if (supportsCOMDAT())
    loadFn.setComdat(true);
  setDSOLocal(loadFn.getOperation());
  if (loadFn.isDeclaration()) {
    mlir::Block *entry = loadFn.addEntryBlock();
    mlir::OpBuilder::InsertionGuard guard(builder);
    builder.setInsertionPointToStart(entry);
    auto initPtrTy =
        builder.getPointerTo(initGV.getSymType(), initGV.getAddrSpaceAttr());
    cir::FuncType objcLoadTy = cir::FuncType::get({initPtrTy}, voidTy);
    cir::FuncOp objcLoadFn = createRuntimeFunction(objcLoadTy, "__objc_load");
    builder.createCallOp(loc, objcLoadFn, builder.createGetGlobal(loc, initGV));
    cir::ReturnOp::create(builder, loc);
  }
  addCompilerUsedGlobal(loadFn);

  auto loadFnPtrTy = builder.getPointerTo(loadFn.getFunctionType());
  cir::GlobalOp ctorGV = createGlobalOp(*this, loc, ".objc_ctor", loadFnPtrTy,
                                        /*isConstant=*/false);
  ctorGV.setLinkage(cir::GlobalLinkageKind::LinkOnceAnyLinkage);
  ctorGV.setGlobalVisibility(cir::VisibilityKind::Hidden);
  mlir::SymbolTable::setSymbolVisibility(ctorGV, getMLIRVisibility(ctorGV));
  ctorGV.setAlignmentAttr(getSize(getPointerAlign()));
  if (isCOFF) {
    ctorGV.setSectionAttr(getStringAttrCached(".CRT$XCLz"));
  } else {
    ctorGV.setSectionAttr(getStringAttrCached(codeGenOpts.UseInitArray
                                                  ? ".init_array"
                                                  : ".ctors"));
  }
  if (supportsCOMDAT())
    ctorGV.setComdat(true);
  setInitializer(ctorGV, cir::GlobalViewAttr::get(
                             loadFnPtrTy,
                             mlir::FlatSymbolRefAttr::get(loadFn.getSymNameAttr())));
  addUsedGlobal(ctorGV);

  if (isCOFF && !gnustep2COFFEarlyInitEntries.empty()) {
    cir::FuncOp earlyInitFn =
        createCIRFunction(loc, ".objc_early_init", fnTy, /*funcDecl=*/nullptr);
    earlyInitFn.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
    mlir::SymbolTable::setSymbolVisibility(
        earlyInitFn, mlir::SymbolTable::Visibility::Private);
    if (earlyInitFn.isDeclaration()) {
      mlir::Block *entry = earlyInitFn.addEntryBlock();
      mlir::OpBuilder::InsertionGuard guard(builder);
      builder.setInsertionPointToStart(entry);
      auto fieldPtrTy = builder.getPointerTo(voidPtrTy);
      for (const GNUstep2COFFEarlyInitEntry &initEntry :
           gnustep2COFFEarlyInitEntries) {
        mlir::Value fieldPtr = builder.createGetMember(
            loc, fieldPtrTy, builder.createGetGlobal(loc, initEntry.destGlobal),
            /*name=*/"", initEntry.destFieldIndex);
        mlir::Value sourceAddr = builder.createBitcast(
            loc, builder.createGetGlobal(loc, initEntry.sourceGlobal), voidPtrTy);
        builder.createStore(
            loc, sourceAddr,
            Address(fieldPtr, voidPtrTy, getPointerAlign()));
      }
      cir::ReturnOp::create(builder, loc);
    }

    auto earlyInitFnPtrTy = builder.getPointerTo(earlyInitFn.getFunctionType());
    cir::GlobalOp earlyInitPtrGV = createGlobalOp(
        *this, loc, ".objc_early_init_ptr", earlyInitFnPtrTy,
        /*isConstant=*/true);
    earlyInitPtrGV.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
    mlir::SymbolTable::setSymbolVisibility(
        earlyInitPtrGV, mlir::SymbolTable::Visibility::Private);
    earlyInitPtrGV.setSectionAttr(getStringAttrCached(".CRT$XCLb"));
    setInitializer(earlyInitPtrGV,
                   cir::GlobalViewAttr::get(
                       earlyInitFnPtrTy,
                       mlir::FlatSymbolRefAttr::get(
                           earlyInitFn.getSymNameAttr())));
    addUsedGlobal(earlyInitPtrGV);
  }

  auto createNullGlobal = [&](llvm::StringRef name, mlir::Type ty,
                              mlir::Attribute init,
                              llvm::StringRef sectionName) {
    cir::GlobalOp gv =
        createGlobalOp(*this, loc, name, ty, /*isConstant=*/false);
    gv.setLinkage(cir::GlobalLinkageKind::LinkOnceODRLinkage);
    gv.setGlobalVisibility(cir::VisibilityKind::Hidden);
    mlir::SymbolTable::setSymbolVisibility(gv, getMLIRVisibility(gv));
    gv.setAlignmentAttr(getSize(getPointerAlign()));
    gv.setSectionAttr(getStringAttrCached(sectionName));
    if (supportsCOMDAT())
      gv.setComdat(true);
    setInitializer(gv, init);
    addUsedGlobal(gv);
  };

  if (!isCOFF) {
    auto selectorTy =
        builder.getAnonRecordTy({voidPtrTy, voidPtrTy}, /*packed=*/false,
                                /*padded=*/false);
    createNullGlobal(".objc_null_selector", selectorTy,
                     builder.getAnonConstRecord(builder.getArrayAttr(
                         {nullPtr, nullPtr}),
                                                /*packed=*/false,
                                                /*padded=*/false, selectorTy),
                     getGNUstep2ObjCSectionName(*this, GNUstep2SelectorSection));

    if (!hasGlobalInSection(
            getGNUstep2ObjCSectionName(*this, GNUstep2CategorySection))) {
      auto categoryTy = builder.getAnonRecordTy(
          {voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy,
           voidPtrTy},
          /*packed=*/false, /*padded=*/false);
      createNullGlobal(".objc_null_category", categoryTy,
                       builder.getAnonConstRecord(
                           builder.getArrayAttr(
                               {nullPtr, nullPtr, nullPtr, nullPtr, nullPtr,
                                nullPtr, nullPtr}),
                           /*packed=*/false, /*padded=*/false, categoryTy),
                       getGNUstep2ObjCSectionName(*this,
                                                  GNUstep2CategorySection));
    }
    if (!hasGlobalInSection(
            getGNUstep2ObjCSectionName(*this, GNUstep2ClassSection))) {
      auto classInitRefTy =
          builder.getAnonRecordTy({voidPtrTy}, /*packed=*/false,
                                  /*padded=*/false);
      createNullGlobal(".objc_null_cls_init_ref", classInitRefTy,
                       builder.getAnonConstRecord(builder.getArrayAttr({nullPtr}),
                                                  /*packed=*/false,
                                                  /*padded=*/false,
                                                  classInitRefTy),
                       getGNUstep2ObjCSectionName(*this,
                                                  GNUstep2ClassSection));
    }
    if (!hasGlobalInSection(getGNUstep2ObjCSectionName(
            *this, GNUstep2ClassReferenceSection))) {
      auto classRefTy =
          builder.getAnonRecordTy({voidPtrTy, voidPtrTy}, /*packed=*/false,
                                  /*padded=*/false);
      createNullGlobal(".objc_null_class_ref", classRefTy,
                       builder.getAnonConstRecord(
                           builder.getArrayAttr({nullPtr, nullPtr}),
                           /*packed=*/false, /*padded=*/false, classRefTy),
                       getGNUstep2ObjCSectionName(*this,
                                                  GNUstep2ClassReferenceSection));
    }
    if (!hasGlobalInSection(
            getGNUstep2ObjCSectionName(*this, GNUstep2ProtocolSection))) {
      auto protocolTy = builder.getAnonRecordTy(
          {voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy,
           voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy},
          /*packed=*/false, /*padded=*/false);
      createNullGlobal(".objc_null_protocol", protocolTy,
                       builder.getAnonConstRecord(
                           builder.getArrayAttr(
                               {nullPtr, nullPtr, nullPtr, nullPtr, nullPtr,
                                nullPtr, nullPtr, nullPtr, nullPtr, nullPtr,
                                nullPtr}),
                           /*packed=*/false, /*padded=*/false, protocolTy),
                       getGNUstep2ObjCSectionName(*this,
                                                  GNUstep2ProtocolSection));
    }
    if (!hasGlobalInSection(getGNUstep2ObjCSectionName(
            *this, GNUstep2ProtocolReferenceSection))) {
      auto protocolRefTy =
          builder.getAnonRecordTy({voidPtrTy}, /*packed=*/false,
                                  /*padded=*/false);
      createNullGlobal(".objc_null_protocol_ref", protocolRefTy,
                       builder.getAnonConstRecord(builder.getArrayAttr({nullPtr}),
                                                  /*packed=*/false,
                                                  /*padded=*/false,
                                                  protocolRefTy),
                       getGNUstep2ObjCSectionName(*this,
                                                  GNUstep2ProtocolReferenceSection));
    }
    if (!hasGlobalInSection(
            getGNUstep2ObjCSectionName(*this, GNUstep2ClassAliasSection))) {
      auto classAliasTy =
          builder.getAnonRecordTy({voidPtrTy, voidPtrTy}, /*packed=*/false,
                                  /*padded=*/false);
      createNullGlobal(".objc_null_class_alias", classAliasTy,
                       builder.getAnonConstRecord(
                           builder.getArrayAttr({nullPtr, nullPtr}),
                           /*packed=*/false, /*padded=*/false, classAliasTy),
                       getGNUstep2ObjCSectionName(*this,
                                                  GNUstep2ClassAliasSection));
    }
    if (!hasGlobalInSection(getGNUstep2ObjCSectionName(
            *this, GNUstep2ConstantStringSection))) {
      auto constantStringTy = builder.getAnonRecordTy(
          {voidPtrTy, i32Ty, i32Ty, i32Ty, i32Ty, voidPtrTy},
          /*packed=*/false, /*padded=*/false);
      createNullGlobal(
          ".objc_null_constant_string", constantStringTy,
          builder.getAnonConstRecord(
              builder.getArrayAttr(
                  {nullPtr, cir::IntAttr::get(i32Ty, 0),
                   cir::IntAttr::get(i32Ty, 0), cir::IntAttr::get(i32Ty, 0),
                   cir::IntAttr::get(i32Ty, 0), nullPtr}),
              /*packed=*/false, /*padded=*/false, constantStringTy),
          getGNUstep2ObjCSectionName(*this, GNUstep2ConstantStringSection));
    }
  }
}

cir::GlobalOp CIRGenModule::getObjCClassSymbol(mlir::Location loc,
                                               const ObjCInterfaceDecl *id) {
  assert((usesDarwinNonFragileObjCRuntime(*this)) ||
         getLangOpts().ObjCRuntime.getKind() == ObjCRuntime::ObjFW);
  assert(id && "expected Objective-C interface");

  std::string runtimeName = id->getObjCRuntimeNameAsString().str();
  std::string name =
      getLangOpts().ObjCRuntime.getKind() == ObjCRuntime::ObjFW
          ? ("_OBJC_CLASS_" + runtimeName)
          : ("OBJC_CLASS_$_" + runtimeName);
  if (auto gv = mlir::dyn_cast_or_null<cir::GlobalOp>(getGlobalValue(name))) {
    if (id->isWeakImported() && gv.isDeclaration())
      gv.setLinkage(cir::GlobalLinkageKind::ExternalWeakLinkage);
    gv.setConstant(false);
    setObjCClassSymbolDLLStorageClass(*this, gv, id);
    return gv;
  }
  auto linkage = id->isWeakImported()
                     ? cir::GlobalLinkageKind::ExternalWeakLinkage
                     : cir::GlobalLinkageKind::ExternalLinkage;
  cir::GlobalOp gv = createOrReplaceCXXRuntimeVariable(
      loc, name, uInt8Ty, linkage, CharUnits::Zero());
  gv.setConstant(false);
  setObjCClassSymbolDLLStorageClass(*this, gv, id);
  return gv;
}

cir::GlobalOp CIRGenModule::getObjCClassRefGlobal(mlir::Location loc,
                                                  const ObjCInterfaceDecl *id) {
  const bool isGNUstep2 = usesGNUstep2ObjCRuntime(*this);
  const bool isDarwinFragile = usesDarwinFragileObjCRuntime(*this);
  assert((usesDarwinNonFragileObjCRuntime(*this) || isDarwinFragile ||
          isGNUstep2) &&
         "ObjC class refs currently require a Darwin Objective-C runtime or "
         "GNUstep v2");
  assert(id && "expected Objective-C interface");

  if (auto it = objcClassRefGlobals.find(id); it != objcClassRefGlobals.end())
    return it->second;

  if (isGNUstep2) {
    std::string name = mangleGNUObjCPublicSymbol(
        *this, "OBJC_REF_CLASS_", id->getObjCRuntimeNameAsString());
    cir::GlobalOp gv = createOrReplaceCXXRuntimeVariable(
        loc, name, voidPtrTy, cir::GlobalLinkageKind::ExternalLinkage,
        CharUnits::Zero());
    gv.setConstant(false);
    setGNUstep2ClassDLLStorageClass(*this, gv, id);
    objcClassRefGlobals[id] = gv;
    return gv;
  }

  if (usesDarwinFragileObjCRuntime(*this)) {
    cir::GlobalOp classNameGV =
        getObjCClassNameGlobal(id->getObjCRuntimeNameAsString());
    cir::PointerType classNamePtrTy =
        builder.getPointerTo(classNameGV.getSymType(), classNameGV.getAddrSpaceAttr());

    cir::GlobalOp gv = createGlobalOp(
        *this, loc, getUniqueGlobalName("OBJC_CLASS_REFERENCES_"),
        classNamePtrTy, /*isConstant=*/false);
    gv.setLinkage(cir::GlobalLinkageKind::PrivateLinkage);
    mlir::SymbolTable::setSymbolVisibility(
        gv, mlir::SymbolTable::Visibility::Private);
    gv.setAlignmentAttr(getSize(getPointerAlign()));
    gv.setSectionAttr(
        getStringAttrCached("__OBJC,__cls_refs,literal_pointers,no_dead_strip"));
    setInitializer(gv, builder.getGlobalViewAttr(classNamePtrTy, classNameGV));
    setDSOLocal(static_cast<mlir::Operation *>(gv));
    addCompilerUsedGlobal(gv);
    objcClassRefGlobals[id] = gv;
    return gv;
  }

  cir::GlobalOp classGV = getObjCClassSymbol(loc, id);
  cir::PointerType classPtrTy =
      builder.getPointerTo(classGV.getSymType(), classGV.getAddrSpaceAttr());

  cir::GlobalOp gv = createGlobalOp(
      *this, loc, getUniqueGlobalName("OBJC_CLASSLIST_REFERENCES_$_"),
      classPtrTy, /*isConstant=*/false);
  std::string sectionName =
      getObjCSectionName(*this, "__objc_classrefs", "regular,no_dead_strip");
  gv.setLinkage(getObjCMetadataLinkage(*this, sectionName));
  mlir::SymbolTable::setSymbolVisibility(gv,
                                         mlir::SymbolTable::Visibility::Private);
  gv.setAlignmentAttr(getSize(getPointerAlign()));
  gv.setSectionAttr(getStringAttrCached(sectionName));
  auto classView = builder.getGlobalViewAttr(classPtrTy, classGV);
  if (id->hasAttr<ObjCClassStubAttr>()) {
    classView = cir::GlobalViewAttr::get(
        classPtrTy, classView.getSymbol(), classView.getIndices(),
        builder.getI64IntegerAttr(1));
  }
  setInitializer(gv, classView);
  setDSOLocal(static_cast<mlir::Operation *>(gv));
  addCompilerUsedGlobal(gv);

  objcClassRefGlobals[id] = gv;
  return gv;
}

cir::GlobalOp CIRGenModule::getObjCMetaClassSymbol(mlir::Location loc,
                                                   const ObjCInterfaceDecl *id) {
  assert(usesDarwinNonFragileObjCRuntime(*this) &&
         "ObjC metaclass symbols currently require the Next non-fragile "
         "runtime");
  assert(id && "expected Objective-C interface");

  std::string name =
      ("OBJC_METACLASS_$_" + id->getObjCRuntimeNameAsString()).str();
  if (auto gv = mlir::dyn_cast_or_null<cir::GlobalOp>(getGlobalValue(name))) {
    if (id->isWeakImported() && gv.isDeclaration())
      gv.setLinkage(cir::GlobalLinkageKind::ExternalWeakLinkage);
    gv.setConstant(false);
    setObjCClassSymbolDLLStorageClass(*this, gv, id);
    return gv;
  }
  auto linkage = id->isWeakImported()
                     ? cir::GlobalLinkageKind::ExternalWeakLinkage
                     : cir::GlobalLinkageKind::ExternalLinkage;
  cir::GlobalOp gv = createOrReplaceCXXRuntimeVariable(
      loc, name, uInt8Ty, linkage, CharUnits::Zero());
  gv.setConstant(false);
  setObjCClassSymbolDLLStorageClass(*this, gv, id);
  return gv;
}

cir::GlobalOp CIRGenModule::getObjCSuperClassRefGlobal(mlir::Location loc,
                                                       const ObjCInterfaceDecl *id,
                                                       bool isClassMessage) {
  assert(usesDarwinNonFragileObjCRuntime(*this) &&
         "ObjC super refs currently require the NeXT non-fragile runtime");
  assert(id && "expected Objective-C interface");

  auto &cache =
      isClassMessage ? objcSuperMetaClassRefGlobals : objcSuperClassRefGlobals;
  if (auto it = cache.find(id); it != cache.end())
    return it->second;

  cir::GlobalOp classGV = isClassMessage ? getObjCMetaClassSymbol(loc, id)
                                         : getObjCClassSymbol(loc, id);
  cir::PointerType classPtrTy =
      builder.getPointerTo(classGV.getSymType(), classGV.getAddrSpaceAttr());

  cir::GlobalOp gv = createGlobalOp(
      *this, loc, getUniqueGlobalName("OBJC_CLASSLIST_SUP_REFS_$_"),
      classPtrTy, /*isConstant=*/false);
  std::string sectionName =
      getObjCSectionName(*this, "__objc_superrefs", "regular,no_dead_strip");
  gv.setLinkage(cir::GlobalLinkageKind::PrivateLinkage);
  mlir::SymbolTable::setSymbolVisibility(gv,
                                         mlir::SymbolTable::Visibility::Private);
  gv.setAlignmentAttr(getSize(getPointerAlign()));
  gv.setSectionAttr(getStringAttrCached(sectionName));
  auto classView = builder.getGlobalViewAttr(classPtrTy, classGV);
  if (!isClassMessage && id->hasAttr<ObjCClassStubAttr>()) {
    classView = cir::GlobalViewAttr::get(
        classPtrTy, classView.getSymbol(), classView.getIndices(),
        builder.getI64IntegerAttr(1));
  }
  setInitializer(gv, classView);
  setDSOLocal(static_cast<mlir::Operation *>(gv));
  addCompilerUsedGlobal(gv);

  cache[id] = gv;
  return gv;
}

mlir::Value CIRGenModule::getObjCClass(CIRGenFunction &cgf,
                                       const ObjCInterfaceDecl *id,
                                       mlir::Location loc) {
  if (usesDarwinObjCRuntime(*this) && id->hasAttr<ObjCRuntimeVisibleAttr>()) {
    cir::GlobalOp classNameGV =
        getGlobalForConstantCString(loc, id->getObjCRuntimeNameAsString());
    mlir::Value classNameAddr = builder.createGetGlobal(loc, classNameGV);
    if (classNameAddr.getType() != voidPtrTy)
      classNameAddr = builder.createBitcast(loc, classNameAddr, voidPtrTy);

    cir::FuncType fnTy = cir::FuncType::get({voidPtrTy}, voidPtrTy);
    cir::FuncOp lookupClassFn = createRuntimeFunction(fnTy, "objc_lookUpClass");
    cir::CallOp call = builder.createCallOp(loc, lookupClassFn, {classNameAddr});
    call->setAttr("nothrow", mlir::UnitAttr::get(&getMLIRContext()));
    return call.getResult();
  }

  cir::GlobalOp classRefGV = getObjCClassRefGlobal(loc, id);
  mlir::Value classRefAddr = builder.createGetGlobal(loc, classRefGV);
  const bool isDarwinMachO =
      getTriple().isOSDarwin() && getTriple().isOSBinFormatMachO();
  if (!isDarwinMachO) {
    assert((usesDarwinNonFragileObjCRuntime(*this) ||
            (getLangOpts().ObjCRuntime.getKind() == ObjCRuntime::GNUstep &&
             getLangOpts().ObjCRuntime.getVersion() >= VersionTuple(2))) &&
           "non-Darwin ObjC class refs currently require the Next "
           "non-fragile runtime or GNUstep v2");
    mlir::Value classRef = builder.createLoad(
        loc, Address(classRefAddr, classRefGV.getSymType(), getPointerAlign()));
    if (classRef.getType() != voidPtrTy)
      classRef = builder.createBitcast(loc, classRef, voidPtrTy);
    return classRef;
  }

  if (usesDarwinNonFragileObjCRuntime(*this) &&
      id->hasAttr<ObjCClassStubAttr>()) {
    cir::PointerType ptrToClassRefTy =
        builder.getPointerTo(classRefGV.getSymType(), classRefGV.getAddrSpaceAttr());
    cir::FuncType fnTy = cir::FuncType::get({ptrToClassRefTy}, voidPtrTy);
    cir::FuncOp loadClassrefFn = createRuntimeFunction(fnTy, "objc_loadClassref");
    loadClassrefFn.setLinkage(cir::GlobalLinkageKind::ExternalWeakLinkage);
    mlir::NamedAttrList attrs;
    if (auto extraAttrs = loadClassrefFn.getExtraAttrsAttr())
      attrs = mlir::NamedAttrList{extraAttrs.getElements().getValue()};
    attrs.set("nonlazybind", mlir::UnitAttr::get(&getMLIRContext()));
    attrs.set(cir::NoThrowAttr::getMnemonic(),
              cir::NoThrowAttr::get(&getMLIRContext()));
    attrs.set("side_effect", cir::SideEffectAttr::get(&getMLIRContext(),
                                                      cir::SideEffect::Const));
    loadClassrefFn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
        attrs.getDictionary(&getMLIRContext())));
    return builder.createCallOp(loc, loadClassrefFn, classRefAddr).getResult();
  }
  return builder.createLoad(
      loc, Address(classRefAddr, classRefGV.getSymType(), getPointerAlign()));
}

mlir::Value CIRGenModule::getObjCSuperClass(CIRGenFunction &cgf,
                                            const ObjCInterfaceDecl *id,
                                            mlir::Location loc,
                                            bool isClassMessage) {
  assert(usesDarwinNonFragileObjCRuntime(*this) &&
         "ObjC super refs currently require the NeXT non-fragile runtime");
  cir::GlobalOp classRefGV = getObjCSuperClassRefGlobal(loc, id, isClassMessage);
  mlir::Value classRefAddr = builder.createGetGlobal(loc, classRefGV);
  if (!isClassMessage && id->hasAttr<ObjCClassStubAttr>()) {
    cir::PointerType ptrToClassRefTy =
        builder.getPointerTo(classRefGV.getSymType(), classRefGV.getAddrSpaceAttr());
    cir::FuncType fnTy = cir::FuncType::get({ptrToClassRefTy}, voidPtrTy);
    cir::FuncOp loadClassrefFn = createRuntimeFunction(fnTy, "objc_loadClassref");
    loadClassrefFn.setLinkage(cir::GlobalLinkageKind::ExternalWeakLinkage);
    mlir::NamedAttrList attrs;
    if (auto extraAttrs = loadClassrefFn.getExtraAttrsAttr())
      attrs = mlir::NamedAttrList{extraAttrs.getElements().getValue()};
    attrs.set("nonlazybind", mlir::UnitAttr::get(&getMLIRContext()));
    attrs.set(cir::NoThrowAttr::getMnemonic(),
              cir::NoThrowAttr::get(&getMLIRContext()));
    attrs.set("side_effect", cir::SideEffectAttr::get(&getMLIRContext(),
                                                      cir::SideEffect::Const));
    loadClassrefFn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
        attrs.getDictionary(&getMLIRContext())));
    return builder.createCallOp(loc, loadClassrefFn, classRefAddr).getResult();
  }
  return builder.createLoad(
      loc, Address(classRefAddr, classRefGV.getSymType(), getPointerAlign()));
}

QualType CIRGenModule::getObjCFastEnumerationStateType() {
  if (objCFastEnumerationStateType.isNull()) {
    RecordDecl *record =
        astContext.buildImplicitRecord("__objcFastEnumerationState");
    record->startDefinition();

    QualType fieldTypes[] = {
        astContext.UnsignedLongTy,
        astContext.getPointerType(astContext.getObjCIdType()),
        astContext.getPointerType(astContext.UnsignedLongTy),
        astContext.getConstantArrayType(astContext.UnsignedLongTy,
                                        llvm::APInt(32, 5), nullptr,
                                        ArraySizeModifier::Normal, 0)};

    for (QualType fieldType : fieldTypes) {
      FieldDecl *field = FieldDecl::Create(
          astContext, record, SourceLocation(), SourceLocation(), nullptr,
          fieldType, /*TInfo=*/nullptr, /*BitWidth=*/nullptr,
          /*Mutable=*/false, ICIS_NoInit);
      field->setAccess(AS_public);
      record->addDecl(field);
    }

    record->completeDefinition();
    objCFastEnumerationStateType = astContext.getCanonicalTagType(record);
  }

  return objCFastEnumerationStateType;
}

cir::FuncOp CIRGenModule::getObjCEnumerationMutationFunc() {
  if (objcEnumerationMutationFunc)
    return objcEnumerationMutationFunc;

  cir::FuncType fnTy = cir::FuncType::get({voidPtrTy}, voidTy);
  objcEnumerationMutationFunc =
      createRuntimeFunction(fnTy, "objc_enumerationMutation");
  return objcEnumerationMutationFunc;
}

cir::FuncOp CIRGenModule::getObjCMsgSendFunc() {
  if (objcMsgSendFunc)
    return objcMsgSendFunc;

  cir::FuncType fnTy = cir::FuncType::get(
      {voidPtrTy, builder.getPointerTo(uInt8Ty)}, voidPtrTy,
      /*isVarArg=*/true);
  objcMsgSendFunc = createRuntimeFunction(fnTy, "objc_msgSend");
  if (usesDarwinObjCRuntime(*this)) {
    mlir::NamedAttrList attrs;
    if (auto extraAttrs = objcMsgSendFunc.getExtraAttrsAttr())
      attrs = mlir::NamedAttrList{extraAttrs.getElements().getValue()};
    attrs.set("nonlazybind", mlir::UnitAttr::get(&getMLIRContext()));
    objcMsgSendFunc.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
        attrs.getDictionary(&getMLIRContext())));
  }
  return objcMsgSendFunc;
}

cir::FuncOp CIRGenModule::getObjCMsgSendFpretFunc() {
  if (objcMsgSendFpretFunc)
    return objcMsgSendFpretFunc;

  cir::FuncType fnTy = cir::FuncType::get(
      {voidPtrTy, builder.getPointerTo(uInt8Ty)}, doubleTy,
      /*isVarArg=*/true);
  objcMsgSendFpretFunc = createRuntimeFunction(fnTy, "objc_msgSend_fpret");
  return objcMsgSendFpretFunc;
}

cir::FuncOp CIRGenModule::getObjCMsgSendFp2retFunc() {
  if (objcMsgSendFp2retFunc)
    return objcMsgSendFp2retFunc;

  mlir::Type longDoubleTy =
      builder.getLongDoubleTy(astContext.getFloatTypeSemantics(astContext.LongDoubleTy));
  cir::ComplexType fp2RetTy = cir::ComplexType::get(longDoubleTy);
  cir::FuncType fnTy = cir::FuncType::get(
      {voidPtrTy, builder.getPointerTo(uInt8Ty)}, fp2RetTy,
      /*isVarArg=*/true);
  objcMsgSendFp2retFunc = createRuntimeFunction(fnTy, "objc_msgSend_fp2ret");
  return objcMsgSendFp2retFunc;
}

cir::FuncOp CIRGenModule::getObjCMsgSendStretFunc() {
  if (objcMsgSendStretFunc)
    return objcMsgSendStretFunc;

  cir::FuncType fnTy = cir::FuncType::get(
      {voidPtrTy, builder.getPointerTo(uInt8Ty)}, voidTy,
      /*isVarArg=*/true);
  objcMsgSendStretFunc = createRuntimeFunction(fnTy, "objc_msgSend_stret");
  return objcMsgSendStretFunc;
}

cir::FuncOp CIRGenModule::getObjCMsgSendStret2Func() {
  if (objcMsgSendStret2Func)
    return objcMsgSendStret2Func;

  cir::FuncType fnTy = cir::FuncType::get(
      {voidPtrTy, builder.getPointerTo(uInt8Ty)}, voidTy,
      /*isVarArg=*/true);
  objcMsgSendStret2Func = createRuntimeFunction(fnTy, "objc_msgSend_stret2");
  return objcMsgSendStret2Func;
}

cir::FuncOp CIRGenModule::getObjCMsgSendSuperFunc() {
  if (objcMsgSendSuperFunc)
    return objcMsgSendSuperFunc;

  auto objCSuperTy = builder.getAnonRecordTy({voidPtrTy, voidPtrTy});
  cir::FuncType fnTy = cir::FuncType::get(
      {builder.getPointerTo(objCSuperTy), builder.getPointerTo(uInt8Ty)},
      voidPtrTy, /*isVarArg=*/true);
  objcMsgSendSuperFunc = createRuntimeFunction(fnTy, "objc_msgSendSuper");
  return objcMsgSendSuperFunc;
}

cir::FuncOp CIRGenModule::getObjCMsgSendSuperStretFunc() {
  if (objcMsgSendSuperStretFunc)
    return objcMsgSendSuperStretFunc;

  auto objCSuperTy = builder.getAnonRecordTy({voidPtrTy, voidPtrTy});
  cir::FuncType fnTy = cir::FuncType::get(
      {voidPtrTy, builder.getPointerTo(objCSuperTy),
       builder.getPointerTo(uInt8Ty)},
      voidTy, /*isVarArg=*/true);
  objcMsgSendSuperStretFunc =
      createRuntimeFunction(fnTy, "objc_msgSendSuper_stret");
  return objcMsgSendSuperStretFunc;
}

cir::FuncOp CIRGenModule::getObjCMsgSendSuper2Func() {
  if (objcMsgSendSuper2Func)
    return objcMsgSendSuper2Func;

  auto objCSuperTy = builder.getAnonRecordTy({voidPtrTy, voidPtrTy});
  cir::FuncType fnTy = cir::FuncType::get(
      {builder.getPointerTo(objCSuperTy), builder.getPointerTo(uInt8Ty)},
      voidPtrTy, /*isVarArg=*/true);
  objcMsgSendSuper2Func = createRuntimeFunction(fnTy, "objc_msgSendSuper2");
  return objcMsgSendSuper2Func;
}

cir::FuncOp CIRGenModule::getObjCMsgSendSuper2StretFunc() {
  if (objcMsgSendSuper2StretFunc)
    return objcMsgSendSuper2StretFunc;

  auto objCSuperTy = builder.getAnonRecordTy({voidPtrTy, voidPtrTy});
  cir::FuncType fnTy = cir::FuncType::get(
      {voidPtrTy, builder.getPointerTo(objCSuperTy), builder.getPointerTo(uInt8Ty)},
      voidTy, /*isVarArg=*/true);
  objcMsgSendSuper2StretFunc =
      createRuntimeFunction(fnTy, "objc_msgSendSuper2_stret");
  return objcMsgSendSuper2StretFunc;
}

cir::FuncOp CIRGenModule::getObjCMsgLookupFunc() {
  if (objcMsgLookupFunc)
    return objcMsgLookupFunc;

  auto selectorTy =
      mlir::cast<cir::PointerType>(convertType(getASTContext().getObjCSelType()));
  cir::FuncType fnTy =
      cir::FuncType::get({voidPtrTy, selectorTy}, voidPtrTy, /*isVarArg=*/false);
  objcMsgLookupFunc = createRuntimeFunction(fnTy, "objc_msg_lookup");
  return objcMsgLookupFunc;
}

cir::FuncOp CIRGenModule::getObjCMsgLookupStretFunc() {
  if (objcMsgLookupStretFunc)
    return objcMsgLookupStretFunc;

  auto selectorTy =
      mlir::cast<cir::PointerType>(convertType(getASTContext().getObjCSelType()));
  cir::FuncType fnTy =
      cir::FuncType::get({voidPtrTy, selectorTy}, voidPtrTy, /*isVarArg=*/false);
  objcMsgLookupStretFunc =
      createRuntimeFunction(fnTy, "objc_msg_lookup_stret");
  return objcMsgLookupStretFunc;
}

cir::FuncOp CIRGenModule::getObjCMsgLookupSuperFunc() {
  if (objcMsgLookupSuperFunc)
    return objcMsgLookupSuperFunc;

  auto objCSuperTy = builder.getAnonRecordTy({voidPtrTy, voidPtrTy});
  auto selectorTy =
      mlir::cast<cir::PointerType>(convertType(getASTContext().getObjCSelType()));
  cir::FuncType fnTy = cir::FuncType::get(
      {builder.getPointerTo(objCSuperTy), selectorTy},
      voidPtrTy, /*isVarArg=*/false);
  objcMsgLookupSuperFunc =
      createRuntimeFunction(fnTy, "objc_msg_lookup_super");
  return objcMsgLookupSuperFunc;
}

cir::FuncOp CIRGenModule::getObjCMsgLookupSuperStretFunc() {
  if (objcMsgLookupSuperStretFunc)
    return objcMsgLookupSuperStretFunc;

  auto objCSuperTy = builder.getAnonRecordTy({voidPtrTy, voidPtrTy});
  auto selectorTy =
      mlir::cast<cir::PointerType>(convertType(getASTContext().getObjCSelType()));
  cir::FuncType fnTy = cir::FuncType::get(
      {builder.getPointerTo(objCSuperTy), selectorTy},
      voidPtrTy, /*isVarArg=*/false);
  objcMsgLookupSuperStretFunc =
      createRuntimeFunction(fnTy, "objc_msg_lookup_super_stret");
  return objcMsgLookupSuperStretFunc;
}

cir::FuncOp CIRGenModule::getObjCMsgLookupSenderFunc() {
  if (objcMsgLookupSenderFunc)
    return objcMsgLookupSenderFunc;

  auto slotTy = builder.getAnonRecordTy(
      {voidPtrTy, voidPtrTy, voidPtrTy, builder.getSInt32Ty(), voidPtrTy});
  auto selectorTy =
      mlir::cast<cir::PointerType>(convertType(getASTContext().getObjCSelType()));
  cir::FuncType fnTy = cir::FuncType::get(
      {builder.getPointerTo(voidPtrTy), selectorTy, voidPtrTy},
      builder.getPointerTo(slotTy), /*isVarArg=*/false);
  objcMsgLookupSenderFunc =
      createRuntimeFunction(fnTy, "objc_msg_lookup_sender");
  return objcMsgLookupSenderFunc;
}

cir::FuncOp CIRGenModule::getObjCSlotLookupSuperFunc() {
  if (objcSlotLookupSuperFunc)
    return objcSlotLookupSuperFunc;

  auto objCSuperTy = builder.getAnonRecordTy({voidPtrTy, voidPtrTy});
  auto slotTy = builder.getAnonRecordTy(
      {voidPtrTy, voidPtrTy, voidPtrTy, builder.getSInt32Ty(), voidPtrTy});
  auto selectorTy =
      mlir::cast<cir::PointerType>(convertType(getASTContext().getObjCSelType()));
  cir::FuncType fnTy = cir::FuncType::get(
      {builder.getPointerTo(objCSuperTy), selectorTy},
      builder.getPointerTo(slotTy), /*isVarArg=*/false);
  objcSlotLookupSuperFunc =
      createRuntimeFunction(fnTy, "objc_slot_lookup_super");
  return objcSlotLookupSuperFunc;
}

LangAS CIRGenModule::getGlobalConstantAddressSpace() const {
  // OpenCL v1.2 s6.5.3: a string literal is in the constant address space.
  if (getLangOpts().OpenCL)
    return LangAS::opencl_constant;
  if (getLangOpts().SYCLIsDevice)
    return LangAS::sycl_global;
  if (auto as = getTarget().getConstantAddressSpace())
    return as.value();
  return LangAS::Default;
}

mlir::ptr::MemorySpaceAttrInterface
CIRGenModule::getDefaultGlobalsAddressSpace() const {
  if (mlir::DataLayoutSpecInterface spec = getModule().getDataLayoutSpec()) {
    auto key = spec.getGlobalMemorySpaceIdentifier(builder.getContext());
    if (mlir::DataLayoutEntryInterface entry = spec.getSpecForIdentifier(key)) {
      if (auto globalAS = llvm::dyn_cast<mlir::IntegerAttr>(entry.getValue())) {
        unsigned addrSpace = globalAS.getValue().getZExtValue();
        if (addrSpace)
          return cir::TargetAddressSpaceAttr::get(builder.getContext(),
                                                  addrSpace);
        return {};
      }
    }
  }

  LangAS langAS = const_cast<CIRGenModule *>(this)->getGlobalVarAddressSpace(
      nullptr);
  return cir::toCIRLangAddressSpaceAttr(builder.getContext(), langAS);
}

LangAS CIRGenModule::getGlobalVarAddressSpace(const VarDecl *d) {
  if (getLangOpts().OpenCL) {
    LangAS as = d ? d->getType().getAddressSpace() : LangAS::opencl_global;
    assert(as == LangAS::opencl_global || as == LangAS::opencl_constant ||
           as == LangAS::opencl_local || as == LangAS::opencl_generic ||
           as == LangAS::opencl_global_device ||
           as == LangAS::opencl_global_host);
    return as;
  }

  if (getLangOpts().CUDA && getLangOpts().CUDAIsDevice) {
    if (d) {
      if (d->hasAttr<CUDAConstantAttr>())
        return LangAS::cuda_constant;
      if (d->hasAttr<CUDASharedAttr>())
        return LangAS::cuda_shared;
      if (d->hasAttr<CUDADeviceAttr>())
        return LangAS::cuda_device;
      if (d->getType().isConstQualified())
        return LangAS::cuda_constant;
    }
    return LangAS::cuda_device;
  }

  if (getLangOpts().SYCLIsDevice &&
      (!d || d->getType().getAddressSpace() == LangAS::Default))
    return LangAS::sycl_global;

  return getTargetCIRGenInfo().getGlobalVarAddressSpace(*this, d);
}

mlir::ptr::MemorySpaceAttrInterface
CIRGenModule::getGlobalMemorySpaceAttr() {
  if (mlir::DataLayoutSpecInterface spec = getModule().getDataLayoutSpec()) {
    auto key = spec.getGlobalMemorySpaceIdentifier(&getMLIRContext());
    if (mlir::DataLayoutEntryInterface entry = spec.getSpecForIdentifier(key)) {
      if (auto globalAS = llvm::dyn_cast<mlir::IntegerAttr>(entry.getValue())) {
        if (unsigned addrSpace = globalAS.getValue().getZExtValue())
          return cir::TargetAddressSpaceAttr::get(&getMLIRContext(), addrSpace);
        return {};
      }
    }
  }
  return cir::toCIRLangAddressSpaceAttr(&getMLIRContext(),
                                        getGlobalVarAddressSpace(nullptr));
}

// TODO(cir): this could be a common AST helper for both CIR and LLVM codegen.
LangAS CIRGenModule::getLangTempAllocaAddressSpace() const {
  if (getLangOpts().OpenCL)
    return LangAS::opencl_private;

  // For temporaries inside functions, CUDA treats them as normal variables.
  // LangAS::cuda_device, on the other hand, is reserved for those variables
  // explicitly marked with __device__.
  if (getLangOpts().CUDAIsDevice)
    return LangAS::Default;

  // OpenMP target-device code follows the regular temp/parameter spill path:
  // target-specific alloca address spaces are handled by the alloca itself and
  // any required casts back to the language-visible address space.
  if (getLangOpts().SYCLIsDevice)
    errorNYI("SYCL temp address space");

  return LangAS::Default;
}

void CIRGenModule::emitExplicitCastExprType(const ExplicitCastExpr *e,
                                            CIRGenFunction *cgf) {
  if (cgf && e->getType()->isVariablyModifiedType())
    cgf->emitVariablyModifiedType(e->getType());

  // OGCG can force debug-type completion while handling explicit cast expr
  // types. CIR defers debug metadata materialization to lowering.
}

mlir::Value CIRGenModule::emitMemberPointerConstant(const UnaryOperator *e) {
  mlir::Location loc = getLoc(e->getSourceRange());

  const auto *decl = cast<DeclRefExpr>(e->getSubExpr())->getDecl();

  // A member function pointer.
  if (const auto *methodDecl = dyn_cast<CXXMethodDecl>(decl)) {
    auto ty = mlir::cast<cir::MethodType>(convertType(e->getType()));
    auto attr =
        getCXXABI().emitMemberFunctionPointer(methodDecl, e->getType(), ty);
    return cir::ConstantOp::create(builder, loc, attr);
  }

  // Otherwise, a member data pointer.
  auto ty = mlir::cast<cir::DataMemberType>(convertType(e->getType()));
  // Handle IndirectFieldDecl (anonymous union/struct members) by resolving
  // through the chain to get the first FieldDecl in the parent record.
  const FieldDecl *fieldDecl;
  if (const auto *ifd = dyn_cast<IndirectFieldDecl>(decl))
    fieldDecl = cast<FieldDecl>(ifd->chain().front());
  else
    fieldDecl = cast<FieldDecl>(decl);

  // Empty [[no_unique_address]] fields have no storage in the CIR record
  // type and thus no entry in fieldIdxMap. Use byte offset like OGCG does.
  if (isEmptyFieldForLayout(getASTContext(), fieldDecl)) {
    CharUnits fieldOffset = getASTContext().toCharUnitsFromBits(
        getASTContext().getFieldOffset(fieldDecl));
    auto attr = cir::DataMemberAttr::get(ty.getContext(), ty, std::nullopt,
                                         static_cast<unsigned>(
                                             fieldOffset.getQuantity()));
    return cir::ConstantOp::create(builder, loc, attr);
  }

  // In a union, all members overlap at offset 0. The CIR field-index path
  // can't be used because DataMemberAttr::verify checks that the field type
  // at the given index matches the member type, but all union fields map to
  // index 0 regardless of their type. Use byte offset (always 0) like OGCG.
  if (fieldDecl->getParent()->isUnion()) {
    auto attr = cir::DataMemberAttr::get(ty.getContext(), ty, std::nullopt,
                                         /*offsetBytes=*/0u);
    return cir::ConstantOp::create(builder, loc, attr);
  }

  // Use the CIR record layout to get the correct field index that accounts
  // for base class subobjects in the CIR RecordType.
  const CIRGenRecordLayout &layout =
      getTypes().getCIRGenRecordLayout(fieldDecl->getParent());
  return cir::ConstantOp::create(
      builder, loc, builder.getDataMemberAttr(ty, layout.getCIRFieldNo(fieldDecl)));
}

void CIRGenModule::emitDeclContext(const DeclContext *dc) {
  for (Decl *decl : dc->decls()) {
    // Unlike other DeclContexts, the contents of an ObjCImplDecl at TU scope
    // are themselves considered "top-level", so EmitTopLevelDecl on an
    // ObjCImplDecl does not recursively visit them. We need to do that in
    // case they're nested inside another construct (LinkageSpecDecl /
    // ExportDecl) that does stop them from being considered "top-level".
    if (auto *oid = dyn_cast<ObjCImplDecl>(decl)) {
      errorNYI(oid->getSourceRange(), "emitDeclConext: ObjCImplDecl");
    }

    emitTopLevelDecl(decl);
  }
}

std::string CIRGenModule::getDependentLibraryOption(llvm::StringRef lib) const {
  if (getTriple().isOSBinFormatELF())
    return lib.str();

  if (getTriple().isOSWindows())
    return "/DEFAULTLIB:" + qualifyWindowsLibrary(lib);

  return std::string("-l") + lib.str();
}

std::optional<std::string>
CIRGenModule::getDetectMismatchOption(llvm::StringRef name,
                                      llvm::StringRef value) const {
  if (!getTriple().isOSWindows())
    return std::nullopt;

  return "/FAILIFMISMATCH:\"" + name.str() + "=" + value.str() + "\"";
}

void CIRGenModule::emitPragmaCommentDecl(const PragmaCommentDecl *decl) {
  auto commentKind = decl->getCommentKind();
  switch (commentKind) {
  case PCK_Unknown:
    llvm_unreachable("unexpected pragma comment kind");

  case PCK_Lib: {
    std::string option = getDependentLibraryOption(decl->getArg());
    if (option.empty())
      return;
    if (getTriple().isOSBinFormatELF()) {
      auto existing = mlir::dyn_cast_or_null<mlir::ArrayAttr>(
          getModule()->getAttr("cir.dependent_libraries"));
      llvm::SmallVector<mlir::Attribute> libs;
      if (existing)
        libs.append(existing.begin(), existing.end());
      libs.push_back(getStringAttrCached(option));
      getModule()->setAttr("cir.dependent_libraries",
                           mlir::ArrayAttr::get(&getMLIRContext(), libs));
      return;
    }

    mlir::OpBuilder::InsertionGuard guard(builder);
    builder.setInsertionPointToStart(getModule().getBody());
    llvm::SmallVector<mlir::Attribute> opts{getStringAttrCached(option)};
    cir::LinkerOptionsOp::create(
        builder, getLoc(decl->getSourceRange()),
        mlir::ArrayAttr::get(&getMLIRContext(), opts));
    return;
  }

  case PCK_Linker:
    // Frontend already diagnoses non-lib linker pragmas on ELF and suppresses
    // declaration creation there. For the remaining targets, mirror OGCG by
    // forwarding the raw linker argument into linker options metadata.
    [[fallthrough]];
  case PCK_Compiler:
  case PCK_ExeStr:
  case PCK_User:
    break;
  }

  // We ignore compiler/exestr/user pragma comments. For linker comments, pass
  // through verbatim like OGCG.
  if (commentKind == PCK_Linker) {
    mlir::OpBuilder::InsertionGuard guard(builder);
    builder.setInsertionPointToStart(getModule().getBody());
    llvm::SmallVector<mlir::Attribute> opts{
        getStringAttrCached(decl->getArg())};
    cir::LinkerOptionsOp::create(
        builder, getLoc(decl->getSourceRange()),
        mlir::ArrayAttr::get(&getMLIRContext(), opts));
  }
}

void CIRGenModule::emitPragmaDetectMismatchDecl(
    const PragmaDetectMismatchDecl *decl) {
  if (auto option = getDetectMismatchOption(decl->getName(), decl->getValue())) {
    mlir::OpBuilder::InsertionGuard guard(builder);
    builder.setInsertionPointToStart(getModule().getBody());
    llvm::SmallVector<mlir::Attribute> opts{getStringAttrCached(*option)};
    cir::LinkerOptionsOp::create(
        builder, getLoc(decl->getSourceRange()),
        mlir::ArrayAttr::get(&getMLIRContext(), opts));
  }
}

static llvm::GlobalValue::LinkageTypes
convertCIRToLLVMLinkageForCoverage(cir::GlobalLinkageKind linkage) {
  using CIR = cir::GlobalLinkageKind;
  using LLVM = llvm::GlobalValue::LinkageTypes;

  switch (linkage) {
  case CIR::AvailableExternallyLinkage:
    return LLVM::AvailableExternallyLinkage;
  case CIR::CommonLinkage:
    return LLVM::CommonLinkage;
  case CIR::ExternalLinkage:
    return LLVM::ExternalLinkage;
  case CIR::ExternalWeakLinkage:
    return LLVM::ExternalWeakLinkage;
  case CIR::InternalLinkage:
    return LLVM::InternalLinkage;
  case CIR::LinkOnceAnyLinkage:
    return LLVM::LinkOnceAnyLinkage;
  case CIR::LinkOnceODRLinkage:
    return LLVM::LinkOnceODRLinkage;
  case CIR::PrivateLinkage:
    return LLVM::PrivateLinkage;
  case CIR::WeakAnyLinkage:
    return LLVM::WeakAnyLinkage;
  case CIR::WeakODRLinkage:
    return LLVM::WeakODRLinkage;
  }
  llvm_unreachable("Unknown CIR linkage type");
}

std::string CIRGenModule::getCoverageMappingPGOName(cir::FuncOp fn) const {
  llvm::StringRef rawName = fn.getSymName();
  if (auto attr = fn->getAttrOfType<mlir::StringAttr>("cir.frontend_pgo_name"))
    rawName = attr.getValue();
  return llvm::getPGOFuncName(
      rawName, convertCIRToLLVMLinkageForCoverage(fn.getLinkage()),
      codeGenOpts.MainFileName, llvm::IndexedInstrProf::Version);
}

void CIRGenModule::trackCoverageMappingDecl(Decl *decl, llvm::StringRef symbolName,
                                            llvm::StringRef pgoName) {
  if (!codeGenOpts.CoverageMapping || !lowersToLLVMIR)
    return;

  Decl *trackedDecl = nullptr;
  if (auto *fd = dyn_cast<FunctionDecl>(decl)) {
    if (!fd->doesThisDeclarationHaveABody() || fd->isImmediateFunction())
      return;
    if (auto *def = fd->getDefinition())
      fd = def;
    trackedDecl = fd;
  } else if (auto *omd = dyn_cast<ObjCMethodDecl>(decl)) {
    if (!omd->getBody())
      return;
    trackedDecl = omd;
  } else if (auto *bd = dyn_cast<BlockDecl>(decl)) {
    if (!bd->hasBody())
      return;
    trackedDecl = bd;
  } else if (auto *cd = dyn_cast<CapturedDecl>(decl)) {
    if (!cd->hasBody())
      return;
    trackedDecl = cd;
  } else {
    return;
  }

  coverageMappingDecls.insert(trackedDecl);
  if (!symbolName.empty())
    coverageMappingSymbolNames[trackedDecl] = symbolName.str();
  if (!pgoName.empty())
    coverageMappingPGONames[trackedDecl] = pgoName.str();
}

// Emit code for a single top level declaration.
// Keep the recursive top-level dispatcher split across smaller helpers.
LLVM_ATTRIBUTE_NOINLINE bool CIRGenModule::emitCommonTopLevelDecl(Decl *decl) {
  const Decl::Kind kind = decl->getKind();

  if (kind == Decl::CXXConstructor) {
    trackCoverageMappingDecl(decl);
    getCXXABI().emitCXXConstructors(cast<CXXConstructorDecl>(decl));
    return true;
  }

  if (kind == Decl::CXXDestructor) {
    trackCoverageMappingDecl(decl);
    getCXXABI().emitCXXDestructors(cast<CXXDestructorDecl>(decl));
    return true;
  }

  if (kind == Decl::CXXConversion || kind == Decl::CXXMethod ||
      kind == Decl::Function) {
    auto *fd = cast<FunctionDecl>(decl);
    trackCoverageMappingDecl(decl);
    // Consteval functions shouldn't be emitted.
    if (!fd->isConsteval())
      emitGlobal(fd);
    return true;
  }

  if (kind == Decl::Var || kind == Decl::Decomposition ||
      kind == Decl::VarTemplateSpecialization) {
    auto *vd = cast<VarDecl>(decl);
    emitGlobal(vd);
    if (auto *dd = dyn_cast<DecompositionDecl>(decl))
      for (auto *b : dd->flat_bindings())
        if (auto *hd = b->getHoldingVar())
          emitGlobal(hd);
    return true;
  }

  if (kind == Decl::ObjCMethod) {
    auto *omd = cast<ObjCMethodDecl>(decl);
    if (omd->getBody()) {
      trackCoverageMappingDecl(decl);
      CIRGenFunction cgf(*this, builder);
      mlir::OpBuilder::InsertionGuard guard(builder);
      cgf.generateObjCMethod(omd);
    }
    return true;
  }

  if (kind == Decl::LinkageSpec || kind == Decl::Namespace ||
      kind == Decl::Export) {
    emitDeclContext(Decl::castToDeclContext(decl));
    return true;
  }

  if (kind == Decl::ClassTemplateSpecialization || kind == Decl::CXXRecord) {
    auto *crd = cast<CXXRecordDecl>(decl);
    // OGCG may complete debug type info here. CIR defers debug metadata
    // materialization to lowering.
    for (auto *childDecl : crd->decls())
      if (isa<VarDecl, CXXRecordDecl, EnumDecl, OpenACCDeclareDecl>(childDecl))
        emitTopLevelDecl(childDecl);
    return true;
  }

  return false;
}

LLVM_ATTRIBUTE_NOINLINE void
CIRGenModule::emitUncommonTopLevelDecl(Decl *decl) {
  const Decl::Kind kind = decl->getKind();

  if (kind == Decl::OpenACCRoutine) {
    emitGlobalOpenACCRoutineDecl(cast<OpenACCRoutineDecl>(decl));
    return;
  }

  if (kind == Decl::OpenACCDeclare) {
    emitGlobalOpenACCDeclareDecl(cast<OpenACCDeclareDecl>(decl));
    return;
  }

  if (kind == Decl::OMPThreadPrivate) {
    emitOMPThreadPrivateDecl(cast<OMPThreadPrivateDecl>(decl));
    return;
  }

  if (kind == Decl::OMPGroupPrivate) {
    emitOMPGroupPrivateDecl(cast<OMPGroupPrivateDecl>(decl));
    return;
  }

  if (kind == Decl::OMPAllocate) {
    emitOMPAllocateDecl(cast<OMPAllocateDecl>(decl));
    return;
  }

  if (kind == Decl::OMPCapturedExpr) {
    emitOMPCapturedExpr(cast<OMPCapturedExprDecl>(decl));
    return;
  }

  if (kind == Decl::OMPDeclareReduction) {
    emitOMPDeclareReduction(cast<OMPDeclareReductionDecl>(decl));
    return;
  }

  if (kind == Decl::OMPDeclareMapper) {
    emitOMPDeclareMapper(cast<OMPDeclareMapperDecl>(decl));
    return;
  }

  if (kind == Decl::OMPRequires) {
    emitOMPRequiresDecl(cast<OMPRequiresDecl>(decl));
    return;
  }

  if (kind == Decl::PragmaComment) {
    emitPragmaCommentDecl(cast<PragmaCommentDecl>(decl));
    return;
  }

  if (kind == Decl::PragmaDetectMismatch) {
    emitPragmaDetectMismatchDecl(cast<PragmaDetectMismatchDecl>(decl));
    return;
  }

  if (kind == Decl::Enum || kind == Decl::Using ||
      kind == Decl::UsingDirective || kind == Decl::UsingEnum ||
      kind == Decl::NamespaceAlias || kind == Decl::Typedef ||
      kind == Decl::TypeAlias || kind == Decl::Record) {
    // OGCG may complete debug type info for these declarations. CIR defers
    // debug metadata materialization to lowering.
    return;
  }

  // No code generation needed.
  if (kind == Decl::Block || kind == Decl::ClassTemplate ||
      kind == Decl::Concept || kind == Decl::CXXDeductionGuide ||
      kind == Decl::Empty ||
      kind == Decl::Binding || // Structured binding — codegen handled via DecompositionDecl
      kind == Decl::FunctionTemplate || kind == Decl::IndirectField ||
      kind == Decl::StaticAssert || kind == Decl::TypeAliasTemplate ||
      kind == Decl::UsingShadow || kind == Decl::VarTemplate ||
      kind == Decl::VarTemplatePartialSpecialization) {
    return;
  }

  // Objective-C Decls

  // Forward declarations, no (immediate) code generation.
  if (kind == Decl::ObjCInterface || kind == Decl::ObjCCategory)
    return;

  if (kind == Decl::ObjCProtocol) {
    const auto *proto = cast<ObjCProtocolDecl>(decl);
    if ((!usesDarwinFragileObjCRuntime(*this) &&
         !usesDarwinNonFragileObjCRuntime(*this) &&
         !usesGNUObjCRuntime(*this)) ||
        !proto->isThisDeclarationADefinition() ||
        proto->isNonRuntimeProtocol())
      return;

    (void)getObjCProtocolGlobal(builder.getUnknownLoc(), proto);
    if (usesDarwinNonFragileObjCRuntime(*this))
      (void)getObjCProtocolLabelGlobal(builder.getUnknownLoc(), proto);
    return;
  }

  if (kind == Decl::ObjCCategoryImpl) {
    // Individual method bodies arrive as separate Decl::ObjCMethod visits.
    if (usesGNUObjCRuntime(*this))
      (void)getObjCGNUCategoryGlobal(
          builder.getUnknownLoc(), cast<ObjCCategoryImplDecl>(decl));
    else if (usesDarwinFragileObjCRuntime(*this))
      (void)getObjCDarwinFragileCategoryGlobal(
          builder.getUnknownLoc(), cast<ObjCCategoryImplDecl>(decl));
    else if (usesDarwinNonFragileObjCRuntime(*this))
      (void)getObjCDarwinCategoryGlobal(
          builder.getUnknownLoc(), cast<ObjCCategoryImplDecl>(decl));
    return;
  }

  if (kind == Decl::ObjCImplementation) {
    // Individual method bodies arrive as separate Decl::ObjCMethod visits via
    // HandleTopLevelDecl.
    auto *oid =
        const_cast<ObjCImplementationDecl *>(cast<ObjCImplementationDecl>(decl));
    emitObjCPropertyImplementations(oid);
    emitObjCIvarInitializations(oid);
    if (usesGNUObjCRuntime(*this))
      (void)getObjCGNUClassGlobal(builder.getUnknownLoc(), oid);
    if (usesDarwinFragileObjCRuntime(*this))
      (void)getObjCDarwinFragileClassGlobal(builder.getUnknownLoc(), oid);
    else if (usesDarwinNonFragileObjCRuntime(*this))
      (void)getObjCDarwinClassGlobal(builder.getUnknownLoc(), oid);
    return;
  }

  if (kind == Decl::ObjCCompatibleAlias) {
    registerObjCCompatibleAlias(cast<ObjCCompatibleAliasDecl>(decl));
    return;
  }

  if (kind == Decl::Import) {
    auto *import = cast<ImportDecl>(decl);

    // If we've already imported this module, we're done.
    if (!importedModules.insert(import->getImportedModule()))
      return;

    // Emit debug information for direct imports.
    if (!import->getImportedOwningModule()) {
      // OGCG emits explicit import debug entries via DI->EmitImportDecl.
      // CIR records debug compile-unit metadata at module scope and lowers
      // debug info later; there is no dedicated CIR import-debug op.
      // Keep direct-import handling non-fatal here.
    }

    // For C++ standard modules we are done - we will call the module
    // initializer for imported modules, and that will likewise call those
    // for any imports it has. The imported module init names are collected
    // in release() and stored as a module attribute for LoweringPrepare.
    if (import->getImportedModule() &&
        import->getImportedModule()->isNamedModule()) {
      return;
    }

    // For clang C++ module map modules the initializers for sub-modules are
    // emitted here.

    // Find all of the submodules and emit the module initializers.
    llvm::SmallPtrSet<clang::Module *, 16> visited;
    SmallVector<clang::Module *, 16> stack;
    visited.insert(import->getImportedModule());
    stack.push_back(import->getImportedModule());

    while (!stack.empty()) {
      clang::Module *mod = stack.pop_back_val();
      if (!emittedModuleInitializers.insert(mod).second)
        continue;

      for (auto *d : astContext.getModuleInitializers(mod))
        emitTopLevelDecl(d);

      // Visit the submodules of this module.
      for (auto *submodule : mod->submodules()) {
        // Skip explicit children; they need to be explicitly imported to
        // emit the initializers.
        if (submodule->IsExplicit)
          continue;

        if (visited.insert(submodule).second)
          stack.push_back(submodule);
      }
    }
    return;
  }

  if (kind == Decl::FileScopeAsm) {
    // File-scope asm is ignored during device-side CUDA compilation.
    if (langOpts.CUDA && langOpts.CUDAIsDevice)
      return;
    // File-scope asm is ignored during device-side OpenMP compilation.
    if (langOpts.OpenMPIsTargetDevice)
      return;
    // File-scope asm is ignored during device-side SYCL compilation.
    if (langOpts.SYCLIsDevice)
      return;
    auto *file_asm = cast<FileScopeAsmDecl>(decl);
    std::string line = file_asm->getAsmString();
    globalScopeAsm.push_back(getStringAttrCached(line));
    return;
  }

  errorNYI(decl->getBeginLoc(), "declaration of kind", decl->getDeclKindName());
}

void CIRGenModule::emitTopLevelDecl(Decl *decl) {

  // Ignore dependent declarations.
  if (decl->isTemplated())
    return;

  if (emitCommonTopLevelDecl(decl))
    return;

  emitUncommonTopLevelDecl(decl);
}

void CIRGenModule::setInitializer(cir::GlobalOp &op, mlir::Attribute value) {
  // Recompute visibility when updating initializer.
  op.setInitialValueAttr(value);
  mlir::SymbolTable::setSymbolVisibility(op,
                                         CIRGenModule::getMLIRVisibility(op));
}

std::pair<cir::FuncType, cir::FuncOp> CIRGenModule::getAddrAndTypeOfCXXStructor(
    GlobalDecl gd, const CIRGenFunctionInfo *fnInfo, cir::FuncType fnType,
    bool dontDefer, ForDefinition_t isForDefinition) {
  auto *md = cast<CXXMethodDecl>(gd.getDecl());

  if (isa<CXXDestructorDecl>(md)) {
    // Always alias equivalent complete destructors to base destructors in the
    // MS ABI.
    if (getTarget().getCXXABI().isMicrosoft() &&
        gd.getDtorType() == Dtor_Complete &&
        md->getParent()->getNumVBases() == 0) {
      gd = gd.getWithDtorType(Dtor_Base);
    }
  }

  if (!fnType) {
    if (!fnInfo)
      fnInfo = &getTypes().arrangeCXXStructorDeclaration(gd);
    fnType = getTypes().getFunctionType(*fnInfo);
  }

  auto fn = getOrCreateCIRFunction(getMangledName(gd), fnType, gd,
                                   /*ForVtable=*/false, dontDefer,
                                   /*IsThunk=*/false, isForDefinition);

  return {fnType, fn};
}

mlir::Operation *CIRGenModule::getWeakRefReference(const ValueDecl *vd) {
  const AliasAttr *aa = vd->getAttr<AliasAttr>();
  assert(aa && "WeakRef without alias?");

  mlir::Type declTy = getTypes().convertTypeForMem(vd->getType());

  // See if there is already something with the target's name in the module.
  StringRef aliaseeName = aa->getAliasee();
  mlir::Operation *entry = getGlobalValue(aliaseeName);

  // Weakref aliasees can name aliases; resolve to the underlying symbol.
  // This matches OGCG's ability to produce ConstantAddress for existing
  // alias/global/function symbols and avoids crashing when weakref points to
  // a variable alias.
  llvm::SmallDenseSet<mlir::StringAttr, 4> visitedAliases;
  while (auto aliasOp = mlir::dyn_cast_if_present<cir::AliasOp>(entry)) {
    if (!visitedAliases.insert(aliasOp.getSymNameAttr()).second)
      llvm_unreachable("circular weakref alias chain");
    aliaseeName = aliasOp.getAliasee();
    entry = getGlobalValue(aliaseeName);
  }

  if (entry)
    return entry;

  if (mlir::isa<cir::FuncType>(declTy)) {
    const auto *fd = cast<FunctionDecl>(vd);
    auto funcOp = getOrCreateCIRFunction(
        aliaseeName, declTy, GlobalDecl(fd), /*ForVTable=*/false,
        /*DontDefer=*/false, /*IsThunk=*/false, NotForDefinition);

    // Match OGCG: newly created weakref targets are extern_weak declarations.
    funcOp.setLinkage(cir::GlobalLinkageKind::ExternalWeakLinkage);
    funcOp.setSymVisibility("private");
    weakRefReferences.insert(funcOp);
    return funcOp;
  }

  const auto *varDecl = dyn_cast<VarDecl>(vd);
  auto globalOp = getOrCreateCIRGlobal(aliaseeName, declTy, LangAS::Default,
                                       varDecl, NotForDefinition);
  globalOp.setLinkage(cir::GlobalLinkageKind::ExternalWeakLinkage);
  mlir::SymbolTable::setSymbolVisibility(globalOp,
                                         CIRGenModule::getMLIRVisibility(globalOp));
  weakRefReferences.insert(globalOp);
  return globalOp;
}

cir::FuncOp CIRGenModule::getAddrOfThunk(StringRef name, mlir::Type fnTy,
                                         GlobalDecl gd) {
  return getOrCreateCIRFunction(name, fnTy, gd, /*ForVTable=*/true,
                                /*DontDefer=*/true, /*IsThunk=*/true);
}

cir::FuncOp CIRGenModule::getAddrOfFunction(clang::GlobalDecl gd,
                                            mlir::Type funcType, bool forVTable,
                                            bool dontDefer,
                                            ForDefinition_t isForDefinition) {
  assert(!cast<FunctionDecl>(gd.getDecl())->isConsteval() &&
         "consteval function should never be emitted");

  if (!funcType) {
    const auto *fd = cast<FunctionDecl>(gd.getDecl());
    funcType = convertType(fd->getType());
    // OpenCL kernel stubs need the function type derived from the arranged
    // global declaration (which adjusts the calling convention to C).
    if (DeviceKernelAttr::isOpenCLSpelling(fd->getAttr<DeviceKernelAttr>()) &&
        gd.getKernelReferenceKind() == KernelReferenceKind::Stub) {
      const CIRGenFunctionInfo &fi = getTypes().arrangeGlobalDeclaration(gd);
      funcType = getTypes().getFunctionType(fi);
    }
  }

  // Devirtualized destructor calls may come through here instead of via
  // getAddrOfCXXStructor. Make sure we use the MS ABI base destructor instead
  // of the complete destructor when necessary.
  if (const auto *dd = dyn_cast<CXXDestructorDecl>(gd.getDecl())) {
    if (getTarget().getCXXABI().isMicrosoft() &&
        gd.getDtorType() == Dtor_Complete &&
        dd->getParent()->getNumVBases() == 0) {
      gd = GlobalDecl(dd, Dtor_Base);
    }
  }

  StringRef mangledName = getMangledName(gd);
  cir::FuncOp func =
      getOrCreateCIRFunction(mangledName, funcType, gd, forVTable, dontDefer,
                             /*isThunk=*/false, isForDefinition);

  if ((langOpts.HIP || langOpts.CUDA) && !langOpts.CUDAIsDevice &&
      cast<FunctionDecl>(gd.getDecl())->hasAttr<CUDAGlobalAttr>())
    (void)getCUDARuntime().getKernelHandle(func, gd);

  return func;
}

static std::string getCPUSpecificMangling(CIRGenModule &cgm, StringRef name) {
  const TargetInfo &target = cgm.getTarget();
  return (Twine('.') + Twine(target.CPUSpecificManglingCharacter(name))).str();
}

static void appendCPUSpecificCPUDispatchMangling(CIRGenModule &cgm,
                                                  const CPUSpecificAttr *attr,
                                                  unsigned cpuIndex,
                                                  raw_ostream &out) {
  // cpu_specific gets the mangling character, cpu_dispatch gets .resolver
  // if IFunc is supported.
  if (attr)
    out << getCPUSpecificMangling(cgm, attr->getCPUName(cpuIndex)->getName());
  else if (cgm.getTarget().supportsIFunc())
    out << ".resolver";
}

static std::string getMangledNameImpl(CIRGenModule &cgm, GlobalDecl gd,
                                      const NamedDecl *nd,
                                      bool omitMultiVersionMangling = false) {
  SmallString<256> buffer;

  llvm::raw_svector_ostream out(buffer);
  MangleContext &mc = cgm.getCXXABI().getMangleContext();

  if (!cgm.getModuleNameHash().empty())
    mc.needsUniqueInternalLinkageNames();

  bool shouldMangle = mc.shouldMangleDeclName(nd);
  if (shouldMangle) {
    mc.mangleName(gd.getWithDecl(nd), out);
  } else {
    IdentifierInfo *ii = nd->getIdentifier();
    assert(ii && "Attempt to mangle unnamed decl.");

    const auto *fd = dyn_cast<FunctionDecl>(nd);
    if (fd &&
        fd->getType()->castAs<FunctionType>()->getCallConv() == CC_X86RegCall) {
      if (cgm.getLangOpts().RegCall4)
        out << "__regcall4__" << ii->getName();
      else
        out << "__regcall3__" << ii->getName();
    } else if (fd && fd->hasAttr<CUDAGlobalAttr>() &&
               gd.getKernelReferenceKind() == KernelReferenceKind::Stub) {
      out << "__device_stub__" << ii->getName();
    } else if (fd &&
               DeviceKernelAttr::isOpenCLSpelling(
                   fd->getAttr<DeviceKernelAttr>()) &&
               gd.getKernelReferenceKind() == KernelReferenceKind::Stub) {
      out << "__clang_ocl_kern_imp_" << ii->getName();
    } else {
      out << ii->getName();
    }
  }

  // Check if the module name hash should be appended for internal linkage
  // symbols. This should come before multi-version target suffixes are
  // appended. This is to keep the name and module hash suffix of the internal
  // linkage function together. The unique suffix should only be added when name
  // mangling is done to make sure that the final name can be properly
  // demangled. For example, for C functions without prototypes, name mangling
  // is not done and the unique suffix should not be appended then.
  // Mirrors OG's isUniqueInternalLinkageDecl (CodeGenModule.cpp): only
  // internal-linkage functions get the module name hash.
  if (shouldMangle && !cgm.getModuleNameHash().empty()) {
    const Decl *d = gd.getDecl();
    bool isUniqueInternalLinkageDecl =
        isa<FunctionDecl>(d) &&
        cgm.getFunctionLinkage(gd) == cir::GlobalLinkageKind::InternalLinkage;
    if (isUniqueInternalLinkageDecl) {
      assert(cgm.getCodeGenOpts().UniqueInternalLinkageNames &&
             "Hash computed when not explicitly requested");
      out << cgm.getModuleNameHash();
    }
  }

  if (const auto *fd = dyn_cast<FunctionDecl>(nd)) {
    if (fd->isMultiVersion() && !omitMultiVersionMangling) {
      switch (fd->getMultiVersionKind()) {
      case MultiVersionKind::Target: {
        auto *attr = fd->getAttr<TargetAttr>();
        assert(attr && "Expected TargetAttr for target multiversion");
        if (!attr->isDefaultVersion()) {
          // Append target feature suffix matching OGCG's
          // ABIInfo::appendAttributeMangling. For "default", no suffix.
          StringRef featuresStr = attr->getFeaturesStr();
          if (featuresStr == "default") {
            out << ".default";
          } else {
            out << '.';
            const TargetInfo &TI = cgm.getTarget();
            ParsedTargetAttr info = TI.parseTargetAttr(featuresStr);
            llvm::sort(info.Features, [&TI](StringRef lhs, StringRef rhs) {
              return TI.getFMVPriority({lhs.substr(1)})
                  .ugt(TI.getFMVPriority({rhs.substr(1)}));
            });
            bool isFirst = true;
            if (!info.CPU.empty()) {
              out << "arch_" << info.CPU;
              isFirst = false;
            }
            for (StringRef feat : info.Features) {
              if (!isFirst)
                out << '_';
              out << feat.substr(1);
              isFirst = false;
            }
          }
        }
        break;
      }
      case MultiVersionKind::TargetVersion: {
        auto *attr = fd->getAttr<TargetVersionAttr>();
        assert(attr && "Expected TargetVersionAttr");
        StringRef namesStr = attr->getNamesStr();
        if (namesStr == "default") {
          out << ".default";
        } else if (cgm.getTarget().getTriple().isAArch64()) {
          // AArch64 target_version uses the same ._M<feature> mangling
          // as target_clones, matching AArch64ABIInfo::appendAttributeMangling.
          out << "._";

          SmallVector<StringRef, 8> features;
          namesStr.split(features, "+");
          for (auto &feat : features)
            feat = feat.trim();

          llvm::sort(features, [](StringRef lhs, StringRef rhs) {
            return lhs.compare(rhs) < 0;
          });

          llvm::SmallDenseSet<StringRef, 8> uniqueFeats;
          const TargetInfo &TI = cgm.getTarget();
          for (StringRef feat : features) {
            if (!TI.doesFeatureAffectCodeGen(feat))
              continue;
            auto ext = llvm::AArch64::parseFMVExtension(feat);
            if (!ext)
              continue;
            if (uniqueFeats.insert(ext->Name).second)
              out << 'M' << ext->Name;
          }
        } else {
          out << '.';
          const TargetInfo &TI = cgm.getTarget();
          ParsedTargetAttr info = TI.parseTargetAttr(namesStr);
          llvm::sort(info.Features, [&TI](StringRef lhs, StringRef rhs) {
            return TI.getFMVPriority({lhs.substr(1)})
                .ugt(TI.getFMVPriority({rhs.substr(1)}));
          });
          bool isFirst = true;
          for (StringRef feat : info.Features) {
            if (!isFirst)
              out << '_';
            out << feat.substr(1);
            isFirst = false;
          }
          if (info.Features.empty())
            out << namesStr;
        }
        break;
      }
      case MultiVersionKind::CPUDispatch:
      case MultiVersionKind::CPUSpecific:
        if (!omitMultiVersionMangling)
          appendCPUSpecificCPUDispatchMangling(
              cgm, fd->getAttr<CPUSpecificAttr>(),
              gd.getMultiVersionIndex(), out);
        break;
      case MultiVersionKind::TargetClones: {
        const auto *attr = fd->getAttr<TargetClonesAttr>();
        assert(attr && "Expected TargetClonesAttr");
        unsigned index = gd.getMultiVersionIndex();
        StringRef featuresStr = attr->getFeatureStr(index);

        // AArch64 target_clones uses OGCG's AArch64 ABI mangling scheme
        // (._M<feature>...) instead of generic x86 target parsing.
        if (cgm.getTarget().getTriple().isAArch64()) {
          if (featuresStr == "default") {
            out << ".default";
          } else {
            out << "._";

            SmallVector<StringRef, 8> features;
            featuresStr.split(features, "+");
            for (auto &feat : features)
              feat = feat.trim();

            llvm::sort(features, [](StringRef lhs, StringRef rhs) {
              return lhs.compare(rhs) < 0;
            });

            llvm::SmallDenseSet<StringRef, 8> uniqueFeats;
            const TargetInfo &TI = cgm.getTarget();
            for (StringRef feat : features) {
              if (!TI.doesFeatureAffectCodeGen(feat))
                continue;
              auto ext = llvm::AArch64::parseFMVExtension(feat);
              if (!ext)
                continue;
              if (uniqueFeats.insert(ext->Name).second)
                out << 'M' << ext->Name;
            }
          }
          break;
        }

        if (featuresStr == "default") {
          out << ".default";
        } else {
          out << '.';
          const TargetInfo &TI = cgm.getTarget();
          ParsedTargetAttr info = TI.parseTargetAttr(featuresStr);
          llvm::sort(info.Features, [&TI](StringRef lhs, StringRef rhs) {
            return TI.getFMVPriority({lhs.substr(1)})
                .ugt(TI.getFMVPriority({rhs.substr(1)}));
          });
          bool isFirst = true;
          if (!info.CPU.empty()) {
            out << "arch_" << info.CPU;
            isFirst = false;
          }
          for (StringRef feat : info.Features) {
            if (!isFirst)
              out << '_';
            out << feat.substr(1);
            isFirst = false;
          }
        }
        // On non-AArch64 targets, append the mangled index to disambiguate
        // duplicate feature strings. AArch64 does not use the index suffix.
        if (!cgm.getTarget().getTriple().isAArch64())
          out << '.' << attr->getMangledIndex(index);
        break;
      }
      case MultiVersionKind::None:
        llvm_unreachable("None multiversion type isn't valid here");
      }
    }
  }
  if (cgm.getLangOpts().GPURelocatableDeviceCode) {
    cgm.errorNYI(nd->getSourceRange(),
                 "getMangledName: GPU relocatable device code");
  }

  return std::string(out.str());
}

void CIRGenModule::emitCPUDispatchDefinition(GlobalDecl gd) {
  const auto *fd = cast<FunctionDecl>(gd.getDecl());
  assert(fd && "Not a FunctionDecl?");
  assert(fd->isCPUDispatchMultiVersion() && "Not a cpu_dispatch function?");
  const auto *dd = fd->getAttr<CPUDispatchAttr>();
  assert(dd && "Not a cpu_dispatch function?");

  // Non-IFunc platforms (e.g. Windows) require musttail dispatch instead of
  // an ifunc resolver returning a function pointer. Not yet implemented.
  if (!getTarget().supportsIFunc()) {
    errorNYI(fd->getSourceRange(),
             "cpu_dispatch on non-IFunc platform (e.g. Windows)");
    return;
  }

  const CIRGenFunctionInfo &fi = getTypes().arrangeGlobalDeclaration(gd);
  cir::FuncType funcType = getTypes().getFunctionType(fi);
  auto funcPtrType = cir::PointerType::get(funcType);

  // Get base name (without multiversion suffix) and resolver name.
  std::string baseName = getMangledNameImpl(*this, gd, fd, /*omit=*/true);
  std::string resolverName = baseName + ".resolver";

  // Create the resolver function type: () -> ptr (returns function pointer).
  auto resolverFuncType =
      cir::FuncType::get({}, funcPtrType, false);

  // Create or get the resolver FuncOp.
  auto resolverFunc = getOrCreateCIRFunction(
      resolverName, resolverFuncType, GlobalDecl{}, /*forVTable=*/false,
      /*dontDefer=*/false, /*isThunk=*/false, NotForDefinition);

  // Resolver functions should NOT be dso_local.
  resolverFunc.setDsoLocal(false);
  resolverFunc->setAttr("cir.no_noundef_return",
                        builder.getUnitAttr());

  // Don't re-emit if the resolver already has a body.
  if (!resolverFunc.isDeclaration())
    return;

  resolverFunc.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
      builder.getDictionaryAttr(
          {builder.getNamedAttr("disable_sanitizer_instrumentation",
                                builder.getUnitAttr())})));

  if (supportsCOMDAT()) {
    auto linkage = getMultiversionLinkage(*this, gd);
    if (linkage != cir::GlobalLinkageKind::InternalLinkage)
      resolverFunc.setComdat(true);
  }

  // Collect cpu_specific variants and their features.
  struct CPUDispatchOption {
    std::string mangledName;
    llvm::SmallVector<StringRef, 32> features;
  };
  llvm::SmallVector<CPUDispatchOption, 10> options;
  const TargetInfo &target = getTarget();
  unsigned index = 0;
  for (const IdentifierInfo *ii : dd->cpus()) {
    CPUDispatchOption opt;
    opt.mangledName =
        baseName + getCPUSpecificMangling(*this, ii->getName());

    // Ensure the cpu_specific variant function exists (at least as a
    // declaration).
    if (!getGlobalValue(opt.mangledName)) {
      GlobalDecl existingDecl = manglings.lookup(opt.mangledName);
      if (existingDecl.getDecl() &&
          existingDecl.getDecl()->getAsFunction()->isDefined()) {
        emitGlobalFunctionDefinition(existingDecl, nullptr);
      } else {
        if (!existingDecl.getDecl())
          existingDecl = gd.getWithMultiVersionIndex(index);
        getOrCreateCIRFunction(opt.mangledName, funcType, existingDecl,
                               /*forVTable=*/false, /*dontDefer=*/true,
                               /*isThunk=*/false, ForDefinition);
      }
    }

    // Get CPU features for this variant.
    target.getCPUSpecificCPUDispatchFeatures(ii->getName(), opt.features);
    llvm::transform(opt.features, opt.features.begin(),
                    [](StringRef str) { return str.substr(1); });
    llvm::erase_if(opt.features, [&target](StringRef feat) {
      return !target.validateCpuSupports(feat);
    });
    options.push_back(std::move(opt));
    ++index;
  }

  // Sort by CPU feature priority (highest first).
  llvm::stable_sort(options, [](const CPUDispatchOption &lhs,
                                const CPUDispatchOption &rhs) {
    return llvm::X86::getCpuSupportsMask(lhs.features) >
           llvm::X86::getCpuSupportsMask(rhs.features);
  });

  // Remove duplicate "default" entries (those with zero feature masks).
  // If multiple entries have all-zero masks (e.g. 'pentium' and 'generic'),
  // keep only the one with the higher mangling letter.
  while (options.size() > 1 &&
         llvm::all_of(llvm::X86::getCpuSupportsMask(
                          (options.end() - 2)->features),
                      [](auto x) { return x == 0; })) {
    StringRef lhsName = (options.end() - 2)->mangledName;
    StringRef rhsName = (options.end() - 1)->mangledName;
    if (lhsName.compare(rhsName) < 0)
      options.erase(options.end() - 2);
    else
      options.erase(options.end() - 1);
  }

  // Build the resolver body.
  auto loc = getLoc(fd->getSourceRange());
  mlir::OpBuilder::InsertionGuard guard(builder);

  auto int32Ty = cir::IntType::get(&getMLIRContext(), 32, true);
  auto cpuFeatures1Ty = cir::ArrayType::get(int32Ty, 1);
  auto cpuModelTy = getX86CpuModelType(getMLIRContext());
  auto cpuFeatures2Ty = cir::ArrayType::get(int32Ty, 3);

  // Pre-create module-level declarations before entering the resolver body.
  auto voidTy = cir::VoidType::get(&getMLIRContext());
  auto cpuInitFnTy = cir::FuncType::get({}, voidTy, false);
  auto cpuInitFn =
      createRuntimeFunction(cpuInitFnTy, "__cpu_indicator_init", {},
                            /*isLocal=*/true);
  // Match OGCG: explicitly force dso_local on __cpu_indicator_init.
  cpuInitFn.setDsoLocal(true);

  auto getOrCreateExternGlobal = [&](StringRef name,
                                     mlir::Type ty) -> cir::GlobalOp {
    if (auto *existing = getGlobalValue(name))
      return mlir::cast<cir::GlobalOp>(existing);
    mlir::OpBuilder::InsertionGuard g(builder);
    if (lastGlobalOp)
      builder.setInsertionPointAfter(lastGlobalOp);
    else
      builder.setInsertionPointToStart(getModule().getBody());
    auto op = cir::GlobalOp::create(
        builder, loc, name, ty,
        /*isConstant=*/false,
        cir::GlobalLinkageKind::ExternalLinkage);
    op.setDsoLocal(true);
    op.setSymVisibility("private");
    insertGlobalSymbol(op);
    lastGlobalOp = op;
    return op;
  };

  getOrCreateExternGlobal("__cpu_model", cpuModelTy);

  // Check if any variant needs __cpu_features2.
  bool needsCpuFeatures2 = false;
  for (const auto &opt : options) {
    if (opt.features.empty())
      continue;
    std::array<uint32_t, 4> mask =
        llvm::X86::getCpuSupportsMask(opt.features);
    if (mask[1] != 0 || mask[2] != 0 || mask[3] != 0) {
      needsCpuFeatures2 = true;
      break;
    }
  }
  if (needsCpuFeatures2)
    getOrCreateExternGlobal("__cpu_features2", cpuFeatures2Ty);

  // Build the resolver function body as an if-else-trap decision tree.
  // Each variant is checked in priority order (highest first). If its CPU
  // features match, return its function pointer. If no variant matches,
  // emit trap + unreachable. This matches OGCG's EmitX86MultiVersionResolver.
  mlir::Block *entryBlock = builder.createBlock(&resolverFunc.getBody());
  builder.setInsertionPointToStart(entryBlock);

  // Call __cpu_indicator_init().
  cir::CallOp::create(
      builder, loc,
      mlir::SymbolRefAttr::get(&getMLIRContext(), cpuInitFn.getSymName()),
      voidTy, mlir::ValueRange{});

  // Helper: emit the feature-check condition for a given option into the
  // current block. Returns the condition value, or nullptr for default.
  auto emitFeatureCheck =
      [&](const CPUDispatchOption &opt) -> mlir::Value {
    std::array<uint32_t, 4> featureMask =
        llvm::X86::getCpuSupportsMask(opt.features);
    bool isDefault =
        llvm::all_of(featureMask, [](auto x) { return x == 0; });
    if (isDefault || opt.features.empty())
      return nullptr; // default: always matches

    mlir::Value cond;

    // Check feature mask[0] (from __cpu_model.__cpu_features[0], element [3])
    if (featureMask[0] != 0) {
      auto modelAddr = cir::GetGlobalOp::create(
          builder, loc, cir::PointerType::get(cpuModelTy), "__cpu_model");
      auto featuresPtr = cir::GetMemberOp::create(
          builder, loc, cir::PointerType::get(cpuFeatures1Ty), modelAddr,
          /*name=*/"", /*index=*/3);
      auto idx0 = cir::ConstantOp::create(
          builder, loc, int32Ty, builder.getSInt32AttrLocalCached(0));
      auto featPtr = cir::GetElementOp::create(
          builder, loc, cir::PointerType::get(int32Ty), featuresPtr, idx0);
      auto align4 = mlir::IntegerAttr::get(
          mlir::IntegerType::get(&getMLIRContext(), 64), 4);
      auto featVal = cir::LoadOp::create(
          builder, loc, featPtr, /*isDeref=*/false, /*isVolatile=*/false,
          /*is_nontemporal=*/false, /*alignment=*/align4,
          /*sync_scope=*/cir::SyncScopeKindAttr{},
          /*mem_order=*/cir::MemOrderAttr{}, /*tbaa=*/cir::TBAAAttr{});
      auto maskVal = cir::ConstantOp::create(
          builder, loc, int32Ty,
          builder.getSInt32AttrLocalCached(featureMask[0]));
      auto andResult = cir::BinOp::create(
          builder, loc, int32Ty, cir::BinOpKind::And, featVal, maskVal,
          {}, {}, {}, {}, {}, {});
      cond = cir::CmpOp::create(
          builder, loc, cir::CmpOpKind::eq, andResult, maskVal);
    }

    // Check feature mask[1..3] (from __cpu_features2[0..2])
    auto boolTy = cir::BoolType::get(&getMLIRContext());
    for (int i = 1; i < 4; ++i) {
      if (featureMask[i] == 0)
        continue;
      auto feat2Addr = cir::GetGlobalOp::create(
          builder, loc, cir::PointerType::get(cpuFeatures2Ty),
          "__cpu_features2");
      auto idxI = cir::ConstantOp::create(
          builder, loc, int32Ty, builder.getSInt32AttrLocalCached(i - 1));
      auto feat2Ptr = cir::GetElementOp::create(
          builder, loc, cir::PointerType::get(int32Ty), feat2Addr, idxI);
      auto align4 = mlir::IntegerAttr::get(
          mlir::IntegerType::get(&getMLIRContext(), 64), 4);
      auto feat2Val = cir::LoadOp::create(
          builder, loc, feat2Ptr, /*isDeref=*/false, /*isVolatile=*/false,
          /*is_nontemporal=*/false, /*alignment=*/align4,
          /*sync_scope=*/cir::SyncScopeKindAttr{},
          /*mem_order=*/cir::MemOrderAttr{}, /*tbaa=*/cir::TBAAAttr{});
      auto maskVal = cir::ConstantOp::create(
          builder, loc, int32Ty,
          builder.getSInt32AttrLocalCached(featureMask[i]));
      auto andResult = cir::BinOp::create(
          builder, loc, int32Ty, cir::BinOpKind::And, feat2Val, maskVal,
          {}, {}, {}, {}, {}, {});
      auto thisCond = cir::CmpOp::create(
          builder, loc, cir::CmpOpKind::eq, andResult, maskVal);
      if (cond)
        cond = cir::BinOp::create(builder, loc, boolTy,
                                   cir::BinOpKind::And, cond, thisCond,
                                   {}, {}, {}, {}, {}, {});
      else
        cond = thisCond;
    }
    return cond;
  };

  // Build the if-else-trap decision tree. Options are already sorted by
  // priority (highest first). For each option:
  //   - If default (null cond): return unconditionally.
  //   - If conditional: branch to return block if match, else fall through.
  // If no option matched, trap + unreachable.
  for (const auto &opt : options) {
    mlir::Value cond = emitFeatureCheck(opt);
    mlir::Value fnAddr = cir::GetGlobalOp::create(
        builder, loc, funcPtrType, opt.mangledName);

    if (!cond) {
      // Default entry: return unconditionally.
      cir::ReturnOp::create(builder, loc, fnAddr);
      // Remaining options are dead code after default; stop emitting.
      // (OGCG stops emitting after a ConstantInt true condition too.)
      goto resolver_done;
    }

    // Conditional: if features match, return; else fall through.
    // Create the return block and the else (fall-through) block.
    auto &region = resolverFunc.getBody();
    auto *curBlock = builder.getInsertionBlock();
    auto *retBlock = new mlir::Block();
    region.push_back(retBlock);
    auto *elseBlock = new mlir::Block();
    region.push_back(elseBlock);

    // Terminate the current block with a conditional branch.
    builder.setInsertionPointToEnd(curBlock);
    cir::BrCondOp::create(builder, loc, cond, retBlock, elseBlock);

    // Emit return in the retBlock.
    builder.setInsertionPointToStart(retBlock);
    cir::ReturnOp::create(builder, loc, fnAddr);

    // Continue emitting in elseBlock.
    builder.setInsertionPointToStart(elseBlock);
  }

  // No default variant — trap (which is a terminator in CIR, equivalent to
  // OGCG's llvm.trap + unreachable).
  cir::TrapOp::create(builder, loc);

resolver_done:

  // Set resolver attributes. Matches OGCG setMultiVersionResolverAttributes.
  resolverFunc.setLinkage(getMultiversionLinkage(*this, gd));
  setGlobalVisibility(resolverFunc, fd);
  setDSOLocal(resolverFunc.getOperation());

  // Create the ifunc (Linux with IFunc support).
  // OGCG creates both an ifunc (<base>.ifunc) and an alias (<base>) pointing
  // to it. We name the CIR ifunc with the base name (for call-site
  // references), and record the name so the LLVM translation step can rename
  // the ifunc to <base>.ifunc and create the <base> alias.
  if (getTarget().supportsIFunc()) {
    auto linkage = getMultiversionLinkage(*this, gd);

    // Create the ifunc: <base> -> <base>.resolver
    auto ifuncFunc = getOrCreateCIRFunction(
        baseName, funcType, GlobalDecl{}, /*forVTable=*/false,
        /*dontDefer=*/false, /*isThunk=*/false, NotForDefinition);
    ifuncFunc.setAliasee(resolverName);
    ifuncFunc->setAttr("cir.ifunc", builder.getUnitAttr());
    ifuncFunc.setLinkage(linkage);
    setFunctionAttributes(gd, ifuncFunc, /*isIncompleteFunction=*/false,
                          /*isThunk=*/false);
    addSyntheticCallsiteNonnullAttrs(*this, gd, ifuncFunc,
                                     /*isThunk=*/false);
    // Match OGCG SetCommonAttributes → setDSOLocal: internal linkage ifuncs
    // must be dso_local; weak_odr ifuncs under PIC are not.
    setDSOLocal(ifuncFunc.getOperation());

    // Record for alias creation in LLVM translation step.
    appendUniqueModuleStringAttr(getModule(), builder,
                                 kCPUDispatchIFuncAliasesAttr, baseName);
  }
}

static FunctionDecl *
createOpenACCBindTempFunction(ASTContext &ctx, const IdentifierInfo *bindName,
                              const FunctionDecl *protoFunc) {
  // If this is a C no-prototype function, we can take the 'easy' way out and
  // just create a function with no arguments/functions, etc.
  if (!protoFunc->hasPrototype())
    return FunctionDecl::Create(
        ctx, /*DC=*/ctx.getTranslationUnitDecl(),
        /*StartLoc=*/SourceLocation{}, /*NLoc=*/SourceLocation{}, bindName,
        protoFunc->getType(), /*TInfo=*/nullptr, StorageClass::SC_None);

  QualType funcTy = protoFunc->getType();
  auto *fpt = cast<FunctionProtoType>(protoFunc->getType());

  // If this is a member function, add an explicit 'this' to the function type.
  if (auto *methodDecl = dyn_cast<CXXMethodDecl>(protoFunc);
      methodDecl && methodDecl->isImplicitObjectMemberFunction()) {
    llvm::SmallVector<QualType> paramTypes{fpt->getParamTypes()};
    paramTypes.insert(paramTypes.begin(), methodDecl->getThisType());

    funcTy = ctx.getFunctionType(fpt->getReturnType(), paramTypes,
                                 fpt->getExtProtoInfo());
    fpt = cast<FunctionProtoType>(funcTy);
  }

  auto *tempFunc =
      FunctionDecl::Create(ctx, /*DC=*/ctx.getTranslationUnitDecl(),
                           /*StartLoc=*/SourceLocation{},
                           /*NLoc=*/SourceLocation{}, bindName, funcTy,
                           /*TInfo=*/nullptr, StorageClass::SC_None);

  SmallVector<ParmVarDecl *> params;
  params.reserve(fpt->getNumParams());

  // Add all of the parameters.
  for (unsigned i = 0, e = fpt->getNumParams(); i != e; ++i) {
    ParmVarDecl *parm = ParmVarDecl::Create(
        ctx, tempFunc, /*StartLoc=*/SourceLocation{},
        /*IdLoc=*/SourceLocation{},
        /*Id=*/nullptr, fpt->getParamType(i), /*TInfo=*/nullptr,
        StorageClass::SC_None, /*DefArg=*/nullptr);
    parm->setScopeInfo(0, i);
    params.push_back(parm);
  }

  tempFunc->setParams(params);

  return tempFunc;
}

std::string
CIRGenModule::getOpenACCBindMangledName(const IdentifierInfo *bindName,
                                        const FunctionDecl *attachedFunction) {
  FunctionDecl *tempFunc = createOpenACCBindTempFunction(
      getASTContext(), bindName, attachedFunction);

  std::string ret = getMangledNameImpl(*this, GlobalDecl(tempFunc), tempFunc);

  // This does nothing (it is a do-nothing function), since this is a
  // slab-allocator, but leave a call in to immediately destroy this in case we
  // ever come up with a way of getting allocations back.
  getASTContext().Deallocate(tempFunc);
  return ret;
}

StringRef CIRGenModule::getMangledName(GlobalDecl gd) {
  GlobalDecl canonicalGd = getCanonicalDeclForMangledName(gd, getTarget());

  // Fast path: most lookups are repeated queries for the same canonical decl.
  // Reuse the cached mangled name instead of remangling.
  auto existing = mangledDeclNames.find(canonicalGd);
  if (existing != mangledDeclNames.end())
    return existing->second;

  // Keep the first result in the case of a mangling collision.
  const auto *nd = cast<NamedDecl>(gd.getDecl());
  std::string mangledName = getMangledNameImpl(*this, gd, nd);

  auto result = manglings.insert(std::make_pair(mangledName, gd));
  return mangledDeclNames[canonicalGd] = result.first->first();
}

void CIRGenModule::forEachMangledDecl(
    llvm::function_ref<void(llvm::StringRef, clang::GlobalDecl)> Callback)
    const {
  for (const auto &[GD, MangledName] : mangledDeclNames)
    Callback(MangledName, GD);
}

GlobalDecl CIRGenModule::getCanonicalDeclForMangledName(
    GlobalDecl gd, const TargetInfo &target) {
  GlobalDecl canonicalGd = gd.getCanonicalDecl();

  // Some ABIs don't have constructor variants. Make sure that base and
  // complete constructors get mangled the same.
  if (const auto *cd = dyn_cast<CXXConstructorDecl>(canonicalGd.getDecl())) {
    if (!target.getCXXABI().hasConstructorVariants()) {
      CXXCtorType origCtorType = gd.getCtorType();
      assert((origCtorType == Ctor_Base || origCtorType == Ctor_Complete) &&
             "unexpected constructor variant for ABI without constructor "
             "variants");
      if (origCtorType == Ctor_Base)
        canonicalGd = GlobalDecl(cd, Ctor_Complete);
    }
  }

  return canonicalGd;
}

void CIRGenModule::recordDeferredInlineMemberCandidate(GlobalDecl gd,
                                                       StringRef mangledName) {
  GlobalDecl canonicalGd = getCanonicalDeclForMangledName(gd, getTarget());
  if (!deferredInlineMemberDeclCandidateSet.insert(canonicalGd).second)
    return;
  deferredInlineMemberDeclCandidates.push_back({canonicalGd, mangledName});
}

void CIRGenModule::recordDeferredImplicitTemplateRetryCandidate(
    GlobalDecl gd, StringRef mangledName) {
  GlobalDecl canonicalGd = getCanonicalDeclForMangledName(gd, getTarget());
  if (!deferredImplicitTemplateRetrySet.insert(canonicalGd).second)
    return;
  deferredImplicitTemplateRetryCandidates.push_back({canonicalGd, mangledName});
}

void CIRGenModule::emitTentativeDefinition(const VarDecl *d) {
  assert(!d->getInit() && "Cannot emit definite definitions here!");

  StringRef mangledName = getMangledName(d);
  mlir::Operation *gv = getGlobalValue(mangledName);

  // If we already have a definition, not declaration, with the same mangled
  // name, emitting of declaration is not required (and would actually overwrite
  // the emitted definition).
  if (gv && !mlir::cast<cir::GlobalOp>(gv).isDeclaration())
    return;

  // If we have not seen a reference to this variable yet, place it into the
  // deferred declarations table to be emitted if needed later.
  if (!mustBeEmitted(d) && !gv) {
    deferredDecls[mangledName] = d;
    return;
  }

  // The tentative definition is the only definition.
  emitGlobalVarDefinition(d);
}

bool CIRGenModule::mustBeEmitted(const ValueDecl *global) {
  // Never defer when EmitAllDecls is specified.
  if (langOpts.EmitAllDecls)
    return true;

  const auto *vd = dyn_cast<VarDecl>(global);
  if (vd &&
      ((codeGenOpts.KeepPersistentStorageVariables &&
        (vd->getStorageDuration() == SD_Static ||
         vd->getStorageDuration() == SD_Thread)) ||
       (codeGenOpts.KeepStaticConsts && vd->getStorageDuration() == SD_Static &&
        vd->getType().isConstQualified())))
    return true;

  return getASTContext().DeclMustBeEmitted(global);
}

bool CIRGenModule::mayBeEmittedEagerly(const ValueDecl *global) {
  // In OpenMP 5.0 variables and function may be marked as
  // device_type(host/nohost) and we should not emit them eagerly unless we sure
  // that they must be emitted on the host/device. To be sure we need to have
  // seen a declare target with an explicit mentioning of the function, we know
  // we have if the level of the declare target attribute is -1. Note that we
  // check somewhere else if we should emit this at all.
  if (langOpts.OpenMP >= 50 && !langOpts.OpenMPSimd) {
    std::optional<OMPDeclareTargetDeclAttr *> activeAttr =
        OMPDeclareTargetDeclAttr::getActiveAttr(global);
    if (!activeAttr || (*activeAttr)->getLevel() != (unsigned)-1)
      return false;
  }

  const auto *fd = dyn_cast<FunctionDecl>(global);
  if (fd) {
    // Implicit template instantiations may change linkage if they are later
    // explicitly instantiated, so they should not be emitted eagerly.
    if (fd->getTemplateSpecializationKind() == TSK_ImplicitInstantiation)
      return false;
    // Implicit special member functions (copy/move ctors, assignment operators)
    // with linkonce_odr linkage should be deferred — they're instantiated by
    // Sema but may never be called. Emitting them eagerly produces extra code.
    // OGCG defers these by not having them enter EmitGlobal at all; CIR
    // achieves the same by deferring them here.
    if (fd->isImplicit() && !fd->isUsed(/*CheckUsedAttr=*/false)) {
      if (auto *md = dyn_cast<CXXMethodDecl>(fd)) {
        if (md->isCopyAssignmentOperator() || md->isMoveAssignmentOperator())
          return false;
      }
    }
    // Defer until all versions have been semantically checked.
    if (fd->hasAttr<TargetVersionAttr>() && !fd->isMultiVersion())
      return false;
    if (langOpts.SYCLIsDevice) {
      errorNYI(fd->getSourceRange(), "mayBeEmittedEagerly: SYCL");
    }
  }
  const auto *vd = dyn_cast<VarDecl>(global);
  if (vd)
    if (astContext.getInlineVariableDefinitionKind(vd) ==
        ASTContext::InlineVariableDefinitionKind::WeakUnknown)
      // A definition of an inline constexpr static data member may change
      // linkage later if it's redeclared outside the class.
      return false;

  // OGCG checks CXX20ModuleInits here: module-owned variable initializers
  // must be deferred so they run in the correct module initialization order.
  assert(!cir::MissingFeatures::mayBeEmittedEagerlyCXX20ModuleInits());

  // If OpenMP is enabled and threadprivates must be generated like TLS, delay
  // codegen for global variables, because they may be marked as threadprivate.
  if (langOpts.OpenMP && langOpts.OpenMPUseTLS &&
      astContext.getTargetInfo().isTLSSupported() && isa<VarDecl>(global) &&
      !global->getType().isConstantStorage(astContext, false, false) &&
      !OMPDeclareTargetDeclAttr::isDeclareTargetDeclaration(global))
    return false;

  assert((fd || vd) &&
         "Only FunctionDecl and VarDecl should hit this path so far.");
  return true;
}

static bool shouldAssumeDSOLocal(const CIRGenModule &cgm,
                                 cir::CIRGlobalValueInterface gv) {
  auto hasDLLImportStorageClass = [&]() {
    if (auto globalOp = dyn_cast<cir::GlobalOp>(gv.getOperation())) {
      if (auto dllAttr =
              globalOp->getAttrOfType<mlir::StringAttr>("cir.dll_storage_class"))
        return dllAttr.getValue() == "dllimport";
      return false;
    }

    auto funcOp = dyn_cast<cir::FuncOp>(gv.getOperation());
    if (!funcOp)
      return false;
    auto extraAttrs = funcOp.getExtraAttrs();
    if (!extraAttrs)
      return false;
    mlir::NamedAttrList attrs{extraAttrs->getElements().getValue()};
    auto dllAttr = mlir::dyn_cast_or_null<mlir::StringAttr>(
        attrs.get("dll_storage_class"));
    return dllAttr && dllAttr.getValue() == "dllimport";
  };

  if (gv.hasLocalLinkage())
    return true;

  if (!gv.hasDefaultVisibility() && !gv.hasExternalWeakLinkage())
    return true;

  // In OGCG, isa<llvm::Function>(GV) returns false for GlobalAlias. In CIR,
  // function aliases are still FuncOps with an aliasee attribute. We must
  // exclude them from function-specific checks to match OGCG behavior.
  auto funcOp = dyn_cast<cir::FuncOp>(gv.getOperation());
  bool isIFunc = funcOp && funcOp->hasAttr("cir.ifunc");
  bool isRealFunction = funcOp && !funcOp.getAliasee() && !isIFunc;

  // DLLImport explicitly marks the GV as external.
  if (hasDLLImportStorageClass())
    return false;

  const llvm::Triple &tt = cgm.getTriple();
  const CodeGenOptions &cgOpts = cgm.getCodeGenOpts();
  if (tt.isOSCygMing()) {
    // In MinGW and Cygwin, variables without DLLImport can still be
    // automatically imported from a DLL by the linker; don't mark variables
    // that potentially could come from another DLL as DSO local.

    // With EmulatedTLS, TLS variables can be autoimported from other DLLs
    // (and this actually happens in the public interface of libstdc++), so
    // such variables can't be marked as DSO local. (Native TLS variables
    // can't be dllimported at all, though.)
    if (auto globalOp = dyn_cast<cir::GlobalOp>(gv.getOperation()))
      if (gv.isDeclarationForLinker() &&
          (!globalOp.getTlsModelAttr() || cgOpts.EmulatedTLS) &&
          cgOpts.AutoImport)
        return false;
  }

  // On COFF, don't mark 'extern_weak' symbols as DSO local. If these symbols
  // remain unresolved in the link, they can be resolved to zero, which is
  // outside the current DSO.
  if (tt.isOSBinFormatCOFF() && gv.hasExternalWeakLinkage())
    return false;

  // Every other GV is local on COFF.
  // Make an exception for windows OS in the triple: Some firmware builds use
  // *-win32-macho triples. This (accidentally?) produced windows relocations
  // without GOT tables in older clang versions; Keep this behaviour.
  // FIXME: even thread local variables?
  if (tt.isOSBinFormatCOFF() || (tt.isOSWindows() && tt.isOSBinFormatMachO()))
    return true;

  // Only handle COFF and ELF for now.
  if (!tt.isOSBinFormatELF())
    return false;

  llvm::Reloc::Model rm = cgOpts.RelocationModel;
  const LangOptions &lOpts = cgm.getLangOpts();
  if (rm != llvm::Reloc::Static && !lOpts.PIE) {
    // On ELF, if -fno-semantic-interposition is specified and the target
    // supports local aliases, there will be neither CC1
    // -fsemantic-interposition nor -fhalf-no-semantic-interposition. Set
    // dso_local on the function if using a local alias is preferable (can avoid
    // PLT indirection).
    if (!(isRealFunction && gv.canBenefitFromLocalAlias()))
      return false;
    return !(lOpts.SemanticInterposition || lOpts.HalfNoSemanticInterposition);
  }

  // A definition cannot be preempted from an executable.
  // CIR models ifuncs as declaration-shaped FuncOps with `cir.ifunc` so they
  // can satisfy CallOp's callee verifier. Match LLVM GlobalIFunc semantics by
  // treating them as definitions for dso_local decisions.
  if (isIFunc || !gv.isDeclarationForLinker())
    return true;

  // Most PIC code sequences that assume that a symbol is local cannot produce a
  // 0 if it turns out the symbol is undefined. While this is ABI and relocation
  // depended, it seems worth it to handle it here.
  if (rm == llvm::Reloc::PIC_ && gv.hasExternalWeakLinkage())
    return false;

  // PowerPC64 prefers TOC indirection to avoid copy relocations.
  if (tt.isPPC64())
    return false;

  if (cgOpts.DirectAccessExternalData) {
    // If -fdirect-access-external-data (default for -fno-pic), set dso_local
    // for non-thread-local variables. If the symbol is not defined in the
    // executable, a copy relocation will be needed at link time. dso_local is
    // excluded for thread-local variables because they generally don't support
    // copy relocations.
    if (auto globalOp = dyn_cast<cir::GlobalOp>(gv.getOperation())) {
      // TLS globals have tls_model set on the GlobalOp.
      if (!globalOp.getTlsModelAttr())
        return true;
    }

    // -fno-pic sets dso_local on a function declaration to allow direct
    // accesses when taking its address (similar to a data symbol). If the
    // function is not defined in the executable, a canonical PLT entry will be
    // needed at link time. -fno-direct-access-external-data can avoid the
    // canonical PLT entry. We don't generalize this condition to -fpie/-fpic as
    // it could just cause trouble without providing perceptible benefits.
    // In OGCG this checks isa<llvm::Function>(GV) — excludes GlobalAlias.
    if (isRealFunction && !cgOpts.NoPLT && rm == llvm::Reloc::Static)
      return true;
  }

  // If we can use copy relocations we can assume it is local.

  // Otherwise don't assume it is local.

  return false;
}

void CIRGenModule::setGlobalVisibility(mlir::Operation *op,
                                       const NamedDecl *d) const {
  auto gv = dyn_cast<cir::CIRGlobalValueInterface>(op);
  if (!gv)
    return;

  // Internal definitions always have default visibility.
  if (gv.hasLocalLinkage()) {
    if (auto globalOp = dyn_cast<cir::GlobalOp>(op)) {
      globalOp.setGlobalVisibility(cir::VisibilityKind::Default);
      mlir::SymbolTable::setSymbolVisibility(globalOp,
                                             getMLIRVisibility(globalOp));
    } else if (auto funcOp = dyn_cast<cir::FuncOp>(op)) {
      funcOp.setGlobalVisibility(cir::VisibilityKind::Default);
      mlir::SymbolTable::setSymbolVisibility(
          funcOp, getMLIRVisibilityFromCIRLinkage(funcOp.getLinkage()));
    }
    return;
  }
  if (!d)
    return;

  // Set visibility for definitions, and for declarations if requested globally
  // or set explicitly.
  LinkageInfo lv = d->getLinkageAndVisibility();

  // OGCG also handles DLL import/export visibility here. Windows-only.

  if (lv.isVisibilityExplicit() || getLangOpts().SetVisibilityForExternDecls ||
      !gv.isDeclarationForLinker()) {
    if (auto globalOp = dyn_cast<cir::GlobalOp>(op))
      globalOp.setGlobalVisibility(getCIRVisibilityKind(lv.getVisibility()));
    else if (auto funcOp = dyn_cast<cir::FuncOp>(op))
      funcOp.setGlobalVisibility(getCIRVisibilityKind(lv.getVisibility()));
  }
}

void CIRGenModule::setDSOLocal(cir::CIRGlobalValueInterface gv) const {
  gv.setDSOLocal(shouldAssumeDSOLocal(*this, gv));
}

void CIRGenModule::setDSOLocal(mlir::Operation *op) const {
  if (auto globalValue = dyn_cast<cir::CIRGlobalValueInterface>(op))
    setDSOLocal(globalValue);
}

void CIRGenModule::setGVProperties(mlir::Operation *op,
                                   const NamedDecl *d) const {
  // Set DLL storage class for dllexport/dllimport.
  // Matches OGCG's setDLLImportDLLExport (CodeGenModule.cpp:1961-1970).
  // Handles both GlobalOp and FuncOp (declarations AND definitions).
  // FuncOp definitions also get DLL attrs in setCIRFunctionAttributesForDefinition,
  // but declarations only go through this path.
  if (d && d->isExternallyVisible()) {
    if (auto gv = mlir::dyn_cast<cir::GlobalOp>(op)) {
      if (d->hasAttr<DLLImportAttr>())
        gv->setAttr("cir.dll_storage_class", getStringAttrCached("dllimport"));
      else if ((d->hasAttr<DLLExportAttr>() ||
                shouldMapVisibilityToDLLExport(d)) &&
               !gv.isDeclarationForLinker())
        gv->setAttr("cir.dll_storage_class", getStringAttrCached("dllexport"));
    } else if (auto fn = mlir::dyn_cast<cir::FuncOp>(op)) {
      // For FuncOp, set DLL attrs via extra_attrs dict (same mechanism as
      // setCIRFunctionAttributesForDefinition). This handles declarations
      // that setCIRFunctionAttributesForDefinition doesn't reach.
      mlir::NamedAttrList attrs;
      if (auto existingAttrs = fn.getExtraAttrs())
        attrs = mlir::NamedAttrList{existingAttrs->getElements().getValue()};
      bool changed = false;
      if (d->hasAttr<DLLImportAttr>() && !attrs.get("dll_storage_class")) {
        attrs.set("dll_storage_class", getStringAttrCached("dllimport"));
        changed = true;
      } else if ((d->hasAttr<DLLExportAttr>() ||
                  shouldMapVisibilityToDLLExport(d)) &&
                 !fn.isDeclaration() && !attrs.get("dll_storage_class")) {
        attrs.set("dll_storage_class", getStringAttrCached("dllexport"));
        changed = true;
      }
      if (changed)
        fn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
            attrs.getDictionary(op->getContext())));
    }
  }
  setGVPropertiesAux(op, d);
}

void CIRGenModule::setGVPropertiesAux(mlir::Operation *op,
                                      const NamedDecl *d) const {
  setGlobalVisibility(op, d);
  setDSOLocal(op);
  // LLVM global `partition` is not represented in CIR global emission.
}

cir::TLS_Model CIRGenModule::getDefaultCIRTLSModel() const {
  switch (getCodeGenOpts().getDefaultTLSModel()) {
  case CodeGenOptions::GeneralDynamicTLSModel:
    return cir::TLS_Model::GeneralDynamic;
  case CodeGenOptions::LocalDynamicTLSModel:
    return cir::TLS_Model::LocalDynamic;
  case CodeGenOptions::InitialExecTLSModel:
    return cir::TLS_Model::InitialExec;
  case CodeGenOptions::LocalExecTLSModel:
    return cir::TLS_Model::LocalExec;
  }
  llvm_unreachable("Invalid TLS model!");
}

void CIRGenModule::setTLSMode(mlir::Operation *op, const VarDecl &d) {
  assert(d.getTLSKind() && "setting TLS mode on non-TLS var!");

  cir::TLS_Model tlm = getDefaultCIRTLSModel();

  // Override the TLS model if it is explicitly specified.
  if (const TLSModelAttr *attr = d.getAttr<TLSModelAttr>()) {
    tlm = llvm::StringSwitch<cir::TLS_Model>(attr->getModel())
              .Case("global-dynamic", cir::TLS_Model::GeneralDynamic)
              .Case("local-dynamic", cir::TLS_Model::LocalDynamic)
              .Case("initial-exec", cir::TLS_Model::InitialExec)
              .Case("local-exec", cir::TLS_Model::LocalExec);
  }

  auto global = cast<cir::GlobalOp>(op);
  global.setTlsModel(tlm);
}

void CIRGenModule::setCIRFunctionAttributes(GlobalDecl globalDecl,
                                            const CIRGenFunctionInfo &info,
                                            cir::FuncOp func, bool isThunk) {
  // TODO(cir): More logic of constructAttributeList is needed.
  cir::CallingConv callingConv;
  cir::SideEffect sideEffect;

  // Initialize PAL with existing extra attributes to merge attributes.
  mlir::NamedAttrList pal;
  if (auto existingExtraAttrs = func.getExtraAttrs())
    pal = mlir::NamedAttrList{existingExtraAttrs->getElements().getValue()};
  constructAttributeList(func.getName(), info, globalDecl, pal, callingConv,
                         sideEffect,
                         /*attrOnCallSite=*/false, isThunk);

  // Store side effect (const/pure) on the function definition so the lowering
  // pass can emit willreturn and memory(none/read) on the LLVM function.
  if (sideEffect != cir::SideEffect::All)
    pal.set("side_effect",
            cir::SideEffectAttr::get(&getMLIRContext(), sideEffect));

  if (pal.get("clspv_libclc_builtin")) {
    func->setAttr("cir.clspv_libclc_builtin",
                  mlir::UnitAttr::get(&getMLIRContext()));
    pal.erase("clspv_libclc_builtin");
  }

  func.setExtraAttrsAttr(
      cir::ExtraFuncAttributesAttr::get(pal.getDictionary(&getMLIRContext())));

  // TODO(cir): Check X86_VectorCall incompatibility wiht WinARM64EC
  //
  // Runtime helpers synthesized without a backing FunctionDecl may already
  // carry an explicit runtime calling convention (for example ARM AAPCS on
  // cross-DSO CFI helpers). Preserve that assignment instead of resetting them
  // to the default C convention reconstructed from a null GlobalDecl.
  if (globalDecl.getDecl() || func.getCallingConv() == cir::CallingConv::C)
    func.setCallingConv(callingConv);

  // Carry __attribute__((regparm(N))) onto the FuncOp so the X86-32 ABI
  // lowering pass can mark the register-passed parameters `inreg`. Only emit
  // when the attribute is present (getHasRegParm), so that regparm(0) — an
  // explicit "0 register parameters" request — stays distinct from no
  // attribute, mirroring clang::FunctionType::ExtInfo's +1 encoding.
  if (info.getExtInfo().getHasRegParm())
    func->setAttr("cir.regparm",
                  mlir::IntegerAttr::get(
                      mlir::IntegerType::get(&getMLIRContext(), 32),
                      info.getExtInfo().getRegParm()));

  // Win32 x86 direct-CIR currently skips ABI lowering entirely, so preserve
  // the primitive fastcall/vectorcall register ABI marker that OGCG would
  // attach during x86-32 ABI classification.
  if (getTarget().getTriple().getArch() == llvm::Triple::x86 &&
      getTarget().getTriple().getOS() == llvm::Triple::Win32 &&
      (callingConv == cir::CallingConv::X86FastCall ||
       callingConv == cir::CallingConv::X86VectorCall)) {
    unsigned freeRegs = 2;
    auto unitAttr = mlir::UnitAttr::get(&getMLIRContext());
    for (auto [argIdx, argType] : llvm::enumerate(info.argTypes())) {
      if (argIdx >= func.getNumArguments() || freeRegs == 0)
        break;

      if (getASTContext().getTypeSize(argType) > 32)
        continue;
      if (!argType->isIntegralOrEnumerationType() &&
          !argType->isAnyPointerType() &&
          !argType.getTypePtr()->isReferenceType())
        continue;

      func.setArgAttr(argIdx, "cir.inreg", unitAttr);
      --freeRegs;
    }
  }

  // Add nonnull/dereferenceable/align attributes for reference return types.
  // References are non-null pointers to dereferenceable, aligned memory.
  // These attributes come from C++ language semantics, not ABI lowering, so
  // they must be set here in CIRGen where QualType information is available.
  // Matches OGCG CGCall.cpp:2739-2755.
  if (!isThunk && func.getNumResults() > 0) {
    QualType retType = info.getReturnType();
    if (const auto *refTy = retType->getAs<ReferenceType>()) {
      QualType pointeeTy = refTy->getPointeeType();
      // dereferenceable(N) — minimum object size (non-virtual size for
      // non-final classes). Matches OGCG getMinimumObjectSize.
      if (!pointeeTy->isIncompleteType() && pointeeTy->isConstantSizeType()) {
        uint64_t size = getMinimumObjectSize(pointeeTy).getQuantity();
        // LLVM's addDereferenceableAttr silently drops size 0 (e.g. function
        // types). Match that behavior to avoid spurious dereferenceable(0).
        if (size > 0)
          func.setResultAttr(
              0, "cir.dereferenceable",
              mlir::IntegerAttr::get(
                  mlir::IntegerType::get(&getMLIRContext(), 64), size));
      }
      // nonnull — references cannot be null (only for default address space).
      // Matches OGCG CGCall.cpp:2746-2748.
      if (getASTContext().getTargetAddressSpace(
              pointeeTy.getAddressSpace()) == 0 &&
          !getCodeGenOpts().NullPointerIsValid)
        func.setResultAttr(0, "cir.nonnull",
                           mlir::UnitAttr::get(&getMLIRContext()));
      // align N — natural alignment of the pointee type.
      // Uses getNaturalTypeAlignment which handles incomplete array types
      // (via getBaseElementType), CXX record non-virtual alignment, etc.
      // Matches OGCG's getNaturalPointeeTypeAlignment call in
      // CGCall.cpp:2751.
      if (pointeeTy->isObjectType()) {
        unsigned align =
            getNaturalTypeAlignment(pointeeTy, /*baseInfo=*/nullptr,
                                    /*tbaaInfo=*/nullptr,
                                    /*forPointeeType=*/true)
                .getQuantity();
        func.setResultAttr(
            0, "cir.align",
            mlir::IntegerAttr::get(
                mlir::IntegerType::get(&getMLIRContext(), 64), align));
      }
    }
  }

  // Add noundef on 'this' pointer for instance methods.
  // Target lowering adds noundef for function definitions but skips
  // declarations. For declarations (which never go through target lowering),
  // we must set it here. Matches OGCG CGCall.cpp:2838 which adds NoUndef
  // to all params including 'this' in constructAttributeList.
  if (!isThunk && info.isInstanceMethod() && func.getNumArguments() > 0) {
    // 'this' is always arg 0 for instance methods.
    // Only add if not already present (target lowering may have set it for
    // definitions).
    if (!func.getArgAttr(0, "cir.noundef"))
      func.setArgAttr(0, "cir.noundef",
                       mlir::UnitAttr::get(&getMLIRContext()));
  }

  // Mark parameters whose AST type is promotable for ABI purposes but whose
  // CIR type doesn't capture this (e.g. wchar_t, char32_t are 32-bit integers
  // indistinguishable from 'int' in CIR, but C/C++ requires them to be
  // sign/zero-extended). The target lowering's isPromotableIntegerType() only
  // checks width < 32, so these need explicit marking.
  // Note: Only mark BuiltinType promotables (wchar_t, char32_t, etc.), not
  // enum types — CIR's target lowering handles enum promotability separately
  // via its own isPromotableIntegerType, and marking enums here would
  // incorrectly add signext/zeroext to types OGCG classifies as Direct.
  for (auto [argIdx, argType] : llvm::enumerate(info.argTypes())) {
    if (argIdx >= func.getNumArguments())
      break;
    if (isa<BuiltinType>(argType) &&
        astContext.isPromotableIntegerType(argType)) {
      mlir::Type cirTy = getTypes().convertType(argType);
      if (auto intTy = mlir::dyn_cast<cir::IntType>(cirTy))
        if (intTy.getWidth() >= 32)
          func.setArgAttr(argIdx, "cir.promotable",
                          mlir::UnitAttr::get(&getMLIRContext()));
    }
  }

  // Same for the return type: if it's promotable but width >= 32, mark it.
  QualType retType = info.getReturnType();
  if (!retType->isVoidType() && func.getNumResults() > 0 &&
      retType->isBuiltinType() &&
      astContext.isPromotableIntegerType(retType)) {
    mlir::Type cirTy = getTypes().convertType(retType);
    if (auto intTy = mlir::dyn_cast<cir::IntType>(cirTy))
      if (intTy.getWidth() >= 32)
        func.setResultAttr(0, "cir.promotable",
                           mlir::UnitAttr::get(&getMLIRContext()));
  }

  // Mark _Atomic aggregate parameters so the ABI lowering pass can force
  // indirect passing on targets where AtomicType changes the classification
  // (e.g., x86_64 where _Atomic(struct S) falls through to Memory class).
  // CIR strips AtomicType during type conversion, so this attribute carries
  // the information to the TargetLowering pass. Also set in
  // emitFunctionProlog for definitions, but declarations don't go through
  // emitFunctionProlog, so we handle them here too.
  //
  // Also mark __mfp8 parameters. CIR converts __mfp8 to <1 x i8>
  // which the ABI classifier treats as an illegal vector and coerces to i32.
  // In OGCG, __mfp8 is BuiltinType::MFloat8 (not VectorType) so the ABI
  // classifier passes it directly as <1 x i8>.
  for (auto [argIdx, argType] : llvm::enumerate(info.argTypes())) {
    if (argIdx >= func.getNumArguments())
      break;
    if (isa<AtomicType>(argType)) {
      QualType valueType = cast<AtomicType>(argType)->getValueType();
      if (valueType->isRecordType())
        func.setArgAttr(argIdx, "cir.atomic",
                        mlir::UnitAttr::get(&getMLIRContext()));
    }
    if (isa<BuiltinType>(argType) &&
        cast<BuiltinType>(argType)->getKind() == BuiltinType::MFloat8)
      func.setArgAttr(argIdx, "cir.mfp8",
                      mlir::UnitAttr::get(&getMLIRContext()));
  }

  // Mark _Atomic aggregate return type for indirect passing.
  if (const auto *atomicRet = retType->getAs<AtomicType>()) {
    if (atomicRet->getValueType()->isRecordType())
      func->setAttr("cir.atomic_ret",
                     mlir::UnitAttr::get(&getMLIRContext()));
  }

  // Mark __mfp8 return type.
  if (isa<BuiltinType>(retType) &&
      cast<BuiltinType>(retType)->getKind() == BuiltinType::MFloat8)
    func->setAttr("cir.mfp8_ret",
                   mlir::UnitAttr::get(&getMLIRContext()));
}

void CIRGenModule::setInternalFunctionAttributes(
    GlobalDecl gd, cir::FuncOp func, const CIRGenFunctionInfo &info,
    bool isThunk) {
  setCIRFunctionAttributes(gd, info, func, isThunk);
  getTargetCIRGenInfo().setTargetAttributes(gd.getDecl(), func, *this);

  if (isa_and_nonnull<BlockDecl>(gd.getDecl())) {
    // Block invokes are decl-backed internal functions, but the definition
    // helper currently only has a FunctionDecl path and a decl-less helper
    // path. Reuse the helper path for common definition attrs, then restore
    // the O0 optnone/noinline behavior that OGCG applies to BlockDecl-backed
    // internal functions.
    setCIRFunctionAttributesForDefinition(GlobalDecl{}, func, isThunk);
    if (!codeGenOpts.DisableO0ImplyOptNone &&
        codeGenOpts.OptimizationLevel == 0) {
      func.setOptNone(true);
      func.setInlineKind(cir::InlineKind::NoInline);
    }
  } else {
    setCIRFunctionAttributesForDefinition(gd, func, isThunk);
  }

  func.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
}

void CIRGenModule::addFunctionTypeMetadataForIcall(const FunctionDecl *fd,
                                                   cir::FuncOp func) {
  // Only if we are checking indirect calls.
  if (!fd || !langOpts.Sanitize.has(SanitizerKind::CFIICall))
    return;

  // Non-static class methods are handled via vtable/member-function checks.
  if (const auto *md = dyn_cast<CXXMethodDecl>(fd); md && !md->isStatic())
    return;

  QualType fnType = generalizeFunctionTypeForCfiIcall(
      astContext, fd->getType(), /*generalizePointers=*/false);
  QualType genPtrFnType = generalizeFunctionTypeForCfiIcall(
      astContext, fd->getType(), /*generalizePointers=*/true);
  bool hiddenTypeId = !isExternallyVisible(fnType->getLinkage());

  auto *ctx = &getMLIRContext();
  auto i64Ty = mlir::IntegerType::get(ctx, 64);
  auto zero = mlir::IntegerAttr::get(i64Ty, 0);

  SmallVector<mlir::Attribute, 4> entries;
  if (auto existing =
          func->getAttrOfType<mlir::ArrayAttr>("cir.func.type_metadata"))
    entries.append(existing.begin(), existing.end());

  auto hasPayload = [&](mlir::Attribute payload) {
    for (mlir::Attribute existing : entries) {
      auto pair = mlir::dyn_cast<mlir::ArrayAttr>(existing);
      if (!pair || pair.size() < 2)
        continue;
      if (pair[1] == payload)
        return true;
    }
    return false;
  };

  auto addPayload = [&](mlir::Attribute payload) {
    if (hasPayload(payload))
      return;
    if (hiddenTypeId) {
      entries.push_back(mlir::ArrayAttr::get(
          ctx, {zero, payload, getStringAttrCached("hidden")}));
      return;
    }
    entries.push_back(mlir::ArrayAttr::get(ctx, {zero, payload}));
  };

  std::string typeId = createStableMetadataIdentifierKeyForType(fnType);
  addPayload(getStringAttrCached(typeId));

  std::string generalizedTypeId =
      createStableMetadataIdentifierKeyForType(genPtrFnType);
  generalizedTypeId += ".generalized";
  addPayload(getStringAttrCached(generalizedTypeId));

  // Emit a hash-based bit set entry for cross-DSO calls.
  if (codeGenOpts.SanitizeCfiCrossDso)
    addPayload(mlir::IntegerAttr::get(i64Ty, llvm::MD5Hash(typeId)));

  if (!entries.empty())
    func->setAttr("cir.func.type_metadata", mlir::ArrayAttr::get(ctx, entries));
}

void CIRGenModule::addFunctionTypeMetadataForCallGraphSection(
    const FunctionDecl *fd, cir::FuncOp func) {
  if (!fd)
    return;

  std::string typeId = createMetadataIdentifierStringForType(
      generalizeFunctionTypeForCfiIcall(astContext, fd->getType(),
                                        /*generalizePointers=*/true));
  if (!typeId.empty())
    typeId += ".generalized";
  auto payload = getStringAttrCached(typeId);
  if (payload.getValue().empty())
    return;

  auto *ctx = &getMLIRContext();
  auto i64Ty = mlir::IntegerType::get(ctx, 64);
  auto zero = mlir::IntegerAttr::get(i64Ty, 0);

  SmallVector<mlir::Attribute, 4> entries;
  if (auto existing =
          func->getAttrOfType<mlir::ArrayAttr>("cir.func.type_metadata"))
    entries.append(existing.begin(), existing.end());

  for (mlir::Attribute existing : entries) {
    auto pair = mlir::dyn_cast<mlir::ArrayAttr>(existing);
    if (!pair || pair.size() != 2)
      continue;
    if (pair[1] == payload)
      return;
  }

  entries.push_back(mlir::ArrayAttr::get(ctx, {zero, payload}));
  func->setAttr("cir.func.call_graph_type_metadata",
                mlir::ArrayAttr::get(ctx, entries));
}

// Generalize a transparent union type to its first field type.
// Matches OGCG CodeGenModule.cpp:2391-2401.
static QualType GeneralizeTransparentUnion(QualType Ty) {
  const RecordType *UT = Ty->getAsUnionType();
  if (!UT)
    return Ty;
  const RecordDecl *UD = UT->getDecl()->getDefinitionOrSelf();
  if (!UD->hasAttr<TransparentUnionAttr>())
    return Ty;
  if (!UD->fields().empty())
    return UD->fields().begin()->getType();
  return Ty;
}

// Generalize a type for CFI pointer generalization.
// Matches OGCG CodeGenModule.cpp:2407-2417.
static QualType GeneralizeType(ASTContext &Ctx, QualType Ty,
                               bool GeneralizePointers) {
  Ty = GeneralizeTransparentUnion(Ty);
  if (!GeneralizePointers || !Ty->isPointerType())
    return Ty;
  return Ctx.getPointerType(
      QualType(Ctx.VoidTy)
          .withCVRQualifiers(Ty->getPointeeType().getCVRQualifiers()));
}

// Generalize a FunctionType's return and argument types.
// Matches OGCG CodeGenModule.cpp:2420-2438.
static QualType GeneralizeFunctionType(ASTContext &Ctx, QualType Ty,
                                       bool GeneralizePointers) {
  if (auto *FnType = Ty->getAs<FunctionProtoType>()) {
    SmallVector<QualType, 8> GeneralizedParams;
    for (auto &Param : FnType->param_types())
      GeneralizedParams.push_back(
          GeneralizeType(Ctx, Param, GeneralizePointers));
    return Ctx.getFunctionType(
        GeneralizeType(Ctx, FnType->getReturnType(), GeneralizePointers),
        GeneralizedParams, FnType->getExtProtoInfo());
  }
  if (auto *FnType = Ty->getAs<FunctionNoProtoType>())
    return Ctx.getFunctionNoProtoType(
        GeneralizeType(Ctx, FnType->getReturnType(), GeneralizePointers));
  llvm_unreachable("Encountered unknown FunctionType");
}

uint32_t CIRGenModule::createKCFITypeId(QualType T, StringRef Salt) {
  // Matches OGCG CodeGenModule::CreateKCFITypeId (CodeGenModule.cpp:2440-2463).
  T = GeneralizeFunctionType(
      getASTContext(), T,
      getCodeGenOpts().SanitizeCfiICallGeneralizePointers);
  if (auto *FnType = T->getAs<FunctionProtoType>())
    T = getASTContext().getFunctionType(
        FnType->getReturnType(), FnType->getParamTypes(),
        FnType->getExtProtoInfo().withExceptionSpec(EST_None));

  std::string OutName;
  llvm::raw_string_ostream Out(OutName);
  getCXXABI().getMangleContext().mangleCanonicalTypeName(
      T, Out, getCodeGenOpts().SanitizeCfiICallNormalizeIntegers);

  if (!Salt.empty())
    Out << "." << Salt;

  if (getCodeGenOpts().SanitizeCfiICallNormalizeIntegers)
    Out << ".normalized";
  if (getCodeGenOpts().SanitizeCfiICallGeneralizePointers)
    Out << ".generalized";

  return llvm::getKCFITypeID(OutName, getCodeGenOpts().SanitizeKcfiHash);
}

uint32_t CIRGenModule::createUBSanFunctionTypeHash(QualType T) {
  // Matches OGCG CodeGenFunction::getUBSanFunctionTypeHash.
  if (!T->isFunctionNoProtoType())
    T = getASTContext().getFunctionTypeWithExceptionSpec(T, EST_None);

  std::string outName;
  llvm::raw_string_ostream out(outName);
  getCXXABI().getMangleContext().mangleCanonicalTypeName(T, out, false);
  return static_cast<uint32_t>(llvm::xxh3_64bits(outName));
}

std::optional<uint32_t>
CIRGenModule::getUBSanFunctionSignature(const FunctionDecl *FD) {
  if (!FD)
    return std::nullopt;
  if (const auto *MD = dyn_cast<CXXMethodDecl>(FD))
    if (!MD->isStatic())
      return std::nullopt;
  return getTargetCIRGenInfo().getUBSanFunctionSignature();
}

uint16_t CIRGenModule::getPointerAuthDeclDiscriminator(GlobalDecl declaration) {
  uint16_t &entityHash = ptrAuthDiscriminatorHashes[declaration];

  if (entityHash == 0) {
    llvm::StringRef name = getMangledName(declaration);
    entityHash = llvm::getPointerAuthStableSipHash(name);
  }

  return entityHash;
}

uint64_t CIRGenModule::getPointerAuthOtherDiscriminator(
    const PointerAuthSchema &schema, GlobalDecl decl, QualType ty) const {
  switch (schema.getOtherDiscrimination()) {
  case PointerAuthSchema::Discrimination::None:
    return 0;
  case PointerAuthSchema::Discrimination::Type:
    assert(!ty.isNull() && "type not provided for type-discriminated schema");
    return getASTContext().getPointerAuthTypeDiscriminator(ty);
  case PointerAuthSchema::Discrimination::Decl:
    assert(decl.getDecl() &&
           "declaration not provided for decl-discriminated schema");
    return const_cast<CIRGenModule *>(this)->getPointerAuthDeclDiscriminator(
        decl);
  case PointerAuthSchema::Discrimination::Constant:
    return schema.getConstantDiscrimination();
  }
  llvm_unreachable("bad discrimination kind");
}

bool CIRGenModule::shouldSignPointer(const PointerAuthSchema &schema) const {
  auto authenticationMode = schema.getAuthenticationMode();
  return authenticationMode == PointerAuthenticationMode::SignAndStrip ||
         authenticationMode == PointerAuthenticationMode::SignAndAuth;
}

std::optional<PointerAuthQualifier>
CIRGenModule::computeVTPointerAuthentication(
    const CXXRecordDecl *record) const {
  if (!record || !record->getDefinition() || !record->isPolymorphic())
    return std::nullopt;

  const PointerAuthSchema &defaultAuthentication =
      getCodeGenOpts().PointerAuth.CXXVTablePointers;
  if (!defaultAuthentication)
    return std::nullopt;

  ASTContext &context = getASTContext();
  const CXXRecordDecl *primaryBase =
      context.baseForVTableAuthentication(record);

  unsigned key = defaultAuthentication.getKey();
  bool addressDiscriminated = defaultAuthentication.isAddressDiscriminated();
  auto defaultDiscrimination = defaultAuthentication.getOtherDiscrimination();
  unsigned typeBasedDiscriminator =
      context.getPointerAuthVTablePointerDiscriminator(primaryBase);

  unsigned discriminator = 0;
  switch (defaultDiscrimination) {
  case PointerAuthSchema::Discrimination::None:
    break;
  case PointerAuthSchema::Discrimination::Type:
    discriminator = typeBasedDiscriminator;
    break;
  case PointerAuthSchema::Discrimination::Constant:
    discriminator = defaultAuthentication.getConstantDiscrimination();
    break;
  case PointerAuthSchema::Discrimination::Decl:
    llvm_unreachable(
        "CXXVTablePointers schema must not use declaration discrimination");
  }

  if (auto explicitAuthentication =
          primaryBase->getAttr<VTablePointerAuthenticationAttr>()) {
    auto explicitAddressDiscrimination =
        explicitAuthentication->getAddressDiscrimination();
    auto explicitDiscriminator =
        explicitAuthentication->getExtraDiscrimination();

    unsigned explicitKey = explicitAuthentication->getKey();
    if (explicitKey == VTablePointerAuthenticationAttr::NoKey)
      return std::nullopt;

    if (explicitKey != VTablePointerAuthenticationAttr::DefaultKey) {
      if (explicitKey == VTablePointerAuthenticationAttr::ProcessIndependent)
        key = static_cast<unsigned>(PointerAuthSchema::ARM8_3Key::ASDA);
      else {
        assert(explicitKey ==
               VTablePointerAuthenticationAttr::ProcessDependent);
        key = static_cast<unsigned>(PointerAuthSchema::ARM8_3Key::ASDB);
      }
    }

    if (explicitAddressDiscrimination !=
        VTablePointerAuthenticationAttr::DefaultAddressDiscrimination)
      addressDiscriminated =
          explicitAddressDiscrimination ==
          VTablePointerAuthenticationAttr::AddressDiscrimination;

    if (explicitDiscriminator ==
        VTablePointerAuthenticationAttr::TypeDiscrimination)
      discriminator = typeBasedDiscriminator;
    else if (explicitDiscriminator ==
             VTablePointerAuthenticationAttr::CustomDiscrimination)
      discriminator = explicitAuthentication->getCustomDiscriminationValue();
    else if (explicitDiscriminator ==
             VTablePointerAuthenticationAttr::NoExtraDiscrimination)
      discriminator = 0;
  }

  return PointerAuthQualifier::Create(
      key, addressDiscriminated, discriminator,
      PointerAuthenticationMode::SignAndAuth,
      /*IsIsaPointer=*/false, /*AuthenticatesNullValues=*/false);
}

std::optional<CIRGenPointerAuthInfo>
CIRGenModule::getFunctionPointerAuthInfo(QualType ty) const {
  const auto &schema = getCodeGenOpts().PointerAuth.FunctionPointers;
  if (!schema)
    return std::nullopt;
  if (schema.getAuthenticationMode() == PointerAuthenticationMode::None)
    return std::nullopt;

  assert(!schema.isAddressDiscriminated() &&
         "function pointers cannot use address-specific discrimination");

  if (ty->isFunctionPointerType() || ty->isFunctionReferenceType())
    ty = ty->getPointeeType();

  uint64_t discriminator = 0;
  if (!ty.isNull() && ty->isFunctionType()) {
    switch (schema.getOtherDiscrimination()) {
    case PointerAuthSchema::Discrimination::None:
      break;
    case PointerAuthSchema::Discrimination::Type:
      discriminator = getASTContext().getPointerAuthTypeDiscriminator(ty);
      break;
    case PointerAuthSchema::Discrimination::Constant:
      discriminator = schema.getConstantDiscrimination();
      break;
    case PointerAuthSchema::Discrimination::Decl:
      llvm_unreachable(
          "function pointer auth schema must not use decl discrimination");
    }
  }

  return CIRGenPointerAuthInfo{
      /*enabled=*/true,
      /*key=*/schema.getKey(),
      /*discriminator=*/discriminator,
      /*authenticationMode=*/schema.getAuthenticationMode(),
      /*isIsaPointer=*/false,
      /*authenticatesNullValues=*/false,
  };
}

std::optional<CIRGenPointerAuthInfo>
CIRGenModule::getMemberFunctionPointerAuthInfo(QualType memberPointerType) const {
  const auto &schema = getCodeGenOpts().PointerAuth.CXXMemberFunctionPointers;
  if (!schema)
    return std::nullopt;
  if (schema.getAuthenticationMode() == PointerAuthenticationMode::None)
    return std::nullopt;

  assert(memberPointerType->getAs<MemberPointerType>() &&
         "MemberPointerType expected");
  assert(!schema.isAddressDiscriminated() &&
         "member function pointers cannot use address-specific discrimination");

  uint64_t discriminator = 0;
  switch (schema.getOtherDiscrimination()) {
  case PointerAuthSchema::Discrimination::None:
    break;
  case PointerAuthSchema::Discrimination::Type:
    discriminator =
        getASTContext().getPointerAuthTypeDiscriminator(memberPointerType);
    break;
  case PointerAuthSchema::Discrimination::Constant:
    discriminator = schema.getConstantDiscrimination();
    break;
  case PointerAuthSchema::Discrimination::Decl:
    llvm_unreachable(
        "member function pointer auth schema must not use decl discrimination");
  }

  return CIRGenPointerAuthInfo{
      /*enabled=*/true,
      /*key=*/schema.getKey(),
      /*discriminator=*/discriminator,
      /*authenticationMode=*/schema.getAuthenticationMode(),
      /*isIsaPointer=*/false,
      /*authenticatesNullValues=*/false,
  };
}

void CIRGenModule::setCIRKCFIType(const FunctionDecl *FD, cir::FuncOp F) {
  // Attach KCFI type hash as a CIR attribute on the function.
  // Matches OGCG CodeGenModule::setKCFIType (CodeGenModule.cpp:3206-3218).
  llvm::StringRef Salt;
  if (const auto *FP = FD->getType()->getAs<FunctionProtoType>())
    if (const auto &Info = FP->getExtraAttributeInfo())
      Salt = Info.CFISalt;

  uint32_t TypeId = createKCFITypeId(FD->getType(), Salt);
  F->setAttr("cir.kcfi_type",
             mlir::IntegerAttr::get(
                 mlir::IntegerType::get(&getMLIRContext(), 32), TypeId));
}

bool CIRGenModule::alwaysHasLTOVisibilityPublic(const CXXRecordDecl *RD) {
  // Ported from OGCG CGVTables.cpp:AlwaysHasLTOVisibilityPublic.
  if (RD->hasAttr<LTOVisibilityPublicAttr>() || RD->hasAttr<UuidAttr>() ||
      RD->hasAttr<DLLExportAttr>() || RD->hasAttr<DLLImportAttr>())
    return true;

  if (!getCodeGenOpts().LTOVisibilityPublicStd)
    return false;

  const DeclContext *DC = RD;
  while (true) {
    auto *D = cast<Decl>(DC);
    DC = DC->getParent();
    if (isa<TranslationUnitDecl>(DC->getRedeclContext())) {
      if (auto *ND = dyn_cast<NamespaceDecl>(D))
        if (const IdentifierInfo *II = ND->getIdentifier())
          if (II->isStr("std") || II->isStr("stdext"))
            return true;
      break;
    }
  }

  return false;
}

bool CIRGenModule::hasHiddenLTOVisibility(const CXXRecordDecl *RD) {
  // Ported from OGCG CGVTables.cpp:HasHiddenLTOVisibility.
  LinkageInfo LV = RD->getLinkageAndVisibility();
  if (!isExternallyVisible(LV.getLinkage()))
    return true;

  if (!getTriple().isOSBinFormatCOFF() &&
      LV.getVisibility() != HiddenVisibility)
    return false;

  return !alwaysHasLTOVisibilityPublic(RD);
}

SmallVector<const CXXRecordDecl *, 0>
CIRGenModule::getMostBaseClasses(const CXXRecordDecl *RD) {
  // Ported from OGCG CodeGenModule.cpp:getMostBaseClasses.
  llvm::SetVector<const CXXRecordDecl *> MostBases;

  std::function<void(const CXXRecordDecl *)> CollectMostBases;
  CollectMostBases = [&](const CXXRecordDecl *RD) {
    if (RD->getNumBases() == 0)
      MostBases.insert(RD);
    for (const CXXBaseSpecifier &B : RD->bases())
      CollectMostBases(B.getType()->getAsCXXRecordDecl());
  };
  CollectMostBases(RD);
  return MostBases.takeVector();
}

static std::string createMetadataIdentifierStringImpl(CIRGenModule &CGM,
                                                      QualType T,
                                                      StringRef Suffix,
                                                      bool includeHidden) {
  // Ported from OGCG CodeGenModule::CreateMetadataIdentifierImpl.
  if (auto *FnType = T->getAs<FunctionProtoType>())
    T = CGM.getASTContext().getFunctionType(
        FnType->getReturnType(), FnType->getParamTypes(),
        FnType->getExtProtoInfo().withExceptionSpec(EST_None));

  if (includeHidden || isExternallyVisible(T->getLinkage())) {
    std::string OutName;
    llvm::raw_string_ostream Out(OutName);

    CGM.getCXXABI().getMangleContext().mangleCanonicalTypeName(
        T, Out, CGM.getCodeGenOpts().SanitizeCfiICallNormalizeIntegers);

    if (CGM.getCodeGenOpts().SanitizeCfiICallNormalizeIntegers)
      Out << ".normalized";

    Out << Suffix;
    return OutName;
  }
  // Non-externally-visible type → return empty string (distinct MDNode case).
  return {};
}

std::string CIRGenModule::createMetadataIdentifierStringForType(QualType T) {
  return createMetadataIdentifierStringImpl(*this, T, "",
                                            /*includeHidden=*/false);
}

std::string
CIRGenModule::createStableMetadataIdentifierKeyForType(QualType T) {
  return createMetadataIdentifierStringImpl(*this, T, "",
                                            /*includeHidden=*/true);
}

std::string
CIRGenModule::createMetadataIdentifierStringForVirtualMemPtrType(QualType T) {
  return createMetadataIdentifierStringImpl(*this, T, ".virtual",
                                            /*includeHidden=*/false);
}

std::string
CIRGenModule::createStableMetadataIdentifierKeyForVirtualMemPtrType(QualType T) {
  return createMetadataIdentifierStringImpl(*this, T, ".virtual",
                                            /*includeHidden=*/true);
}

void CIRGenModule::setFunctionAttributes(GlobalDecl globalDecl,
                                         cir::FuncOp func,
                                         bool isIncompleteFunction,
                                         bool isThunk) {
  // NOTE(cir): Original CodeGen checks if this is an intrinsic. In CIR we
  // represent them in dedicated ops. The correct attributes are ensured during
  // translation to LLVM. Thus, we don't need to check for them here.

  if (!isIncompleteFunction) {
    if (const auto *omd =
            dyn_cast<ObjCMethodDecl>(globalDecl.getDecl()))
      setCIRFunctionAttributes(
          globalDecl, getTypes().arrangeObjCMethodDeclaration(omd), func,
          isThunk);
    else
      setCIRFunctionAttributes(
          globalDecl, getTypes().arrangeGlobalDeclaration(globalDecl), func,
          isThunk);
  }

  getTargetCIRGenInfo().setTargetAttributes(globalDecl.getDecl(), func, *this);

  // Set 'returned' attribute on the 'this' parameter for
  // constructors/destructors that return 'this' (HasThisReturn).
  // Matches OGCG's SetFunctionAttributes (CodeGenModule.cpp:3280-3287).
  // Also set in CIRGenFunction::startFunction for definitions, but
  // declarations don't go through startFunction, so we handle them here.
  if (!isThunk && getCXXABI().hasThisReturn(globalDecl) &&
      !(getTriple().isiOS() && getTriple().isOSVersionLT(6))) {
    if (func.getNumArguments() > 0)
      func.setArgAttr(0, "cir.returned",
                      mlir::UnitAttr::get(&getMLIRContext()));
  }

  // Set linkage and visibility in case we never see a definition.
  // OGCG (CodeGenModule.cpp:3132-3140) sets ExternalWeakLinkage on function
  // declarations with WeakAttr or that are weak-imported.
  const auto *fd = dyn_cast<FunctionDecl>(globalDecl.getDecl());

  // Set nonnull/align/dereferenceable on the 'this' pointer parameter for
  // C++ instance methods. OGCG does this in ConstructAttributeList
  // (CGCall.cpp:2782-2817) using FI.isInstanceMethod(). For definitions,
  // startFunction() handles this (CIRGenFunction.cpp:859-892), but
  // declarations don't go through startFunction, so we handle them here.
  // Thunks are excluded (matching OGCG's !IsThunk check).
  if (!isThunk && fd)
  if (const auto *md = dyn_cast<CXXMethodDecl>(fd);
      md && md->isImplicitObjectMemberFunction() && func.getNumArguments() > 0) {
    QualType thisTy = md->getThisType();
    QualType pointeeTy = thisTy->getPointeeType();

    if (!getCodeGenOpts().NullPointerIsValid) {
      func.setArgAttr(0, "cir.nonnull",
                      mlir::UnitAttr::get(&getMLIRContext()));
      func.setArgAttr(0, "cir.callsite_nonnull",
                      mlir::UnitAttr::get(&getMLIRContext()));
      uint64_t size = getMinimumObjectSize(pointeeTy).getQuantity();
      func.setArgAttr(
          0, "cir.dereferenceable",
          mlir::IntegerAttr::get(mlir::IntegerType::get(&getMLIRContext(), 64),
                                 size));
    } else {
      // With -fno-delete-null-pointer-checks, use dereferenceable_or_null.
      // Matches OGCG CGCall.cpp:2800-2808.
      uint64_t size = getMinimumObjectSize(pointeeTy).getQuantity();
      func.setArgAttr(
          0, "cir.dereferenceable_or_null",
          mlir::IntegerAttr::get(mlir::IntegerType::get(&getMLIRContext(), 64),
                                 size));
    }
    if (!pointeeTy->isIncompleteType()) {
      // For C++ class this pointers, use getNonVirtualAlignment to avoid
      // inheriting virtual base alignment. The this pointer may point to a
      // subclass, so we can only guarantee the non-virtual part's alignment.
      // For final classes, the full alignment is safe.
      unsigned align;
      if (const auto *rd = pointeeTy->getAsCXXRecordDecl()) {
        if (!rd->hasDefinition()) {
          align = 1;
        } else {
          auto &layout = getASTContext().getASTRecordLayout(rd);
          if (rd->isEffectivelyFinal())
            align = layout.getAlignment().getQuantity();
          else
            align = layout.getNonVirtualAlignment().getQuantity();
        }
      } else {
        align = getASTContext().getTypeAlignInChars(pointeeTy).getQuantity();
      }
      func.setArgAttr(
          0, "cir.align",
          mlir::IntegerAttr::get(mlir::IntegerType::get(&getMLIRContext(), 64),
                                 align));
    }
  }

  // The remaining attributes are FunctionDecl-specific. ObjCMethodDecl is not
  // a subclass of FunctionDecl, so bail out early.
  if (!fd)
    return;

  // Set nonnull/align/dereferenceable on reference parameters for extern
  // declarations. For definitions, startFunction() handles this
  // (CIRGenFunction.cpp:800-834). Declarations don't go through
  // startFunction, so we handle them here.
  // OGCG does this in ConstructAttributeList (CGCall.cpp:2942-2954).
  {
    // Compute the CIR function arg index offset: for instance methods,
    // the first CIR arg is 'this' (handled above), and prototype params
    // start at index 1.
    unsigned argOffset = 0;
    if (const auto *md = dyn_cast<CXXMethodDecl>(fd)) {
      if (md->isImplicitObjectMemberFunction())
        argOffset = 1;
      // Constructors/destructors with virtual bases may have an implicit VTT
      // parameter after 'this'. Account for it to avoid assigning reference
      // parameter attributes to the VTT slot. Use the actual globalDecl
      // (which carries the correct ctor/dtor kind) so that only base
      // constructors/destructors (which actually have the VTT parameter)
      // get the offset increment. Complete-object constructors (CI1) do
      // NOT have a VTT parameter.
      if (isa<CXXConstructorDecl>(fd) || isa<CXXDestructorDecl>(fd)) {
        if (getCXXABI().needsVTTParameter(globalDecl))
          argOffset++;
      }
    }

    for (unsigned i = 0; i < fd->getNumParams(); ++i) {
      unsigned argIdx = i + argOffset;
      if (argIdx >= func.getNumArguments())
        break;
      QualType paramType = fd->getParamDecl(i)->getType();

      // Member pointers, nullptr_t, _Atomic, and __mfp8 types should NOT get
      // noundef. Matches OGCG DetermineNoUndef (CGCall.cpp:2301-2310).
      bool isMfp8Param = paramType->isMFloat8Type();
      if (!isMfp8Param) {
        if (const auto *VT = paramType->getAs<VectorType>())
          isMfp8Param = VT->getElementType()->isMFloat8Type();
      }
      if (paramType->isMemberPointerType() || paramType->isNullPtrType() ||
          paramType->isAtomicType() || isMfp8Param) {
        func.setArgAttr(argIdx, "cir.no_noundef",
                        mlir::UnitAttr::get(&getMLIRContext()));
      }

      // noescape — mark parameters with __attribute__((noescape)) so that
      // the lowering pass can emit captures(none)/nocapture.
      // For definitions, EmitFunctionProlog handles this
      // (CIRGenFunction.cpp). Declarations don't go through
      // EmitFunctionProlog, so we handle them here.
      // Matches OGCG CGCall.cpp:3014.
      if (fd->getParamDecl(i)->hasAttr<NoEscapeAttr>())
        func.setArgAttr(argIdx, "cir.noescape",
                        mlir::UnitAttr::get(&getMLIRContext()));

      // swiftasync / swiftself / swifterror from the parameter ABI attribute.
      // For definitions EmitFunctionProlog handles these (CIRGenFunction.cpp);
      // declarations don't go through it, so handle them here. Matches OGCG's
      // per-parameter ParameterABI loop in ConstructAttributeList (CGCall.cpp).
      if (fd->getParamDecl(i)->hasAttr<SwiftAsyncContextAttr>())
        func.setArgAttr(argIdx, "cir.swiftasync",
                        mlir::UnitAttr::get(&getMLIRContext()));
      else if (fd->getParamDecl(i)->hasAttr<SwiftContextAttr>())
        func.setArgAttr(argIdx, "cir.swiftself",
                        mlir::UnitAttr::get(&getMLIRContext()));
      else if (fd->getParamDecl(i)->hasAttr<SwiftErrorResultAttr>())
        func.setArgAttr(argIdx, "cir.swifterror",
                        mlir::UnitAttr::get(&getMLIRContext()));

      if (fd->getParamDecl(i)->hasAttr<SwiftIndirectResultAttr>() &&
          paramType->isPointerType()) {
        auto unitAttr = mlir::UnitAttr::get(&getMLIRContext());
        func.setArgAttr(argIdx, "cir.swift_indirect_result", unitAttr);
        func.setArgAttr(argIdx, "cir.noalias", unitAttr);

        QualType pointeeTy = paramType->getPointeeType();
        if (!pointeeTy->isIncompleteType() && pointeeTy->isConstantSizeType()) {
          auto info = getASTContext().getTypeInfoInChars(pointeeTy);
          uint64_t size = info.Width.getQuantity();
          if (size > 0)
            func.setArgAttr(argIdx, "cir.dereferenceable",
                            getI64AttrCached(size));

          uint64_t align = info.Align.getQuantity();
          if (auto existingAlign = func.getArgAttrOfType<mlir::IntegerAttr>(
                  argIdx, "cir.align"))
            align = std::max<uint64_t>(align, existingAlign.getInt());
          func.setArgAttr(argIdx, "cir.align", getI64AttrCached(align));
        }
      }

      if (const auto *refTy = paramType->getAs<ReferenceType>()) {
        QualType pointeeTy = refTy->getPointeeType();
        if (!pointeeTy->isIncompleteType() &&
            pointeeTy->isConstantSizeType()) {
          uint64_t size = getMinimumObjectSize(pointeeTy).getQuantity();
          if (size > 0)
            func.setArgAttr(
                argIdx, "cir.dereferenceable",
                mlir::IntegerAttr::get(
                    mlir::IntegerType::get(&getMLIRContext(), 64), size));
        }
        // nonnull — references cannot be null (only for default address
        // space). Matches OGCG CGCall.cpp:2946-2948.
        if (getASTContext().getTargetAddressSpace(
                pointeeTy.getAddressSpace()) == 0 &&
            !getCodeGenOpts().NullPointerIsValid)
          func.setArgAttr(argIdx, "cir.nonnull",
                          mlir::UnitAttr::get(&getMLIRContext()));
        if (getASTContext().getTargetAddressSpace(
                pointeeTy.getAddressSpace()) == 0 &&
            !getCodeGenOpts().NullPointerIsValid)
          func.setArgAttr(argIdx, "cir.callsite_nonnull",
                          mlir::UnitAttr::get(&getMLIRContext()));
        if (pointeeTy->isObjectType()) {
          // For C++ class pointees, use non-virtual alignment to avoid
          // inheriting virtual base alignment.
          unsigned align;
          if (pointeeTy->isIncompleteType()) {
            align = 1;
          } else if (const auto *rd = pointeeTy->getAsCXXRecordDecl()) {
            if (!rd->hasDefinition()) {
              align = 1;
            } else {
              auto &layout = getASTContext().getASTRecordLayout(rd);
              if (rd->isEffectivelyFinal())
                align = layout.getAlignment().getQuantity();
              else
                align = layout.getNonVirtualAlignment().getQuantity();
            }
          } else {
            align =
                getASTContext().getTypeAlignInChars(pointeeTy).getQuantity();
          }
          func.setArgAttr(
              argIdx, "cir.align",
              mlir::IntegerAttr::get(
                  mlir::IntegerType::get(&getMLIRContext(), 64), align));
        }
      }

      // OpenCL kernel pointer parameters: the OpenCL spec (v3.0.10 s6.3.5)
      // guarantees alignment to the natural alignment of the pointee type.
      // Matches OGCG CGCall.cpp:2956-2969.
      if (paramType->isPointerType() &&
          DeviceKernelAttr::isOpenCLSpelling(
              fd->getAttr<DeviceKernelAttr>())) {
        QualType pointeeTy = paramType->getPointeeType();
        if (!pointeeTy->isIncompleteType() &&
            pointeeTy->isConstantSizeType()) {
          unsigned align =
              getNaturalTypeAlignment(pointeeTy, nullptr).getAsAlign().value();
          unsigned finalAlign = align;
          if (auto existingAlign = func.getArgAttrOfType<mlir::IntegerAttr>(
                  argIdx, "cir.align"))
            finalAlign = std::max<unsigned>(finalAlign, existingAlign.getInt());
          func.setArgAttr(
              argIdx, "cir.align",
              mlir::IntegerAttr::get(
                  mlir::IntegerType::get(&getMLIRContext(), 64), finalAlign));
        }
      }

    }
  }

  // Set nonnull on the return value for functions with ReturnsNonNullAttr
  // (e.g., non-nothrow operator new). Matches OGCG CGCall.cpp:2514.
  if (fd->hasAttr<ReturnsNonNullAttr>() &&
      !getCodeGenOpts().NullPointerIsValid &&
      func.getNumResults() > 0) {
    func.setResultAttr(0, "cir.nonnull",
                       mlir::UnitAttr::get(&getMLIRContext()));
  }

  // Member pointer, nullptr_t, and _Atomic returns should NOT get noundef.
  //  - member pointers: -1 is a valid null representation
  //  - nullptr_t: explicitly excluded by OGCG DetermineNoUndef
  //  - _Atomic types: fail isScalarType() in OGCG so no noundef
  // Mark the function so target lowering skips noundef on the return value.
  // Matches OGCG CGCall.cpp:2301-2310.
  if ((fd->getReturnType()->isMemberPointerType() ||
       fd->getReturnType()->isNullPtrType() ||
       fd->getReturnType()->isAtomicType()) &&
      func.getNumResults() > 0) {
    func->setAttr("cir.no_noundef_return",
                   mlir::UnitAttr::get(&getMLIRContext()));
  }

  // Mark extern "C" functions in C++ mode so the target lowering can
  // skip noundef on the return value (matching OGCG's HasStrictReturn).
  // This covers declarations; setCIRFunctionAttributesForDefinition covers
  // definitions. Both paths are needed because declarations (e.g.,
  // extern "C" int printf(const char *, ...)) don't go through the
  // definition path.
  if (getLangOpts().CPlusPlus && fd->isExternC())
    func->setAttr("cir.extern_c", mlir::UnitAttr::get(&getMLIRContext()));

  TypeProvenance returnProvenance = getTypeProvenance(fd->getReturnType());
  if (hasTypeProvenance(returnProvenance, TypeProvenance::BitInt))
    func->setAttr("cir.ret.bitint",
                  mlir::UnitAttr::get(&getMLIRContext()));
  if (hasTypeProvenance(returnProvenance, TypeProvenance::Matrix))
    func->setAttr("cir.ret.matrix",
                  mlir::UnitAttr::get(&getMLIRContext()));

  llvm::SmallVector<bool, 8> bitIntArgs;
  llvm::SmallVector<bool, 8> matrixArgs;
  bitIntArgs.reserve(fd->getNumParams());
  matrixArgs.reserve(fd->getNumParams());
  bool hasBitIntArg = false;
  bool hasMatrixArg = false;
  for (unsigned i = 0; i < fd->getNumParams(); ++i) {
    TypeProvenance provenance = getTypeProvenance(fd->getParamDecl(i)->getType());
    bool isBitInt = hasTypeProvenance(provenance, TypeProvenance::BitInt);
    bool isMatrix = hasTypeProvenance(provenance, TypeProvenance::Matrix);
    bitIntArgs.push_back(isBitInt);
    matrixArgs.push_back(isMatrix);
    hasBitIntArg |= isBitInt;
    hasMatrixArg |= isMatrix;
  }
  if (hasBitIntArg)
    func->setAttr("cir.bitint_args",
                  mlir::DenseBoolArrayAttr::get(&getMLIRContext(),
                                               bitIntArgs));
  if (hasMatrixArg)
    func->setAttr("cir.matrix_args",
                  mlir::DenseBoolArrayAttr::get(&getMLIRContext(),
                                               matrixArgs));

  // Set noalias on the return value for functions with RestrictAttr
  // (__attribute__((malloc))) without a deallocator. Matches OGCG
  // CGCall.cpp:2511-2513.
  if (const auto *ra = fd->getAttr<RestrictAttr>();
      ra && ra->getDeallocator() == nullptr && func.getNumResults() > 0) {
    func.setResultAttr(0, "cir.noalias",
                       mlir::UnitAttr::get(&getMLIRContext()));
  }

  // Propagate maybe_undef attribute from function parameters to CIR FuncOp
  // arg attributes. This is checked during target lowering to insert
  // cir.freeze on call arguments, matching OGCG's IsArgumentMaybeUndef +
  // Builder.CreateFreeze pattern (CGCall.cpp:5385-5723).
  for (unsigned i = 0; i < fd->getNumParams(); ++i) {
    const ParmVarDecl *param = fd->getParamDecl(i);
    if (param && param->hasAttr<MaybeUndefAttr>())
      func.setArgAttr(i, "cir.maybe_undef",
                       mlir::UnitAttr::get(&getMLIRContext()));
  }

  LinkageInfo lv = fd->getLinkageAndVisibility();
  if (isExternallyVisible(lv.getLinkage()) &&
      (fd->hasAttr<WeakAttr>() || fd->isWeakImported()))
    func.setLinkage(cir::GlobalLinkageKind::ExternalWeakLinkage);

  // Only a few attributes are set on declarations; these may later be
  // overridden by a definition.
  setGVProperties(func, dyn_cast<NamedDecl>(globalDecl.getDecl()));

  // If we plan on emitting this inline builtin, we can't treat it as a builtin.
  if (fd->isInlineBuiltinDeclaration()) {
    const FunctionDecl *fdBody;
    bool hasBody = fd->hasBody(fdBody);
    (void)hasBody;
    assert(hasBody && "Inline builtin declarations should always have an "
                      "available body!");
    // If the inline builtin body will be emitted, mark the declaration as
    // nobuiltin to prevent LLVM from treating it as a known builtin. Matches
    // OGCG CodeGenModule.cpp:3255-3259.
    if (shouldEmitFunction(fdBody)) {
      mlir::NamedAttrList existingAttrs;
      if (auto extraAttrs = func.getExtraAttrs())
        existingAttrs =
            mlir::NamedAttrList{extraAttrs->getElements().getValue()};
      existingAttrs.set("nobuiltin", mlir::UnitAttr::get(&getMLIRContext()));
      func.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
          existingAttrs.getDictionary(&getMLIRContext())));
    }
  }

  // Replaceable global allocation functions (operator new, operator delete)
  // get 'nobuiltin' on their declaration per the Itanium C++ ABI. Individual
  // call sites that are actual new-/delete-expressions override this with
  // 'builtin' so LLVM can fold them. Matches OGCG's SetFunctionAttributes.
  if (fd->isReplaceableGlobalAllocationFunction()) {
    mlir::NamedAttrList existingAttrs;
    if (auto extraAttrs = func.getExtraAttrs())
      existingAttrs = mlir::NamedAttrList{extraAttrs->getElements().getValue()};
    existingAttrs.set("nobuiltin", mlir::UnitAttr::get(&getMLIRContext()));
    func.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
        existingAttrs.getDictionary(&getMLIRContext())));
  }

  // Set function section from SectionAttr on declarations.
  // OGCG also checks CodeSegAttr first. CodeSegAttr is Windows PE/COFF only.
  if (const auto *sa = fd->getAttr<SectionAttr>())
    func.setSectionAttr(getStringAttrCached(sa->getName()));
  else if (const auto *sa = fd->getAttr<PragmaClangTextSectionAttr>())
    func.setSectionAttr(getStringAttrCached(sa->getName()));

  // OGCG sets UnnamedAddr::Global for ctors, dtors, and virtual methods
  // on declarations (CodeGenModule.cpp:3328-3332).
  if (isa<CXXConstructorDecl>(fd) || isa<CXXDestructorDecl>(fd) ||
      (isa<CXXMethodDecl>(fd) && cast<CXXMethodDecl>(fd)->isVirtual())) {
    mlir::NamedAttrList attrs;
    if (auto extraAttrs = func.getExtraAttrs())
      attrs = mlir::NamedAttrList{extraAttrs->getElements().getValue()};
    attrs.set("unnamed_addr", mlir::UnitAttr::get(&getMLIRContext()));
    func.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
        attrs.getDictionary(&getMLIRContext())));
  }
  // OGCG emits CFI type metadata for icall on function declarations
  // (CodeGenModule.cpp:3334-3346).
  // Don't emit entries on declarations in cross-DSO canonical mode.
  if (!codeGenOpts.SanitizeCfiCrossDso ||
      !codeGenOpts.SanitizeCfiCanonicalJumpTables)
    addFunctionTypeMetadataForIcall(fd, func);
  if (codeGenOpts.CallGraphSection)
    addFunctionTypeMetadataForCallGraphSection(fd, func);

  // Attach KCFI type hash on function declarations and definitions.
  // Matches OGCG CodeGenModule.cpp:3345-3346.
  if (getLangOpts().Sanitize.has(SanitizerKind::KCFI))
    setCIRKCFIType(fd, func);

  // Attach Vector Function ABI variant names for `#pragma omp declare simd`.
  // Matches OGCG CodeGenModule.cpp:3361-3362.
  if (getLangOpts().OpenMP && fd->hasAttr<OMPDeclareSimdDeclAttr>())
    emitOMPDeclareSimdFunction(fd, func);
}

/// Returns true if exception handling is enabled and might actually be used
/// enabled.  This means, for example, that C with -fexceptions enables this.
static bool hasUnwindExceptions(const LangOptions &langOpts) {
  // If exceptions are completely disabled, obviously this is false.
  if (!langOpts.Exceptions)
    return false;

  // If C++ exceptions are enabled, this is true.
  if (langOpts.CXXExceptions)
    return true;

  // If ObjC exceptions are enabled, this depends on the ABI.
  if (langOpts.ObjCExceptions)
    return langOpts.ObjCRuntime.hasUnwindExceptions();

  return true;
}

void CIRGenModule::setDefaultFunctionAttributesForCompilerGenerated(
    cir::FuncOp fn) {
  // Trivial default function attributes (no-trapping-math, frame-pointer,
  // stack-protector-buffer-size, etc.) — matches OGCG's
  // SetLLVMFunctionAttributes -> ConstructAttributeList ->
  // getDefaultFunctionAttributes.
  mlir::NamedAttrList defaultAttrs;
  if (auto existingExtraAttrs = fn.getExtraAttrs())
    defaultAttrs =
        mlir::NamedAttrList{existingExtraAttrs->getElements().getValue()};
  getDefaultFunctionAttributes("", /*hasOptnone=*/false,
                               /*attrOnCallSite=*/false, defaultAttrs);

  // Default target CPU/features from command line.
  llvm::StringRef targetCPU = getTarget().getTargetOpts().CPU;
  if (!targetCPU.empty())
    defaultAttrs.set("target-cpu", getStringAttrCached(targetCPU));
  llvm::StringRef tuneCPU = getTarget().getTargetOpts().TuneCPU;
  if (!tuneCPU.empty())
    defaultAttrs.set("tune-cpu", getStringAttrCached(tuneCPU));
  auto features = getTarget().getTargetOpts().Features;
  if (!features.empty()) {
    llvm::erase_if(features, [&](const std::string &f) {
      return getTarget().isReadOnlyFeature(f.substr(1));
    });
    llvm::sort(features);
    defaultAttrs.set("target-features",
                     getStringAttrCached(llvm::join(features, ",")));
  }

  fn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
      defaultAttrs.getDictionary(&getMLIRContext())));
}

void CIRGenModule::getDefaultInternalFunctionAttributes(
    mlir::NamedAttrList &defaultAttrs) {
  // getTrivialDefaultFunctionAttributes: no-trapping-math,
  // stack-protector-buffer-size, frame-pointer, etc.
  getDefaultFunctionAttributes("", /*hasOptnone=*/false,
                               /*attrOnCallSite=*/false, defaultAttrs);

  // setCIRFunctionAttributesForDefinition (D=nullptr path): uwtable.
  if (codeGenOpts.UnwindTables) {
    auto attr = cir::UWTableAttr::get(
        &getMLIRContext(), cir::UWTableKind(codeGenOpts.UnwindTables));
    defaultAttrs.set(attr.getMnemonic(), attr);
  }

  // Stack protector.
  if (!getTriple().isGPU()) {
    auto ssp = getLangOpts().getStackProtector();
    if (ssp == LangOptions::SSPOn)
      defaultAttrs.set("ssp", mlir::UnitAttr::get(&getMLIRContext()));
    else if (ssp == LangOptions::SSPStrong)
      defaultAttrs.set("sspstrong", mlir::UnitAttr::get(&getMLIRContext()));
    else if (ssp == LangOptions::SSPReq)
      defaultAttrs.set("sspreq", mlir::UnitAttr::get(&getMLIRContext()));
  }

  // nothrow (when exceptions are disabled).
  if (!hasUnwindExceptions(getLangOpts())) {
    auto attr = cir::NoThrowAttr::get(&getMLIRContext());
    defaultAttrs.set(attr.getMnemonic(), attr);
  }

  // Sanitizer attrs for compiler-generated init/cleanup functions.
  // Matches OGCG's CreateGlobalInitOrCleanUpFunction, which treats
  // compiler-generated functions as belonging to the main file when
  // consulting source-based no_sanitize lists.
  auto isMainFileNoSanitize = [&](SanitizerMask kind) {
    const auto &noSanitizeL = astContext.getNoSanitizeList();
    auto &sm = astContext.getSourceManager();
    StringRef mainFile =
        sm.getFileEntryRefForID(sm.getMainFileID())->getName();
    return noSanitizeL.containsMainFile(kind, mainFile) ||
           noSanitizeL.containsFile(kind, mainFile);
  };
  auto setUnitAttrIf = [&](StringRef name, bool enabled) {
    if (enabled)
      defaultAttrs.set(name, mlir::UnitAttr::get(&getMLIRContext()));
  };
  setUnitAttrIf(
      "sanitize_address",
      (langOpts.Sanitize.has(SanitizerKind::Address) &&
       !isMainFileNoSanitize(SanitizerKind::Address)) ||
          (langOpts.Sanitize.has(SanitizerKind::KernelAddress) &&
           !isMainFileNoSanitize(SanitizerKind::KernelAddress)));
  setUnitAttrIf(
      "sanitize_hwaddress",
      (langOpts.Sanitize.has(SanitizerKind::HWAddress) &&
       !isMainFileNoSanitize(SanitizerKind::HWAddress)) ||
          (langOpts.Sanitize.has(SanitizerKind::KernelHWAddress) &&
           !isMainFileNoSanitize(SanitizerKind::KernelHWAddress)));
  setUnitAttrIf("sanitize_memtag",
                langOpts.Sanitize.has(SanitizerKind::MemtagStack) &&
                    !isMainFileNoSanitize(SanitizerKind::MemtagStack));
  setUnitAttrIf("sanitize_thread",
                langOpts.Sanitize.has(SanitizerKind::Thread) &&
                    !isMainFileNoSanitize(SanitizerKind::Thread));
  setUnitAttrIf("sanitize_type",
                langOpts.Sanitize.has(SanitizerKind::Type) &&
                    !isMainFileNoSanitize(SanitizerKind::Type));
  setUnitAttrIf("sanitize_numerical_stability",
                langOpts.Sanitize.has(SanitizerKind::NumericalStability) &&
                    !isMainFileNoSanitize(SanitizerKind::NumericalStability));
  setUnitAttrIf(
      "sanitize_memory",
      (langOpts.Sanitize.has(SanitizerKind::Memory) &&
       !isMainFileNoSanitize(SanitizerKind::Memory)) ||
          (langOpts.Sanitize.has(SanitizerKind::KernelMemory) &&
           !isMainFileNoSanitize(SanitizerKind::KernelMemory)));
  setUnitAttrIf("safestack",
                langOpts.Sanitize.has(SanitizerKind::SafeStack) &&
                    !isMainFileNoSanitize(SanitizerKind::SafeStack));
  setUnitAttrIf("shadowcallstack",
                langOpts.Sanitize.has(SanitizerKind::ShadowCallStack) &&
                    !isMainFileNoSanitize(SanitizerKind::ShadowCallStack));

  // Default target CPU/features (no per-function TargetAttr overrides).
  llvm::StringRef targetCPU = getTarget().getTargetOpts().CPU;
  if (!targetCPU.empty())
    defaultAttrs.set("target-cpu", getStringAttrCached(targetCPU));
  llvm::StringRef tuneCPU = getTarget().getTargetOpts().TuneCPU;
  if (!tuneCPU.empty())
    defaultAttrs.set("tune-cpu", getStringAttrCached(tuneCPU));
  auto features = getTarget().getTargetOpts().Features;
  if (!features.empty()) {
    llvm::erase_if(features, [&](const std::string &f) {
      return getTarget().isReadOnlyFeature(f.substr(1));
    });
    llvm::sort(features);
    defaultAttrs.set("target-features",
                     getStringAttrCached(llvm::join(features, ",")));
  }

  // min-legal-vector-width (always "0" for compiler-generated functions).
  if (getTarget().getTriple().isX86())
    defaultAttrs.set("min-legal-vector-width", getStringAttrCached("0"));

  // Compiler-generated init/cleanup helpers have no Decl, so only the
  // language-option builtin suppression applies here. Match OGCG's
  // SetInternalFunctionAttributes(GlobalDecl(), Fn, FI) path.
  if (langOpts.NoBuiltin) {
    defaultAttrs.set("no-builtins",
                     mlir::UnitAttr::get(&getMLIRContext()));
  } else {
    for (const auto &builtinName : langOpts.NoBuiltinFuncs)
      defaultAttrs.set(("no-builtin-" + builtinName),
                       mlir::UnitAttr::get(&getMLIRContext()));
  }

  // XRay instrumentation attributes for compiler-generated functions.
  // These functions have no Decl (D=nullptr in OGCG's StartFunction), so
  // the XRayInstrumentAttr check is skipped and we fall into the else branch
  // which sets xray-instruction-threshold. Matches OGCG
  // CodeGenFunction.cpp:913-941.
  if (codeGenOpts.XRayInstrumentFunctions) {
    defaultAttrs.set(
        "xray-instruction-threshold",
        getStringAttrCached(llvm::itostr(codeGenOpts.XRayInstructionThreshold)));

    if (codeGenOpts.XRayIgnoreLoops)
      defaultAttrs.set("xray-ignore-loops",
                       mlir::UnitAttr::get(&getMLIRContext()));

    if (!codeGenOpts.XRayInstrumentationBundle.has(XRayInstrKind::FunctionExit))
      defaultAttrs.set("xray-skip-exit",
                       mlir::UnitAttr::get(&getMLIRContext()));

    if (!codeGenOpts.XRayInstrumentationBundle.has(
            XRayInstrKind::FunctionEntry))
      defaultAttrs.set("xray-skip-entry",
                       mlir::UnitAttr::get(&getMLIRContext()));
  }

  // Internal compiler-generated functions (D == nullptr in OGCG) only carry
  // an explicit noinline when the frontend has disabled normal inlining
  // entirely.
  if (codeGenOpts.getInlining() == CodeGenOptions::OnlyAlwaysInlining)
    defaultAttrs.set("noinline", mlir::UnitAttr::get(&getMLIRContext()));
}

ProfileList::ExclusionType
CIRGenModule::isFunctionBlockedByProfileList(llvm::StringRef funcName,
                                             SourceLocation loc) const {
  const auto &profileList = astContext.getProfileList();
  if (profileList.isEmpty())
    return ProfileList::Allow;

  llvm::driver::ProfileInstrKind kind = getCodeGenOpts().getProfileInstr();
  if (auto v = profileList.isFunctionExcluded(funcName, kind))
    return *v;
  if (loc.isValid())
    if (auto v = profileList.isLocationExcluded(loc, kind))
      return *v;

  auto &sm = astContext.getSourceManager();
  if (auto mainFile = sm.getFileEntryRefForID(sm.getMainFileID()))
    if (auto v = profileList.isFileExcluded(mainFile->getName(), kind))
      return *v;
  return profileList.getDefault(kind);
}

ProfileList::ExclusionType
CIRGenModule::isFunctionBlockedFromProfileInstr(llvm::StringRef funcName,
                                                SourceLocation loc) const {
  auto v = isFunctionBlockedByProfileList(funcName, loc);
  if (v != ProfileList::Allow)
    return v;

  auto numGroups = getCodeGenOpts().ProfileTotalFunctionGroups;
  if (numGroups > 1) {
    auto funcNameRef = llvm::ArrayRef<uint8_t>(
        reinterpret_cast<const uint8_t *>(funcName.data()), funcName.size());
    auto group = llvm::crc32(funcNameRef) % numGroups;
    if (group != getCodeGenOpts().ProfileSelectedFunctionGroup)
      return ProfileList::Skip;
  }
  return ProfileList::Allow;
}

void CIRGenModule::setCIRFunctionAttributesForDefinition(clang::GlobalDecl gd,
                                                         cir::FuncOp f,
                                                         bool isThunk) {
  const auto *objCMethod = dyn_cast_or_null<ObjCMethodDecl>(gd.getDecl());
  const auto *decl = dyn_cast_or_null<FunctionDecl>(gd.getDecl());
  // Get existing extra attributes or create empty dict.
  mlir::NamedAttrList attrs;
  if (auto existingExtraAttrs = f.getExtraAttrs())
    attrs = mlir::NamedAttrList{existingExtraAttrs->getElements().getValue()};

  if ((!decl || !decl->hasAttr<NoUwtableAttr>()) && codeGenOpts.UnwindTables) {
    auto attr = cir::UWTableAttr::get(
        &getMLIRContext(), cir::UWTableKind(codeGenOpts.UnwindTables));
    attrs.set(attr.getMnemonic(), attr);
  }

  // Stack protector attributes. Matches OGCG CodeGenModule.cpp:2739-2749.
  if (decl && decl->hasAttr<NoStackProtectorAttr>()) {
    // Do nothing — explicitly suppressed.
  } else if (decl && decl->hasAttr<StrictGuardStackCheckAttr>() &&
             !getTriple().isGPU() &&
             getLangOpts().getStackProtector() == LangOptions::SSPOn) {
    attrs.set("sspstrong", mlir::UnitAttr::get(&getMLIRContext()));
  } else if (!getTriple().isGPU()) {
    auto ssp = getLangOpts().getStackProtector();
    if (ssp == LangOptions::SSPOn)
      attrs.set("ssp", mlir::UnitAttr::get(&getMLIRContext()));
    else if (ssp == LangOptions::SSPStrong)
      attrs.set("sspstrong", mlir::UnitAttr::get(&getMLIRContext()));
    else if (ssp == LangOptions::SSPReq)
      attrs.set("sspreq", mlir::UnitAttr::get(&getMLIRContext()));
  }

  // Stack clash protection and probe size.
  // Matches OGCG CodeGenModule.cpp:2729-2734.
  if (codeGenOpts.StackClashProtector)
    attrs.set("probe-stack", getStringAttrCached("inline-asm"));
  if (codeGenOpts.StackProbeSize && codeGenOpts.StackProbeSize != 4096)
    attrs.set("stack-probe-size",
              getUIntStringAttrCached(codeGenOpts.StackProbeSize));

  // Add nothrow attribute if exceptions aren't enabled.
  if (!hasUnwindExceptions(getLangOpts())) {
    auto attr = cir::NoThrowAttr::get(&getMLIRContext());
    attrs.set(attr.getMnemonic(), attr);
  }

  std::optional<cir::InlineKind> existingInlineKind = f.getInlineKind();
  bool isNoInline =
      existingInlineKind && *existingInlineKind == cir::InlineKind::NoInline;
  bool isAlwaysInline = existingInlineKind &&
                        *existingInlineKind == cir::InlineKind::AlwaysInline;
  if (!decl) {
    if (objCMethod && objCMethod->isDirectMethod() &&
        usesDarwinObjCRuntime(*this)) {
      // Match OGCG's CodeGenFunction::StartObjCMethod, which forces Darwin
      // direct-method definitions hidden even though the decl visibility is
      // otherwise default.
      f.setGlobalVisibility(cir::VisibilityKind::Hidden);
    }

    // ObjC method definitions also reach this decl-less path, so mirror the
    // O0/optnone and inline-attribute handling that regular FunctionDecl
    // definitions get below.
    bool shouldAddOptNone = false;
    if (objCMethod) {
      shouldAddOptNone =
          !codeGenOpts.DisableO0ImplyOptNone &&
          codeGenOpts.OptimizationLevel == 0 &&
          !objCMethod->hasAttr<MinSizeAttr>() &&
          !objCMethod->hasAttr<AlwaysInlineAttr>();
    }

    if (objCMethod &&
        (shouldAddOptNone || objCMethod->hasAttr<OptimizeNoneAttr>()) &&
        !isAlwaysInline) {
      f.setOptNone(true);
      f.setInlineKind(cir::InlineKind::NoInline);
    } else if (objCMethod && objCMethod->hasAttr<NoInlineAttr>() &&
               !isAlwaysInline) {
      f.setInlineKind(cir::InlineKind::NoInline);
    } else if (objCMethod && objCMethod->hasAttr<AlwaysInlineAttr>() &&
               !isNoInline) {
      f.setInlineKind(cir::InlineKind::AlwaysInline);
    } else if (!isAlwaysInline &&
               codeGenOpts.getInlining() ==
                   CodeGenOptions::OnlyAlwaysInlining) {
      // If inlining is disabled and we don't have a declaration to control
      // inlining, mark the function as 'noinline' unless it is explicitly
      // marked as 'alwaysinline'.
      f.setInlineKind(cir::InlineKind::NoInline);
    }

    // Compiler-generated functions (captured statements, etc.) still need
    // trivial default attrs (no-trapping-math, stack-protector-buffer-size,
    // frame-pointer, etc.) and default target-cpu/target-features. This
    // matches OGCG's SetInternalFunctionAttributes which calls both
    // SetLLVMFunctionAttributes (trivial defaults) and
    // SetLLVMFunctionAttributesForDefinition even when D=nullptr.
    getDefaultFunctionAttributes("", /*hasOptnone=*/false,
                                 /*attrOnCallSite=*/false, attrs);

    llvm::StringRef targetCPU = getTarget().getTargetOpts().CPU;
    if (!targetCPU.empty())
      attrs.set("target-cpu", getStringAttrCached(targetCPU));
    llvm::StringRef tuneCPU = getTarget().getTargetOpts().TuneCPU;
    if (!tuneCPU.empty())
      attrs.set("tune-cpu", getStringAttrCached(tuneCPU));
    auto features = getTarget().getTargetOpts().Features;
    if (!features.empty()) {
      llvm::erase_if(features, [&](const std::string &feat) {
        return getTarget().isReadOnlyFeature(feat.substr(1));
      });
      llvm::sort(features);
      attrs.set("target-features",
                 getStringAttrCached(llvm::join(features, ",")));
    }
    f.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
        attrs.getDictionary(&getMLIRContext())));

    if (objCMethod && getLangOpts().Sanitize.has(SanitizerKind::Thread) &&
        !isInNoSanitizeList(SanitizerKind::Thread, f.getSymName(),
                            objCMethod->getLocation()) &&
        !hasNoSanitizeThreadAttr(objCMethod) &&
        shouldIgnoreThreadCheckingAtRuntime(objCMethod))
      markAsIgnoreThreadCheckingAtRuntime(f);
    return;
  }

  // Handle SME attributes that apply to function definitions.
  // Matches OGCG CodeGenModule.cpp:2766-2776.
  if (decl->hasAttr<ArmLocallyStreamingAttr>())
    attrs.set("aarch64_pstate_sm_body",
              mlir::UnitAttr::get(&getMLIRContext()));

  if (auto *armNewAttr = decl->getAttr<ArmNewAttr>()) {
    if (armNewAttr->isNewZA())
      attrs.set("aarch64_new_za", mlir::UnitAttr::get(&getMLIRContext()));
    if (armNewAttr->isNewZT0())
      attrs.set("aarch64_new_zt0", mlir::UnitAttr::get(&getMLIRContext()));
  }

  // Add strictfp function attribute when the function uses strict FP semantics.
  // Matches OGCG's CodeGenFunction.cpp:1091-1095 which checks
  // UsesFPIntrin()/StrictFPAttr and non-default rounding/exception modes.
  if (const auto *fd = dyn_cast<FunctionDecl>(decl)) {
    if (fd->UsesFPIntrin() || fd->hasAttr<StrictFPAttr>())
      attrs.set("strictfp", mlir::UnitAttr::get(&getMLIRContext()));
  }

  // Constructors, destructors, and virtual methods have their address not
  // semantically significant. Match OGCG's SetFunctionAttributes logic.
  if (isa<CXXConstructorDecl>(decl) || isa<CXXDestructorDecl>(decl))
    attrs.set("unnamed_addr", mlir::UnitAttr::get(&getMLIRContext()));
  else if (const auto *md = dyn_cast<CXXMethodDecl>(decl))
    if (md->isVirtual())
      attrs.set("unnamed_addr", mlir::UnitAttr::get(&getMLIRContext()));

  // Handle per-function MinSizeAttr from __attribute__((minsize)).
  // Global -Oz is already handled via getTrivialDefaultFunctionAttributes.
  if (decl->hasAttr<MinSizeAttr>())
    attrs.set("minsize", mlir::UnitAttr::get(&getMLIRContext()));

  // Handle __attribute__((noduplicate)) on function definitions.
  // Matches OGCG CodeGenModule.cpp:2810-2811.
  if (decl->hasAttr<NoDuplicateAttr>())
    attrs.set("noduplicate", mlir::UnitAttr::get(&getMLIRContext()));

  // Track whether we need to add the optnone attribute,
  // starting with the default for this optimization level.
  bool shouldAddOptNone =
      !codeGenOpts.DisableO0ImplyOptNone && codeGenOpts.OptimizationLevel == 0;
  // We can't add optnone in the following cases, it won't pass the verifier.
  shouldAddOptNone &= !decl->hasAttr<MinSizeAttr>();
  shouldAddOptNone &= !decl->hasAttr<AlwaysInlineAttr>();

  // Handle optnone and inline attributes
  if ((shouldAddOptNone || decl->hasAttr<OptimizeNoneAttr>()) &&
      !isAlwaysInline) {
    // Add optnone, but do so only if the function isn't always_inline.
    f.setOptNone(true);

    // OptimizeNone implies noinline; we should not be inlining such functions.
    f.setInlineKind(cir::InlineKind::NoInline);

    // We still need to handle naked functions even though optnone subsumes
    // much of their semantics.
    if (decl->hasAttr<NakedAttr>())
      attrs.set("naked", mlir::UnitAttr::get(&getMLIRContext()));

    // OptimizeNone wins over OptimizeForSize and MinSize.
    attrs.erase("minsize");
    attrs.erase("optsize");
  } else if (decl->hasAttr<NakedAttr>()) {
    // Naked implies noinline: we should not be inlining such functions.
    attrs.set("naked", mlir::UnitAttr::get(&getMLIRContext()));
    f.setInlineKind(cir::InlineKind::NoInline);
  } else if (decl->hasAttr<NoInlineAttr>() && !isAlwaysInline) {
    // Add noinline if the function isn't always_inline.
    f.setInlineKind(cir::InlineKind::NoInline);
  } else if (decl->hasAttr<AlwaysInlineAttr>() && !isNoInline) {
    // Don't override AlwaysInline with NoInline, or vice versa, since we can't
    // specify both in IR.
    f.setInlineKind(cir::InlineKind::AlwaysInline);
  } else if (codeGenOpts.getInlining() == CodeGenOptions::OnlyAlwaysInlining) {
    // If inlining is disabled, force everything that isn't always_inline
    // to carry an explicit noinline attribute.
    if (!isAlwaysInline) {
      f.setInlineKind(cir::InlineKind::NoInline);
    }
  } else {
    // Otherwise, propagate the inline hint attribute and potentially use its
    // absence to mark things as noinline.
    // Search function and template pattern redeclarations for inline.
    auto checkForInline = [](const FunctionDecl *decl) {
      auto checkRedeclForInline = [](const FunctionDecl *redecl) {
        return redecl->isInlineSpecified();
      };
      if (any_of(decl->redecls(), checkRedeclForInline))
        return true;
      const FunctionDecl *pattern = decl->getTemplateInstantiationPattern();
      if (!pattern)
        return false;
      return any_of(pattern->redecls(), checkRedeclForInline);
    };
    if (checkForInline(decl)) {
      f.setInlineKind(cir::InlineKind::InlineHint);
    } else if (codeGenOpts.getInlining() == CodeGenOptions::OnlyHintInlining &&
               !decl->isInlined() && !isAlwaysInline) {
      f.setInlineKind(cir::InlineKind::NoInline);
    }
  }

  // Handle cold and hot attributes.
  if (decl->hasAttr<ColdAttr>()) {
    f.setCold(true);
    // Cold functions get OptimizeForSize unless optnone is set.
    // Matches OGCG CodeGenModule.cpp:2856-2857.
    if (!shouldAddOptNone && !decl->hasAttr<OptimizeNoneAttr>())
      attrs.set("optsize", mlir::UnitAttr::get(&getMLIRContext()));
  }
  if (decl->hasAttr<HotAttr>()) {
    auto attr = cir::HotAttr::get(&getMLIRContext());
    attrs.set(attr.getMnemonic(), attr);
  }
  if (const auto *ea = decl->getAttr<ErrorAttr>()) {
    auto attr = cir::DontCallAttr::get(
        getStringAttrCached(ea->getUserDiagnostic()), ea->isError());
    attrs.set(attr.getMnemonic(), attr);
  }

  // Apply sanitizer-related function attributes, matching OGCG's
  // StartFunction sanitizer attribute setup.
  // This is function-attribute parity only; CIR sanitizer IR checks remain NYI.
  SanitizerSet sanOpts;
  sanOpts.set(getLangOpts().Sanitize.Mask);
  do {
#define SANITIZER(NAME, ID)                                                    \
  if (sanOpts.empty())                                                         \
    break;                                                                     \
  if (sanOpts.has(SanitizerKind::ID) &&                                        \
      isInNoSanitizeList(SanitizerKind::ID, f.getSymName(),                    \
                         decl->getLocation()))                                 \
    sanOpts.clear(SanitizerKind::ID);
#include "clang/Basic/Sanitizers.def"
#undef SANITIZER
  } while (false);

  const bool sanitizeBounds = sanOpts.hasOneOf(SanitizerKind::Bounds);
  SanitizerMask noSanitizeMask;
  bool noSanitizeCoverage = false;
  for (auto *attr : decl->specific_attrs<NoSanitizeAttr>()) {
    noSanitizeMask |= attr->getMask();
    if (attr->hasCoverage())
      noSanitizeCoverage = true;
  }

  // Apply no_sanitize filtering.
  sanOpts.clear(noSanitizeMask);
  if (noSanitizeMask & SanitizerKind::Address)
    sanOpts.set(SanitizerKind::KernelAddress, false);
  if (noSanitizeMask & SanitizerKind::KernelAddress)
    sanOpts.set(SanitizerKind::Address, false);
  if (noSanitizeMask & SanitizerKind::HWAddress)
    sanOpts.set(SanitizerKind::KernelHWAddress, false);
  if (noSanitizeMask & SanitizerKind::KernelHWAddress)
    sanOpts.set(SanitizerKind::HWAddress, false);

  if (sanitizeBounds && !sanOpts.hasOneOf(SanitizerKind::Bounds))
    attrs.set("nosanitize_bounds", mlir::UnitAttr::get(&getMLIRContext()));

  if (noSanitizeCoverage && getCodeGenOpts().hasSanitizeCoverage())
    attrs.set("nosanitize_coverage",
              mlir::UnitAttr::get(&getMLIRContext()));

  // Some passes need the non-negated no_sanitize attribute, matching OGCG's
  // CodeGenFunction::StartFunction.
  if (getCodeGenOpts().hasSanitizeBinaryMetadata()) {
    if (noSanitizeMask & SanitizerKind::Thread)
      attrs.set("no_sanitize_thread",
                mlir::UnitAttr::get(&getMLIRContext()));
  }

  // disable_sanitizer_instrumentation suppresses sanitizer function attrs.
  if (decl->hasAttr<DisableSanitizerInstrumentationAttr>()) {
    attrs.set("disable_sanitizer_instrumentation",
              mlir::UnitAttr::get(&getMLIRContext()));
  } else {
    if (sanOpts.hasOneOf(SanitizerKind::Address | SanitizerKind::KernelAddress))
      attrs.set("sanitize_address", mlir::UnitAttr::get(&getMLIRContext()));
    if (sanOpts.hasOneOf(SanitizerKind::HWAddress |
                         SanitizerKind::KernelHWAddress))
      attrs.set("sanitize_hwaddress", mlir::UnitAttr::get(&getMLIRContext()));
    if (sanOpts.has(SanitizerKind::MemtagStack))
      attrs.set("sanitize_memtag", mlir::UnitAttr::get(&getMLIRContext()));
    if (sanOpts.has(SanitizerKind::Thread))
      attrs.set("sanitize_thread", mlir::UnitAttr::get(&getMLIRContext()));
    if (sanOpts.has(SanitizerKind::Type))
      attrs.set("sanitize_type", mlir::UnitAttr::get(&getMLIRContext()));
    if (sanOpts.has(SanitizerKind::NumericalStability))
      attrs.set("sanitize_numerical_stability",
                mlir::UnitAttr::get(&getMLIRContext()));
    if (sanOpts.hasOneOf(SanitizerKind::Memory | SanitizerKind::KernelMemory))
      attrs.set("sanitize_memory", mlir::UnitAttr::get(&getMLIRContext()));
    if (sanOpts.has(SanitizerKind::AllocToken))
      attrs.set("sanitize_alloc_token",
                mlir::UnitAttr::get(&getMLIRContext()));
  }
  if (sanOpts.has(SanitizerKind::SafeStack))
    attrs.set("safestack", mlir::UnitAttr::get(&getMLIRContext()));
  if (sanOpts.has(SanitizerKind::ShadowCallStack))
    attrs.set("shadowcallstack", mlir::UnitAttr::get(&getMLIRContext()));
  if (sanOpts.has(SanitizerKind::Realtime))
    if (decl->getASTContext().hasAnyFunctionEffects())
      for (const FunctionEffectWithCondition &fe : decl->getFunctionEffects()) {
        if (fe.Effect.kind() == FunctionEffect::Kind::NonBlocking)
          attrs.set("sanitize_realtime",
                    mlir::UnitAttr::get(&getMLIRContext()));
        else if (fe.Effect.kind() == FunctionEffect::Kind::Blocking)
          attrs.set("sanitize_realtime_blocking",
                    mlir::UnitAttr::get(&getMLIRContext()));
      }
  if (sanOpts.hasOneOf(SanitizerKind::Fuzzer | SanitizerKind::FuzzerNoLink))
    attrs.set("optforfuzzing", mlir::UnitAttr::get(&getMLIRContext()));

  // Propagate fn_ret_thunk_extern for Spectre v2 return thunk mitigation.
  // Matches OGCG CodeGenFunction.cpp:1013-1022.
  if (auto *a = decl->getAttr<FunctionReturnThunksAttr>()) {
    switch (a->getThunkType()) {
    case FunctionReturnThunksAttr::Kind::Keep:
      break;
    case FunctionReturnThunksAttr::Kind::Extern:
      attrs.set("fn_ret_thunk_extern", mlir::UnitAttr::get(&getMLIRContext()));
      break;
    }
  } else if (codeGenOpts.FunctionReturnThunks) {
    attrs.set("fn_ret_thunk_extern", mlir::UnitAttr::get(&getMLIRContext()));
  }

  // C++ [basic.start.main]p3: "The function main shall not be used within
  // a program." Also applies to OpenCL, HLSL, SYCL, and CUDA kernels.
  // Matches OGCG CodeGenFunction.cpp:1080-1084.
  if (const auto *fd = dyn_cast<FunctionDecl>(decl)) {
    if ((getLangOpts().CPlusPlus && fd->isMain()) || getLangOpts().OpenCL ||
        getLangOpts().HLSL || getLangOpts().SYCLIsDevice ||
        (getLangOpts().CUDA && fd->hasAttr<CUDAGlobalAttr>()))
      attrs.set("norecurse", mlir::UnitAttr::get(&getMLIRContext()));

    // When -mstack-alignment=N is specified, main needs stackrealign because
    // the C runtime may call it with default (lower) stack alignment.
    // Matches OGCG CodeGenFunction.cpp:1098-1102.
    if ((fd->isMain() || fd->isMSVCRTEntryPoint()) &&
        codeGenOpts.StackAlignment)
      attrs.set("stackrealign", mlir::UnitAttr::get(&getMLIRContext()));
  }

  // Propagate -finstrument-functions / -finstrument-functions-after-inlining /
  // -finstrument-function-entry-bare attributes, matching OGCG's
  // StartFunction() and FinishFunction() in CodeGenFunction.cpp.
  // Functions with __attribute__((no_instrument_function)) are excluded.
  if (!decl->hasAttr<NoInstrumentFunctionAttr>()) {
    if (codeGenOpts.InstrumentFunctions) {
      attrs.set("instrument-function-entry",
                getStringAttrCached("__cyg_profile_func_enter"));
      attrs.set("instrument-function-exit",
                getStringAttrCached("__cyg_profile_func_exit"));
    }
    if (codeGenOpts.InstrumentFunctionsAfterInlining) {
      attrs.set("instrument-function-entry-inlined",
                getStringAttrCached("__cyg_profile_func_enter"));
      attrs.set("instrument-function-exit-inlined",
                getStringAttrCached("__cyg_profile_func_exit"));
    }
    if (codeGenOpts.InstrumentFunctionEntryBare)
      attrs.set("instrument-function-entry-inlined",
                getStringAttrCached("__cyg_profile_func_enter_bare"));
    // -pg / -mfentry profiling: add counting-function or fentry-call attr.
    // Matches OGCG CodeGenFunction.cpp:1174-1196.
    if (codeGenOpts.InstrumentForProfiling) {
      if (codeGenOpts.CallFEntry)
        attrs.set("fentry-call", getStringAttrCached("true"));
      else
        attrs.set("instrument-function-entry-inlined",
                  getStringAttrCached(getTarget().getMCountName()));
      if (codeGenOpts.MNopMCount) {
        if (!codeGenOpts.CallFEntry)
          getDiags().Report(diag::err_opt_not_valid_without_opt)
              << "-mnop-mcount" << "-mfentry";
        attrs.set("mnop-mcount", mlir::UnitAttr::get(&getMLIRContext()));
      }
      if (codeGenOpts.RecordMCount) {
        if (!codeGenOpts.CallFEntry)
          getDiags().Report(diag::err_opt_not_valid_without_opt)
              << "-mrecord-mcount" << "-mfentry";
        attrs.set("mrecord-mcount", mlir::UnitAttr::get(&getMLIRContext()));
      }
    }
  }

  // NoProfile function attribute from __attribute__((no_profile_instrument_function)).
  // Matches OGCG CodeGenFunction.cpp:1005-1006.
  if (decl->hasAttr<NoProfileFunctionAttr>())
    attrs.set("noprofile", mlir::UnitAttr::get(&getMLIRContext()));

  if (codeGenOpts.getProfileInstr() != llvm::driver::ProfileInstrKind::ProfileNone) {
    switch (isFunctionBlockedFromProfileInstr(f.getSymName(), decl->getLocation())) {
    case ProfileList::Skip:
      attrs.set("skipprofile", mlir::UnitAttr::get(&getMLIRContext()));
      break;
    case ProfileList::Forbid:
      attrs.set("noprofile", mlir::UnitAttr::get(&getMLIRContext()));
      break;
    case ProfileList::Allow:
      break;
    }
  }

  // XRay instrumentation attributes, matching OGCG
  // CodeGenFunction.cpp:895-941.
  bool alwaysXRayAttr = false;
  if (const auto *xrayAttr = decl->getAttr<XRayInstrumentAttr>()) {
    if (codeGenOpts.XRayInstrumentationBundle.has(
            XRayInstrKind::FunctionEntry) ||
        codeGenOpts.XRayInstrumentationBundle.has(
            XRayInstrKind::FunctionExit)) {
      if (xrayAttr->alwaysXRayInstrument() &&
          codeGenOpts.XRayInstrumentFunctions) {
        attrs.set("function-instrument", getStringAttrCached("xray-always"));
        alwaysXRayAttr = true;
      }
      if (xrayAttr->neverXRayInstrument())
        attrs.set("function-instrument", getStringAttrCached("xray-never"));
      if (const auto *logArgs = decl->getAttr<XRayLogArgsAttr>())
        if (codeGenOpts.XRayInstrumentFunctions)
          attrs.set("xray-log-args",
                    getUIntStringAttrCached(logArgs->getArgumentCount()));
    }
  } else {
    if (codeGenOpts.XRayInstrumentFunctions) {
      // Check external xray instrument files before falling back to
      // xray-instruction-threshold. Matches OGCG
      // CodeGenFunction.cpp:914 / CodeGenModule.cpp:3889-3913.
      const auto &xRayFilter = getASTContext().getXRayFilter();
      using ImbueAttr = XRayFunctionFilter::ImbueAttribute;
      auto xrayAttr = ImbueAttr::NONE;
      auto loc = decl->getLocation();
      if (loc.isValid())
        xrayAttr = xRayFilter.shouldImbueLocation(loc, StringRef());
      if (xrayAttr == ImbueAttr::NONE)
        xrayAttr = xRayFilter.shouldImbueFunction(f.getSymName());
      switch (xrayAttr) {
      case ImbueAttr::NONE:
        attrs.set("xray-instruction-threshold",
                  getStringAttrCached(
                      llvm::itostr(codeGenOpts.XRayInstructionThreshold)));
        break;
      case ImbueAttr::ALWAYS:
        attrs.set("function-instrument", getStringAttrCached("xray-always"));
        break;
      case ImbueAttr::ALWAYS_ARG1:
        attrs.set("function-instrument", getStringAttrCached("xray-always"));
        attrs.set("xray-log-args", getStringAttrCached("1"));
        break;
      case ImbueAttr::NEVER:
        attrs.set("function-instrument", getStringAttrCached("xray-never"));
        break;
      }
    }
  }

  if (codeGenOpts.XRayInstrumentFunctions) {
    if (codeGenOpts.XRayIgnoreLoops)
      attrs.set("xray-ignore-loops", mlir::UnitAttr::get(&getMLIRContext()));

    if (!codeGenOpts.XRayInstrumentationBundle.has(
            XRayInstrKind::FunctionExit))
      attrs.set("xray-skip-exit", mlir::UnitAttr::get(&getMLIRContext()));

    if (!codeGenOpts.XRayInstrumentationBundle.has(
            XRayInstrKind::FunctionEntry))
      attrs.set("xray-skip-entry", mlir::UnitAttr::get(&getMLIRContext()));

    auto funcGroups = codeGenOpts.XRayTotalFunctionGroups;
    if (funcGroups > 1) {
      auto funcName = f.getSymName();
      auto funcNameRef = llvm::ArrayRef<uint8_t>(
          reinterpret_cast<const uint8_t *>(funcName.data()),
          funcName.size());
      auto group = llvm::crc32(funcNameRef) % funcGroups;
      if (group != codeGenOpts.XRaySelectedFunctionGroup &&
          !alwaysXRayAttr)
        attrs.set("function-instrument", getStringAttrCached("xray-never"));
    }
  }

  // Patchable function entry attributes, matching OGCG
  // CodeGenFunction.cpp:957-985.
  {
    unsigned count, offset;
    llvm::StringRef section;
    if (const auto *attr = decl->getAttr<PatchableFunctionEntryAttr>()) {
      count = attr->getCount();
      offset = attr->getOffset();
      section = attr->getSection();
    } else {
      count = codeGenOpts.PatchableFunctionEntryCount;
      offset = codeGenOpts.PatchableFunctionEntryOffset;
    }
    if (section.empty())
      section = codeGenOpts.PatchableFunctionEntrySection;
    if (count && offset <= count) {
      attrs.set("patchable-function-entry",
                getUIntStringAttrCached(count - offset));
      if (offset)
        attrs.set("patchable-function-prefix",
                  getUIntStringAttrCached(offset));
      if (!section.empty())
        attrs.set("patchable-function-entry-section",
                  getStringAttrCached(section));
    }
    // COFF/CodeView hot patching on x86/x64 only.
    if (codeGenOpts.HotPatch &&
        getTarget().getTriple().isX86() &&
        getTarget().getTriple().getEnvironment() != llvm::Triple::CODE16)
      attrs.set(
          "patchable-function",
          getStringAttrCached("prologue-short-redirect"));
  }

  // OGCG handles function alignment: max decl alignment, -falign-functions,
  // and C++ ABI member function 2-byte alignment requirement.
  {
    unsigned alignment = decl->getMaxAlignment() / astContext.getCharWidth();
    if (alignment)
      f.setAlignment(alignment);

    if (!decl->hasAttr<AlignedAttr>())
      if (getLangOpts().FunctionAlignment)
        f.setAlignment(1ull << getLangOpts().FunctionAlignment);

    // Some C++ ABIs require 2-byte alignment for member functions, in order to
    // reserve a bit for differentiating between virtual and non-virtual member
    // functions. If the current target's C++ ABI requires this and this is a
    // member function, set its alignment accordingly.
    if (getTarget().getCXXABI().areMemberFunctionsAligned()) {
      if (isa<CXXMethodDecl>(decl)) {
        // C++ ABIs require 2-byte minimum alignment for member functions to
        // reserve a bit for member function pointers. Only set alignment when
        // the function's effective alignment is less than 2. The effective
        // alignment comes from either the explicit alignment or the data
        // layout's function alignment (Fn in the datalayout string).
        // On AArch64 (Fn32 = 4-byte), no explicit align is needed.
        // On x86 (no Fn = 1-byte default), align 2 must be set.
        // Matches OGCG CodeGenModule.cpp:2881 getPointerAlignment check.
        std::optional<uint64_t> curAlign = f.getAlignment();
        // Use the target's data layout to determine the default function
        // alignment. AArch64 has Fn32 (4 bytes), x86 defaults to 1 byte.
        llvm::DataLayout llvmDL(getTarget().getDataLayoutString());
        unsigned dlFnAlign = llvmDL.getFunctionPtrAlign()
                                 .value_or(llvm::Align(1))
                                 .value();
        unsigned effectiveAlign = curAlign.value_or(dlFnAlign);
        if (effectiveAlign < 2)
          f.setAlignment(2);
      }
    }
  }

  // Add no-jump-tables value (OGCG: CodeGenFunction.cpp:987-989).
  if (getCodeGenOpts().NoUseJumpTables)
    attrs.set("no-jump-tables", getStringAttrCached("true"));

  // Add no-inline-line-tables (OGCG: CodeGenFunction.cpp:991-993).
  if (getCodeGenOpts().NoInlineLineTables)
    attrs.set("no-inline-line-tables",
              mlir::UnitAttr::get(&getMLIRContext()));

  // Add profile-sample-accurate (OGCG: CodeGenFunction.cpp:995-997).
  if (getCodeGenOpts().ProfileSampleAccurate)
    attrs.set("profile-sample-accurate",
              mlir::UnitAttr::get(&getMLIRContext()));

  // Add use-sample-profile (OGCG: CodeGenFunction.cpp:999-1000).
  if (!getCodeGenOpts().SampleProfileFile.empty())
    attrs.set("use-sample-profile",
              mlir::UnitAttr::get(&getMLIRContext()));

  // Add cfi-canonical-jump-table when explicitly requested on this function.
  // Matches OGCG CodeGenFunction.cpp:1002-1003.
  if (decl && decl->hasAttr<CFICanonicalJumpTableAttr>())
    attrs.set("cfi-canonical-jump-table",
              mlir::UnitAttr::get(&getMLIRContext()));

  // Add inline-max-stacksize (OGCG: CodeGenModule.cpp:3351-3352).
  if (getCodeGenOpts().InlineMaxStackSize != UINT_MAX)
    attrs.set("inline-max-stacksize",
              getUIntStringAttrCached(getCodeGenOpts().InlineMaxStackSize));

  // Propagate target-cpu, tune-cpu, and target-features attributes to function
  // definitions, matching OGCG's GetCPUAndFeaturesAttributes.
  {
    llvm::StringRef targetCPU = getTarget().getTargetOpts().CPU;
    llvm::StringRef tuneCPU = getTarget().getTargetOpts().TuneCPU;
    std::vector<std::string> features;

    const auto *fd = decl->getMostRecentDecl();
    const auto *td = fd ? fd->getAttr<TargetAttr>() : nullptr;
    const auto *tv = fd ? fd->getAttr<TargetVersionAttr>() : nullptr;
    const auto *sd = fd ? fd->getAttr<CPUSpecificAttr>() : nullptr;
    const auto *tc = fd ? fd->getAttr<TargetClonesAttr>() : nullptr;

    if (td || tv || sd || tc) {
      // Per-function target attribute overrides. Guard the complex
      // multiversion cases; handle the common TargetAttr case.
      if (td) {
        ParsedTargetAttr parsedAttr =
            getTarget().parseTargetAttr(td->getFeaturesStr());
        if (!parsedAttr.CPU.empty() &&
            getTarget().isValidCPUName(parsedAttr.CPU)) {
          targetCPU = parsedAttr.CPU;
          tuneCPU = "";
        }
        if (!parsedAttr.Tune.empty() &&
            getTarget().isValidCPUName(parsedAttr.Tune))
          tuneCPU = parsedAttr.Tune;
      }

      if (sd) {
        // Apply the selected cpu_specific CPU as tune-cpu for this MV clone.
        tuneCPU = sd->getCPUName(gd.getMultiVersionIndex())->getName();
      }

      llvm::StringMap<bool> featureMap;
      GlobalDecl featureGD = gd;
      // If the provided GD is empty (e.g. helper-generated funcs), construct
      // one from the declaration. This preserves prior behavior while allowing
      // multiversion index-aware feature maps when GD is available.
      if (!featureGD) {
        if (const auto *cd = dyn_cast<CXXConstructorDecl>(decl))
          featureGD = GlobalDecl(cd, Ctor_Complete);
        else if (const auto *dd = dyn_cast<CXXDestructorDecl>(decl))
          featureGD = GlobalDecl(dd, Dtor_Complete);
        else
          featureGD = GlobalDecl(decl);
      }
      astContext.getFunctionFeatureMap(featureMap, featureGD);
      for (const auto &entry : featureMap)
        features.push_back((entry.getValue() ? "+" : "-") +
                           entry.getKey().str());

      // Add metadata for AArch64 FMV (target_version/target_clones), matching
      // OGCG GetCPUAndFeaturesAttributes.
      if (getTarget().getTriple().isAArch64()) {
        llvm::SmallVector<StringRef, 8> feats;
        bool isDefault = false;
        if (tv) {
          isDefault = tv->isDefaultVersion();
          tv->getFeatures(feats);
        } else if (tc) {
          isDefault = tc->isDefaultVersion(gd.getMultiVersionIndex());
          tc->getFeatures(feats, gd.getMultiVersionIndex());
        }
        if (isDefault) {
          attrs.set("fmv-features", mlir::UnitAttr::get(&getMLIRContext()));
        } else if (!feats.empty()) {
          std::set<StringRef> orderedFeats(feats.begin(), feats.end());
          std::string fmvFeatures;
          for (StringRef feat : orderedFeats)
            fmvFeatures.append("," + feat.str());
          attrs.set("fmv-features",
                    getStringAttrCached(fmvFeatures.substr(1)));
        }
      }
    } else {
      features = getTarget().getTargetOpts().Features;
    }

    if (!targetCPU.empty())
      attrs.set("target-cpu", getStringAttrCached(targetCPU));
    if (!tuneCPU.empty())
      attrs.set("tune-cpu", getStringAttrCached(tuneCPU));
    if (!features.empty()) {
      llvm::erase_if(features, [&](const std::string &f) {
        return getTarget().isReadOnlyFeature(f.substr(1));
      });
      llvm::sort(features);
      attrs.set("target-features",
                getStringAttrCached(llvm::join(features, ",")));
    }
  }

  // OGCG emits CFI type metadata for cross-DSO canonical jump tables
  // on function definitions (CodeGenModule.cpp:2885-2895).
  if (codeGenOpts.SanitizeCfiCrossDso &&
      codeGenOpts.SanitizeCfiCanonicalJumpTables) {
    // Skip available_externally functions. They won't be codegen'ed in the
    // current module anyway.
    if (astContext.GetGVALinkageForFunction(decl) != GVA_AvailableExternally)
      addFunctionTypeMetadataForIcall(decl, f);
  }
  if (codeGenOpts.CallGraphSection &&
      astContext.GetGVALinkageForFunction(decl) != GVA_AvailableExternally)
    addFunctionTypeMetadataForCallGraphSection(decl, f);
  // OGCG emits type metadata for member function pointer checks on
  // CXXMethodDecl definitions (CodeGenModule.cpp:2902-2913).
  if (const auto *methodDecl = dyn_cast<CXXMethodDecl>(decl)) {
    auto requiresMemberFunctionPointerTypeMetadata =
        [&](const CXXMethodDecl *md) {
          if (!codeGenOpts.LTOUnit || !hasHiddenLTOVisibility(md->getParent()))
            return false;

          return md->isImplicitObjectMemberFunction() && !md->isVirtual() &&
                 !isa<CXXConstructorDecl, CXXDestructorDecl>(md);
        };

    if (requiresMemberFunctionPointerTypeMetadata(methodDecl)) {
      auto *ctx = &getMLIRContext();
      auto i64Ty = mlir::IntegerType::get(ctx, 64);
      auto zero = mlir::IntegerAttr::get(i64Ty, 0);
      SmallVector<mlir::Attribute, 4> entries;
      if (auto existing =
              f->getAttrOfType<mlir::ArrayAttr>("cir.func.type_metadata"))
        entries.append(existing.begin(), existing.end());

      auto hasPayload = [&](mlir::Attribute payload) {
        for (mlir::Attribute existing : entries) {
          auto pair = mlir::dyn_cast<mlir::ArrayAttr>(existing);
          if (!pair || pair.size() != 2)
            continue;
          if (pair[1] == payload)
            return true;
        }
        return false;
      };

      for (const CXXRecordDecl *base :
           getMostBaseClasses(methodDecl->getParent())) {
        QualType memberPointerType = astContext.getMemberPointerType(
            methodDecl->getType(), std::nullopt, base);
        auto payload = getStringAttrCached(
            createMetadataIdentifierStringForType(memberPointerType));
        if (payload.getValue().empty() || hasPayload(payload))
          continue;
        entries.push_back(mlir::ArrayAttr::get(ctx, {zero, payload}));
      }

      if (!entries.empty())
        f->setAttr("cir.func.type_metadata", mlir::ArrayAttr::get(ctx, entries));
    }
  }

  // Set DLL storage class for dllexport/dllimport functions.
  // Matches OGCG's setDLLImportDLLExport (CodeGenModule.cpp:1961-1970).
  // Note: this function is only called for definitions, so the
  // !isDeclarationForLinker() guard for dllexport is implicitly satisfied.
  if (decl && decl->isExternallyVisible()) {
    if (decl->hasAttr<DLLImportAttr>())
      attrs.set("dll_storage_class", getStringAttrCached("dllimport"));
    else if (decl->hasAttr<DLLExportAttr>() ||
             shouldMapVisibilityToDLLExport(decl))
      attrs.set("dll_storage_class", getStringAttrCached("dllexport"));
  }

  f.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
      attrs.getDictionary(&getMLIRContext())));
}

cir::FuncOp CIRGenModule::getOrCreateCIRFunction(
    StringRef mangledName, mlir::Type funcType, GlobalDecl gd, bool forVTable,
    bool dontDefer, bool isThunk, ForDefinition_t isForDefinition,
    mlir::ArrayAttr extraAttrs) {
  const Decl *d = gd.getDecl();

  if (isThunk) {
    // Thunks are handled; continue past to create the function normally.
  }

  if (const auto *fd = dyn_cast_or_null<FunctionDecl>(d)) {
    if (getLangOpts().OpenMPIsTargetDevice &&
        isEmittingOpenMPTargetGenericFunction())
      markAsOpenMPTargetGenericFunction(fd);

    if (getLangOpts().OpenMPIsTargetDevice &&
        !markAsOpenMPTargetGlobal(gd) && fd->isDefined() && !dontDefer &&
        !isForDefinition) {
      const FunctionDecl *fdDef = fd->getDefinition();
      assert(fdDef && "defined function should have a definition");
      GlobalDecl gdDef;
      if (const auto *cd = dyn_cast<CXXConstructorDecl>(fdDef))
        gdDef = GlobalDecl(cd, gd.getCtorType());
      else if (const auto *dd = dyn_cast<CXXDestructorDecl>(fdDef))
        gdDef = GlobalDecl(dd, gd.getDtorType());
      else
        gdDef = GlobalDecl(fdDef);
      emitGlobalDecl(gdDef, getMangledName(gdDef));
    }

    // Any attempts to use a MultiVersion function should result in retrieving
    // the iFunc instead. Name mangling will handle the rest of the changes.
    // OGCG calls GetOrCreateMultiVersionResolver here, which creates an
    // ifunc/resolver and defers the resolver body emission to TU finalization.
    if (fd->isMultiVersion()) {
      updateMultiVersionNames(gd, fd, mangledName);
    }
    if (fd->isMultiVersion() && !isForDefinition) {
      if (fd->isTargetVersionMultiVersion() ||
          fd->isTargetClonesMultiVersion()) {
        // Match OGCG's AArch64 behavior: do not create an ifunc eagerly on
        // first use. Instead, keep references to the unsuffixed symbol and
        // defer resolver/ifunc emission until TU finalization, once we know
        // whether a default version definition exists in this TU.
        if (getTarget().getTriple().isAArch64()) {
          addDeferredMultiVersionResolverToEmit(gd);
          std::string baseName =
              getMangledNameImpl(*this, gd, fd, /*omit=*/true);
          auto fn = getOrCreateCIRFunction(
              baseName, funcType, GlobalDecl{}, /*forVTable=*/false,
              /*dontDefer=*/false, /*isThunk=*/false, NotForDefinition);
          setFunctionAttributes(gd, fn, /*isIncompleteFunction=*/false,
                                /*isThunk=*/false);
          addSyntheticCallsiteNonnullAttrs(*this, gd, fn,
                                           /*isThunk=*/false);
          return fn;
        }
        // Non-AArch64 target_version or target_clones: create resolver/ifunc
        // immediately so that call references resolve to the ifunc symbol.
        return getOrCreateMultiVersionResolver(gd);
      }
      if (fd->isTargetMultiVersion())
        return getOrCreateMultiVersionResolver(gd);
      // For cpu_dispatch and cpu_specific, calls should go through the ifunc
      // (named with the base name, without multiversion suffix). This mirrors
      // OGCG's UpdateMultiVersionNames + GetOrCreateMultiVersionResolver which
      // appends ".ifunc" for both cpu_dispatch and cpu_specific. In CIR, the
      // ifunc uses the base name directly (no ".ifunc" suffix).
      if (fd->isCPUDispatchMultiVersion() ||
          fd->isCPUSpecificMultiVersion()) {
        std::string baseName =
            getMangledNameImpl(*this, gd, fd, /*omit=*/true);
        if (auto *existing = getGlobalValue(baseName))
          if (auto fn = mlir::dyn_cast<cir::FuncOp>(existing))
            return fn;
        // Ifunc doesn't exist yet (forward reference). Create a placeholder
        // FuncOp with the base name that emitCPUDispatchDefinition will
        // later convert to an ifunc.
        return getOrCreateCIRFunction(
            baseName, funcType, GlobalDecl{}, /*forVTable=*/false,
            /*dontDefer=*/false, /*isThunk=*/false, NotForDefinition);
      }
    }
  }

  // Lookup the entry, lazily creating it if necessary.
  mlir::Operation *entry = getGlobalValue(mangledName);
  if (entry) {
    // If the entry is a cir.alias (from __attribute__((alias))), convert it
    // to a cir.func with the aliasee attribute. OGCG handles this
    // transparently because llvm::GlobalAlias isa llvm::GlobalValue and can
    // be returned directly. In CIR, we need a FuncOp for call targets, so
    // replace the AliasOp with a FuncOp that has its aliasee set.
    if (auto aliasOp = mlir::dyn_cast<cir::AliasOp>(entry)) {
      auto aliaseeRef = aliasOp.getAliasee();
      auto linkage = aliasOp.getLinkage();
      bool isWeak = aliasOp.getWeak();
      bool isIfunc = aliasOp.getIsIfunc();
      auto loc = aliasOp.getLoc();

      // Erase the alias and create a FuncOp declaration in its place.
      // Keep lastGlobalOp coherent: if it points to this alias, clearing it
      // avoids creating later globals at a dangling insertion point.
      if (lastGlobalOp == aliasOp)
        lastGlobalOp = nullptr;
      eraseGlobalSymbol(aliasOp);
      aliasOp.erase();

      auto *funcDecl = llvm::dyn_cast_or_null<FunctionDecl>(gd.getDecl());
      cir::FuncOp fn = createCIRFunction(
          loc, mangledName, mlir::cast<cir::FuncType>(funcType), funcDecl);
      fn.setAliasee(aliaseeRef);
      // Preserve the ifunc flag from the AliasOp so the LLVM lowering
      // creates an llvm.mlir.ifunc instead of an llvm.mlir.alias.
      if (isIfunc)
        fn->setAttr("cir.ifunc",
                     mlir::UnitAttr::get(fn->getContext()));
      // Apply function attributes (nonnull, noalias, nobuiltin, etc.) that
      // would normally be set when creating the FuncOp on the normal path.
      if (d)
        setFunctionAttributes(gd, fn, /*isIncompleteFunction=*/false, isThunk);
      // Set linkage AFTER setFunctionAttributes because setFunctionAttributes
      // may override linkage for weak functions (ExternalWeakLinkage). The
      // alias linkage from the original AliasOp takes precedence.
      fn.setLinkage(isWeak ? cir::GlobalLinkageKind::WeakAnyLinkage : linkage);
      return fn;
    }

    // If the entry is a cir.global (from a variable declaration with the
    // same asm label), replace it with a function. This happens when a
    // function and a variable share the same asm-level name, e.g.:
    //   void foo3() __asm__("var");
    //   int foo4 __asm__("var") = 4;
    // OGCG handles this transparently because llvm::GlobalVariable isa
    // llvm::GlobalValue and opaque pointers allow returning any global.
    // In CIR, we need a FuncOp for call targets, so rename the GlobalOp
    // and create a FuncOp with the original name, then update all existing
    // references to point to the renamed global.
    if (auto globalOp = mlir::dyn_cast<cir::GlobalOp>(entry)) {
      // Generate a unique renamed name for the global variable.
      std::string renamedName = (mangledName + ".renamed").str();
      unsigned renamedSuffix = 0;
      while (auto *existing = getGlobalValue(renamedName)) {
        if (existing == globalOp)
          break;
        renamedName = (mangledName + ".renamed." +
                       llvm::Twine(++renamedSuffix))
                          .str();
      }

      // Update all existing symbol references from the old name to the new
      // name within the module. Use a direct walk over GetGlobalOp instead
      // of replaceAllSymbolUses which materializes full DictionaryAttrs per op.
      auto oldNameAttr = globalOp.getSymNameAttr();
      getModule().walk([&](cir::GetGlobalOp ggo) {
        if (ggo.getNameAttr().getAttr() == oldNameAttr)
          ggo.setName(renamedName);
      });

      // Rename the global and re-register it in our symbol table cache.
      // Store the original name so LowerToLLVM can map it back.
      globalOp->setAttr("cir.asm_label_original_name",
                        mlir::StringAttr::get(&getMLIRContext(), mangledName));
      eraseGlobalSymbol(globalOp);
      globalOp.setSymName(renamedName);
      insertGlobalSymbol(globalOp);

      auto *funcDecl = llvm::dyn_cast_or_null<FunctionDecl>(gd.getDecl());
      cir::FuncOp fn = createCIRFunction(
          getLoc(funcDecl ? funcDecl->getSourceRange() : SourceRange()),
          mangledName, mlir::cast<cir::FuncType>(funcType), funcDecl);
      return fn;
    }

    assert(mlir::isa<cir::FuncOp>(entry));

    // If this entry was created as extern_weak due to a weakref, reset its
    // linkage if the actual declaration is not weak. Matches OGCG's
    // WeakRefReferences.erase(Entry) logic in GetOrCreateLLVMFunction.
    if (weakRefReferences.erase(entry)) {
      const auto *fd = cast_or_null<FunctionDecl>(d);
      if (fd && !fd->hasAttr<WeakAttr>()) {
        auto fn = cast<cir::FuncOp>(entry);
        fn.setLinkage(cir::GlobalLinkageKind::ExternalLinkage);
      }
    }

    // Handle dropped DLL attributes.
    // OGCG resets DLL storage class to default here for Windows PE/COFF.
    // CIR skips this — Windows is not a supported target.
    if (d && !d->hasAttr<DLLImportAttr>() && !d->hasAttr<DLLExportAttr>())
      setDSOLocal(entry);

    // If there are two attempts to define the same mangled name, issue an
    // error.
    auto fn = cast<cir::FuncOp>(entry);
    // If we already have a definition with the same type, just return it.
    // This can happen with inline methods in classes that get emitted multiple
    // times.
    if (fn && fn.getFunctionType() == funcType) {
      return fn;
    }
    // OGCG reports err_duplicate_mangled_name here when two different
    // definitions produce the same mangled name. CIR's CIRGen doesn't emit
    // clang diagnostics; MLIR's symbol table catches true duplicates.

    if (!isForDefinition) {
      // In OGCG, this returns the existing function directly because LLVM
      // uses opaque pointers — the function pointer type doesn't matter for
      // calls. In CIR with typed function pointers, a type mismatch between
      // the function declaration and call arguments causes verification
      // failures. We handle three cases:
      //
      // 1. Placeholder void() type (from convertFunctionTypeInternal due to
      //    incomplete parameter types) → update to the real type.
      //
      // 2. Same parameter count but different pointee types (from incomplete
      //    vs complete record types in pointer parameters) → update the type
      //    and fix existing call sites with pointer bitcasts.
      //
      // 3. Different parameter count (e.g., member function pointer created
      //    a free-function type without 'this', then a direct call provides
      //    the correct method type with 'this') → update the type and fix
      //    existing pointer references.
      if (fn.isDeclaration()) {
        auto existingTy = fn.getFunctionType();
        auto newTy = mlir::cast<cir::FuncType>(funcType);
        // A single symbol can be referenced with two return types that differ
        // only by a target where `long double` and `double` share a format:
        // bionic's <stdlib.h> declares `long double strtold(...)
        // __RENAME_LDBL(strtod, ...)`, aliasing strtold onto the strtod symbol.
        // On arm32 that yields one @strtod referenced as returning both
        // !cir.double and !cir.long_double<!cir.double>. OGCG never sees this
        // because both lower to the same LLVM `double`. Keep the existing decl
        // type instead of flipping it (which would orphan already-emitted call
        // sites); emitCall coerces each call result to its expected type with a
        // same-size bitcast.
        auto isFormatCompatibleFPReturnOnly = [](cir::FuncType a,
                                                 cir::FuncType b) {
          if (a.getNumInputs() != b.getNumInputs() ||
              a.isVarArg() != b.isVarArg())
            return false;
          for (unsigned i = 0, e = a.getNumInputs(); i != e; ++i)
            if (a.getInput(i) != b.getInput(i))
              return false;
          auto fa = mlir::dyn_cast<cir::FPTypeInterface>(a.getReturnType());
          auto fb = mlir::dyn_cast<cir::FPTypeInterface>(b.getReturnType());
          return fa && fb && fa.getWidth() == fb.getWidth();
        };
        if (existingTy != newTy &&
            !isFormatCompatibleFPReturnOnly(existingTy, newTy)) {
          bool isPlaceholder = existingTy.getNumInputs() == 0 &&
                               !existingTy.isVarArg() &&
                               mlir::isa<cir::VoidType>(
                                   existingTy.getReturnType());

          if (isPlaceholder) {
            // Case 1: placeholder → real type. No existing calls to fix
            // (calls to void() have 0 args and verification skips no-proto).
            fn.setFunctionType(newTy);
          } else {
            // Non-placeholder type mismatch (same param count with stale pointee
            // types or different count, e.g. method placeholder vs full type):
            // collect symbol users once, update the function type, then repair
            // typed pointer users.
            llvm::SmallVector<cir::CallOp, 4> callUsers;
            llvm::SmallVector<cir::GetGlobalOp, 4> getGlobalUsers;
            auto targetNameAttr = fn.getSymNameAttr();
            // Collect direct call/get_global users from finished functions
            // known to reference this symbol, plus in-flight functions that
            // are not indexed yet.
            forEachPotentialFunctionUserOfSymbol(
                targetNameAttr, [&](cir::FuncOp moduleFn) {
                  moduleFn.walk([&](mlir::Operation *op) {
                if (auto call = mlir::dyn_cast<cir::CallOp>(op)) {
                  if (auto callee = call.getCalleeAttr();
                      callee && callee.getAttr() == targetNameAttr)
                    callUsers.push_back(call);
                } else if (auto getGlobal = mlir::dyn_cast<cir::GetGlobalOp>(op)) {
                  if (getGlobal.getNameAttr().getAttr() == targetNameAttr)
                    getGlobalUsers.push_back(getGlobal);
                }
                  });
                });
            collectTopLevelGetGlobalUsers(targetNameAttr, getGlobalUsers);
            fn.setFunctionType(newTy);
            if (!callUsers.empty() || !getGlobalUsers.empty())
              replacePointerTypeArgs(fn, fn, callUsers, getGlobalUsers);
          }
        }
      }
      maybeTrackDeferredInlineMemberCandidate(gd, mangledName);
      return fn;
    }

    // TODO(cir): classic codegen checks here if this is a llvm::GlobalAlias.
    // How will we support this?
  }

  auto *funcDecl = llvm::dyn_cast_or_null<FunctionDecl>(gd.getDecl());
  bool invalidLoc = !d ||
                    d->getSourceRange().getBegin().isInvalid() ||
                    d->getSourceRange().getEnd().isInvalid();
  cir::FuncOp funcOp = createCIRFunction(
      invalidLoc ? theModule->getLoc() : getLoc(d->getSourceRange()),
      mangledName, mlir::cast<cir::FuncType>(funcType), funcDecl);

  if (funcDecl && funcDecl->hasAttr<AnnotateAttr>())
    deferredAnnotations[mangledName] = funcDecl;

  // If we already created a function with the same mangled name (but different
  // type) before, take its name and add it to the list of functions to be
  // replaced with F at the end of CodeGen.
  //
  // This happens if there is a prototype for a function (e.g. "int f()") and
  // then a definition of a different type (e.g. "int f(int x)").
  if (entry) {
    // This might be an implementation of a function without a prototype, in
    // which case, try to do special replacement of calls which match the new
    // prototype. The really key thing here is that we also potentially drop
    // arguments from the call site so as to make a direct call, which makes the
    // inliner happier and suppresses a number of optimizer warnings (!) about
    // dropping arguments.
    //
    // Do not pre-check SymbolTable uses here: replaceUsesOfNonProtoTypeWith-
    // RealFunction already does typed user collection with an early-return
    // when there are no users.
    replaceUsesOfNonProtoTypeWithRealFunction(entry, funcOp);

    // Obliterate no-proto declaration.
    entry->erase();
  }

  if (d) {
    // Set linkage and visibility in case we never see a definition.
    // Matches OGCG's setLinkageForGV call in GetOrCreateLLVMFunction.
    if (auto *nd = dyn_cast<NamedDecl>(d)) {
      LinkageInfo lv = nd->getLinkageAndVisibility();
      if (isExternallyVisible(lv.getLinkage()) &&
          (nd->hasAttr<WeakAttr>() || nd->isWeakImported()))
        funcOp.setLinkage(cir::GlobalLinkageKind::ExternalWeakLinkage);
    }
    // Detect incomplete function types: if convertFunctionTypeInternal returned
    // a placeholder void() because the function type depends on incomplete tag
    // types, skip setting function attributes. Matches OGCG's
    // IsIncompleteFunction logic in GetOrCreateLLVMFunction.
    bool isIncompleteFunction = false;
    if (const auto *fd = dyn_cast<FunctionDecl>(d)) {
      const auto *ft = fd->getType()->castAs<FunctionType>();
      if (!getTypes().isFuncTypeConvertible(ft))
        isIncompleteFunction = true;
    }
    setFunctionAttributes(gd, funcOp, isIncompleteFunction, isThunk);
    if (auto btfTags = collectCIRBTFDeclTagAttrs(getMLIRContext(), d);
        !btfTags.empty())
      funcOp->setAttr("cir.debug.btf_decl_tag", builder.getArrayAttr(btfTags));
  }

  // 'dontDefer' actually means don't move this to the deferredDeclsToEmit list.
  if (dontDefer) {
    // TODO(cir): This assertion will need an additional condition when we
    // support incomplete functions.
    assert(funcOp.getFunctionType() == funcType);
    return funcOp;
  }

  maybeTrackDeferredInlineMemberCandidate(gd, mangledName);

  // All MSVC dtors other than the base dtor are linkonce_odr and delegate to
  // each other bottoming out wiht the base dtor. Therefore we emit non-base
  // dtors on usage, even if there is no dtor definition in the TU.
  if (isa_and_nonnull<CXXDestructorDecl>(d) &&
      getCXXABI().useThunkForDtorVariant(cast<CXXDestructorDecl>(d),
                                         gd.getDtorType())) {
    addDeferredDeclToEmit(gd);
  }

  // This is the first use or definition of a mangled name. If there is a
  // deferred decl with this name, remember that we need to emit it at the end
  // of the file.
  auto ddi = deferredDecls.find(mangledName);
  if (ddi != deferredDecls.end()) {
    // Move the potentially referenced deferred decl to the
    // DeferredDeclsToEmit list, and remove it from DeferredDecls (since we
    // don't need it anymore).
    addDeferredDeclToEmit(ddi->second);
    deferredDecls.erase(ddi);

    // Otherwise, there are cases we have to worry about where we're using a
    // declaration for which we must emit a definition but where we might not
    // find a top-level definition.
    //   - member functions defined inline in their classes
    //   - friend functions defined inline in some class
    //   - special member functions with implicit definitions
    // If we ever change our AST traversal to walk into class methods, this
    // will be unnecessary.
    //
    // We also don't emit a definition for a function if it's going to be an
    // entry in a vtable, unless it's already marked as used.
  } else if (getLangOpts().CPlusPlus && d) {
    const auto *fn = cast<FunctionDecl>(d);
    bool found = false;
    // Look for a declaration that's lexically in a record.
    for (const auto *fd = fn->getMostRecentDecl(); fd;
         fd = fd->getPreviousDecl()) {
      if (isa<CXXRecordDecl>(fd->getLexicalDeclContext())) {
        if (fd->doesThisDeclarationHaveABody()) {
          addDeferredDeclToEmit(gd.getWithDecl(fd));
          found = true;
          break;
        }
      }
    }
    // For implicit template instantiations (e.g. std::forward<int>,
    // std::move<T>) whose bodies were never instantiated by Sema,
    // trigger on-demand instantiation via Sema and defer emission.
    if (!found && sema &&
        fn->getTemplateSpecializationKind() == TSK_ImplicitInstantiation &&
        !fn->hasBody()) {
      // Once this specialization is queued for deferred retry, avoid repeated
      // Sema instantiation attempts at each new reference. release() performs
      // the actual retry loop once the translation unit is complete, which
      // avoids forcing early instantiation while referenced types are still
      // incomplete (e.g. rvalue std::static_pointer_cast over forward-declared
      // listeners in large system-header-heavy TUs).
      GlobalDecl canonicalGd = getCanonicalDeclForMangledName(gd, getTarget());
      if (deferredImplicitTemplateRetrySet.contains(canonicalGd))
        return funcOp;

      recordDeferredImplicitTemplateRetryCandidate(gd, mangledName);
      return funcOp;
    } else if (!found && !fn->hasBody() && fn->isInlined() &&
               fn->getTemplateSpecializationKind() !=
                   TSK_ImplicitInstantiation) {
      // Record inline member declarations for end-of-TU deferred-emission
      // checks in release() without requiring a full mangledDeclNames scan.
      // Final linkage/body/definition checks are still performed in release().
      recordDeferredInlineMemberCandidate(gd, mangledName);
    }
  }

  return funcOp;
}

cir::FuncOp
CIRGenModule::createCIRFunction(mlir::Location loc, StringRef name,
                                cir::FuncType funcType,
                                const clang::FunctionDecl *funcDecl) {
  cir::FuncOp func;
  {
    mlir::OpBuilder::InsertionGuard guard(builder);

    // Some global emissions are triggered while emitting a function, e.g.
    // void s() { x.method() }
    //
    // Match OGCG's ordering: declarations materialized while emitting
    // another function body are appended after the current function.
    // OGCG's GetOrCreateLLVMFunction appends to the module's function list,
    // so declarations appear after the function that first references them.
    // Definitions (e.g. nested/deferred) are inserted before the current
    // function to keep them available for forward references.
    CIRGenFunction *cgf = this->curCGF;
    if (cgf) {
      const bool isDeclarationOnly =
          funcDecl && !funcDecl->doesThisDeclarationHaveABody();
      if (isDeclarationOnly)
        builder.setInsertionPointAfter(cgf->curFn);
      else
        builder.setInsertionPoint(cgf->curFn);
    } else
      builder.clearInsertionPoint();

    func = cir::FuncOp::create(builder, loc, name, funcType);

    symbolLookupCache[func.getSymNameAttr()] = func;

    if (funcDecl)
      func.setAstAttr(cir::makeFuncDeclAttr(funcDecl, &getMLIRContext()));
    if (funcDecl)
      if (auto declareTargetAttr =
              getOpenMPDeclareTargetAttr(getMLIRContext(), funcDecl))
        func->setAttr("omp.declare_target", *declareTargetAttr);
    if (funcDecl)
      if (std::optional<OMPDeclareTargetDeclAttr *> activeAttr =
              OMPDeclareTargetDeclAttr::getActiveAttr(funcDecl);
          activeAttr && (*activeAttr)->getIndirect())
        func->setAttr("cir.omp.declare_target_indirect",
                      mlir::UnitAttr::get(&getMLIRContext()));

    if (funcDecl && !funcDecl->hasPrototype())
      func.setNoProto(true);

    assert(func.isDeclaration() && "expected empty body");

    // A declaration gets private visibility by default, but external linkage
    // as the default linkage.
    func.setLinkageAttr(cir::GlobalLinkageKindAttr::get(
        &getMLIRContext(), cir::GlobalLinkageKind::ExternalLinkage));
    mlir::SymbolTable::setSymbolVisibility(
        func, mlir::SymbolTable::Visibility::Private);

    // Initialize with empty dict of extra attributes.
    func.setExtraAttrsAttr(
        cir::ExtraFuncAttributesAttr::get(builder.getDictionaryAttr({})));

    // Mark C++ special member functions (Constructor, Destructor etc.)
    setCXXSpecialMemberAttr(func, funcDecl);

    if (!cgf)
      theModule.push_back(func);

    if (funcDecl && this->getLangOpts().OpenACC) {
      // We only have to handle this attribute, since OpenACCAnnotAttrs are
      // handled via the end-of-TU work.
      for (const auto *attr :
           funcDecl->specific_attrs<OpenACCRoutineDeclAttr>())
        emitOpenACCRoutineDecl(funcDecl, func, attr->getLocation(),
                               attr->Clauses);
    }
  }
  return func;
}

cir::FuncOp
CIRGenModule::createCIRBuiltinFunction(mlir::Location loc, StringRef name,
                                       cir::FuncType ty,
                                       const clang::FunctionDecl *fd) {
  cir::FuncOp fnOp = createCIRFunction(loc, name, ty, fd);
  fnOp.setBuiltin(true);
  return fnOp;
}

void CIRGenModule::maybeSetDisableTailCallsForBlockInvoke(
    cir::FuncOp fn, const clang::BlockDecl *blockDecl) {
  if (!getCodeGenOpts().NoEscapingBlockTailCalls || !blockDecl ||
      blockDecl->doesNotEscape())
    return;

  mlir::NamedAttrList attrs;
  if (auto extraAttrs = fn.getExtraAttrsAttr()) {
    for (auto attr : extraAttrs.getElements().getValue())
      attrs.append(attr.getName(), attr.getValue());
  }
  attrs.set("disable-tail-calls", getStringAttrCached("true"));
  fn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
      attrs.getDictionary(&getMLIRContext())));
}

static cir::CtorKind getCtorKindFromDecl(const CXXConstructorDecl *ctor) {
  if (ctor->isDefaultConstructor())
    return cir::CtorKind::Default;
  if (ctor->isCopyConstructor())
    return cir::CtorKind::Copy;
  if (ctor->isMoveConstructor())
    return cir::CtorKind::Move;
  return cir::CtorKind::Custom;
}

static cir::AssignKind getAssignKindFromDecl(const CXXMethodDecl *method) {
  if (method->isCopyAssignmentOperator())
    return cir::AssignKind::Copy;
  if (method->isMoveAssignmentOperator())
    return cir::AssignKind::Move;
  llvm_unreachable("not a copy or move assignment operator");
}

void CIRGenModule::setCXXSpecialMemberAttr(
    cir::FuncOp funcOp, const clang::FunctionDecl *funcDecl) {
  if (!funcDecl)
    return;

  if (const auto *dtor = dyn_cast<CXXDestructorDecl>(funcDecl)) {
    auto cxxDtor = cir::CXXDtorAttr::get(
        convertType(getASTContext().getCanonicalTagType(dtor->getParent())),
        dtor->isTrivial());
    funcOp.setCxxSpecialMemberAttr(cxxDtor);
    return;
  }

  if (const auto *ctor = dyn_cast<CXXConstructorDecl>(funcDecl)) {
    cir::CtorKind kind = getCtorKindFromDecl(ctor);
    auto cxxCtor = cir::CXXCtorAttr::get(
        convertType(getASTContext().getCanonicalTagType(ctor->getParent())),
        kind, ctor->isTrivial());
    funcOp.setCxxSpecialMemberAttr(cxxCtor);
    return;
  }

  const auto *method = dyn_cast<CXXMethodDecl>(funcDecl);
  if (method && (method->isCopyAssignmentOperator() ||
                 method->isMoveAssignmentOperator())) {
    cir::AssignKind assignKind = getAssignKindFromDecl(method);
    auto cxxAssign = cir::CXXAssignAttr::get(
        convertType(getASTContext().getCanonicalTagType(method->getParent())),
        assignKind, method->isTrivial());
    funcOp.setCxxSpecialMemberAttr(cxxAssign);
    return;
  }
}

static const FunctionDecl *getRuntimeFunctionDecl(ASTContext &astContext,
                                                  StringRef name) {
  TranslationUnitDecl *tuDecl = astContext.getTranslationUnitDecl();
  DeclContext *dc = TranslationUnitDecl::castToDeclContext(tuDecl);

  IdentifierInfo &id = astContext.Idents.get(name);
  for (const auto *result : dc->lookup(&id))
    if (const auto *fd = dyn_cast<FunctionDecl>(result))
      return fd;

  if (!astContext.getLangOpts().CPlusPlus)
    return nullptr;

  IdentifierInfo &cxxId =
      (name == "_ZSt9terminatev" || name == "?terminate@@YAXXZ")
          ? astContext.Idents.get("terminate")
          : astContext.Idents.get(name);

  for (const auto &nsName : {"__cxxabiv1", "std"}) {
    IdentifierInfo &ns = astContext.Idents.get(nsName);
    for (const auto *result : dc->lookup(&ns)) {
      const NamespaceDecl *nsDecl = dyn_cast<NamespaceDecl>(result);
      if (auto *linkageSpec = dyn_cast<LinkageSpecDecl>(result))
        for (const auto *linkageResult : linkageSpec->lookup(&ns))
          if ((nsDecl = dyn_cast<NamespaceDecl>(linkageResult)))
            break;

      if (!nsDecl)
        continue;
      for (const auto *nsResult : nsDecl->lookup(&cxxId))
        if (const auto *fd = dyn_cast<FunctionDecl>(nsResult))
          return fd;
    }
  }

  return nullptr;
}

static void setWindowsItaniumDLLImport(CIRGenModule &cgm, bool isLocal,
                                       cir::FuncOp funcOp, StringRef name) {
  // In Windows Itanium environments, try to mark runtime functions
  // dllimport. For Mingw and MSVC, don't. We don't really know if the user
  // will link their standard library statically or dynamically. Marking
  // functions imported when they are not imported can cause linker errors
  // and warnings.
  if (!isLocal && cgm.getTriple().isWindowsItaniumEnvironment() &&
      !cgm.getCodeGenOpts().LTOVisibilityPublicStd) {
    const FunctionDecl *fd = getRuntimeFunctionDecl(cgm.getASTContext(), name);
    if (!fd || fd->hasAttr<DLLImportAttr>()) {
      mlir::NamedAttrList attrs;
      if (auto extraAttrs = funcOp.getExtraAttrs())
        attrs = mlir::NamedAttrList{extraAttrs->getElements().getValue()};
      attrs.set("dll_storage_class", cgm.getStringAttrCached("dllimport"));
      funcOp.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
          attrs.getDictionary(&cgm.getMLIRContext())));
    }
  }
}

cir::FuncOp CIRGenModule::createRuntimeFunction(cir::FuncType ty,
                                                StringRef name, mlir::ArrayAttr,
                                                bool isLocal,
                                                bool assumeConvergent,
                                                bool dontDefer) {
  if (assumeConvergent) {
    errorNYI("createRuntimeFunction: assumeConvergent");
  }

  cir::FuncOp entry = getOrCreateCIRFunction(name, ty, GlobalDecl(),
                                             /*forVtable=*/false, dontDefer);

  if (entry) {
    // OGCG would call setLLVMFunctionFEnvAttributes here for runtime
    // functions (no FunctionDecl). That function has no definition in OGCG.
    // Runtime functions with constrained FP are not yet handled in CIR.
    // Match OGCG's RuntimeCC for synthesized runtime helpers. This is usually
    // C, but some targets (for example 32-bit ARM EABI) require a distinct
    // runtime calling convention for ABI parity.
    entry.setCallingConv(getTargetCIRGenInfo().getRuntimeCallingConv(*this));
    setWindowsItaniumDLLImport(*this, isLocal, entry, name);
    // Use the proper shouldAssumeDSOLocal() logic, matching OGCG's
    // CreateRuntimeFunction (CodeGenModule.cpp:5447). Previously this was
    // unconditionally true, which added spurious dso_local on ELF PIC
    // declarations.
    setDSOLocal(entry.getOperation());
    // Mark as a runtime function so the CallConvLowering pass skips
    // noundef/signext/zeroext attribute emission. Matches OGCG's
    // `if (D) SetFunctionAttributes(...)` in GetOrCreateLLVMFunction, which
    // skips attribute setting for runtime functions (null Decl).
    entry->setAttr("cir.no_proto_attrs",
                   mlir::UnitAttr::get(&getMLIRContext()));
  }

  return entry;
}

mlir::SymbolTable::Visibility
CIRGenModule::getMLIRVisibility(cir::GlobalOp op) {
  // MLIR doesn't accept public symbols declarations (only
  // definitions).
  if (op.isDeclaration())
    return mlir::SymbolTable::Visibility::Private;
  return getMLIRVisibilityFromCIRLinkage(op.getLinkage());
}

mlir::SymbolTable::Visibility
CIRGenModule::getMLIRVisibilityFromCIRLinkage(cir::GlobalLinkageKind glk) {
  switch (glk) {
  case cir::GlobalLinkageKind::InternalLinkage:
  case cir::GlobalLinkageKind::PrivateLinkage:
    return mlir::SymbolTable::Visibility::Private;
  case cir::GlobalLinkageKind::ExternalLinkage:
  case cir::GlobalLinkageKind::ExternalWeakLinkage:
  case cir::GlobalLinkageKind::LinkOnceAnyLinkage:
  case cir::GlobalLinkageKind::LinkOnceODRLinkage:
  case cir::GlobalLinkageKind::AvailableExternallyLinkage:
  case cir::GlobalLinkageKind::CommonLinkage:
  case cir::GlobalLinkageKind::WeakAnyLinkage:
  case cir::GlobalLinkageKind::WeakODRLinkage:
    return mlir::SymbolTable::Visibility::Public;
  }
  llvm_unreachable("getMLIRVisibilityFromCIRLinkage: unhandled linkage kind");
}

cir::VisibilityKind CIRGenModule::getGlobalVisibilityKindFromClangVisibility(
    clang::VisibilityAttr::VisibilityType visibility) {
  switch (visibility) {
  case clang::VisibilityAttr::VisibilityType::Default:
    return cir::VisibilityKind::Default;
  case clang::VisibilityAttr::VisibilityType::Hidden:
    return cir::VisibilityKind::Hidden;
  case clang::VisibilityAttr::VisibilityType::Protected:
    return cir::VisibilityKind::Protected;
  }
  llvm_unreachable("unexpected visibility value");
}

cir::VisibilityAttr
CIRGenModule::getGlobalVisibilityAttrFromDecl(const Decl *decl) {
  const clang::VisibilityAttr *va = decl->getAttr<clang::VisibilityAttr>();
  cir::VisibilityAttr cirVisibility =
      cir::VisibilityAttr::get(&getMLIRContext());
  if (va) {
    cirVisibility = cir::VisibilityAttr::get(
        &getMLIRContext(),
        getGlobalVisibilityKindFromClangVisibility(va->getVisibility()));
  }
  return cirVisibility;
}

bool CIRGenModule::shouldOpportunisticallyEmitVTables() {
  return codeGenOpts.OptimizationLevel > 0;
}

void CIRGenModule::emitVTablesOpportunistically() {
  // Try to emit external vtables as available_externally if they have emitted
  // all inlined virtual functions.  It runs after emitDeferred() and therefore
  // is not allowed to create new references to things that need to be emitted
  // lazily. Note that it also uses the fact that we eagerly emit RTTI.

  assert(
      (opportunisticVTables.empty() || shouldOpportunisticallyEmitVTables()) &&
      "Only emit opportunistic vtables with optimizations");

  for (const CXXRecordDecl *rd : opportunisticVTables) {
    assert(getVTables().isVTableExternal(rd) &&
           "This queue should only contain external vtables");
    if (getCXXABI().canSpeculativelyEmitVTable(rd))
      vtables.generateClassData(rd);
  }
  opportunisticVTables.clear();
}

bool CIRGenModule::isDeferredInlineMemberCandidate(GlobalDecl gd,
                                                   StringRef mangledName) {
  if (!gd.getDecl())
    return false;
  auto *fd = dyn_cast<FunctionDecl>(gd.getDecl());
  if (!fd || fd->hasBody())
    return false;
  if (fd->getTemplateSpecializationKind() == TSK_ImplicitInstantiation)
    return false;
  if (!fd->isInlined())
    return false;
  const FunctionDecl *def = nullptr;
  if (!fd->isDefined(def) || !def)
    return false;
  auto *op = getGlobalValue(mangledName);
  if (!op)
    return false;
  auto funcOp = dyn_cast<cir::FuncOp>(op);
  if (!funcOp || !funcOp.isDeclaration())
    return false;
  auto linkage = getFunctionLinkage(gd);
  return linkage == cir::GlobalLinkageKind::LinkOnceODRLinkage ||
         linkage == cir::GlobalLinkageKind::WeakODRLinkage;
}

void CIRGenModule::maybeTrackDeferredInlineMemberCandidate(GlobalDecl gd,
                                                           StringRef mangledName) {
  if (isDeferredInlineMemberCandidate(gd, mangledName))
    deferredInlineMemberCandidates.insert(gd);
}

void CIRGenModule::release() {
  // Process deferred implicit template instantiations that were referenced
  // but whose bodies Sema never instantiated (e.g. std::forward<int>,
  // std::move<T>). Uses a worklist built once from mangledDeclNames, then
  // only scans NEW entries after each emitDeferred round to avoid
  // re-scanning the full map.
  // Process deferred implicit template instantiations and inline member
  // functions that have declarations in the module but no bodies yet.
  // Uses targeted worklists to avoid re-scanning the full mangledDeclNames
  // map on each iteration.
  if (sema && !deferredImplicitTemplateRetryCandidates.empty()) {
    auto collectReferencedFunctionSymbols = [&]() {
      llvm::DenseSet<StringRef> candidateFunctionSymbols;
      for (auto &[gd, name] : deferredImplicitTemplateRetryCandidates)
        candidateFunctionSymbols.insert(name);

      llvm::DenseSet<StringRef> referencedFunctionSymbols;
      if (candidateFunctionSymbols.empty())
        return referencedFunctionSymbols;
      size_t remainingSymbols = candidateFunctionSymbols.size();

      for (auto moduleFn : theModule.getOps<cir::FuncOp>()) {
        // Declarations have no bodies/users.
        if (moduleFn.isDeclaration())
          continue;
        auto walkResult = moduleFn.walk([&](mlir::Operation *op) {
          if (remainingSymbols == 0)
            return mlir::WalkResult::interrupt();
          if (auto call = mlir::dyn_cast<cir::CallOp>(op)) {
            if (auto callee = call.getCalleeAttr()) {
              StringRef calleeName = callee.getAttr().getValue();
              if (candidateFunctionSymbols.contains(calleeName)) {
                if (referencedFunctionSymbols.insert(calleeName).second &&
                    remainingSymbols > 0)
                  remainingSymbols--;
              }
            }
          } else if (auto getGlobal = mlir::dyn_cast<cir::GetGlobalOp>(op)) {
            auto globalName = getGlobal.getName();
            if (candidateFunctionSymbols.contains(globalName)) {
              if (referencedFunctionSymbols.insert(globalName).second &&
                  remainingSymbols > 0)
                remainingSymbols--;
            }
          }
          return mlir::WalkResult::advance();
        });
        if (walkResult.wasInterrupted() && remainingSymbols == 0)
          break;
      }
      return referencedFunctionSymbols;
    };

    llvm::DenseSet<StringRef> referencedFunctionSymbols =
        collectReferencedFunctionSymbols();

    // Build initial worklist of implicit template instantiations that need
    // retrying from declarations that previously failed eager instantiation.
    SmallVector<std::pair<GlobalDecl, StringRef>> templateWorklist;
    size_t scannedUpTo = deferredImplicitTemplateRetryCandidates.size();
    for (auto &[gd, name] : deferredImplicitTemplateRetryCandidates) {
      if (!referencedFunctionSymbols.contains(name))
        continue;
      auto *fd = dyn_cast<FunctionDecl>(gd.getDecl());
      if (!fd || fd->hasBody())
        continue;
      if (fd->getTemplateSpecializationKind() != TSK_ImplicitInstantiation)
        continue;
      const FunctionDecl *pattern = fd->getTemplateInstantiationPattern();
      if (!pattern || !pattern->hasBody())
        continue;
      auto *op = getGlobalValue(name);
      if (!op)
        continue;
      auto funcOp = dyn_cast<cir::FuncOp>(op);
      if (funcOp && funcOp.isDeclaration())
        templateWorklist.push_back({gd, name});
    }

    // Process template worklist iteratively. Each round may instantiate
    // templates that reference new implicit instantiations.
    unsigned iterations = 0;
    while (!templateWorklist.empty() && iterations < 64) {
      iterations++;
      SmallVector<GlobalDecl> toDefer;
      SmallVector<std::pair<GlobalDecl, StringRef>> nextWorklist;
      for (auto &[gd, name] : templateWorklist) {
        if (!referencedFunctionSymbols.contains(name))
          continue;
        auto *fd = dyn_cast<FunctionDecl>(gd.getDecl());
        if (fd->hasBody())
          continue;
        auto *op = getGlobalValue(name);
        if (!op)
          continue;
        auto funcOp = dyn_cast<cir::FuncOp>(op);
        if (!funcOp || !funcOp.isDeclaration())
          continue;
        auto *mutableFd = const_cast<FunctionDecl *>(fd);
        sema->InstantiateFunctionDefinition(fd->getLocation(), mutableFd,
                                            /*Recursive=*/false,
                                            /*DefinitionRequired=*/true);
        if (mutableFd->hasBody())
          toDefer.push_back(gd);
        else
          nextWorklist.push_back({gd, name});
      }
      templateWorklist = std::move(nextWorklist);
      if (toDefer.empty())
        break;
      for (auto &gd : toDefer)
        addDeferredDeclToEmit(gd);
      emitDeferred(getCodeGenOpts().ClangIRBuildDeferredThreshold);
      referencedFunctionSymbols = collectReferencedFunctionSymbols();

      // Only scan NEW entries added to the retry-candidate list after
      // emitDeferred.
      size_t newSize = deferredImplicitTemplateRetryCandidates.size();
      if (newSize > scannedUpTo) {
        auto it = deferredImplicitTemplateRetryCandidates.begin();
        std::advance(it, scannedUpTo);
        for (; it != deferredImplicitTemplateRetryCandidates.end(); ++it) {
          if (!referencedFunctionSymbols.contains(it->second))
            continue;
          auto *fd = dyn_cast<FunctionDecl>(it->first.getDecl());
          if (!fd || fd->hasBody())
            continue;
          if (fd->getTemplateSpecializationKind() != TSK_ImplicitInstantiation)
            continue;
          const FunctionDecl *pattern = fd->getTemplateInstantiationPattern();
          if (!pattern || !pattern->hasBody())
            continue;
          auto *op = getGlobalValue(it->second);
          if (!op)
            continue;
          auto funcOp = dyn_cast<cir::FuncOp>(op);
          if (funcOp && funcOp.isDeclaration())
            templateWorklist.push_back({it->first, it->second});
        }
        scannedUpTo = newSize;
      }
    }
  }

  // Emit inline member functions that were referenced but whose bodies
  // were not emitted. This handles cases like inline constructors/destructors
  // defined in class body with non-trivial members (e.g., std::string).
  // Only emit linkonce_odr/weak_odr functions — NOT available_externally.
  {
    SmallVector<GlobalDecl> toDefer;
    for (auto &[gd, name] : deferredInlineMemberDeclCandidates) {
      auto *fd = dyn_cast<FunctionDecl>(gd.getDecl());
      if (!fd || fd->hasBody())
        continue;
      if (fd->getTemplateSpecializationKind() == TSK_ImplicitInstantiation)
        continue;
      if (!fd->isInlined())
        continue;
      const FunctionDecl *def = nullptr;
      if (!fd->isDefined(def) || !def)
        continue;
      auto *op = getGlobalValue(name);
      if (!op)
        continue;
      auto funcOp = dyn_cast<cir::FuncOp>(op);
      if (!funcOp || !funcOp.isDeclaration())
        continue;
      auto linkage = getFunctionLinkage(gd);
      if (linkage != cir::GlobalLinkageKind::LinkOnceODRLinkage &&
          linkage != cir::GlobalLinkageKind::WeakODRLinkage)
        continue;
      toDefer.push_back(gd);
    }
    for (auto &gd : toDefer)
      addDeferredDeclToEmit(gd);
    if (!toDefer.empty())
      emitDeferred(getCodeGenOpts().ClangIRBuildDeferredThreshold);
    deferredInlineMemberDeclCandidates.clear();
    deferredInlineMemberDeclCandidateSet.clear();
    deferredInlineMemberCandidates.clear();
  }

  emitDeferred(getCodeGenOpts().ClangIRBuildDeferredThreshold);
  emitVTablesOpportunistically();

  // All vtables (and their relative-vtable aliases) have now been emitted.
  // Apply the accumulated relative-vtable symbol renames in a single module
  // walk instead of one walk per alias (avoids O(aliases * module-size)).
  vtables.applyDeferredRelativeVTableRenames();

  applyReplacements();

  // OGCG emits EmitCXXGlobalInitFunc / EmitCXXGlobalCleanUpFunc here to
  // create the global C++ initialization (_GLOBAL__sub_I_) and cleanup
  // functions. CIR handles global init/cleanup through global_ctor/global_dtor
  // attributes and LoweringPrepare.

  // When compiling C++20 modules, store imported module initializer names
  // in a module attribute so LoweringPrepare can emit calls to them.
  if (langOpts.CPlusPlusModules &&
      getCXXABI().getMangleContext().getKind() ==
          ItaniumMangleContext::MK_Itanium) {
    SmallVector<mlir::Attribute> moduleInitNames;
    for (clang::Module *m : importedModules) {
      if (m->isHeaderLikeModule())
        continue;
      if (!m->isNamedModuleInterfaceHasInit())
        continue;
      SmallString<256> initName;
      llvm::raw_svector_ostream out(initName);
      cast<ItaniumMangleContext>(getCXXABI().getMangleContext())
          .mangleModuleInitializer(m, out);
      moduleInitNames.push_back(getStringAttrCached(initName));
    }
    if (!moduleInitNames.empty())
      theModule->setAttr("cir.module_init_imports",
                         builder.getArrayAttr(moduleInitNames));
  }

  // Emit TLS wrapper functions for thread-local variables (Itanium ABI).
  getCXXABI().emitCXXThreadLocalInitFuncs();

  theModule->setAttr(cir::CIRDialect::getModuleLevelAsmAttrName(),
                     builder.getArrayAttr(globalScopeAsm));

  emitGlobalAnnotations();

  if (codeGenOpts.UnwindTables)
    theModule->setAttr(
        cir::CIRDialect::getUWTableAttrName(),
        cir::UWTableAttr::get(&getMLIRContext(),
                              cir::UWTableKind(codeGenOpts.UnwindTables)));

  // Store the default function attributes for compiler-generated internal
  // functions (e.g. __cxx_global_var_init, _GLOBAL__sub_I_). These functions
  // are created later in LoweringPrepare, which doesn't have access to
  // CodeGenOptions. Matches OGCG's SetInternalFunctionAttributes which calls
  // SetLLVMFunctionAttributes + SetLLVMFunctionAttributesForDefinition with
  // D=nullptr to set nounwind, sspstrong, target-features, etc.
  {
    mlir::NamedAttrList defaultAttrs;
    getDefaultInternalFunctionAttributes(defaultAttrs);

    theModule->setAttr(
        "cir.default_internal_func_attrs",
        cir::ExtraFuncAttributesAttr::get(
            defaultAttrs.getDictionary(&getMLIRContext())));
  }

  // Emit OpenCL specific module metadata: OpenCL version.
  if (langOpts.CUDAIsDevice && getTriple().isSPIRV())
    llvm_unreachable("CUDA SPIR-V NYI");
  if (langOpts.OpenCL) {
    buildOpenCLMetadata();
    // Emit SPIR version.
    if (getTriple().isSPIR())
      llvm_unreachable("SPIR target NYI");
  }

  auto attrUsesObjCEHTypes = [](mlir::Attribute attr) {
    auto globalView = mlir::dyn_cast<cir::GlobalViewAttr>(attr);
    return globalView &&
           globalView.getSymbol().getValue().starts_with("OBJC_EHTYPE");
  };

  auto tryUsesObjCEHTypes = [&](cir::TryOp tryOp) {
    if (auto handlerTypes = tryOp.getHandlerTypesAttr()) {
      for (mlir::Attribute attr : handlerTypes)
        if (attrUsesObjCEHTypes(attr))
          return true;
    }

    if (auto filterTypes =
            tryOp->getAttrOfType<mlir::ArrayAttr>("cir.filter_types")) {
      for (mlir::Attribute attr : filterTypes)
        if (attrUsesObjCEHTypes(attr))
          return true;
    }

    return false;
  };

  // Match OGCG's SimplifyPersonality(): in ObjC++ on the NeXT runtime, only
  // rewrite to the C++ personality when every use of the ObjC++ personality
  // is C++-only (or cleanup-only). OGCG does this as a module-wide rewrite of
  // the shared personality symbol, so mixed modules must keep the ObjC
  // personality even for cleanup-only functions.
  if (langOpts.CPlusPlus && langOpts.ObjC && langOpts.Exceptions &&
      langOpts.ObjCRuntime.isNeXTFamily()) {
    const EHPersonality &objCXX = EHPersonality::get(*this, /*fd=*/nullptr);
    const EHPersonality &cxx = [&]() -> const EHPersonality & {
      const llvm::Triple &triple = getTarget().getTriple();
      if (triple.isWindowsMSVCEnvironment())
        return EHPersonality::MSVC_CxxFrameHandler3;
      if (triple.isOSAIX())
        return EHPersonality::XL_CPlusPlus;
      if (codeGenOpts.hasSjLjExceptions())
        return EHPersonality::GNU_CPlusPlus_SJLJ;
      if (codeGenOpts.hasDWARFExceptions())
        return EHPersonality::GNU_CPlusPlus;
      if (codeGenOpts.hasSEHExceptions())
        return EHPersonality::GNU_CPlusPlus_SEH;
      if (codeGenOpts.hasWasmExceptions())
        return EHPersonality::GNU_Wasm_CPlusPlus;
      return EHPersonality::GNU_CPlusPlus;
    }();

    if (std::strcmp(objCXX.personalityFn, cxx.personalityFn) != 0) {
      bool hasObjCXXPersonalityUses = false;
      bool objCXXPersonalityHasOnlyCXXEHUses = true;

      for (auto funcOp : getModule().getOps<cir::FuncOp>()) {
        std::optional<llvm::StringRef> personality = funcOp.getPersonality();
        if (!personality || *personality != objCXX.personalityFn)
          continue;

        hasObjCXXPersonalityUses = true;
        funcOp.walk([&](cir::TryOp tryOp) {
          if (tryUsesObjCEHTypes(tryOp))
            objCXXPersonalityHasOnlyCXXEHUses = false;
          return objCXXPersonalityHasOnlyCXXEHUses
                     ? mlir::WalkResult::advance()
                     : mlir::WalkResult::interrupt();
        });
        if (!objCXXPersonalityHasOnlyCXXEHUses)
          break;
      }

      if (hasObjCXXPersonalityUses && objCXXPersonalityHasOnlyCXXEHUses) {
        mlir::Type i32Ty = builder.getI32Type();
        auto personalityTy = cir::FuncType::get({}, i32Ty, /*isVarArg=*/true);
        createRuntimeFunction(personalityTy, cxx.personalityFn);

        for (auto funcOp : getModule().getOps<cir::FuncOp>()) {
          std::optional<llvm::StringRef> personality = funcOp.getPersonality();
          if (!personality || *personality != objCXX.personalityFn)
            continue;
          funcOp.setPersonality(cxx.personalityFn);
        }
      }
    }
  }

  // OGCG emits unmangled extern "C" aliases before validating aliases/ifuncs.
  // CIR needs the same ordering so static C++ resolvers inside extern "C"
  // blocks can retarget placeholder ifunc resolver references before they are
  // checked for being fully defined.
  emitStaticExternCAliases();
  checkAliases();

  // OGCG's checkAliases() also adds disable_sanitizer_instrumentation to ifunc
  // resolver functions (CodeGenModule.cpp:833-837). We replicate that here
  // after validation has stripped any invalid ifuncs.
  for (auto &op : getModule().getBody()->getOperations()) {
    // Ifuncs in CIR: (1) FuncOp with cir.ifunc attr and aliasee, or
    //                (2) AliasOp with is_ifunc flag.
    std::optional<llvm::StringRef> resolverName;
    if (auto funcOp = mlir::dyn_cast<cir::FuncOp>(op)) {
      if (funcOp->hasAttr("cir.ifunc") && funcOp.getAliasee())
        resolverName = *funcOp.getAliasee();
    } else if (auto aliasOp = mlir::dyn_cast<cir::AliasOp>(op)) {
      if (aliasOp.getIsIfunc())
        resolverName = aliasOp.getAliasee();
    }
    if (!resolverName)
      continue;
    if (auto *resolverEntry = getGlobalValue(*resolverName)) {
      if (auto resolverFunc = mlir::dyn_cast<cir::FuncOp>(resolverEntry)) {
        mlir::NamedAttrList attrs;
        if (auto existingAttrs = resolverFunc.getExtraAttrs())
          attrs =
              mlir::NamedAttrList{existingAttrs->getElements().getValue()};
        attrs.set("disable_sanitizer_instrumentation",
                  mlir::UnitAttr::get(&getMLIRContext()));
        resolverFunc.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
            attrs.getDictionary(&getMLIRContext())));
      }
    }
  }

  // OGCG calls EmitModuleLinkOptions() to emit llvm.linker.options metadata.
  // CIR emits cir.linker_options in CIRGenerator::Initialize and lowers it to
  // llvm.linker.options in CIR-to-LLVM.

  // Dependent libraries are emitted in CIRGenerator::Initialize.

  // OGCG calls registerGlobalDtorsWithAtExit() in Release() to convert
  // queued global dtors into global ctors that register dtors through
  // __cxa_atexit/atexit. CIR mirrors this in LoweringPrepare:
  // functions marked with "cir.register_global_dtor_with_atexit" are moved
  // from cir.global_dtors to cir.global_ctors with generated
  // __GLOBAL_init_<priority> helpers.

  // OGCG calls EmitCtorList for llvm.global_ctors and llvm.global_dtors.
  // CIR models ctor/dtor registration via GlobalOp ctor/dtor regions and
  // lowers them to LLVM global ctor/dtor lists in CIR-to-LLVM lowering.

  // Emit deferred multi-version resolver functions (ifunc + resolver body).
  // Mirrors OGCG's emitMultiVersionFunctions() call in Release().
  emitMultiVersionFunctions();

  // Emit cross-DSO CFI check helpers at end-of-TU, matching OGCG Release().
  // The check stub references __cfi_check_fail, and __cfi_check_fail must be
  // preserved from DCE, so add it to llvm.used (as OGCG does).
  if (codeGenOpts.SanitizeCfiCrossDso) {
    mlir::Location loc = theModule.getLoc();
    auto i64Ty = cir::IntType::get(&getMLIRContext(), 64, /*isSigned=*/false);
    auto i8Ty = builder.getUInt8Ty();
    auto i32Ty = builder.getUInt32Ty();
    CharUnits intAlign =
        astContext.toCharUnitsFromBits(astContext.getTargetInfo().getIntAlign());
    auto intptrTy =
        mlir::cast<cir::IntType>(convertType(astContext.getUIntPtrType()));
    auto ptrTy = voidPtrTy;
    auto checkFailTy =
        cir::FuncType::get({ptrTy, ptrTy}, voidTy, /*isVarArg=*/false);
    auto checkTy =
        cir::FuncType::get({i64Ty, ptrTy, ptrTy}, voidTy, /*isVarArg=*/false);
    auto sourceLocationTy =
        builder.getAnonRecordTy({ptrTy, i32Ty, i32Ty},
                                /*packed=*/false, /*padded=*/false);
    auto cfiCheckFailDataTy =
        builder.getAnonRecordTy({i8Ty, sourceLocationTy, ptrTy},
                                /*packed=*/false, /*padded=*/false);
    auto noSanitizeAttr = mlir::UnitAttr::get(&getMLIRContext());
    auto branchWeightsAttr = builder.getArrayAttr(
        {builder.getI32IntegerAttr(1024 * 1024 - 1),
         builder.getI32IntegerAttr(1)});
    const bool defaultNoMerge = codeGenOpts.OptimizationLevel == 0;
    auto markNoSanitize = [&](mlir::Operation *op) {
      if (op)
        op->setAttr("cir.nosanitize", noSanitizeAttr);
    };
    auto emitCFICheckTrap = [&](mlir::OpBuilder &b, mlir::Location trapLoc,
                               bool noMerge = false) {
      auto trap = cir::TrapOp::create(b, trapLoc);
      trap->setAttr(
          "cir.ubsantrap_handler",
          mlir::IntegerAttr::get(
              mlir::IntegerType::get(b.getContext(), 8), 2));
      trap->setAttr("cir.nosanitize", noSanitizeAttr);
      if (noMerge)
        trap->setAttr("cir.nomerge", mlir::UnitAttr::get(&getMLIRContext()));
      else
        trap->setAttr("cir.ubsantrap_allow_merge",
                      mlir::UnitAttr::get(&getMLIRContext()));
    };

    // __cfi_check_fail(ptr CallSiteTypeId, ptr Addr)
    auto checkFailFn = createRuntimeFunction(
        checkFailTy, "__cfi_check_fail",
        /*extraAttrs=*/{}, /*isLocal=*/false, /*assumeConvergent=*/false);
    checkFailFn.setLinkage(cir::GlobalLinkageKind::WeakODRLinkage);
    checkFailFn.setGlobalVisibility(cir::VisibilityKind::Hidden);
    setCIRFunctionAttributesForDefinition(GlobalDecl{}, checkFailFn);
    getTargetCIRGenInfo().setTargetAttributes(/*decl=*/nullptr,
                                              checkFailFn.getOperation(), *this);
    setDSOLocal(checkFailFn.getOperation());
    mlir::SymbolTable::setSymbolVisibility(
        checkFailFn, getMLIRVisibilityFromCIRLinkage(checkFailFn.getLinkage()));
    auto getCFICheckFailKind = [&](auto sanitizerKind) -> llvm::StringRef {
      if (!getLangOpts().Sanitize.has(sanitizerKind) ||
          getCodeGenOpts().SanitizeTrap.has(sanitizerKind)) {
        return "trap";
      }
      return getCodeGenOpts().SanitizeRecover.has(sanitizerKind) ? "recover"
                                                                 : "abort";
    };
    theModule->setAttr("cir.cfi.check_fail.vcall_kind",
                       builder.getStringAttr(
                           getCFICheckFailKind(SanitizerKind::CFIVCall)));
    theModule->setAttr("cir.cfi.check_fail.nvcall_kind",
                       builder.getStringAttr(
                           getCFICheckFailKind(SanitizerKind::CFINVCall)));
    theModule->setAttr("cir.cfi.check_fail.derived_cast_kind",
                       builder.getStringAttr(getCFICheckFailKind(
                           SanitizerKind::CFIDerivedCast)));
    theModule->setAttr("cir.cfi.check_fail.unrelated_cast_kind",
                       builder.getStringAttr(getCFICheckFailKind(
                           SanitizerKind::CFIUnrelatedCast)));
    theModule->setAttr("cir.cfi.check_fail.icall_kind",
                       builder.getStringAttr(
                           getCFICheckFailKind(SanitizerKind::CFIICall)));
    if (codeGenOpts.OptimizationLevel == 0)
      theModule->setAttr("cir.cfi.check_fail.default_nomerge",
                         mlir::UnitAttr::get(&getMLIRContext()));
    auto setCFICheckFailNoMergeAttr =
        [&](llvm::StringRef attrName,
            SanitizerKind::SanitizerOrdinal sanitizerKind) {
          if (getLangOpts().Sanitize.has(sanitizerKind) &&
              !codeGenOpts.SanitizeMergeHandlers.has(sanitizerKind))
            theModule->setAttr(attrName,
                               mlir::UnitAttr::get(&getMLIRContext()));
        };
    setCFICheckFailNoMergeAttr("cir.cfi.check_fail.vcall_nomerge",
                               SanitizerKind::SO_CFIVCall);
    setCFICheckFailNoMergeAttr("cir.cfi.check_fail.nvcall_nomerge",
                               SanitizerKind::SO_CFINVCall);
    setCFICheckFailNoMergeAttr("cir.cfi.check_fail.derived_cast_nomerge",
                               SanitizerKind::SO_CFIDerivedCast);
    setCFICheckFailNoMergeAttr("cir.cfi.check_fail.unrelated_cast_nomerge",
                               SanitizerKind::SO_CFIUnrelatedCast);
    setCFICheckFailNoMergeAttr("cir.cfi.check_fail.icall_nomerge",
                               SanitizerKind::SO_CFIICall);
    if (getCodeGenOpts().SanitizeMinimalRuntime)
      theModule->setAttr("cir.cfi.check_fail.minimal_runtime",
                         mlir::UnitAttr::get(&getMLIRContext()));
    if (getCodeGenOpts().SanitizeHandlerPreserveAllRegs)
      theModule->setAttr("cir.cfi.check_fail.preserve_all_regs",
                         mlir::UnitAttr::get(&getMLIRContext()));
    if (checkFailFn.isDeclaration()) {
      mlir::Block *entry = checkFailFn.addEntryBlock();
      auto &body = checkFailFn.getBody();
      auto *dispatchBlock = new mlir::Block();
      auto *nullTrapBlock = new mlir::Block();
      body.push_back(nullTrapBlock);
      body.push_back(dispatchBlock);

      builder.setInsertionPointToStart(entry);
      mlir::Value data = checkFailFn.getArgument(0);
      mlir::Value addr = checkFailFn.getArgument(1);
      mlir::Value nullPtr = builder.getNullPtr(ptrTy, loc);
      mlir::Value hasData =
          cir::CmpOp::create(builder, loc, cir::CmpOpKind::ne, data, nullPtr);
      markNoSanitize(hasData.getDefiningOp());
      auto nullCheckBr =
          cir::BrCondOp::create(builder, loc, hasData, dispatchBlock,
                                nullTrapBlock);
      nullCheckBr->setAttr("cir.branch_weights", branchWeightsAttr);
      markNoSanitize(nullCheckBr);

      builder.setInsertionPointToStart(nullTrapBlock);
      emitCFICheckTrap(builder, loc, defaultNoMerge);

      builder.setInsertionPointToStart(dispatchBlock);
      mlir::Value typedData = builder.createPtrBitcast(data, cfiCheckFailDataTy);
      markNoSanitize(typedData.getDefiningOp());
      auto checkKindPtr = builder.createGetMember(
          loc, cir::PointerType::get(i8Ty), typedData, /*name=*/"", /*index=*/0);
      markNoSanitize(checkKindPtr);
      auto checkKind = builder.createLoad(loc, Address(checkKindPtr, intAlign));
      markNoSanitize(checkKind);

      auto typeTestFnTy =
          cir::FuncType::get({ptrTy, ptrTy}, builder.getBoolTy());
      auto typeTestFn = createRuntimeFunction(
          typeTestFnTy, "__cir_type_test", /*extraAttrs=*/{},
          /*isLocal=*/false, /*assumeConvergent=*/false);
      auto nullTypeId = builder.getNullPtr(ptrTy, loc);
      auto validVtableCall =
          builder.createCallOp(loc, typeTestFn, {addr, nullTypeId},
                               typeTestFn.getCallingConv());
      validVtableCall->setDiscardableAttr("cir.type_test_id",
                                          builder.getStringAttr("all-vtables"));
      validVtableCall->setDiscardableAttr("cir.nosanitize", noSanitizeAttr);
      mlir::Value validVtable = validVtableCall.getResult();
      mlir::Value validVtableInt =
          builder.createBoolToInt(validVtable, intptrTy);
      markNoSanitize(validVtableInt.getDefiningOp());

      auto helperStubTy =
          cir::FuncType::get({ptrTy, intptrTy, intptrTy}, voidTy,
                             /*isVarArg=*/false);
      auto helperStub = createRuntimeFunction(
          helperStubTy, "__cir_ubsan_cfi_check_fail_with_data",
          /*extraAttrs=*/{}, /*isLocal=*/false, /*assumeConvergent=*/false);

      struct CFICheckKind {
        uint8_t Kind;
        SanitizerKind::SanitizerOrdinal Ordinal;
      };
      constexpr CFICheckKind checkKinds[] = {
          {0, SanitizerKind::SO_CFIVCall},
          {1, SanitizerKind::SO_CFINVCall},
          {2, SanitizerKind::SO_CFIDerivedCast},
          {3, SanitizerKind::SO_CFIUnrelatedCast},
          {4, SanitizerKind::SO_CFIICall},
      };

      mlir::Block *currentBlock = dispatchBlock;
      for (const auto &check : checkKinds) {
        auto *matchedBlock = new mlir::Block();
        auto *nextBlock = new mlir::Block();
        body.push_back(matchedBlock);
        body.push_back(nextBlock);

        builder.setInsertionPointToEnd(currentBlock);
        auto expectedKind = builder.getConstInt(loc, i8Ty, check.Kind);
        markNoSanitize(expectedKind);
        mlir::Value isThisKind = cir::CmpOp::create(
            builder, loc, cir::CmpOpKind::ne, checkKind, expectedKind);
        markNoSanitize(isThisKind.getDefiningOp());
        auto kindBr =
            cir::BrCondOp::create(builder, loc, isThisKind, nextBlock,
                                  matchedBlock);
        kindBr->setAttr("cir.branch_weights", branchWeightsAttr);
        markNoSanitize(kindBr);

        builder.setInsertionPointToStart(matchedBlock);
        llvm::StringRef handlerKind = getCFICheckFailKind(check.Ordinal);
        bool noMergeHandler =
            defaultNoMerge || (getLangOpts().Sanitize.has(check.Ordinal) &&
                               !codeGenOpts.SanitizeMergeHandlers.has(
                                   check.Ordinal));
        if (handlerKind != "trap") {
          mlir::Value addrInt = builder.createPtrToInt(addr, intptrTy);
          markNoSanitize(addrInt.getDefiningOp());
          auto call = cir::CallOp::create(
              builder, loc,
              mlir::SymbolRefAttr::get(&getMLIRContext(),
                                       helperStub.getSymName()),
              voidTy, mlir::ValueRange{data, addrInt, validVtableInt},
              helperStub.getCallingConv(), cir::SideEffect::All,
              /*exception=*/mlir::UnitAttr{});
          call->setDiscardableAttr("cir.ubsan.handler_name",
                                   builder.getStringAttr("cfi_check_fail"));
          call->setDiscardableAttr("cir.ubsan.handler_kind",
                                   builder.getStringAttr(handlerKind));
          call->setDiscardableAttr("cir.ubsan.type_check_kind",
                                   builder.getI8IntegerAttr(check.Kind));
          call->setDiscardableAttr("cir.nosanitize", noSanitizeAttr);
          if (noMergeHandler) {
            mlir::NamedAttrList callAttrs;
            if (auto existingAttrs =
                    call->getAttrOfType<cir::ExtraFuncAttributesAttr>(
                        "extra_attrs")) {
              callAttrs = mlir::NamedAttrList{
                  existingAttrs.getElements().getValue()};
            }
            callAttrs.set("nomerge", mlir::UnitAttr::get(&getMLIRContext()));
            call->setAttr("extra_attrs", cir::ExtraFuncAttributesAttr::get(
                                             callAttrs.getDictionary(
                                                 &getMLIRContext())));
          }
          if (codeGenOpts.SanitizeMinimalRuntime)
            call->setAttr("cir.cfi.mfcall.minimal_runtime",
                          noSanitizeAttr);
          if (codeGenOpts.SanitizeHandlerPreserveAllRegs)
            call->setAttr("cir.cfi.mfcall.preserve_all_regs",
                          noSanitizeAttr);

          if (handlerKind == "recover") {
            auto ret = cir::ReturnOp::create(builder, loc);
            ret->setAttr("cir.nosanitize", noSanitizeAttr);
          } else {
            auto unreachable = cir::UnreachableOp::create(builder, loc);
            unreachable->setAttr("cir.nosanitize", noSanitizeAttr);
          }
        } else {
          emitCFICheckTrap(builder, loc, noMergeHandler);
        }

        currentBlock = nextBlock;
      }

      builder.setInsertionPointToStart(currentBlock);
      auto ret = cir::ReturnOp::create(builder, loc);
      ret->setAttr("cir.nosanitize", noSanitizeAttr);
      addUsedGlobal(checkFailFn);
    }

    // __cfi_check(i64 TypeId, ptr Addr, ptr DiagData)
    auto checkFn = createRuntimeFunction(
        checkTy, "__cfi_check", /*extraAttrs=*/{}, /*isLocal=*/false,
        /*assumeConvergent=*/false);
    checkFn.setLinkage(cir::GlobalLinkageKind::WeakAnyLinkage);
    checkFn.setAlignment(4096);
    setCIRFunctionAttributesForDefinition(GlobalDecl{}, checkFn);
    getTargetCIRGenInfo().setTargetAttributes(/*decl=*/nullptr,
                                              checkFn.getOperation(), *this);
    setDSOLocal(checkFn.getOperation());
    mlir::SymbolTable::setSymbolVisibility(
        checkFn, getMLIRVisibilityFromCIRLinkage(checkFn.getLinkage()));
    if (checkFn.isDeclaration()) {
      mlir::Block *entry = checkFn.addEntryBlock();
      builder.setInsertionPointToStart(entry);
      cir::CallOp::create(
          builder, loc,
          mlir::SymbolRefAttr::get(&getMLIRContext(), checkFailFn.getSymName()),
          voidTy,
          mlir::ValueRange{checkFn.getArgument(2), checkFn.getArgument(1)},
          checkFailFn.getCallingConv(), cir::SideEffect::All,
          /*exception=*/mlir::UnitAttr{});
      cir::ReturnOp::create(builder, loc);
    }

    // __cfi_check_fail is added to llvm.used above, but cross-DSO helpers are
    // emitted after the earlier module-level emitLLVMUsed() call.
    emitLLVMUsed();
  }

  // Source-based coverage mapping is emitted after CIR has been lowered to
  // LLVM IR, alongside the final LLVM module's profiling globals.

  // OGCG emits PGO profile summary, value profiling flag, and profile
  // version as module metadata.

  // OGCG calls CUDA/HIP runtime finalizeModule to emit the device-side module
  // ctor that registers kernels and globals with the runtime. On the device
  // side this only marks ODR-used device variables as llvm.compiler.used; the
  // host-side ctor emission is still NYI.
  if (langOpts.CUDA) {
    getCUDARuntime().finalizeModule();
    // Host-side CUDA/HIP module ctor emission (kernel/global registration) is
    // still NYI; finalizeModule currently only handles the device side.
    assert(!cir::MissingFeatures::cudaFinalizeModule());

    // Emit a unique ID so that host and device binaries from the same
    // compilation unit can be associated. Matches OGCG CodeGenModule::Release.
    if (langOpts.HIP && langOpts.CUDAIsDevice) {
      auto i8Ty = builder.getUInt8Ty();
      cir::GlobalOp cuidGV = createGlobalOp(
          *this, builder.getUnknownLoc(),
          ("__hip_cuid_" + astContext.getCUIDHash()).str(), i8Ty,
          /*isConstant=*/false, getGlobalMemorySpaceAttr());
      cuidGV.setLinkage(cir::GlobalLinkageKind::ExternalLinkage);
      cuidGV.setInitialValueAttr(cir::IntAttr::get(i8Ty, 0));
      mlir::SymbolTable::setSymbolVisibility(
          cuidGV, mlir::SymbolTable::Visibility::Public);
      addCompilerUsedGlobal(cuidGV);
    }
  }

  // OGCG calls getTargetCodeGenInfo().emitTargetGlobals() and
  // emitTargetMetadata() for target-specific finalization (e.g. AMDGPU
  // code_object_version, HIP printf kind).
  // CIR does not yet model target-specific release metadata emission here.

  // OGCG emits many module flags: Dwarf version, wchar_size, branch protection,
  // CFI, CFG, sanitizer flags, semantic interposition, etc.
  assert(!cir::MissingFeatures::moduleFlags());

  // Emit C++20 module initializers. OGCG calls EmitModuleInitializers here
  // to process module-owned variable initializers and then EmitCXXModuleInitFunc
  // to create the _ZGIW<module> init function + global_ctors entry.
  clang::Module *primary = astContext.getCurrentNamedModule();
  if (primary && !primary->isHeaderLikeModule()) {
    // Emit module initializer decls (variables with init, excluding imports).
    auto emitModuleInits = [&](clang::Module *mod) {
      if (!mod)
        return;
      for (Decl *d : astContext.getModuleInitializers(mod)) {
        if (isa<ImportDecl>(d))
          continue;
        emitTopLevelDecl(d);
      }
    };
    emitModuleInits(primary->getGlobalModuleFragment());
    emitModuleInits(primary);
    emitModuleInits(primary->getPrivateModuleFragment());
  }

  // OGCG emits EmitCXXModuleInitFunc for interface/partition modules to
  // create the _ZGIW<module> function + global_ctors. CIR defers this to
  // LoweringPrepare which handles GlobalOp ctor regions.

  if (usesLegacyGNUObjCRuntime(*this))
    emitObjCLegacyGNUModuleInit(builder.getUnknownLoc());
  else if (usesGNUstep2ObjCRuntime(*this))
    emitObjCGNUModuleInit(builder.getUnknownLoc());
  if (usesDarwinFragileObjCRuntime(*this))
    emitObjCDarwinFragileModuleInfo(builder.getUnknownLoc());
  else if (usesDarwinNonFragileObjCRuntime(*this))
    emitObjCDarwinModuleInfo(builder.getUnknownLoc());

  // Some direct callees (especially deferred local lambdas) only have their
  // final nothrow status after end-of-TU emission is complete. Revisit
  // synthetic try scaffolding and nothrow inference now that the full module
  // is materialized.
  finalizeLateNoThrow(theModule);

  // Emit llvm.used and llvm.compiler.used module attributes after all deferred
  // helpers are materialized so late addUsedGlobal/addCompilerUsedGlobal calls
  // are preserved in the final LLVM arrays.
  emitLLVMUsed();
}

void CIRGenModule::emitAliasForGlobal(StringRef mangledName,
                                      mlir::Operation *op, GlobalDecl aliasGD,
                                      cir::FuncOp aliasee,
                                      cir::GlobalLinkageKind linkage) {

  auto *aliasFD = dyn_cast<FunctionDecl>(aliasGD.getDecl());
  assert(aliasFD && "expected FunctionDecl");
  (void)op;

  // Build or update the alias declaration through the normal structor path so
  // the alias carries the same ABI-lowered signature and parameter attributes
  // as an ordinary declaration of this constructor/destructor.
  cir::FuncOp alias = getAddrAndTypeOfCXXStructor(
                          aliasGD, /*fnInfo=*/nullptr, /*fnType=*/{},
                          /*dontDefer=*/false, NotForDefinition)
                          .second;
  alias.setAliasee(aliasee.getName());
  alias.setLinkage(linkage);
  // Declarations cannot have public MLIR visibility, just mark them private
  // but this really should have no meaning since CIR should not be using
  // this information to derive linkage information.
  mlir::SymbolTable::setSymbolVisibility(
      alias, mlir::SymbolTable::Visibility::Private);

  // Alias constructors and destructors are always unnamed_addr.
  {
    mlir::NamedAttrList attrs;
    if (auto extraAttrs = alias.getExtraAttrs())
      attrs = mlir::NamedAttrList{extraAttrs->getElements().getValue()};
    attrs.set("unnamed_addr", mlir::UnitAttr::get(&getMLIRContext()));
    alias.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
        attrs.getDictionary(&getMLIRContext())));
  }

  // The alias reuses the existing function symbol when one was already created
  // on the normal declaration path, so no symbol RAUW is needed here.

  // Finally, set up the alias with its proper name and attributes.
  setCommonAttributes(aliasGD, alias);
}

mlir::Type CIRGenModule::convertType(QualType type) {
  return genTypes.convertType(type);
}

bool CIRGenModule::verifyModule() const {
  // Verify the module after we have finished constructing it, this will
  // check the structural properties of the IR and invoke any specific
  // verifiers we have on the CIR operations.
  return mlir::verify(theModule).succeeded();
}

namespace {
static std::string getObjCObjFWEHStringName(llvm::StringRef str) {
  static constexpr char hexDigits[] = "0123456789ABCDEF";

  std::string name = "__objc_eh_str_";
  for (unsigned char c : str.bytes()) {
    const bool isAlnum =
        (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
        (c >= 'a' && c <= 'z');
    if (isAlnum) {
      name.push_back(static_cast<char>(c));
      continue;
    }

    name += "_x";
    name.push_back(hexDigits[c >> 4]);
    name.push_back(hexDigits[c & 0xF]);
    name.push_back('_');
  }
  return name;
}

static cir::GlobalOp getObjCObjFWEHStringGlobal(CIRGenModule &cgm,
                                                mlir::Location loc,
                                                llvm::StringRef str) {
  std::string symbolName = getObjCObjFWEHStringName(str);
  if (auto gv = mlir::dyn_cast_or_null<cir::GlobalOp>(
          cgm.getGlobalValue(symbolName)))
    return gv;

  CIRGenBuilderTy &builder = cgm.getBuilder();
  std::string bytes = str.str();
  bytes.push_back('\0');
  auto init =
      mlir::cast<mlir::TypedAttr>(builder.getString(bytes, builder.getUInt8Ty(),
                                                    bytes.size()));

  cir::GlobalOp gv = CIRGenModule::createGlobalOp(
      cgm, loc, symbolName, init.getType(), /*isConstant=*/true);
  gv.setLinkage(cir::GlobalLinkageKind::PrivateLinkage);
  mlir::SymbolTable::setSymbolVisibility(
      gv, mlir::SymbolTable::Visibility::Private);
  gv.setAlignmentAttr(cgm.getSize(CharUnits::One()));
  gv->setAttr("unnamed_addr", mlir::UnitAttr::get(&cgm.getMLIRContext()));
  CIRGenModule::setInitializer(gv, init);
  cgm.setDSOLocal(static_cast<mlir::Operation *>(gv));
  return gv;
}

static mlir::TypedAttr getObjCObjFWEHType(CIRGenModule &cgm,
                                          mlir::Location loc, QualType ty) {
  auto voidPtrTy = cgm.getBuilder().getVoidPtrTy();

  if (ty->isObjCIdType() || ty->isObjCQualifiedIdType()) {
    cir::GlobalOp stringGV = getObjCObjFWEHStringGlobal(cgm, loc, "@id");
    return cgm.getBuilder().getGlobalViewAttr(voidPtrTy, stringGV);
  }

  const auto *objPtrTy = ty->getAs<ObjCObjectPointerType>();
  assert(objPtrTy && "expected Objective-C object pointer EH catch type");

  const ObjCInterfaceType *ifaceTy = objPtrTy->getInterfaceType();
  assert(ifaceTy && "expected Objective-C interface pointer EH catch type");

  cir::GlobalOp stringGV = getObjCObjFWEHStringGlobal(
      cgm, loc, ifaceTy->getDecl()->getIdentifier()->getName());
  return cgm.getBuilder().getGlobalViewAttr(voidPtrTy, stringGV);
}

static cir::GlobalOp getMSVCTypeInfoVTableGlobal(CIRGenModule &cgm,
                                                 mlir::Location loc) {
  auto voidPtrTy = cgm.getBuilder().getVoidPtrTy();
  cir::GlobalOp gv = cgm.createOrReplaceCXXRuntimeVariable(
      loc, "??_7type_info@@6B@", voidPtrTy,
      cir::GlobalLinkageKind::ExternalLinkage, CharUnits::Zero());
  gv.setConstant(true);
  gv->removeAttr("dso_local");
  return gv;
}

static mlir::TypedAttr getObjCGNUMSVCEHType(CIRGenModule &cgm,
                                            mlir::Location loc, QualType ty) {
  auto &builder = cgm.getBuilder();
  auto voidPtrTy = builder.getVoidPtrTy();

  ty = ty.getCanonicalType().getUnqualifiedType();

  SmallString<256> mangledName;
  {
    llvm::raw_svector_ostream out(mangledName);
    cgm.getCXXABI().getMangleContext().mangleCXXRTTI(ty, out);
  }

  if (auto gv =
          mlir::dyn_cast_or_null<cir::GlobalOp>(cgm.getGlobalValue(mangledName)))
    return builder.getGlobalViewAttr(voidPtrTy, gv);

  SmallString<256> typeInfoName;
  {
    llvm::raw_svector_ostream out(typeInfoName);
    cgm.getCXXABI().getMangleContext().mangleCXXRTTIName(ty, out);
  }
  std::string typeInfoBytes = typeInfoName.str().str();
  typeInfoBytes.push_back('\0');
  auto typeInfoInit =
      mlir::cast<mlir::TypedAttr>(builder.getString(typeInfoBytes,
                                                    builder.getUInt8Ty(),
                                                    typeInfoBytes.size()));

  auto typeDescriptorTy = builder.getAnonRecordTy(
      {voidPtrTy, voidPtrTy, typeInfoInit.getType()}, /*packed=*/false,
      /*padded=*/false);
  cir::GlobalOp gv = cgm.createOrReplaceCXXRuntimeVariable(
      loc, mangledName, typeDescriptorTy,
      cir::GlobalLinkageKind::LinkOnceODRLinkage, cgm.getPointerAlign());
  gv.setConstant(false);
  gv->removeAttr("dso_local");
  if (!gv.isDeclaration())
    return builder.getGlobalViewAttr(voidPtrTy, gv);

  mlir::Attribute init = builder.getAnonConstRecord(
      builder.getArrayAttr({builder.getGlobalViewAttr(
                                voidPtrTy,
                                getMSVCTypeInfoVTableGlobal(cgm, loc)),
                            builder.getConstPtrAttr(voidPtrTy, 0),
                            typeInfoInit}),
      /*packed=*/false, /*padded=*/false, typeDescriptorTy);
  CIRGenModule::setInitializer(gv, init);
  return builder.getGlobalViewAttr(voidPtrTy, gv);
}
} // namespace

mlir::Attribute CIRGenModule::getAddrOfRTTIDescriptor(mlir::Location loc,
                                                      QualType ty, bool forEh) {
  // Return a bogus pointer if RTTI is disabled, unless it's for EH.
  // FIXME: should we even be calling this method if RTTI is disabled
  // and it's not for EH?
  if (!shouldEmitRTTI(forEh))
    return builder.getConstNullPtrAttr(builder.getUInt8PtrTy());

  if (forEh && ty->isObjCObjectPointerType() &&
      langOpts.ObjCRuntime.getKind() == ObjCRuntime::ObjFW)
    return getObjCObjFWEHType(*this, loc, ty);

  if (forEh && ty->isObjCObjectPointerType() &&
      langOpts.ObjCRuntime.getKind() == ObjCRuntime::GNUstep &&
      getTriple().isWindowsMSVCEnvironment())
    return getObjCGNUMSVCEHType(*this, loc, ty);

  if (forEh && ty->isObjCObjectPointerType() &&
      langOpts.ObjCRuntime.isGNUFamily()) {
    errorNYI(loc, "getAddrOfRTTIDescriptor: Objc PtrType & Objc RT GUN");
  }

  return getCXXABI().getAddrOfRTTIDescriptor(loc, ty);
}

// TODO(cir): this can be shared with LLVM codegen.
CharUnits CIRGenModule::computeNonVirtualBaseClassOffset(
    const CXXRecordDecl *derivedClass,
    llvm::iterator_range<CastExpr::path_const_iterator> path) {
  CharUnits offset = CharUnits::Zero();

  const ASTContext &astContext = getASTContext();
  const CXXRecordDecl *rd = derivedClass;

  for (const CXXBaseSpecifier *base : path) {
    assert(!base->isVirtual() && "Should not see virtual bases here!");

    // Get the layout.
    const ASTRecordLayout &layout = astContext.getASTRecordLayout(rd);

    const auto *baseDecl = base->getType()->castAsCXXRecordDecl();

    // Add the offset.
    offset += layout.getBaseClassOffset(baseDecl);

    rd = baseDecl;
  }

  return offset;
}

DiagnosticBuilder CIRGenModule::errorNYI(SourceLocation loc,
                                         llvm::StringRef feature) {
  unsigned diagID = diags.getCustomDiagID(
      DiagnosticsEngine::Error, "ClangIR code gen Not Yet Implemented: %0");
  return diags.Report(astContext.getFullLoc(loc), diagID) << feature;
}

DiagnosticBuilder CIRGenModule::errorNYI(SourceRange loc,
                                         llvm::StringRef feature) {
  return errorNYI(loc.getBegin(), feature) << loc;
}

void CIRGenModule::error(SourceLocation loc, StringRef error) {
  unsigned diagID = getDiags().getCustomDiagID(DiagnosticsEngine::Error, "%0");
  getDiags().Report(astContext.getFullLoc(loc), diagID) << error;
}

bool CIRGenModule::stopAutoInit() {
  unsigned stopAfter =
      getASTContext().getLangOpts().TrivialAutoVarInitStopAfter;
  if (stopAfter) {
    // This number is positive only when -ftrivial-auto-var-init-stop-after=*
    // is used.
    if (numAutoVarInit >= stopAfter)
      return true;
    if (!numAutoVarInit) {
      getDiags().Report(diag::warn_trivial_auto_var_limit)
          << stopAfter
          << (getASTContext().getLangOpts().getTrivialAutoVarInit() ==
                      LangOptions::TrivialAutoVarInitKind::Zero
                  ? "zero"
                  : "pattern");
    }
    ++numAutoVarInit;
  }
  return false;
}

bool CIRGenModule::isPaddedAtomicType(QualType type) {
  return isPaddedAtomicType(type->castAs<AtomicType>());
}

bool CIRGenModule::isPaddedAtomicType(const AtomicType *type) {
  return astContext.getTypeSize(type) !=
         astContext.getTypeSize(type->getValueType());
}

std::string CIRGenModule::getBlockAddressLabelName(const LabelDecl &label) const {
  if (!label.isGnuLocal())
    return label.getName().str();
  return (Twine(label.getName()) + "$" + Twine(label.getID())).str();
}

/// Print out an error that codegen doesn't support the specified stmt yet.
void CIRGenModule::errorUnsupported(const Stmt *s, llvm::StringRef type) {
  unsigned diagId = diags.getCustomDiagID(DiagnosticsEngine::Error,
                                          "cannot compile this %0 yet");
  diags.Report(astContext.getFullLoc(s->getBeginLoc()), diagId)
      << type << s->getSourceRange();
}

/// Print out an error that codegen doesn't support the specified decl yet.
void CIRGenModule::errorUnsupported(const Decl *d, llvm::StringRef type) {
  unsigned diagId = diags.getCustomDiagID(DiagnosticsEngine::Error,
                                          "cannot compile this %0 yet");
  diags.Report(astContext.getFullLoc(d->getLocation()), diagId) << type;
}

void CIRGenModule::mapBlockAddress(cir::BlockAddrInfoAttr blockInfo,
                                   cir::LabelOp label) {
  [[maybe_unused]] auto result =
      blockAddressInfoToLabel.try_emplace(blockInfo, label);
  assert(result.second &&
         "attempting to map a blockaddress info that is already mapped");
}

void CIRGenModule::mapUnresolvedBlockAddress(cir::BlockAddressOp op) {
  [[maybe_unused]] auto result = unresolvedBlockAddressToLabel.insert(op);
  assert(result.second &&
         "attempting to map a blockaddress operation that is already mapped");
}

void CIRGenModule::mapResolvedBlockAddress(cir::BlockAddressOp op,
                                           cir::LabelOp label) {
  [[maybe_unused]] auto result = blockAddressToLabel.try_emplace(op, label);
  assert(result.second &&
         "attempting to map a blockaddress operation that is already mapped");
}

void CIRGenModule::updateResolvedBlockAddress(cir::BlockAddressOp op,
                                              cir::LabelOp newLabel) {
  auto *it = blockAddressToLabel.find(op);
  assert(it != blockAddressToLabel.end() &&
         "trying to update a blockaddress not previously mapped");
  assert(!it->second && "blockaddress already has a resolved label");

  it->second = newLabel;
}

cir::LabelOp
CIRGenModule::lookupBlockAddressInfo(cir::BlockAddrInfoAttr blockInfo) {
  return blockAddressInfoToLabel.lookup(blockInfo);
}

mlir::ArrayAttr CIRGenModule::emitAnnotationArgs(const AnnotateAttr *attr) {
  ArrayRef<Expr *> exprs = {attr->args_begin(), attr->args_size()};
  if (exprs.empty())
    return mlir::ArrayAttr::get(&getMLIRContext(), {});

  llvm::FoldingSetNodeID id;
  for (Expr *e : exprs)
    id.Add(cast<clang::ConstantExpr>(e)->getAPValueResult());

  mlir::ArrayAttr &lookup = annotationArgs[id.ComputeHash()];
  if (lookup)
    return lookup;

  llvm::SmallVector<mlir::Attribute, 4> args;
  args.reserve(exprs.size());
  ConstantEmitter constEmitter(*this);
  for (Expr *e : exprs) {
    auto &ce = *cast<clang::ConstantExpr>(e);
    if (auto *const strE =
            clang::dyn_cast<clang::StringLiteral>(ce.IgnoreParenCasts())) {
      args.push_back(getStringAttrCached(strE->getString()));
    } else {
      // Match OGCG by reusing ConstantEmitter::emitAbstract(), which supports
      // all APValue kinds (including structs, arrays, and lvalue/pointers).
      args.push_back(
          constEmitter.emitAbstract(ce.getBeginLoc(), ce.getAPValueResult(),
                                    ce.getType()));
    }
  }

  lookup = builder.getArrayAttr(args);
  return lookup;
}

cir::AnnotationAttr
CIRGenModule::emitAnnotateAttr(const clang::AnnotateAttr *aa,
                               clang::SourceLocation declLoc) {
  mlir::StringAttr annoGV = getStringAttrCached(aa->getAnnotation());
  mlir::ArrayAttr args = emitAnnotationArgs(aa);
  mlir::LocationAttr loc;
  if (declLoc.isValid())
    loc = getLoc(declLoc);
  return cir::AnnotationAttr::get(annoGV, args, loc);
}

void CIRGenModule::addGlobalAnnotations(const ValueDecl *d,
                                        mlir::Operation *gv) {
  assert(d->hasAttr<AnnotateAttr>() && "no annotate attribute");
  assert((isa<cir::GlobalOp>(gv) || isa<cir::FuncOp>(gv)) &&
         "annotation only on globals");
  llvm::SmallVector<mlir::Attribute, 4> annotations;
  for (auto *i : d->specific_attrs<AnnotateAttr>())
    annotations.push_back(emitAnnotateAttr(i, d->getLocation()));
  if (auto global = dyn_cast<cir::GlobalOp>(gv))
    global.setAnnotationsAttr(builder.getArrayAttr(annotations));
  else if (auto func = dyn_cast<cir::FuncOp>(gv))
    func.setAnnotationsAttr(builder.getArrayAttr(annotations));
}

void CIRGenModule::emitGlobalAnnotations() {
  for (const auto &[mangledName, vd] : deferredAnnotations) {
    mlir::Operation *gv = getGlobalValue(mangledName);
    if (gv)
      addGlobalAnnotations(vd, gv);
  }
  deferredAnnotations.clear();
}

static void removeImageAccessQualifier(std::string &tyName) {
  std::string readOnlyQual("__read_only");
  std::string::size_type readOnlyPos = tyName.find(readOnlyQual);
  if (readOnlyPos != std::string::npos)
    // "+ 1" for the space after access qualifier.
    tyName.erase(readOnlyPos, readOnlyQual.size() + 1);
  else {
    std::string writeOnlyQual("__write_only");
    std::string::size_type writeOnlyPos = tyName.find(writeOnlyQual);
    if (writeOnlyPos != std::string::npos)
      tyName.erase(writeOnlyPos, writeOnlyQual.size() + 1);
    else {
      std::string readWriteQual("__read_write");
      std::string::size_type readWritePos = tyName.find(readWriteQual);
      if (readWritePos != std::string::npos)
        tyName.erase(readWritePos, readWriteQual.size() + 1);
    }
  }
}

// Returns the address space id that should be produced to the
// kernel_arg_addr_space metadata. This is always fixed to the ids
// as specified in the SPIR 2.0 specification in order to differentiate
// for example in clGetKernelArgInfo() implementation between the address
// spaces with targets without unique mapping to the OpenCL address spaces
// (basically all single AS CPUs).
static unsigned argInfoAddressSpace(LangAS AS) {
  switch (AS) {
  case LangAS::opencl_global:
    return 1;
  case LangAS::opencl_constant:
    return 2;
  case LangAS::opencl_local:
    return 3;
  case LangAS::opencl_generic:
    return 4; // Not in SPIR 2.0 specs.
  case LangAS::opencl_global_device:
    return 5;
  case LangAS::opencl_global_host:
    return 6;
  default:
    return 0; // Assume private.
  }
}

void CIRGenModule::genKernelArgMetadata(cir::FuncOp fn, const FunctionDecl *fd,
                                        CIRGenFunction *cgf) {
  assert(((fd && cgf) || (!fd && !cgf)) &&
         "Incorrect use - fd and cgf should either be both null or not!");

  const PrintingPolicy &policy = getASTContext().getPrintingPolicy();

  // Integer values for the kernel argument address space qualifiers.
  SmallVector<int32_t, 8> addressQuals;

  // Attrs for the kernel argument access qualifiers (images only).
  SmallVector<mlir::Attribute, 8> accessQuals;

  // Attrs for the kernel argument type names.
  SmallVector<mlir::Attribute, 8> argTypeNames;

  // Attrs for the kernel argument base type names.
  SmallVector<mlir::Attribute, 8> argBaseTypeNames;

  // Attrs for the kernel argument type qualifiers.
  SmallVector<mlir::Attribute, 8> argTypeQuals;

  // Attrs for the kernel argument names.
  SmallVector<mlir::Attribute, 8> argNames;

  if (fd && cgf)
    for (unsigned i = 0, e = fd->getNumParams(); i != e; ++i) {
      const ParmVarDecl *parm = fd->getParamDecl(i);
      // Get argument name.
      argNames.push_back(getStringAttrCached(parm->getName()));

      if (!getLangOpts().OpenCL)
        continue;
      QualType ty = parm->getType();
      std::string typeQuals;

      // Get image and pipe access qualifier:
      if (ty->isImageType() || ty->isPipeType()) {
        const Decl *decl = parm;
        if (const auto *typedefType = ty->getAs<TypedefType>())
          decl = typedefType->getDecl();
        const OpenCLAccessAttr *attr = decl->getAttr<OpenCLAccessAttr>();
        if (attr && attr->isWriteOnly())
          accessQuals.push_back(getStringAttrCached("write_only"));
        else if (attr && attr->isReadWrite())
          accessQuals.push_back(getStringAttrCached("read_write"));
        else
          accessQuals.push_back(getStringAttrCached("read_only"));
      } else {
        accessQuals.push_back(getStringAttrCached("none"));
      }

      auto getTypeSpelling = [&](QualType Ty) {
        auto typeName = Ty.getUnqualifiedType().getAsString(policy);

        if (Ty.isCanonical()) {
          StringRef typeNameRef = typeName;
          // Turn "unsigned type" to "utype"
          if (typeNameRef.consume_front("unsigned "))
            return std::string("u") + typeNameRef.str();
          if (typeNameRef.consume_front("signed "))
            return typeNameRef.str();
        }

        return typeName;
      };

      if (ty->isPointerType()) {
        QualType pointeeTy = ty->getPointeeType();

        // Get address qualifier.
        addressQuals.push_back(
            argInfoAddressSpace(pointeeTy.getAddressSpace()));

        // Get argument type name.
        std::string typeName = getTypeSpelling(pointeeTy) + "*";
        std::string baseTypeName =
            getTypeSpelling(pointeeTy.getCanonicalType()) + "*";
        argTypeNames.push_back(getStringAttrCached(typeName));
        argBaseTypeNames.push_back(getStringAttrCached(baseTypeName));

        // Get argument type qualifiers:
        if (ty.isRestrictQualified())
          typeQuals = "restrict";
        if (pointeeTy.isConstQualified() ||
            (pointeeTy.getAddressSpace() == LangAS::opencl_constant))
          typeQuals += typeQuals.empty() ? "const" : " const";
        if (pointeeTy.isVolatileQualified())
          typeQuals += typeQuals.empty() ? "volatile" : " volatile";
      } else {
        uint32_t addrSpc = 0;
        bool isPipe = ty->isPipeType();
        if (ty->isImageType() || isPipe)
          addrSpc = argInfoAddressSpace(LangAS::opencl_global);

        addressQuals.push_back(addrSpc);

        // Get argument type name.
        ty = isPipe ? ty->castAs<PipeType>()->getElementType() : ty;
        std::string typeName = getTypeSpelling(ty);
        std::string baseTypeName = getTypeSpelling(ty.getCanonicalType());

        // Remove access qualifiers on images
        // (as they are inseparable from type in clang implementation,
        // but OpenCL spec provides a special query to get access qualifier
        // via clGetKernelArgInfo with CL_KERNEL_ARG_ACCESS_QUALIFIER):
        if (ty->isImageType()) {
          removeImageAccessQualifier(typeName);
          removeImageAccessQualifier(baseTypeName);
        }

        argTypeNames.push_back(getStringAttrCached(typeName));
        argBaseTypeNames.push_back(getStringAttrCached(baseTypeName));

        if (isPipe)
          typeQuals = "pipe";
      }
      argTypeQuals.push_back(getStringAttrCached(typeQuals));
    }

  bool shouldEmitArgName = getCodeGenOpts().EmitOpenCLArgMetadata ||
                           getCodeGenOpts().HIPSaveKernelArgName;

  if (getLangOpts().OpenCL) {
    // The kernel arg name is emitted only when `-cl-kernel-arg-info` is on,
    // since it is only used to support `clGetKernelArgInfo` which requires
    // `-cl-kernel-arg-info` to work. The other metadata are mandatory because
    // they are necessary for OpenCL runtime to set kernel argument.
    mlir::ArrayAttr resArgNames = {};
    if (shouldEmitArgName)
      resArgNames = builder.getArrayAttr(argNames);

    // Update the function's extra attributes with the kernel argument metadata.
    auto value = cir::OpenCLKernelArgMetadataAttr::get(
        fn.getContext(), builder.getI32ArrayAttr(addressQuals),
        builder.getArrayAttr(accessQuals), builder.getArrayAttr(argTypeNames),
        builder.getArrayAttr(argBaseTypeNames),
        builder.getArrayAttr(argTypeQuals), resArgNames);
    mlir::NamedAttrList items;
    if (auto existingAttrs = fn.getExtraAttrs())
      items = mlir::NamedAttrList{existingAttrs->getElements().getValue()};
    auto oldValue = items.set(value.getMnemonic(), value);
    if (oldValue != value) {
      fn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
          builder.getContext(), builder.getDictionaryAttr(items)));
    }
  } else {
    if (shouldEmitArgName) {
      // Keep parity with OGCG for HIP/CUDA by preserving kernel argument names
      // under `!kernel_arg_name` metadata whenever the feature is enabled.
      mlir::NamedAttrList items;
      if (auto existingAttrs = fn.getExtraAttrs())
        items = mlir::NamedAttrList{existingAttrs->getElements().getValue()};
      auto value = builder.getArrayAttr(argNames);
      auto oldValue = items.set("kernel_arg_name", value);
      if (oldValue != value) {
        fn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
            builder.getContext(), builder.getDictionaryAttr(items)));
      }
    }
  }
}

void CIRGenModule::buildOpenCLMetadata() {
  // SPIR v2.0 s2.13 - The OpenCL version used by the module is stored in the
  // opencl.ocl.version named metadata node.
  // C++ for OpenCL has a distinct mapping for versions compatible with OpenCL.
  unsigned version = langOpts.getOpenCLCompatibleVersion();
  unsigned major = version / 100;
  unsigned minor = (version % 100) / 10;

  auto clVersionAttr =
      cir::OpenCLVersionAttr::get(builder.getContext(), major, minor);

  theModule->setAttr("cir.cl.version", clVersionAttr);
}

static SanitizerMask expandKernelSanitizerMasks(SanitizerMask mask) {
  if (mask & (SanitizerKind::Address | SanitizerKind::KernelAddress))
    mask |= SanitizerKind::Address | SanitizerKind::KernelAddress;
  return mask;
}

static SanitizerMask getNoSanitizeAttrMask(const VarDecl &vd) {
  if (vd.hasAttr<DisableSanitizerInstrumentationAttr>())
    return SanitizerKind::All;

  SanitizerMask noSanitizeMask;
  for (auto *attr : vd.specific_attrs<NoSanitizeAttr>())
    noSanitizeMask |= attr->getMask();
  return noSanitizeMask;
}

bool CIRGenModule::isInNoSanitizeList(SanitizerMask kind,
                                      llvm::StringRef functionName,
                                      SourceLocation loc) const {
  const auto &noSanitizeL = astContext.getNoSanitizeList();
  if (!functionName.empty() && noSanitizeL.containsFunction(kind, functionName))
    return true;

  auto &sm = astContext.getSourceManager();
  StringRef mainFile = sm.getFileEntryRefForID(sm.getMainFileID())->getName();
  if (noSanitizeL.containsMainFile(kind, mainFile))
    return true;

  if (loc.isValid())
    return noSanitizeL.containsLocation(kind, loc);

  // Compiler-generated functions may lack a source location. Match OGCG by
  // treating them as if they came from the main file.
  return noSanitizeL.containsFile(kind, mainFile);
}

bool CIRGenModule::isInNoSanitizeList(SanitizerMask kind,
                                      llvm::StringRef globalName,
                                      SourceLocation loc, QualType ty,
                                      llvm::StringRef category) const {
  const auto &noSanitizeL = astContext.getNoSanitizeList();
  if (!globalName.empty() &&
      noSanitizeL.containsGlobal(kind, globalName, category))
    return true;

  auto &sm = astContext.getSourceManager();
  StringRef mainFile = sm.getFileEntryRefForID(sm.getMainFileID())->getName();
  if (noSanitizeL.containsMainFile(kind, mainFile, category))
    return true;
  if (loc.isValid() && noSanitizeL.containsLocation(kind, loc, category))
    return true;

  if (ty.isNull())
    return false;

  while (auto at = dyn_cast<ArrayType>(ty.getTypePtr()))
    ty = at->getElementType();
  ty = ty.getCanonicalType().getUnqualifiedType();
  if (!ty->isRecordType())
    return false;

  std::string typeName = ty.getAsString(astContext.getPrintingPolicy());
  return noSanitizeL.containsType(kind, typeName, category);
}

void CIRGenModule::maybeRecordAddressSanitizerGlobalMetadata(
    SourceLocation loc, llvm::StringRef globalName, QualType ty,
    SanitizerMask noSanitizeMask, bool isDynInit, cir::GlobalOp gv) {
  SanitizerSet sanitizeOpts = langOpts.Sanitize;
  if (!(sanitizeOpts.Mask & (SanitizerKind::Address |
                             SanitizerKind::KernelAddress |
                             SanitizerKind::HWAddress |
                             SanitizerKind::KernelHWAddress)))
    return;

  sanitizeOpts.Mask = expandKernelSanitizerMasks(sanitizeOpts.Mask);
  noSanitizeMask = expandKernelSanitizerMasks(noSanitizeMask);
  SanitizerSet noSanitizeAttrSet = {noSanitizeMask & sanitizeOpts.Mask};

  bool noAddress =
      noSanitizeAttrSet.hasOneOf(SanitizerKind::Address) ||
      isInNoSanitizeList(sanitizeOpts.Mask & SanitizerKind::Address,
                         globalName, loc, ty);
  if (noAddress) {
    gv->setAttr("cir.no_sanitize_address",
                mlir::UnitAttr::get(builder.getContext()));
  }

  bool noHWAddress =
      noSanitizeAttrSet.hasOneOf(SanitizerKind::HWAddress) ||
      isInNoSanitizeList(sanitizeOpts.Mask & SanitizerKind::HWAddress,
                         globalName, loc, ty);
  if (noHWAddress) {
    gv->setAttr("cir.no_sanitize_hwaddress",
                mlir::UnitAttr::get(builder.getContext()));
  }

  bool shouldMarkDynInit =
      isDynInit && !noAddress && sanitizeOpts.has(SanitizerKind::Address) &&
      !isInNoSanitizeList(SanitizerKind::Address | SanitizerKind::KernelAddress,
                          globalName, loc, ty, "init");
  if (shouldMarkDynInit) {
    gv->setAttr("cir.sanitize_address_dyninit",
                mlir::UnitAttr::get(builder.getContext()));
  }
}

void CIRGenModule::maybeRecordAddressSanitizerGlobalMetadata(const VarDecl *vd,
                                                             bool isDynInit,
                                                             cir::GlobalOp gv) {
  if (!vd)
    return;

  maybeRecordAddressSanitizerGlobalMetadata(vd->getLocation(), gv.getSymName(),
                                            vd->getType(),
                                            getNoSanitizeAttrMask(*vd),
                                            isDynInit, gv);
}

void CIRGenModule::maybeRecordTypeSanGlobalMetadata(SourceLocation loc,
                                                    llvm::StringRef globalName,
                                                    QualType ty,
                                                    SanitizerMask noSanitizeMask,
                                                    cir::GlobalOp gv) {
  SanitizerSet sanitizeOpts = langOpts.Sanitize;

  // Handle MemtagGlobals sanitizer.
  // Match OGCG SanitizerMetadata::reportGlobal() for MemtagGlobals.
  if (static_cast<bool>(sanitizeOpts.Mask & SanitizerKind::MemtagGlobals)) {
    SanitizerSet noSanitizeAttrSet = {noSanitizeMask & sanitizeOpts.Mask};
    if (!noSanitizeAttrSet.hasOneOf(SanitizerKind::MemTag) &&
        !isInNoSanitizeList(sanitizeOpts.Mask & SanitizerKind::MemTag,
                            globalName, loc, ty)) {
      gv->setAttr("cir.sanitize_memtag",
                  mlir::UnitAttr::get(builder.getContext()));
    }
  }

  // Handle TypeSanitizer.
  if (!sanitizeOpts.has(SanitizerKind::Type) ||
      (noSanitizeMask & SanitizerKind::Type) || ty.isNull())
    return;

  auto tbaaType = getTBAATypeInfo(ty);
  if (!tbaaType)
    return;

  auto charTBAA = getTBAATypeInfo(astContext.CharTy);
  if (tbaaType == charTBAA)
    return;

  gv->setAttr("cir.tysan.type", tbaaType);
}

void CIRGenModule::maybeRecordTypeSanGlobalMetadata(const VarDecl *vd,
                                                    cir::GlobalOp gv) {
  if (!vd)
    return;

  SanitizerMask noSanitizeMask;
  if (vd->hasAttr<DisableSanitizerInstrumentationAttr>())
    noSanitizeMask = SanitizerKind::All;
  else
    for (auto *attr : vd->specific_attrs<NoSanitizeAttr>())
      noSanitizeMask |= attr->getMask();

  // External definitions and incomplete types get handled at the place they
  // are defined. Match OGCG SanitizerMetadata::reportGlobal(const VarDecl&).
  if (vd->hasExternalStorage() || vd->getType()->isIncompleteType())
    noSanitizeMask |= SanitizerKind::Type;

  maybeRecordTypeSanGlobalMetadata(vd->getLocation(), gv.getSymName(),
                                   vd->getType(), noSanitizeMask, gv);
}

cir::TBAAAttr CIRGenModule::getTBAATypeInfo(QualType qTy) {
  if (!tbaa)
    return nullptr;
  return tbaa->getTypeInfo(qTy);
}

TBAAAccessInfo CIRGenModule::getTBAAAccessInfo(QualType accessType) {
  if (!tbaa)
    return TBAAAccessInfo();
  if (langOpts.CUDAIsDevice) {
    // As CUDA builtin surface/texture types are replaced, skip generating TBAA
    // access info.
    if (accessType->isCUDADeviceBuiltinSurfaceType()) {
      if (getTargetCIRGenInfo().getCUDADeviceBuiltinSurfaceDeviceType())
        return TBAAAccessInfo();
    } else if (accessType->isCUDADeviceBuiltinTextureType()) {
      if (getTargetCIRGenInfo().getCUDADeviceBuiltinTextureDeviceType())
        return TBAAAccessInfo();
    }
  }
  return tbaa->getAccessInfo(accessType);
}

TBAAAccessInfo
CIRGenModule::getTBAAVTablePtrAccessInfo(mlir::Type vTablePtrType) {
  if (!tbaa)
    return TBAAAccessInfo();
  return tbaa->getVTablePtrAccessInfo(vTablePtrType);
}

mlir::ArrayAttr CIRGenModule::getTBAAStructInfo(QualType qTy) {
  if (!tbaa)
    return {};
  return tbaa->getTBAAStructInfo(qTy);
}

cir::TBAAAttr CIRGenModule::getTBAABaseTypeInfo(QualType qTy) {
  if (!tbaa)
    return nullptr;
  return tbaa->getBaseTypeInfo(qTy);
}

cir::TBAAAttr CIRGenModule::getTBAAAccessTagInfo(TBAAAccessInfo tbaaInfo) {
  if (!tbaa)
    return nullptr;
  return tbaa->getAccessTagInfo(tbaaInfo);
}

TBAAAccessInfo CIRGenModule::mergeTBAAInfoForCast(TBAAAccessInfo sourceInfo,
                                                  TBAAAccessInfo targetInfo) {
  if (!tbaa)
    return TBAAAccessInfo();
  return tbaa->mergeTBAAInfoForCast(sourceInfo, targetInfo);
}

TBAAAccessInfo
CIRGenModule::mergeTBAAInfoForConditionalOperator(TBAAAccessInfo infoA,
                                                  TBAAAccessInfo infoB) {
  if (!tbaa)
    return TBAAAccessInfo();
  return tbaa->mergeTBAAInfoForConditionalOperator(infoA, infoB);
}
