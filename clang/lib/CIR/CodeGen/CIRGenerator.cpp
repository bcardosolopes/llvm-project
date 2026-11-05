//===--- CIRGenerator.cpp - Emit CIR from ASTs ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This builds an AST and converts it to CIR.
//
//===----------------------------------------------------------------------===//

#include "CIRGenModule.h"

#include "../../CodeGen/BackendConsumer.h"
#include "mlir/Dialect/OpenACC/OpenACC.h"
#include "mlir/Dialect/OpenMP/OpenMPDialect.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Target/LLVMIR/Import.h"

#include "llvm/ADT/ArrayRef.h"
#include "clang/AST/DeclGroup.h"
#include "clang/AST/DeclOpenMP.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/CIR/CIRGenerator.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/Sema/Sema.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/OpenACC/RegisterOpenACCExtensions.h"
#include "clang/CIR/Dialect/OpenMP/RegisterOpenMPExtensions.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Module.h"

using namespace cir;
using namespace clang;

namespace clang {
namespace CodeGen {
bool emitCIRCoverageMapping(
    ASTContext &ASTCtx, DiagnosticsEngine &Diags,
    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS,
    const HeaderSearchOptions &HeaderSearchOpts,
    const PreprocessorOptions &PreprocessorOpts,
    const CodeGenOptions &CodeGenOpts, CoverageSourceInfo &CoverageInfo,
    llvm::Module &LLVMModule, llvm::ArrayRef<Decl *> CoverageDecls,
    const llvm::DenseMap<const Decl *, std::string> &CoverageDeclPGONames,
    const llvm::DenseMap<const Decl *, std::string> &CoverageDeclSymbolNames);
} // namespace CodeGen
} // namespace clang

namespace {

class CoverageMappingSupportVisitor
    : public RecursiveASTVisitor<CoverageMappingSupportVisitor> {
public:
  explicit CoverageMappingSupportVisitor(CIRGen::CIRGenModule &) {}

  bool TraverseLambdaExpr(LambdaExpr *LE) {
    for (Expr *Init : LE->capture_inits()) {
      if (Init && !TraverseStmt(Init))
        return false;
    }
    return true;
  }

};

static bool validateCoverageMappingDecl(CIRGen::CIRGenModule &CGM, Decl *D) {
  CoverageMappingSupportVisitor Visitor(CGM);
  if (auto *FD = dyn_cast<FunctionDecl>(D))
    return Visitor.TraverseDecl(FD);
  if (auto *OMD = dyn_cast<ObjCMethodDecl>(D))
    return Visitor.TraverseDecl(OMD);
  if (auto *BD = dyn_cast<BlockDecl>(D))
    return Visitor.TraverseDecl(BD);
  if (auto *CD = dyn_cast<CapturedDecl>(D))
    return Visitor.TraverseDecl(CD);
  return true;
}

} // namespace

void CIRGenerator::anchor() {}

CIRGenerator::CIRGenerator(clang::DiagnosticsEngine &diags,
                           llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> vfs,
                           const CodeGenOptions &cgo, bool lowersToLLVMIR,
                           clang::CoverageSourceInfo *coverageSourceInfo)
    : diags(diags), fs(std::move(vfs)), codeGenOpts{cgo},
      handlingTopLevelDecls{0}, lowersToLLVMIR(lowersToLLVMIR),
      coverageSourceInfo(coverageSourceInfo) {}
CIRGenerator::~CIRGenerator() {
  // There should normally not be any leftover inline method definitions.
  assert(deferredInlineMemberFuncDefs.empty() || diags.hasErrorOccurred());
}

static void setMLIRDataLayout(mlir::ModuleOp &mod, const llvm::DataLayout &dl) {
  mlir::MLIRContext *mlirContext = mod.getContext();
  mlir::DataLayoutSpecInterface dlSpec =
      mlir::translateDataLayout(dl, mlirContext);
  mod->setAttr(mlir::DLTIDialect::kDataLayoutAttrName, dlSpec);
}

void CIRGenerator::Initialize(ASTContext &astContext) {
  using namespace llvm;

  this->astContext = &astContext;

  mlirContext = std::make_unique<mlir::MLIRContext>();
  // CIR compilation is single-threaded; disable MLIR context locking to avoid
  // pthread_rwlock overhead on every type/attribute uniquer lookup.
  mlirContext->disableMultithreading();
  mlirContext->loadDialect<mlir::DLTIDialect>();
  mlirContext->loadDialect<cir::CIRDialect>();
  mlirContext->getOrLoadDialect<mlir::acc::OpenACCDialect>();
  mlirContext->getOrLoadDialect<mlir::omp::OpenMPDialect>();

  // Register extensions to integrate CIR types with OpenACC.
  mlir::DialectRegistry registry;
  cir::acc::registerOpenACCExtensions(registry);
  cir::omp::registerOpenMPExtensions(registry);
  mlirContext->appendDialectRegistry(registry);

  cgm = std::make_unique<clang::CIRGen::CIRGenModule>(
      *mlirContext.get(), astContext, codeGenOpts, diags, lowersToLLVMIR);
  mlir::ModuleOp mod = cgm->getModule();
  StringRef dataLayoutStr = astContext.getTargetInfo().getDataLayoutString();
  llvm::DataLayout layout = llvm::DataLayout(dataLayoutStr);
  setMLIRDataLayout(mod, layout);

  // Store the original LLVM data layout string so the lowering can restore it
  // faithfully. MLIR's DLTI spec is lossy (drops vector alignments, non-integral
  // pointer address spaces, etc.), so we preserve the original for correctness.
  mod->setAttr("cir.llvm_datalayout",
               mlir::StringAttr::get(mlirContext.get(), dataLayoutStr));

  // OGCG sets the target triple on the LLVM module here. CIR sets it in the
  // CIRGenModule constructor (CIRGenModule.cpp) via cir.triple attribute.
  // OGCG also sets SDK version and Darwin target variant triple here,
  // which CIR does not handle yet.
  // OGCG iterates CodeGenOpts.DependentLibraries and CodeGenOpts.LinkerOptions
  // to add dependent libs and linker options to the module.
  if (!codeGenOpts.DependentLibraries.empty() &&
      astContext.getTargetInfo().getTriple().isOSBinFormatELF()) {
    llvm::SmallVector<mlir::Attribute> libs;
    for (const auto &lib : codeGenOpts.DependentLibraries)
      libs.push_back(cgm->getStringAttrCached(lib));
    mod->setAttr("cir.dependent_libraries",
                 mlir::ArrayAttr::get(mlirContext.get(), libs));
  }
  if (!codeGenOpts.LinkerOptions.empty()) {
    llvm::SmallVector<mlir::Attribute> opts;
    for (const auto &opt : codeGenOpts.LinkerOptions)
      opts.push_back(cgm->getStringAttrCached(opt));
    mlir::OpBuilder builder(mlirContext.get());
    builder.setInsertionPointToStart(mod.getBody());
    cir::LinkerOptionsOp::create(builder, mod.getLoc(),
                                 mlir::ArrayAttr::get(mlirContext.get(), opts));
  }
}

bool CIRGenerator::verifyModule() const { return cgm->verifyModule(); }

mlir::ModuleOp CIRGenerator::getModule() const { return cgm->getModule(); }

void CIRGenerator::releaseModule() {
  if (cgm)
    cgm->releaseModule();
}

bool CIRGenerator::HandleTopLevelDecl(DeclGroupRef group) {
  // Ignore interesting decls from the AST reader after IRGen is finished.
  if (irGenFinished)
    return true;

  if (diags.hasUnrecoverableErrorOccurred())
    return true;

  HandlingTopLevelDeclRAII handlingDecl(*this);

  for (Decl *decl : group)
    cgm->emitTopLevelDecl(decl);

  return true;
}

void CIRGenerator::HandleTranslationUnit(ASTContext &astContext) {
  // Release the Builder when there is no error.
  // Match OGCG here: still run deferred emission on recoverable frontend
  // errors so late function bodies can diagnose additional parity-relevant
  // issues (for example, target ABI checks on deferred inline definitions).
  // CIRGenAction already stops before verifier/pipeline/backend work when any
  // error occurred, so this does not continue into later lowering stages.
  if (!diags.hasUnrecoverableErrorOccurred() && cgm)
    cgm->release();

  irGenFinished = true;

  // If there are errors before or when releasing the cgm, reset the module to
  // stop here before invoking the backend.
  // OGCG resets the LLVM module on error to prevent backend from processing
  // broken IR. CIR relies on MLIR's verifier for this.
}

void CIRGenerator::setSema(Sema &s) {
  if (cgm)
    cgm->setSema(s);
}

bool CIRGenerator::emitCoverageMapping(
    const HeaderSearchOptions &headerSearchOpts,
    const PreprocessorOptions &preprocessorOpts, llvm::Module &llvmModule) {
  if (!codeGenOpts.CoverageMapping)
    return true;

  assert(lowersToLLVMIR &&
         "source-based coverage mapping is only supported after LLVM lowering");

  if (!coverageSourceInfo) {
    cgm->errorNYI("source-based coverage mapping (-fcoverage-mapping)");
    return false;
  }

  for (Decl *D : cgm->getCoverageMappingDecls())
    if (!validateCoverageMappingDecl(*cgm, D))
      return false;

  if (cgm->getCoverageMappingDecls().empty())
    return true;

  llvm::SmallVector<Decl *, 16> coverageDecls(cgm->getCoverageMappingDecls().begin(),
                                              cgm->getCoverageMappingDecls().end());
  return CodeGen::emitCIRCoverageMapping(
             *astContext, diags, fs, headerSearchOpts, preprocessorOpts,
             codeGenOpts, *coverageSourceInfo, llvmModule, coverageDecls,
             cgm->getCoverageMappingPGONames(),
             cgm->getCoverageMappingSymbolNames()) &&
         !diags.hasErrorOccurred();
}

void CIRGenerator::populateFunctionSourceLocations(BackendConsumer &BC) const {
  if (!cgm)
    return;

  cgm->forEachMangledDecl([&](llvm::StringRef MangledName, GlobalDecl GD) {
    const auto *ND = dyn_cast_or_null<NamedDecl>(GD.getDecl());
    if (!ND)
      return;

    FullSourceLoc Loc = ND->getASTContext().getFullLoc(ND->getLocation());
    if (Loc.isValid())
      BC.addFunctionSourceLocation(MangledName, Loc);
  });
}

void CIRGenerator::addRecordLocations(mlir::ModuleOp module) const {
  if (!cgm)
    return;
  mlir::MLIRContext *ctx = module.getContext();
  mlir::Attribute unknown = mlir::UnknownLoc::get(ctx);
  // An implicit record, such as AArch64's __va_list, and its fields have no
  // source location.
  auto locOf = [&](SourceLocation loc) -> mlir::Attribute {
    return loc.isValid() ? mlir::Attribute(cgm->getLoc(loc)) : unknown;
  };
  llvm::SmallVector<mlir::NamedAttribute> entries;
  llvm::DenseSet<mlir::StringAttr> seen;
  cgm->getTypes().forEachRecordLayout(
      [&](const RecordDecl *rd,
          const clang::CIRGen::CIRGenRecordLayout &layout) {
        cir::RecordType rec = layout.getCIRType();
        if (!rec.getName() || rec.isIncomplete() ||
            !seen.insert(rec.getName()).second)
          return;
        // The record's own location, then one per member: the field or base it
        // holds, or the record again for what the source does not declare (a
        // vtable pointer, padding).
        mlir::Attribute recordLoc = locOf(rd->getLocation());
        llvm::SmallVector<mlir::Attribute> locs(1 + rec.getMembers().size(),
                                                unknown);
        locs[0] = recordLoc;
        auto place = [&](unsigned member, SourceLocation loc) {
          if (member + 1 < locs.size() && locs[member + 1] == unknown)
            locs[member + 1] = locOf(loc);
        };
        if (const auto *cxx = dyn_cast<CXXRecordDecl>(rd))
          for (const CXXBaseSpecifier &base : cxx->bases())
            if (const CXXRecordDecl *baseDecl =
                    base.getType()->getAsCXXRecordDecl())
              if (!base.isVirtual() &&
                  layout.hasNonVirtualBaseCIRFieldNo(baseDecl))
                place(layout.getNonVirtualBaseCIRFieldNo(baseDecl),
                      base.getBeginLoc());
        for (const FieldDecl *field : rd->fields())
          if (layout.hasCIRFieldNo(field))
            place(layout.getCIRFieldNo(field), field->getLocation());
        for (mlir::Attribute &loc : llvm::drop_begin(locs))
          if (loc == unknown)
            loc = recordLoc;
        entries.emplace_back(rec.getName(), mlir::ArrayAttr::get(ctx, locs));
      });
  if (!entries.empty())
    module->setAttr(cir::CIRDialect::getRecordLocationsAttrName(),
                    mlir::DictionaryAttr::get(ctx, entries));
}

void CIRGenerator::HandleInlineFunctionDefinition(FunctionDecl *d) {
  if (diags.hasErrorOccurred())
    return;

  assert(d->doesThisDeclarationHaveABody());

  // We may want to emit this definition. However, that decision might be
  // based on computing the linkage, and we have to defer that in case we are
  // inside of something that will chagne the method's final linkage, e.g.
  //   typedef struct {
  //     void bar();
  //     void foo() { bar(); }
  //   } A;
  deferredInlineMemberFuncDefs.push_back(d);

  // Provide some coverage mapping even for methods that aren't emitted.
  // Don't do this for templated classes though, as they may not be
  // instantiable.
  if (coverageSourceInfo && !d->getLexicalDeclContext()->isDependentContext())
    cgm->trackCoverageMappingDecl(d);
}

void CIRGenerator::emitDeferredDecls() {
  if (deferredInlineMemberFuncDefs.empty())
    return;

  // Emit any deferred inline method definitions. Note that more deferred
  // methods may be added during this loop, since ASTConsumer callbacks can be
  // invoked if AST inspection results in declarations being added. Therefore,
  // we use an index to loop over the deferredInlineMemberFuncDefs rather than
  // a range.
  HandlingTopLevelDeclRAII handlingDecls(*this);
  for (unsigned i = 0; i != deferredInlineMemberFuncDefs.size(); ++i)
    cgm->emitTopLevelDecl(deferredInlineMemberFuncDefs[i]);
  deferredInlineMemberFuncDefs.clear();
}

/// HandleTagDeclDefinition - This callback is invoked each time a TagDecl to
/// (e.g. struct, union, enum, class) is completed. This allows the client to
/// hack on the type, which can occur at any point in the file (because these
/// can be defined in declspecs).
void CIRGenerator::HandleTagDeclDefinition(TagDecl *d) {
  if (diags.hasErrorOccurred())
    return;

  // Don't allow re-entrant calls to CIRGen triggered by PCH deserialization to
  // emit deferred decls.
  HandlingTopLevelDeclRAII handlingDecl(*this, /*EmitDeferred=*/false);

  cgm->updateCompletedType(d);

  // For OpenMP emit declare reduction functions, if required.
  if (astContext->getLangOpts().OpenMP) {
    for (Decl *member : d->decls()) {
      if (auto *drd = dyn_cast<OMPDeclareReductionDecl>(member)) {
        if (astContext->DeclMustBeEmitted(drd))
          cgm->emitTopLevelDecl(drd);
      } else if (auto *dmd = dyn_cast<OMPDeclareMapperDecl>(member)) {
        if (astContext->DeclMustBeEmitted(dmd))
          cgm->emitTopLevelDecl(dmd);
      }
    }
  }
}

void CIRGenerator::HandleTagDeclRequiredDefinition(const TagDecl *D) {
  if (diags.hasErrorOccurred())
    return;

  // OGCG creates HandlingTopLevelDeclRAII here to block re-entrant
  // PCH-triggered deferred emission during debug info type completion.
  // CIR defers debug type metadata materialization to lowering, so this
  // re-entrancy guard is not needed.
}

void CIRGenerator::HandleCXXStaticMemberVarInstantiation(VarDecl *D) {
  if (diags.hasErrorOccurred())
    return;

  cgm->handleCXXStaticMemberVarInstantiation(D);
}

void CIRGenerator::HandleOpenACCRoutineReference(const FunctionDecl *FD,
                                                 const OpenACCRoutineDecl *RD) {
  llvm::StringRef mangledName = cgm->getMangledName(FD);
  cir::FuncOp entry =
      mlir::dyn_cast_if_present<cir::FuncOp>(cgm->getGlobalValue(mangledName));

  // if this wasn't generated, don't force it to be.
  if (!entry)
    return;
  cgm->emitOpenACCRoutineDecl(FD, entry, RD->getBeginLoc(), RD->clauses());
}

void CIRGenerator::CompleteTentativeDefinition(VarDecl *d) {
  if (diags.hasErrorOccurred())
    return;

  cgm->emitTentativeDefinition(d);
}

void CIRGenerator::HandleVTable(CXXRecordDecl *rd) {
  if (diags.hasErrorOccurred())
    return;

  cgm->emitVTable(rd);
}
