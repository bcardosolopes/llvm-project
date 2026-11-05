//===- CIRToLLVMIR.cpp - the CIR -> LLVM IR pipeline ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// One definition of what it means to take a CIR module to LLVM IR.
//
// This used to live inside cir-translate, which made it reachable only by
// running that tool: anything else wanting a lowered module had to spawn a
// process and hand it a file. For a link-time pipeline that meant writing the
// merged module out and reading it straight back -- on one real library, 83 MB
// of bytecode out and 548 MB of textual LLVM IR back in -- purely to cross a
// process boundary that carried no information.
//
// It lives here so cir-link can call it directly and cir-translate stays the
// inspectable form of the same pipeline, rather than a second implementation
// of it that happens to agree today.
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/Dialect/IR/CIRStage.h"
#include "clang/CIR/LowerToLLVM.h"
#include "clang/CIR/Passes.h"

#include "mlir/Dialect/DLTI/DLTI.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/Threading.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Interfaces/DataLayoutInterfaces.h"
#include "mlir/Target/LLVMIR/Dialect/All.h"
#include "mlir/Target/LLVMIR/Export.h"
#include "mlir/Target/LLVMIR/Import.h"

#include "llvm/ADT/EquivalenceClasses.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Bitcode/BitcodeWriterPass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/IPO/ThinLTOBitcodeWriter.h"

#include "llvm/IR/PassManager.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/Transforms/Instrumentation/SanitizerBinaryMetadata.h"
#include <optional>

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"

namespace cir {
namespace direct {
extern void registerCIRDialectTranslation(mlir::DialectRegistry &registry);
} // namespace direct

namespace {

/// `requestedTriple` is a tool's --target, threaded through rather than read
/// from a global.
///
/// It deliberately is not an llvm::cl::opt here. A cl::opt in a library is
/// registered at static-initialisation time by every binary that links the
/// library, which for this one is cir-link, cir-opt and clang -- so a `target`
/// option would appear in tools it means nothing to, be reachable in clang as
/// `-mllvm -target=`, and abort the process at startup the day any other
/// translation unit in the same binary registers that name. Each tool owns its
/// own option and passes the value in.
///
/// std::nullopt is "the tool was not asked for a triple", which is a different
/// thing from being asked for an empty one: the goal is that the triple and the
/// data layout are always available on the module, and what gets done to reach
/// that depends on whether the caller expressed a preference:
///
/// +--------+--------+-------------+-----------------+-----------------------+
/// | Option | Triple | Data Layout | Behavior Triple | Behavior Data Layout  |
/// +========+========+=============+=================+=======================+
/// | T      | T      | T           | Overwrite       | Derive from triple    |
/// | T      | T      | F           | Overwrite       | Derive from triple    |
/// | T      | F      | T           | Overwrite       | Derive from triple    |
/// | T      | F      | F           | Overwrite       | Derive from triple    |
/// | F      | T      | T           |                 |                       |
/// | F      | T      | F           |                 | Derive from triple    |
/// | F      | F      | T           | Set default     | Derive from triple    |
/// | F      | F      | F           | Set default     | Derive from triple    |
/// +--------+--------+-------------+-----------------+-----------------------+
std::string
prepareCIRModuleTriple(mlir::ModuleOp mod,
                       std::optional<llvm::StringRef> requestedTriple) {
  std::string triple = requestedTriple ? requestedTriple->str() : std::string();

  // Treat "" as the default target machine.
  if (triple.empty()) {
    triple = llvm::sys::getDefaultTargetTriple();

    mod.emitWarning() << "no target triple provided, assuming " << triple;
  }

  mod->setAttr(cir::CIRDialect::getTripleAttrName(),
               mlir::StringAttr::get(mod.getContext(), triple));
  return triple;
}

llvm::LogicalResult prepareCIRModuleDataLayout(mlir::ModuleOp mod,
                                               llvm::StringRef rawTriple) {
  auto *context = mod.getContext();

  // Data layout is fully determined by the target triple. Here we only pass the
  // triple to get the data layout.
  llvm::IntrusiveRefCntPtr<clang::DiagnosticIDs> diagID(
      new clang::DiagnosticIDs);
  clang::DiagnosticOptions diagOpts;
  llvm::IntrusiveRefCntPtr<clang::DiagnosticsEngine> diagnostics =
      new clang::DiagnosticsEngine(diagID, diagOpts,
                                   new clang::IgnoringDiagConsumer());
  llvm::Triple triple(rawTriple);
  // TODO: Need to set various target options later to populate
  // 'TargetInfo' properly.
  clang::TargetOptions targetOptions;
  targetOptions.Triple = rawTriple;
  llvm::IntrusiveRefCntPtr<clang::TargetInfo> targetInfo =
      clang::TargetInfo::CreateTargetInfo(*diagnostics, targetOptions);
  if (!targetInfo) {
    mod.emitError() << "error: invalid target triple '" << rawTriple << "'\n";
    return llvm::failure();
  }
  std::string layoutString = targetInfo->getDataLayoutString();

  // Registered dialects may not be loaded yet, ensure they are.
  context->loadDialect<mlir::DLTIDialect, mlir::LLVM::LLVMDialect>();

  mlir::DataLayoutSpecInterface dlSpec =
      mlir::translateDataLayout(llvm::DataLayout(layoutString), context);
  mod->setAttr(mlir::DLTIDialect::kDataLayoutAttrName, dlSpec);

  return llvm::success();
}

} // namespace
} // namespace cir

void cir::registerCIRToLLVMIRTranslations(mlir::DialectRegistry &registry) {
  registry.insert<mlir::DLTIDialect, mlir::func::FuncDialect>();
  mlir::registerAllToLLVMIRTranslations(registry);
  cir::direct::registerCIRDialectTranslation(registry);
}

/// Prepare requirements like cir.triple and data layout.
llvm::LogicalResult cir::prepareCIRModuleForTranslation(
    mlir::ModuleOp mod, std::optional<llvm::StringRef> requestedTriple) {
  auto modTriple = mod->getAttrOfType<mlir::StringAttr>(
      cir::CIRDialect::getTripleAttrName());
  auto modDataLayout = mod->getAttr(mlir::DLTIDialect::kDataLayoutAttrName);
  bool hasTargetOption = requestedTriple.has_value();

  // Skip the situation where nothing should be done.
  if (!hasTargetOption && modTriple && modDataLayout)
    return llvm::success();

  std::string triple;

  if (!hasTargetOption && modTriple) {
    // Do nothing if it's already set.
    triple = modTriple.getValue();
  } else {
    // Otherwise, overwrite or set default.
    triple = prepareCIRModuleTriple(mod, requestedTriple);
  }

  // If the data layout is not set, derive it from the triple.
  return prepareCIRModuleDataLayout(mod, triple);
}

static llvm::SanitizerBinaryMetadataOptions
getSanitizerBinaryMetadataOptions(mlir::ModuleOp mod) {
  llvm::SanitizerBinaryMetadataOptions opts;
  opts.Covered = mod->hasAttr("cir.sanitize_binary_metadata_covered");
  opts.Atomics = mod->hasAttr("cir.sanitize_binary_metadata_atomics");
  opts.UAR = mod->hasAttr("cir.sanitize_binary_metadata_uar");
  return opts;
}

static std::vector<std::string>
getSanitizerBinaryMetadataIgnorelistFiles(mlir::ModuleOp mod) {
  std::vector<std::string> ignorelistFiles;
  auto ignorelistAttrs = mod->getAttrOfType<mlir::ArrayAttr>(
      "cir.sanitize_binary_metadata_ignorelist_files");
  if (!ignorelistAttrs)
    return ignorelistFiles;

  ignorelistFiles.reserve(ignorelistAttrs.size());
  for (mlir::Attribute attr : ignorelistAttrs) {
    auto pathAttr = mlir::dyn_cast<mlir::StringAttr>(attr);
    if (!pathAttr)
      continue;
    ignorelistFiles.push_back(pathAttr.getValue().str());
  }
  return ignorelistFiles;
}

static void runStandaloneLLVMPostLoweringPasses(
    llvm::Module &llvmModule, llvm::SanitizerBinaryMetadataOptions sanmdOpts,
    std::vector<std::string> ignorelistFiles) {
  if (!sanmdOpts.Covered && !sanmdOpts.Atomics && !sanmdOpts.UAR)
    return;

  llvm::ModuleAnalysisManager mam;
  llvm::SanitizerBinaryMetadataPass(sanmdOpts, llvm::vfs::getRealFileSystem(),
                                    ignorelistFiles)
      .run(llvmModule, mam);
}

std::unique_ptr<llvm::Module>
cir::lowerCIRModuleToLLVMIR(mlir::ModuleOp mod, llvm::LLVMContext &llvmCtx,
                            std::optional<llvm::StringRef> requestedTriple,
                            InlinerOptions inliner) {
  if (mlir::failed(prepareCIRModuleForTranslation(mod, requestedTriple)))
    return nullptr;

  // Read off the module before lowering: the options live on the CIR module,
  // and the caller is entitled to drop it the moment this returns.
  llvm::SanitizerBinaryMetadataOptions sanmdOpts =
      getSanitizerBinaryMetadataOptions(mod);
  std::vector<std::string> sanmdIgnorelistFiles =
      getSanitizerBinaryMetadataIgnorelistFiles(mod);

  // Finish the lowering clang would have done for this module; the pipeline
  // schedules what is left by the stage the module records. A fat object is
  // captured at cir-prepare, with CallConvLowering still to run. CIR written
  // by hand records no stage and does not get CallConvLowering, which is not
  // idempotent and would coerce again whatever is already in ABI form.
  bool fromClang = cir::getStage(mod).has_value();
  // Clang's optimized output also gets mem2reg, as the driver gives it at -O1
  // and up; cir.opt_info records exactly those levels. It runs after
  // CallConvLowering. This path has no LLVM pipeline of its own before the
  // link, and the captured module carries no variable debug info for
  // promotion to lose.
  bool runMem2Reg =
      fromClang && mod->hasAttr(cir::CIRDialect::getOptInfoAttrName());

  std::unique_ptr<llvm::Module> llvmModule =
      cir::direct::lowerDirectlyFromCIRToLLVMIR(
          mod, llvmCtx,
          /*disableDebugInfo=*/false,
          /*enableVerifier=*/true, /*runCallConvLowering=*/fromClang,
          /*runMem2RegAfterDebugInfo=*/false, runMem2Reg,
          /*reportProfileReadError=*/{}, inliner);
  if (!llvmModule)
    return nullptr;

  runStandaloneLLVMPostLoweringPasses(*llvmModule, sanmdOpts,
                                      std::move(sanmdIgnorelistFiles));

  return llvmModule;
}

namespace {
/// Calls `fn` with the name of every symbol `op` refers to from its own
/// attributes -- callees, address-of, global initializers -- and `blockAddrFn`,
/// when given, with the function of every block address among them.
void forEachSymbolRef(
    mlir::Operation *op, llvm::function_ref<void(mlir::StringAttr)> fn,
    llvm::function_ref<void(mlir::StringAttr)> blockAddrFn = nullptr) {
  auto visit = [&](mlir::Attribute attr) {
    attr.walk([&](mlir::Attribute nested) {
      if (auto ref = mlir::dyn_cast<mlir::SymbolRefAttr>(nested))
        fn(ref.getRootReference());
      else if (auto blockAddr = mlir::dyn_cast<cir::BlockAddrInfoAttr>(nested);
               blockAddr && blockAddrFn)
        blockAddrFn(blockAddr.getFunc().getAttr());
    });
  };
  std::optional<mlir::RegisteredOperationName> info = op->getRegisteredInfo();
  if (!info || !op->getPropertiesStorageSize()) {
    visit(op->getAttrDictionary());
    return;
  }
  for (mlir::StringAttr name : info->getAttributeNames())
    if (std::optional<mlir::Attribute> attr = op->getInherentAttr(name))
      if (*attr)
        visit(*attr);
  for (mlir::NamedAttribute attr : op->getDiscardableAttrs())
    visit(attr.getValue());
}

bool isLocal(mlir::Operation *op) {
  if (auto func = mlir::dyn_cast<cir::FuncOp>(op))
    return cir::isLocalLinkage(func.getLinkage());
  if (auto global = mlir::dyn_cast<cir::GlobalOp>(op))
    return cir::isLocalLinkage(global.getLinkage());
  if (auto alias = mlir::dyn_cast<cir::AliasOp>(op))
    return cir::isLocalLinkage(alias.getLinkage());
  return true;
}

/// A declaration of `def`, for a partition that refers to it while another
/// defines it. It keeps what lowering a reference needs -- the type, the
/// calling convention and argument attributes, the TLS model -- and
/// unnamed_addr, which a link takes as the least of the definition's and
/// every declaration's (a declaration without it would take it off the
/// definition, and stop LTO folding or dropping it). It drops
/// what would make the declaration emit something of its own: a body or an
/// initializer, a comdat, an alias target, constructor or destructor
/// priorities, annotations, a section.
///
/// A function-typed alias is declared like the function it aliases, found
/// through `lookupDef`, so that a call through it uses that function's calling
/// convention and argument attributes.
mlir::Operation *
makeDeclaration(mlir::Operation *def, mlir::OpBuilder &builder,
                llvm::function_ref<mlir::Operation *(mlir::StringAttr)>
                    lookupDef) {
  auto declareGlobal = [&](mlir::StringRef name, mlir::Type type,
                           mlir::Location loc) {
    auto decl = cir::GlobalOp::create(builder, loc, name, type);
    mlir::SymbolTable::setSymbolVisibility(
        decl, mlir::SymbolTable::Visibility::Private);
    return decl;
  };
  if (auto func = mlir::dyn_cast<cir::FuncOp>(def)) {
    auto decl = mlir::cast<cir::FuncOp>(
        builder.insert(func.getOperation()->cloneWithoutRegions()));
    decl.setLinkage(cir::GlobalLinkageKind::ExternalLinkage);
    mlir::SymbolTable::setSymbolVisibility(
        decl, mlir::SymbolTable::Visibility::Private);
    decl.removeComdatAttr();
    decl.removeAliaseeAttr();
    decl.removePersonalityAttr();
    decl.removeGlobalCtorPriorityAttr();
    decl.removeGlobalDtorPriorityAttr();
    decl.removeAnnotationsAttr();
    decl.removeSectionAttr();
    return decl;
  }
  if (auto global = mlir::dyn_cast<cir::GlobalOp>(def)) {
    auto decl = declareGlobal(global.getSymName(), global.getSymType(),
                              global.getLoc());
    if (auto unnamedAddr = global->getAttr("unnamed_addr"))
      decl->setAttr("unnamed_addr", unnamedAddr);
    decl.setConstant(global.getConstant());
    decl.setDsoLocal(global.getDsoLocal());
    decl.setGlobalVisibilityAttr(global.getGlobalVisibilityAttr());
    if (auto tls = global.getTlsModelAttr())
      decl.setTlsModelAttr(tls);
    if (auto alignment = global.getAlignmentAttr())
      decl.setAlignmentAttr(alignment);
    if (auto addrSpace = global.getAddrSpaceAttr())
      decl.setAddrSpaceAttr(addrSpace);
    return decl;
  }
  auto alias = mlir::cast<cir::AliasOp>(def);
  if (auto funcType = mlir::dyn_cast<cir::FuncType>(alias.getSymType())) {
    // An ifunc's target is its resolver, which has a calling convention of its
    // own. Chains of aliases are followed, a few links deep.
    mlir::Operation *target = alias.getOperation();
    for (unsigned link = 0; link != 8; ++link) {
      auto targetAlias = mlir::dyn_cast_or_null<cir::AliasOp>(target);
      if (!targetAlias)
        break;
      target = targetAlias.getIsIfunc()
                   ? nullptr
                   : lookupDef(targetAlias.getAliaseeAttr().getAttr());
    }
    if (auto func = mlir::dyn_cast_or_null<cir::FuncOp>(target);
        func && func.getFunctionType() == funcType) {
      auto decl =
          mlir::cast<cir::FuncOp>(makeDeclaration(func, builder, lookupDef));
      decl.setSymName(alias.getSymName());
      return decl;
    }
    auto decl = cir::FuncOp::create(builder, alias.getLoc(),
                                    alias.getSymName(), funcType);
    mlir::SymbolTable::setSymbolVisibility(
        decl, mlir::SymbolTable::Visibility::Private);
    return decl;
  }
  auto decl =
      declareGlobal(alias.getSymName(), alias.getSymType(), alias.getLoc());
  if (auto unnamedAddr = alias->getAttr("unnamed_addr"))
    decl->setAttr("unnamed_addr", unnamedAddr);
  if (auto tls = alias.getTlsModelAttr())
    decl.setTlsModelAttr(tls);
  return decl;
}

/// Splits `mod` into `count` modules (fewer when it has fewer groups) that
/// together hold what it held, and moves everything out of it.
///
/// Definitions are grouped so that the pieces lower independently and link
/// back to the same program: an internal symbol is grouped with everything
/// that refers to it, an alias with its target, and whatever the module's
/// own attributes name (the constructor and destructor lists, annotations)
/// with the first piece, which alone keeps those attributes. Groups go, in
/// module order, to the lightest piece so far, weighed by op count. Each
/// piece then declares what it refers to and another piece defines.
llvm::SmallVector<mlir::OwningOpRef<mlir::ModuleOp>>
splitModule(mlir::ModuleOp mod, unsigned count) {
  mlir::MLIRContext *ctx = mod.getContext();
  llvm::SmallVector<mlir::Operation *> defs;
  llvm::DenseMap<mlir::StringAttr, unsigned> defIndex;
  llvm::SmallVector<mlir::Operation *> others;
  for (mlir::Operation &op : *mod.getBody()) {
    auto name = op.getAttrOfType<mlir::StringAttr>(
        mlir::SymbolTable::getSymbolAttrName());
    bool isDef = name && ((mlir::isa<cir::FuncOp>(op) &&
                           !mlir::cast<cir::FuncOp>(op).isDeclaration()) ||
                          (mlir::isa<cir::GlobalOp>(op) &&
                           !mlir::cast<cir::GlobalOp>(op).isDeclaration()) ||
                          mlir::isa<cir::AliasOp>(op));
    if (isDef) {
      defIndex[name] = defs.size();
      defs.push_back(&op);
    } else {
      others.push_back(&op);
    }
  }

  // Index defs.size() stands for the first piece.
  const unsigned first = defs.size();
  llvm::EquivalenceClasses<unsigned> groups;
  for (unsigned i = 0; i <= first; ++i)
    groups.insert(i);
  llvm::SmallVector<unsigned> weight(defs.size(), 0);
  llvm::SmallVector<llvm::SmallVector<mlir::StringAttr>> refs(defs.size());
  // The definitions each one must share a piece with, found for every
  // definition in parallel and joined below in module order.
  llvm::SmallVector<llvm::SmallVector<unsigned>> together(defs.size());
  mlir::parallelFor(ctx, 0, defs.size(), [&](size_t i) {
    bool isAlias = mlir::isa<cir::AliasOp>(defs[i]) ||
                   (mlir::isa<cir::FuncOp>(defs[i]) &&
                    mlir::cast<cir::FuncOp>(defs[i]).getAliasee());
    defs[i]->walk([&](mlir::Operation *op) {
      ++weight[i];
      forEachSymbolRef(
          op,
          [&](mlir::StringAttr name) {
            refs[i].push_back(name);
            auto it = defIndex.find(name);
            if (it == defIndex.end())
              return;
            if (isAlias || isLocal(defs[it->second]))
              together[i].push_back(it->second);
          },
          // A block address resolves only within the module that holds its
          // function's body.
          [&](mlir::StringAttr func) {
            if (auto it = defIndex.find(func); it != defIndex.end())
              together[i].push_back(it->second);
          });
    });
  });
  for (unsigned i = 0; i < defs.size(); ++i)
    for (unsigned j : together[i])
      groups.unionSets(i, j);
  // The module's attributes name functions and globals by string as well as
  // by reference; either way, what they name stays with them.
  llvm::SmallVector<mlir::NamedAttribute> sharedAttrs;
  llvm::SmallVector<mlir::NamedAttribute> firstOnlyAttrs;
  for (mlir::NamedAttribute attr : mod->getAttrs()) {
    bool namesDef = false;
    attr.getValue().walk([&](mlir::Attribute nested) {
      mlir::StringAttr name;
      if (auto ref = mlir::dyn_cast<mlir::SymbolRefAttr>(nested))
        name = ref.getRootReference();
      else
        name = mlir::dyn_cast<mlir::StringAttr>(nested);
      if (!name)
        return;
      if (auto it = defIndex.find(name); it != defIndex.end()) {
        namesDef = true;
        groups.unionSets(first, it->second);
      }
    });
    (namesDef ? firstOnlyAttrs : sharedAttrs).push_back(attr);
  }

  // Assign each group, in module order of its first member, to the lightest
  // piece; the first piece's group goes to piece 0.
  llvm::DenseMap<unsigned, uint64_t> groupWeight;
  for (unsigned i = 0; i < defs.size(); ++i)
    groupWeight[groups.getLeaderValue(i)] += weight[i];
  llvm::SmallVector<uint64_t> load(count, 0);
  llvm::DenseMap<unsigned, unsigned> pieceOfLeader;
  unsigned firstLeader = groups.getLeaderValue(first);
  pieceOfLeader[firstLeader] = 0;
  load[0] += groupWeight.lookup(firstLeader);
  llvm::SmallVector<unsigned> piece(defs.size());
  for (unsigned i = 0; i < defs.size(); ++i) {
    unsigned leader = groups.getLeaderValue(i);
    auto it = pieceOfLeader.find(leader);
    if (it == pieceOfLeader.end()) {
      unsigned lightest =
          std::min_element(load.begin(), load.end()) - load.begin();
      it = pieceOfLeader.try_emplace(leader, lightest).first;
      load[lightest] += groupWeight[leader];
    }
    piece[i] = it->second;
  }

  llvm::SmallVector<mlir::OwningOpRef<mlir::ModuleOp>> pieces;
  for (unsigned p = 0; p < count; ++p) {
    auto newMod = mlir::ModuleOp::create(mod.getLoc());
    for (mlir::NamedAttribute attr : sharedAttrs)
      newMod->setAttr(attr.getName(), attr.getValue());
    if (p == 0)
      for (mlir::NamedAttribute attr : firstOnlyAttrs)
        newMod->setAttr(attr.getName(), attr.getValue());
    pieces.push_back(newMod);
  }
  // Declarations and other non-definitions stay in every piece that refers
  // to them; anything else that is not a definition goes to the first.
  for (mlir::Operation *op : others) {
    for (unsigned p = 1; p < count; ++p)
      pieces[p]->getBody()->push_back(op->clone());
    op->moveBefore(pieces[0]->getBody(), pieces[0]->getBody()->end());
  }
  for (unsigned i = 0; i < defs.size(); ++i)
    defs[i]->moveBefore(pieces[piece[i]]->getBody(),
                        pieces[piece[i]]->getBody()->end());

  // Each piece declares what its definitions, and the first piece's
  // attributes, refer to elsewhere. Declared in module order, at the top.
  // Pieces are separate modules, so each is finished on its own thread.
  // Read-only from here on, so the pieces' threads can share it.
  auto lookupDef = [&](mlir::StringAttr name) -> mlir::Operation * {
    auto it = defIndex.find(name);
    return it == defIndex.end() ? nullptr : defs[it->second];
  };
  llvm::SmallVector<llvm::SmallVector<unsigned>> defsOfPiece(count);
  for (unsigned i = 0; i < defs.size(); ++i)
    defsOfPiece[piece[i]].push_back(i);
  mlir::parallelFor(ctx, 0, count, [&](size_t p) {
    llvm::DenseSet<unsigned> needed;
    for (unsigned i : defsOfPiece[p]) {
      for (mlir::StringAttr name : refs[i])
        if (auto it = defIndex.find(name);
            it != defIndex.end() && piece[it->second] != p)
          needed.insert(it->second);
    }
    llvm::SmallVector<unsigned> order(needed.begin(), needed.end());
    llvm::sort(order);
    mlir::OpBuilder builder(ctx);
    builder.setInsertionPointToStart(pieces[p]->getBody());
    for (unsigned i : order)
      makeDeclaration(defs[i], builder, lookupDef);

    // Drop the clones of non-definitions the piece never refers to.
    if (p == 0)
      return;
    llvm::DenseSet<mlir::StringAttr> used;
    pieces[p]->walk([&](mlir::Operation *op) {
      forEachSymbolRef(op, [&](mlir::StringAttr name) { used.insert(name); });
    });
    for (mlir::Operation &op :
         llvm::make_early_inc_range(*pieces[p]->getBody())) {
      auto name = op.getAttrOfType<mlir::StringAttr>(
          mlir::SymbolTable::getSymbolAttrName());
      if (name && !defIndex.count(name) && !used.count(name))
        op.erase();
    }
  });
  return pieces;
}
} // namespace

llvm::LogicalResult cir::lowerCIRModuleToLLVMIRInPartitions(
    mlir::ModuleOp mod, std::optional<llvm::StringRef> requestedTriple,
    InlinerOptions inliner, unsigned partitions,
    llvm::function_ref<llvm::LogicalResult(unsigned, llvm::Module &)> emit) {
  if (mlir::failed(prepareCIRModuleForTranslation(mod, requestedTriple)))
    return mlir::failure();
  llvm::SanitizerBinaryMetadataOptions sanmdOpts =
      getSanitizerBinaryMetadataOptions(mod);
  std::vector<std::string> sanmdIgnorelistFiles =
      getSanitizerBinaryMetadataIgnorelistFiles(mod);
  bool fromClang = cir::getStage(mod).has_value();
  bool runMem2Reg =
      fromClang && mod->hasAttr(cir::CIRDialect::getOptInfoAttrName());
  mlir::MLIRContext *ctx = mod.getContext();

  // The whole-module part: everything up to and including the inliner, which
  // needs to see every caller and callee.
  {
    llvm::TimeTraceScope scope("lowerWholeModule");
    mlir::PassManager pm(ctx);
    cir::direct::populateCIRToLLVMPasses(pm, cir::getStage(mod), fromClang,
                                         runMem2Reg, /*stopAt=*/std::nullopt,
                                         inliner,
                                         /*stopBeforeConversion=*/true);
    pm.enableVerifier(false);
    if (mlir::failed(pm.run(mod)))
      return mlir::failure();
  }

  llvm::SmallVector<mlir::OwningOpRef<mlir::ModuleOp>> pieces;
  {
    llvm::TimeTraceScope scope("splitModule");
    pieces = splitModule(mod, partitions);
  }
  // HoistAllocas, in the whole-module part, recorded on this thread which
  // allocas come from a nested lexical scope, for the debug info. The pieces
  // hold those allocas now and are lowered on other threads; each takes its
  // own back below.
  cir::direct::NestedSourceDebugAllocas nestedAllocas =
      cir::direct::takeNestedSourceDebugAllocas(mod);

  // Load and register everything the per-piece pipelines and translation
  // touch now: from here on, several threads use the context at once.
  {
    mlir::DialectRegistry registry;
    mlir::PassManager pm(ctx);
    cir::direct::populateCIRToLLVMPasses(pm, cir::CIRStage::Flat, false,
                                         false);
    pm.getDependentDialects(registry);
    registerCIRToLLVMIRTranslations(registry);
    ctx->appendDialectRegistry(registry);
    ctx->loadAllAvailableDialects();
  }

  InlinerOptions pieceInliner = inliner;
  pieceInliner.inlinedEarlier = inliner.enabled;
  llvm::TimeTraceScope scope("lowerPieces");
  return mlir::failableParallelForEach(
      ctx, llvm::seq<unsigned>(0, pieces.size()), [&](unsigned p) {
        llvm::TimeTraceScope pieceScope("lowerPiece");
        cir::direct::restoreNestedSourceDebugAllocas(*pieces[p], nestedAllocas);
        llvm::LLVMContext llvmCtx;
        std::unique_ptr<llvm::Module> llvmModule =
            cir::direct::lowerDirectlyFromCIRToLLVMIR(
                *pieces[p], llvmCtx,
                /*disableDebugInfo=*/false,
                /*enableVerifier=*/true, /*runCallConvLowering=*/false,
                /*runMem2RegAfterDebugInfo=*/false,
                /*runMem2RegInLowering=*/false,
                /*reportProfileReadError=*/{}, pieceInliner,
                /*startAtConversion=*/true);
        if (!llvmModule)
          return mlir::failure();
        runStandaloneLLVMPostLoweringPasses(*llvmModule, sanmdOpts,
                                            sanmdIgnorelistFiles);
        mlir::LogicalResult result = emit(p, *llvmModule);
        pieces[p] = nullptr;
        return result;
      });
}

void cir::writeLLVMModuleBitcode(llvm::Module &module, llvm::raw_ostream &os,
                                 const LTOBitcodeOptions &ltoOpts) {
  using Kind = LTOBitcodeOptions::Kind;
  llvm::Triple triple(module.getTargetTriple());

  // Regular LTO emits no summary for Apple vendors (shouldEmitRegularLTOSummary
  // in BackendUtil.cpp), and without a summary the flags go too. Read off the
  // module's own triple, as clang reads it off the target.
  bool regularLTOSummary =
      ltoOpts.kind == Kind::Full && triple.getVendor() != llvm::Triple::Apple;
  if (ltoOpts.kind == Kind::None ||
      (ltoOpts.kind == Kind::Full && !regularLTOSummary)) {
    llvm::WriteBitcodeToFile(module, os);
    return;
  }

  // The module flags clang attaches when it writes LTO bitcode. They describe
  // how the module is being written rather than anything about the translation
  // unit, which is why CIR does not carry them and CIRGen never set them.
  //
  // Every object the merged module replaces carries these, and a disagreement
  // is a hard error out of lld rather than a quiet difference: "inconsistent
  // LTO Unit splitting". Module::Error is clang's merge behaviour and is the
  // point -- inputs that genuinely disagree fail the link rather than have one
  // silently win.
  //
  // The split flag is where the two kinds differ, and the difference is
  // clang's, not ours: for regular LTO it writes 1 whatever the command line
  // said, for ThinLTO it writes CodeGenOpts.EnableSplitLTOUnit.
  if (ltoOpts.kind == Kind::Full && !module.getModuleFlag("ThinLTO"))
    module.addModuleFlag(llvm::Module::Error, "ThinLTO", uint32_t(0));
  if (!module.getModuleFlag("EnableSplitLTOUnit"))
    module.addModuleFlag(
        llvm::Module::Error, "EnableSplitLTOUnit",
        uint32_t(ltoOpts.kind == Kind::Full ? true : ltoOpts.splitLTOUnit));

  // Both writers build a summary, and the summary is what matters:
  // BitcodeModule::getLTOInfo reads EnableSplitLTOUnit out of the summary's
  // flags record, not out of the module flags, so a module written without one
  // reads as unsplit however its module flags are set.
  //
  // The analysis managers go through PassBuilder because building the summary
  // needs ProfileSummaryAnalysis and the block-frequency analyses.
  llvm::PassBuilder passBuilder;
  llvm::LoopAnalysisManager lam;
  llvm::FunctionAnalysisManager fam;
  llvm::CGSCCAnalysisManager cgam;
  llvm::ModuleAnalysisManager mam;
  passBuilder.registerModuleAnalyses(mam);
  passBuilder.registerCGSCCAnalyses(cgam);
  passBuilder.registerFunctionAnalyses(fam);
  passBuilder.registerLoopAnalyses(lam);
  passBuilder.crossRegisterProxies(lam, fam, cgam, mam);

  llvm::ModulePassManager mpm;
  if (ltoOpts.kind == Kind::Thin)
    mpm.addPass(llvm::ThinLTOBitcodeWriterPass(os, /*ThinLinkOS=*/nullptr));
  else
    mpm.addPass(llvm::BitcodeWriterPass(os,
                                        /*ShouldPreserveUseListOrder=*/false,
                                        /*EmitSummaryIndex=*/true));
  mpm.run(module, mam);
}
