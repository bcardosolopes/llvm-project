//====- LowerToLLVM.h- Lowering from CIR to LLVM --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares an interface for converting CIR modules to LLVM IR.
//
//===----------------------------------------------------------------------===//
#ifndef CLANG_CIR_LOWERTOLLVM_H
#define CLANG_CIR_LOWERTOLLVM_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/LogicalResult.h"
#include <functional>
#include <memory>
#include <optional>

namespace llvm {
class LLVMContext;
class Module;
class raw_ostream;
} // namespace llvm

namespace mlir {
class Location;
class ModuleOp;
class Operation;
} // namespace mlir

namespace mlir {
class DialectRegistry;
} // namespace mlir

namespace cir {

/// The CIR inliner, as a lowering pipeline runs it: off unless enabled, and
/// with the inliner's own threshold unless one is given.
struct InlinerOptions {
  bool enabled = false;
  std::optional<unsigned> threshold;
  /// The module was inlined before this lowering, as each piece of a
  /// partitioned lowering is: do not inline again, but scope the inlined
  /// frames.
  bool inlinedEarlier = false;
};

/// Register everything a context needs in order to lower CIR to LLVM IR.
void registerCIRToLLVMIRTranslations(mlir::DialectRegistry &registry);

/// Give the module a target triple and a data layout if it has not got them.
///
/// `requestedTriple` is the caller's --target, or nullopt if it was not given.
/// It is a parameter and not an option read from here on purpose: an
/// llvm::cl::opt in a library is registered by every binary that links it.
llvm::LogicalResult
prepareCIRModuleForTranslation(mlir::ModuleOp mod,
                               std::optional<llvm::StringRef> requestedTriple);

/// Take a CIR module all the way to an llvm::Module: prepare it, lower it, and
/// run the post-lowering passes that belong to this path.
///
/// This exists so that there is exactly one answer to "what does lowering a
/// merged CIR module mean". cir-translate is the inspectable form of this
/// pipeline and cir-link runs it in-process; if the two were assembled
/// separately from the same parts, they could be given different parts, and
/// the thing you inspected would stop being the thing that shipped.
///
/// \p inliner adds the CIR inliner to the pipeline, as clang's
/// -fclangir-inline[-threshold=] does; the caller decides, like the triple.
///
/// Returns null on failure, having emitted diagnostics.
std::unique_ptr<llvm::Module>
lowerCIRModuleToLLVMIR(mlir::ModuleOp mod, llvm::LLVMContext &llvmCtx,
                       std::optional<llvm::StringRef> requestedTriple,
                       InlinerOptions inliner = {});

/// lowerCIRModuleToLLVMIR in `partitions` pieces lowered in parallel. The
/// CIR-to-CIR part of the pipeline -- through mem2reg and the inliner -- runs
/// on the whole module; the module is then split so that an internal symbol
/// stays with everything that refers to it, and each piece is converted and
/// translated on its own, into an LLVMContext of its own. `emit` is called
/// once per piece, with its index, from the thread that lowered it. Linked
/// together, as a regular LTO link does, the pieces are the module the
/// unpartitioned lowering produces.
///
/// The module is consumed: it holds the first piece afterwards.
llvm::LogicalResult lowerCIRModuleToLLVMIRInPartitions(
    mlir::ModuleOp mod, std::optional<llvm::StringRef> requestedTriple,
    InlinerOptions inliner, unsigned partitions,
    llvm::function_ref<llvm::LogicalResult(unsigned, llvm::Module &)> emit);

/// How the build configured the link this module is joining.
///
/// These are facts about the build, not about the module, and nothing here can
/// work them out -- the same reason the triple has to be passed in. They
/// mirror what clang does when it writes bitcode for each kind of link
/// (BackendUtil.cpp), so a module lowered from CIR at link time describes
/// itself the way every object it was merged from does.
struct LTOBitcodeOptions {
  enum class Kind {
    /// Not joining an LTO link: a plain bitcode write.
    None,
    /// Regular (full) LTO: a summary, ThinLTO=0 and EnableSplitLTOUnit=1.
    /// clang sets the split flag to 1 here unconditionally, so splitLTOUnit
    /// does not apply -- the same as -fsplit-lto-unit not affecting
    /// -flto=full.
    Full,
    /// ThinLTO: the ThinLTO bitcode writer, and EnableSplitLTOUnit taken from
    /// splitLTOUnit, which is where it is a real choice.
    Thin,
  };
  Kind kind = Kind::None;
  /// clang's -fsplit-lto-unit, including when the driver defaults it on
  /// (-fwhole-program-vtables, CFI). Only consulted for Kind::Thin.
  bool splitLTOUnit = false;
};

/// Write a module as bitcode, configured for the link it is joining.
void writeLLVMModuleBitcode(llvm::Module &module, llvm::raw_ostream &os,
                            const LTOBitcodeOptions &ltoOpts);

namespace direct {
std::unique_ptr<llvm::Module> lowerDirectlyFromCIRToLLVMIR(
    mlir::ModuleOp mlirModule, llvm::LLVMContext &llvmCtx,
    bool disableDebugInfo = false, bool enableVerifier = true,
    bool runCallConvLowering = false, bool runMem2RegAfterDebugInfo = false,
    bool runMem2RegInLowering = false,
    const std::function<void(llvm::StringRef, llvm::StringRef)>
        &reportProfileReadError = {},
    InlinerOptions inliner = {}, bool startAtConversion = false);

void noteNestedSourceDebugAlloca(mlir::ModuleOp module, mlir::Operation *allocaOp);

/// The allocas noteNestedSourceDebugAlloca recorded for a module on this
/// thread, with the location each had then.
using NestedSourceDebugAllocas =
    llvm::DenseMap<mlir::Operation *, std::optional<mlir::Location>>;

/// Take what was recorded for `module` on this thread. A partitioned lowering
/// records on the whole module and lowers its pieces on other threads.
NestedSourceDebugAllocas takeNestedSourceDebugAllocas(mlir::ModuleOp module);

/// Record, for `module` on this thread, the entries of `taken` for the allocas
/// `module` now holds.
void restoreNestedSourceDebugAllocas(mlir::ModuleOp module,
                                     const NestedSourceDebugAllocas &taken);

/// Clear what the CIR translation interface remembers on this thread while it
/// translates a module; call before and after translating one.
void resetTranslationCaches();
} // namespace direct
} // namespace cir

#endif // CLANG_CIR_LOWERTOLLVM_H
