//===- CallConvLowering.cpp - Rewrites functions according to call convs --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "TargetLowering/LowerModule.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/Threading.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/IR/CIRStage.h"
#include "clang/CIR/MissingFeatures.h"
#include "llvm/ADT/ScopeExit.h"

#define GEN_PASS_DEF_CALLCONVLOWERING
#include "clang/CIR/Dialect/Passes.h.inc"

#include "llvm/Support/TimeProfiler.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/TargetParser/Triple.h"

#include <optional>

namespace cir {

FuncType getFuncPointerTy(mlir::Type typ) {
  if (auto ptr = mlir::dyn_cast<PointerType>(typ))
    return mlir::dyn_cast<FuncType>(ptr.getPointee());
  return {};
}

bool isFuncPointerTy(mlir::Type typ) { return (bool)getFuncPointerTy(typ); }

struct CallConvLowering {
  struct FunctionUseBuckets {
    llvm::SmallVector<CallOp, 4> directCalls;
    llvm::SmallVector<GetGlobalOp, 4> getGlobals;
    llvm::SmallVector<CallOp, 4> indirectCalls;
    llvm::SmallVector<TryCallOp, 4> directTryCalls;
    llvm::SmallVector<TryCallOp, 4> indirectTryCalls;
  };

  CallConvLowering(mlir::ModuleOp module)
      : rewriter(module.getContext()),
        lowerModule(createLowerModule(module, rewriter)) {
    // If the target is not supported for call-conv lowering (e.g. Windows
    // or 32-bit x86), skip pre-scan work. lower() will also bail out.
    if (!lowerModule)
      return;

    // Pre-build symbol use maps to avoid O(N*M) quadratic scans.
    // Without this, each function calls getSymbolUses(module) which walks
    // the entire module — causing hangs on large files.
    // Walk each function once and bucket uses directly by that function to
    // avoid per-indirect-call parent lookups in hot paths.
    // Reserve from top-level op count to avoid an extra full FuncOp counting
    // walk. This is an upper bound for function count, but keeps reserve
    // amortized-O(1) while trimming pre-scan work in large modules.
    unsigned topLevelOpCount = module.getBody()->getOperations().size();
    functionByName.reserve(topLevelOpCount);
    usesByFunction.reserve(topLevelOpCount);

    for (FuncOp fn : module.getOps<FuncOp>())
      functionByName.try_emplace(fn.getSymNameAttr(), fn.getOperation());

    // Build a conservative set of function symbols that have *any* symbol
    // users in the module (not just call/get_global users). This avoids
    // per-declaration getSymbolUses() queries in lower() while preserving
    // correctness for non-call symbol users (e.g. member-function pointers).
    auto collectSymbolUsersFromAttr = [&](mlir::Attribute attr) {
      attr.walk([&](mlir::SymbolRefAttr symbolRef) {
        if (auto it = functionByName.find(symbolRef.getRootReference());
            it != functionByName.end())
          functionsWithAnySymbolUsers.insert(it->second);
      });
      // MethodAttr stores its symbol as std::optional<FlatSymbolRefAttr>,
      // which is not exposed to MLIR's sub-element walk mechanism (tablegen
      // doesn't generate walkImmediateSubElements for OptionalParameter with
      // std::optional<Attribute>). Explicitly extract symbol refs from any
      // MethodAttr nested in the attribute tree.
      attr.walk([&](cir::MethodAttr methodAttr) {
        if (auto sym = methodAttr.getSymbol()) {
          if (auto it = functionByName.find(sym->getAttr());
              it != functionByName.end())
            functionsWithAnySymbolUsers.insert(it->second);
        }
      });
    };
    //
    // Attributes are uniqued and the same ones recur on many ops, so each is
    // walked once. A registered op's inherent attributes are read one by one:
    // getAttrs() on an op that keeps them in properties -- every CIR op --
    // builds and uniques a dictionary for it.
    llvm::DenseSet<mlir::Attribute> scannedAttrs;
    auto scanAttr = [&](mlir::Attribute attr) {
      if (attr && scannedAttrs.insert(attr).second)
        collectSymbolUsersFromAttr(attr);
    };
    module.walk([&](mlir::Operation *op) {
      std::optional<mlir::RegisteredOperationName> info =
          op->getRegisteredInfo();
      if (!info || !op->getPropertiesStorageSize()) {
        for (mlir::NamedAttribute attr : op->getRawDictionaryAttrs())
          scanAttr(attr.getValue());
        return;
      }
      for (mlir::StringAttr name : info->getAttributeNames())
        if (std::optional<mlir::Attribute> attr = op->getInherentAttr(name))
          scanAttr(*attr);
      for (mlir::NamedAttribute attr : op->getDiscardableAttrs())
        scanAttr(attr.getValue());
    });

    // Module-scope helper regions such as omp.declare_reduction are not nested
    // under cir.func, but they can still contain function users that need
    // ABI-aware call-conv rewriting before the referenced FuncOps are erased.
    auto scanNonFunctionUsers = [&](mlir::Operation *root) {
      std::optional<llvm::DenseMap<mlir::StringAttr, mlir::Operation *>>
          resolvedCallees;
      mlir::StringAttr lastResolvedSymbol = nullptr;
      mlir::Operation *lastResolvedSymbolFn = nullptr;
      mlir::Operation *lastResolvedCalleeFn = nullptr;
      FunctionUseBuckets *lastResolvedCalleeUsers = nullptr;
      auto appendDirectUser = [&](mlir::Operation *calleeFn, CallOp callOp) {
        if (calleeFn == lastResolvedCalleeFn && lastResolvedCalleeUsers) {
          lastResolvedCalleeUsers->directCalls.push_back(callOp);
          return;
        }
        auto [useIt, inserted] = usesByFunction.try_emplace(calleeFn);
        lastResolvedCalleeFn = calleeFn;
        lastResolvedCalleeUsers = &useIt->second;
        lastResolvedCalleeUsers->directCalls.push_back(callOp);
      };
      auto appendGetGlobalUser = [&](mlir::Operation *calleeFn,
                                     GetGlobalOp getGlobal) {
        if (calleeFn == lastResolvedCalleeFn && lastResolvedCalleeUsers) {
          lastResolvedCalleeUsers->getGlobals.push_back(getGlobal);
          return;
        }
        auto [useIt, inserted] = usesByFunction.try_emplace(calleeFn);
        lastResolvedCalleeFn = calleeFn;
        lastResolvedCalleeUsers = &useIt->second;
        lastResolvedCalleeUsers->getGlobals.push_back(getGlobal);
      };
      auto resolveCallee =
          [&](mlir::StringAttr symbolName) -> mlir::Operation * {
        if (symbolName == lastResolvedSymbol)
          return lastResolvedSymbolFn;

        if (!resolvedCallees)
          resolvedCallees.emplace();

        if (auto it = resolvedCallees->find(symbolName);
            it != resolvedCallees->end()) {
          lastResolvedSymbol = symbolName;
          lastResolvedSymbolFn = it->second;
          return it->second;
        }

        mlir::Operation *resolved = nullptr;
        if (auto fnIt = functionByName.find(symbolName);
            fnIt != functionByName.end())
          resolved = fnIt->second;

        resolvedCallees->try_emplace(symbolName, resolved);
        lastResolvedSymbol = symbolName;
        lastResolvedSymbolFn = resolved;
        return resolved;
      };

      auto appendDirectTryCallUser = [&](mlir::Operation *calleeFn,
                                         TryCallOp tryCallOp) {
        if (calleeFn == lastResolvedCalleeFn && lastResolvedCalleeUsers) {
          lastResolvedCalleeUsers->directTryCalls.push_back(tryCallOp);
          return;
        }
        auto [useIt, inserted] = usesByFunction.try_emplace(calleeFn);
        lastResolvedCalleeFn = calleeFn;
        lastResolvedCalleeUsers = &useIt->second;
        lastResolvedCalleeUsers->directTryCalls.push_back(tryCallOp);
      };
      root->walk([&](mlir::Operation *op) -> mlir::WalkResult {
        if (mlir::isa<FuncOp>(op))
          return mlir::WalkResult::skip();

        if (auto callOp = mlir::dyn_cast<CallOp>(op)) {
          if (auto callee = callOp.getCalleeAttr()) {
            if (mlir::Operation *calleeFn = resolveCallee(callee.getAttr()))
              appendDirectUser(calleeFn, callOp);
          } else if (callOp.isIndirect()) {
            nonFunctionIndirectCalls.push_back(callOp);
          }
        } else if (auto tryCallOp = mlir::dyn_cast<TryCallOp>(op)) {
          if (auto callee = tryCallOp.getCalleeAttr()) {
            if (mlir::Operation *calleeFn = resolveCallee(callee.getAttr()))
              appendDirectTryCallUser(calleeFn, tryCallOp);
          } else if (tryCallOp.isIndirect()) {
            nonFunctionIndirectTryCalls.push_back(tryCallOp);
          }
        } else if (auto getGlobal = mlir::dyn_cast<GetGlobalOp>(op)) {
          if (isFuncPointerTy(getGlobal.getType())) {
            mlir::Operation *calleeFn =
                resolveCallee(getGlobal.getNameAttr().getAttr());
            if (convert(moduleFor(calleeFn), getGlobal.getType()) ==
                getGlobal.getType())
              return mlir::WalkResult::advance();
            if (calleeFn)
              appendGetGlobalUser(calleeFn, getGlobal);
          }
        }

        return mlir::WalkResult::advance();
      });
    };

    // Each function's calls and function address-takings are found in
    // parallel -- a read-only walk that resolves callees by name -- and put in
    // their callees' buckets serially, in function order, so every bucket
    // holds what it did before in the same order. Whether a get_global of a
    // function needs rewriting asks the target lowering, whose caches are not
    // thread-safe, so it is decided in the serial part.
    struct FoundUse {
      mlir::Operation *op;
      /// The resolved callee; null for an indirect call, or for a get_global
      /// of a symbol that is not a function here.
      mlir::Operation *calleeFn;
    };
    llvm::SmallVector<FuncOp> definitions;
    for (FuncOp fn : module.getOps<FuncOp>())
      if (!fn.isDeclaration())
        definitions.push_back(fn);
    std::vector<llvm::SmallVector<FoundUse, 8>> found(definitions.size());
    mlir::parallelFor(module.getContext(), 0, definitions.size(), [&](size_t i) {
      FuncOp fn = definitions[i];
      // Cache symbol-to-function resolution per function: a body tends to
      // call the same targets many times.
      llvm::DenseMap<mlir::StringAttr, mlir::Operation *> resolvedCallees;
      auto resolveCallee = [&](mlir::StringAttr name) -> mlir::Operation * {
        auto [it, inserted] = resolvedCallees.try_emplace(name, nullptr);
        if (inserted)
          if (auto fnIt = functionByName.find(name); fnIt != functionByName.end())
            it->second = fnIt->second;
        return it->second;
      };
      fn->walk([&](mlir::Operation *op) -> mlir::WalkResult {
        if (auto nestedFn = mlir::dyn_cast<FuncOp>(op);
            nestedFn && nestedFn.getOperation() != fn.getOperation())
          return mlir::WalkResult::skip();
        if (auto callOp = mlir::dyn_cast<CallOp>(op)) {
          if (auto callee = callOp.getCalleeAttr()) {
            if (mlir::Operation *calleeFn = resolveCallee(callee.getAttr()))
              found[i].push_back({op, calleeFn});
          } else if (callOp.isIndirect()) {
            found[i].push_back({op, nullptr});
          }
        } else if (auto tryCallOp = mlir::dyn_cast<TryCallOp>(op)) {
          if (auto callee = tryCallOp.getCalleeAttr()) {
            if (mlir::Operation *calleeFn = resolveCallee(callee.getAttr()))
              found[i].push_back({op, calleeFn});
          } else if (tryCallOp.isIndirect()) {
            found[i].push_back({op, nullptr});
          }
        } else if (auto getGlobal = mlir::dyn_cast<GetGlobalOp>(op)) {
          // Only function-pointer get_global users need call-conv rewriting.
          if (isFuncPointerTy(getGlobal.getType()))
            found[i].push_back(
                {op, resolveCallee(getGlobal.getNameAttr().getAttr())});
        }
        return mlir::WalkResult::advance();
      });
    });
    for (size_t i = 0; i < definitions.size(); ++i) {
      FuncOp fn = definitions[i];
      for (const FoundUse &use : found[i]) {
        if (auto callOp = mlir::dyn_cast<CallOp>(use.op)) {
          if (use.calleeFn)
            usesByFunction[use.calleeFn].directCalls.push_back(callOp);
          else
            usesByFunction[fn.getOperation()].indirectCalls.push_back(callOp);
        } else if (auto tryCallOp = mlir::dyn_cast<TryCallOp>(use.op)) {
          if (use.calleeFn)
            usesByFunction[use.calleeFn].directTryCalls.push_back(tryCallOp);
          else
            usesByFunction[fn.getOperation()].indirectTryCalls.push_back(
                tryCallOp);
        } else {
          auto getGlobal = mlir::cast<GetGlobalOp>(use.op);
          // Skip no-op rewrites where call-conv lowering keeps the same
          // pointer type.
          if (convert(moduleFor(use.calleeFn), getGlobal.getType()) ==
              getGlobal.getType())
            continue;
          if (use.calleeFn)
            usesByFunction[use.calleeFn].getGlobals.push_back(getGlobal);
        }
      }
    }

    for (mlir::Operation &op : module.getBody()->getOperations()) {
      if (mlir::isa<FuncOp>(&op))
        continue;
      scanNonFunctionUsers(&op);
    }

  }

  void lower(FuncOp op) {
    if (!lowerModule)
      return;

    FunctionUseBuckets *users = nullptr;
    if (auto it = usesByFunction.find(op.getOperation());
        it != usesByFunction.end())
      users = &it->second;

    if (!users) {
      // Declarations with no direct call/get_global users can often be skipped
      // entirely. Keep a conservative guard for non-call symbol users (e.g.
      // member-function pointer constants) using the pre-scanned
      // functionsWithAnySymbolUsers set.
      //
      // Exception: inline builtin declarations (e.g. memcpy with
      // __always_inline__+__gnu_inline__) are emitted as LLVM declarations
      // even though calls go to the .inline clone. They need proper ABI
      // attributes (noundef, signext, etc.) from the target lowering pass.
      if (op.isDeclaration()) {
        if (!op->hasAttr("cir.inline_builtin_declaration") &&
            !functionsWithAnySymbolUsers.contains(op.getOperation()))
          return;
      }
      if (moduleFor(op).rewriteFunctionDefinition(op).failed())
        op.emitError("Unable to rewrite function definition");
      return;
    }

    {
      // Direct calls and address-takings of `op` are classified by `op`'s
      // own language -- its definition is what fixes the ABI. The indirect
      // calls bucketed here are the ones `op` makes, so they go by `op`'s
      // language as the caller.
      LowerModule &lm = moduleFor(op);
      for (GetGlobalOp g : users->getGlobals)
        rewriteGetGlobalOp(lm, g);
      for (CallOp c : users->directCalls)
        lowerDirectCallOp(lm, c, op);
      for (CallOp c : users->indirectCalls)
        lowerIndirectCallOp(lm, c);
      for (TryCallOp c : users->directTryCalls)
        lowerDirectTryCallOp(lm, c, op);
      for (TryCallOp c : users->indirectTryCalls)
        lowerIndirectTryCallOp(lm, c);
    }

    if (moduleFor(op).rewriteFunctionDefinition(op).failed())
      op.emitError("Unable to rewrite function definition");
  }

  void lowerNonFunctionIndirectCalls() {
    if (!lowerModule)
      return;

    for (CallOp c : nonFunctionIndirectCalls)
      lowerIndirectCallOp(*lowerModule, c);
    for (TryCallOp c : nonFunctionIndirectTryCalls)
      lowerIndirectTryCallOp(*lowerModule, c);
  }

private:
  /// The target lowering for a function: its own TU's language, when it
  /// records one, and the module's otherwise.
  ///
  /// A module from one TU has one language, but a merged one (cir-link) holds
  /// C and C++ TUs together, and the language changes the ABI -- on AArch64
  /// outside Darwin, C drops empty-record arguments that C++ passes by size.
  /// Classifying a C function by C++ rules gives it a signature its C callers
  /// in other libraries do not use. cir-link records each function's language
  /// as `cir.lang` where it differs from the merged module's.
  LowerModule &moduleFor(mlir::Operation *fn) {
    auto langAttr =
        fn ? fn->getAttrOfType<cir::SourceLanguageAttr>(
                 cir::CIRDialect::getSourceLanguageAttrName())
           : cir::SourceLanguageAttr();
    if (!langAttr)
      return *lowerModule;
    std::unique_ptr<LowerModule> &lm =
        byLanguage[static_cast<unsigned>(langAttr.getValue())];
    if (!lm) {
      lm = createLowerModule(fn->getParentOfType<mlir::ModuleOp>(), rewriter,
                             LowerModuleRequirement::FullTargetLowering,
                             langAttr.getValue());
      cir_cconv_assert(lm && "target lowering exists for the module but not "
                             "for one of its languages");
    }
    return *lm;
  }

  FuncType convert(LowerModule &lm, FuncType t) {
    auto key = std::make_pair(&lm, t);
    if (auto it = convertedFuncTypes.find(key); it != convertedFuncTypes.end())
      return it->second;

    auto &typs = lm.getTypes();
    FuncType converted = typs.getFunctionType(typs.arrangeFreeFunctionType(t));
    convertedFuncTypes.try_emplace(key, converted);
    return converted;
  }

  mlir::Type convert(LowerModule &lm, mlir::Type t) {
    auto key = std::make_pair(&lm, t);
    if (auto it = convertedTypes.find(key); it != convertedTypes.end())
      return it->second;

    if (auto fTy = getFuncPointerTy(t))
      return convertedTypes
          .try_emplace(key, cir::PointerType::get(convert(lm, fTy)))
          .first->second;
    convertedTypes.try_emplace(key, t);
    return t;
  }

  void bitcast(mlir::Value src, mlir::Type newTy) {
    if (src.getType() != newTy) {
      auto cast =
          CastOp::create(rewriter, src.getLoc(), newTy, CastKind::bitcast, src);
      rewriter.replaceAllUsesExcept(src, cast, cast);
    }
  }

  void rewriteGetGlobalOp(LowerModule &lm, GetGlobalOp op) {
    auto resTy = op.getType();
    if (isFuncPointerTy(resTy)) {
      auto convertedTy = convert(lm, resTy);
      // Fast path: avoid creating replacement GetGlobalOp + self-bitcast when
      // call-conv lowering does not actually change the function pointer type.
      if (convertedTy == resTy)
        return;

      rewriter.setInsertionPoint(op);
      auto newOp =
          rewriter.replaceOpWithNewOp<GetGlobalOp>(op, convertedTy, op.getName());
      rewriter.setInsertionPointAfter(newOp);
      bitcast(newOp, resTy);
    }
  }

  void lowerDirectCallOp(LowerModule &lm, CallOp op, FuncOp callee) {
    if (lm.rewriteFunctionCall(op, callee).failed())
      op.emitError("Unable to rewrite function call");
  }

  void lowerDirectTryCallOp(LowerModule &lm, TryCallOp op, FuncOp callee) {
    if (lm.rewriteFunctionCall(op, callee).failed())
      op.emitError("Unable to rewrite try_call");
  }

  void lowerIndirectCallOp(LowerModule &lm, CallOp op) {
    cir_cconv_assert(op.isIndirect());

    rewriter.setInsertionPoint(op);
    auto typ = op.getIndirectCall().getType();
    if (!isFuncPointerTy(typ))
      return;

    if (lm.rewriteFunctionCall(op).failed())
      op.emitError("Unable to rewrite function call");
  }

  void lowerIndirectTryCallOp(LowerModule &lm, TryCallOp op) {
    cir_cconv_assert(op.isIndirect());

    rewriter.setInsertionPoint(op);
    auto typ = op.getIndirectCall().getType();
    if (!isFuncPointerTy(typ))
      return;

    if (lm.rewriteFunctionCall(op).failed())
      op.emitError("Unable to rewrite try_call");
  }

private:
  mlir::PatternRewriter rewriter;
  std::unique_ptr<LowerModule> lowerModule;
  // Target lowering per recorded function language; see moduleFor.
  llvm::SmallDenseMap<unsigned, std::unique_ptr<LowerModule>, 2> byLanguage;
  // Map symbol names to function operations, built once up-front.
  llvm::DenseMap<mlir::StringAttr, mlir::Operation *> functionByName;
  // Typed use buckets keyed directly by function operation.
  llvm::DenseMap<mlir::Operation *, FunctionUseBuckets> usesByFunction;
  // Conservative set of function symbols that have any symbol users in module.
  llvm::DenseSet<mlir::Operation *> functionsWithAnySymbolUsers;
  // Module-scope helper regions can contain indirect calls that are not
  // associated with any FuncOp body and therefore need their own rewrite pass.
  llvm::SmallVector<CallOp, 4> nonFunctionIndirectCalls;
  llvm::SmallVector<TryCallOp, 4> nonFunctionIndirectTryCalls;
  // Memoized type conversions used while rewriting call/global users.
  llvm::DenseMap<std::pair<LowerModule *, FuncType>, FuncType>
      convertedFuncTypes;
  llvm::DenseMap<std::pair<LowerModule *, mlir::Type>, mlir::Type>
      convertedTypes;
};

//===----------------------------------------------------------------------===//
// Pass
//===----------------------------------------------------------------------===//

struct CallConvLoweringPass
    : ::impl::CallConvLoweringBase<CallConvLoweringPass> {
  using CallConvLoweringBase::CallConvLoweringBase;

  void runOnOperation() override;
  llvm::StringRef getArgument() const override {
    return "cir-call-conv-lowering";
  };
};

void CallConvLoweringPass::runOnOperation() {
  // Whichever way this returns, the module has been through it.
  auto recordStage = llvm::make_scope_exit(
      [&] { cir::advanceStage(getOperation(), cir::CIRStage::ABI); });
  auto module = mlir::dyn_cast<mlir::ModuleOp>(getOperation());
  llvm::TimeTraceScope passScope("CallConvLowering");
  std::optional<llvm::TimeTraceScope> scanScope;
  scanScope.emplace("CallConvLowering scan");
  CallConvLowering cc(module);
  scanScope.reset();
  llvm::TimeTraceScope lowerScope("CallConvLowering rewrite");
  cc.lowerNonFunctionIndirectCalls();
  for (auto fn : llvm::make_early_inc_range(module.getOps<FuncOp>()))
    cc.lower(fn);
}

} // namespace cir

namespace mlir {

std::unique_ptr<mlir::Pass> createCallConvLoweringPass() {
  return std::make_unique<cir::CallConvLoweringPass>();
}

} // namespace mlir
