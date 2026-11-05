//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_CIR_DIALECT_PASSES_TD
#define CLANG_CIR_DIALECT_PASSES_TD

include "mlir/Pass/PassBase.td"

def CIRCanonicalize : Pass<"cir-canonicalize"> {
  let summary = "Performs CIR canonicalization";
  let description = [{
    Perform canonicalizations on CIR and removes some redundant operations.

    This pass performs basic cleanup and canonicalization transformations that
    are not intended to affect CIR-to-source fidelity and high-level code
    analysis passes. Example transformations performed in this pass include
    empty scope cleanup, trivial `try` cleanup, redundant branch cleanup, etc.
    Those more "heavyweight" transformations and those transformations that
    could significantly affect CIR-to-source fidelity are performed in the
    `cir-simplify` pass.
  }];

  let constructor = "mlir::createCIRCanonicalizePass()";
  let dependentDialects = ["cir::CIRDialect"];
}

def CIRSimplify : Pass<"cir-simplify"> {
  let summary = "Performs CIR simplification and code optimization";
  let description = [{
    The pass performs semantics-preserving code simplifications and optimizations
    on CIR while maintaining strict program correctness.

    Unlike the `cir-canonicalize` pass, these transformations may reduce the IR's
    structural similarity to the original source code as a trade-off for improved
    code quality. This can affect debugging fidelity by altering intermediate
    representations of folded expressions, hoisted operations, and other
    optimized constructs.

    Example transformations include ternary expression folding and code hoisting
    while preserving program semantics.
  }];
  let constructor = "mlir::createCIRSimplifyPass()";
  let dependentDialects = ["cir::CIRDialect"];
}

def HoistAllocas : Pass<"cir-hoist-allocas"> {
  let summary = "Hoist allocas to the entry of the function";
  let description = [{
    This pass hoist all non-dynamic allocas to the entry of the function.
    This is helpful for later code generation.
  }];
  let constructor = "mlir::createHoistAllocasPass()";
  let dependentDialects = ["cir::CIRDialect"];
}

def CIRFlattenCFG : Pass<"cir-flatten-cfg"> {
  let summary = "Produces flatten CFG";
  let description = [{
    This pass transforms CIR by inlining all the nested regions. Thus,
    the following conditions are true after the pass applied:
    - there are no nested regions in any function body
    - all the blocks in a function belong to the parent region
    In other words, this pass removes such CIR operations like IfOp, LoopOp,
    ScopeOp and etc. and produces a flat CIR.
  }];
  let constructor = "mlir::createCIRFlattenCFGPass()";
  let dependentDialects = ["cir::CIRDialect"];
}

def GotoSolver : Pass<"cir-goto-solver"> {
  let summary = "Replaces goto operations with branches";
  let description = [{
    This pass transforms CIR and replaces goto-s with branch
    operations to the proper blocks.
  }];
  let constructor = "mlir::createGotoSolverPass()";
  let dependentDialects = ["cir::CIRDialect"];
}

def CIRTailMerge : Pass<"cir-tail-merge"> {
  let summary = "Merge duplicated cleanup tails on a flat CFG";
  let description = [{
    CIRGen emits a scope's cleanups inline at every exit edge, because at
    CIRGen time each early exit sits in its own region and cannot branch to a
    shared block. Classic codegen chains one shared cleanup block per scope
    depth instead. Once FlattenCFG has removed the regions, those copies are
    identical tails of blocks that share a successor, and this pass merges
    them back into one block.

    Only tails containing a call are merged: the payoff is dropping a cleanup
    handler back to a single call site, which is what decides whether the
    inliner takes it at -Oz. Tails made up entirely of lifetime markers are
    merged too, for the opposite reason: they cost nothing themselves, but one
    left on each exit edge blocks SimplifyCFG from folding exits that share a
    destination.
  }];
  let constructor = "mlir::createCIRTailMergePass()";
  let dependentDialects = ["cir::CIRDialect"];
}

def PointsToDiagnostic : Pass<"cir-points-to-diagnostics"> {
  let summary = "Dump points-to information";
  let description = [{
    This pass emits remarks for load and store operations containing the set
    of pointers that are potentially accessed by the operation. This is useful
    for debugging and understanding the points-to analysis.
    This pass relies on an intraprocedural, flow-insensitive points-to analysis.
    It only tracks function local state, i.e., stack objects.
    In the future, the analysis will support heap-allocated objects,
    flow-sensitivity, and interprocedural analysis.
  }];
  let constructor = "mlir::createPointsToDiagnosticPass()";
  let dependentDialects = ["cir::CIRDialect"];
}

def LiveObjectDiagnostic : Pass<"cir-live-object-diagnostics"> {
  let summary = "Dump live-object information";
  let description = [{
    Dump live-object information for all reads/writes in the function.
  }];
  let constructor = "mlir::createLiveObjectDiagnosticPass()";
  let dependentDialects = ["cir::CIRDialect"];
}

def CXXABILowering : Pass<"cir-cxxabi-lowering", "mlir::ModuleOp"> {
  let summary = "Lower abstract C++ operations to target-specific ABI form";
  let description = [{
    This pass lowers high-level operations that represent C++ constructs in a
    target-independent way to concrete, target specific operations.
  }];
  let constructor = "mlir::createCXXABILoweringPass()";
  let dependentDialects = ["cir::CIRDialect"];
}


def LoweringPrepare : Pass<"cir-lowering-prepare"> {
  let summary = "Lower to more fine-grained CIR operations before lowering to "
    "other dialects";
  let description = [{
    This pass does preparation work for lowering to other dialects. For example,
    it may expand the global variable initialziation in a more ABI-friendly form.
  }];
  let constructor = "mlir::createLoweringPreparePass()";
  let dependentDialects = ["cir::CIRDialect"];
}

def ABILowering : Pass<"cir-abi-lowering"> {
  let summary = "Expands ABI-dependent types and operations";
  let description = [{
    This pass expands ABI-dependent CIR types and operations to more "primitive"
    ABI-independent CIR types and operations according to the target ABI
    specification.

    Some CIR types, such as pointers to members, may have different layouts and
    representations under different target ABIs. This pass expands these types
    to their underlying representations as specified by the target ABI. For
    example, when targeting Itanium ABI, this pass will replace pointers to
    member functions with a struct with two ptrdiff_t fields.

    Similarly, some CIR operations may also behave differently under different
    target ABIs. This pass also expands these operations to more "primitive"
    CIR operations as specified by the target ABI.
  }];
  let constructor = "mlir::createABILoweringPass()";
  let dependentDialects = ["cir::CIRDialect"];
}

def MoveOpt : Pass<"cir-move-opt"> {
  let summary = "Optimize C/C++ move semantics";
  let description = [{
    When a local variable is being copied and is no longer used,
    the copy is replaced with a move, which may be more efficient.

    This analysis is done at the CIR level, which has sufficient
    structural information for this analysis. Roughly, the analysis is:
    1. Run a points-to analysis to find the set of objects that may be
       accessed by each pointer;
    2. Run a live-object analysis, using the results of (1), to determine
       at what point each object is last used;
    3. For each copy, check if the source object's last use is the copy
       itself, and if so, replace the copy with a move.
  }];
  let constructor = "mlir::createMoveOptPass()";
  let dependentDialects = ["cir::CIRDialect"];
}

def CallConvLowering : Pass<"cir-call-conv-lowering"> {
  let summary = "Handle calling conventions for CIR functions";
  let description = [{
    This pass lowers CIR function definitions and calls according to the
    calling conventions for the target architecture. This pass is necessary
    to properly lower CIR functions to LLVM IR.
  }];
  let constructor = "mlir::createCallConvLoweringPass()";
  let dependentDialects = ["cir::CIRDialect"];
}

def SCFPrepare : Pass<"cir-mlir-scf-prepare"> {
  let summary = "Preparation work for SCF dialect lowering";
  let description = [{
    This pass prepares CIR for lowering to SCF dialect by canonicalizing
    loop induction variables and hoisting loop-invariant operations.
  }];
  let constructor = "mlir::createSCFPreparePass()";
  let dependentDialects = ["cir::CIRDialect"];
}

def LifetimeCheck : Pass<"cir-lifetime-check"> {
  let summary = "Check lifetime safety and generate diagnostics";
  let description = [{
    This pass relies on a lifetime analysis pass and uses the diagnostics
    mechanism to report to the user. It does not change any code.

    A default ctor is specified but is solely in order to make
    tablegen happy, since this pass requires the presence of an ASTContext,
    one can set that up using `mlir::createLifetimeCheckPass(clang::ASTContext &)`
    instead.
  }];
  let constructor = "mlir::createLifetimeCheckPass()";
  let dependentDialects = ["cir::CIRDialect"];

  let options = [
    ListOption<"historyList", "history", "std::string",
               "List of history styles to emit as part of diagnostics."
               " Supported styles: {all|null|invalid}", "llvm::cl::ZeroOrMore">,
    ListOption<"remarksList", "remarks", "std::string",
               "List of remark styles to enable as part of diagnostics."
               " Supported styles: {all|pset}", "llvm::cl::ZeroOrMore">,
    Option<"historyLimit", "history_limit", "unsigned", /*default=*/"1",
           "Max amount of diagnostics to emit on pointer history">
  ];
}

def IdiomRecognizer : Pass<"cir-idiom-recognizer"> {
  let summary = "Raise calls to C/C++ libraries to CIR operations";
  let description = [{
    This pass recognize idiomatic C++ usage and incorporate C++ standard
    containers, library functions calls, and types into CIR operation,
    attributes and types.

    Detections done by this pass can be inspected by users by using
    remarks. Currently supported are `all` and `found-calls`.
  }];
  let constructor = "mlir::createIdiomRecognizerPass()";
  let dependentDialects = ["cir::CIRDialect"];

  let options = [
    ListOption<"remarksList", "remarks", "std::string",
               "Diagnostic remarks to enable"
               " Supported styles: {all|found-calls}", "llvm::cl::ZeroOrMore">,
  ];
}

def LibOpt : Pass<"cir-lib-opt"> {
  let summary = "Optimize C/C++ library calls";
  let description = [{
    By using higher level information from `cir-idiom-recognize`, this pass
    apply transformations to CIR based on specific C/C++ library semantics.

    Transformations done by this pass can be inspected by users by using
    remarks. Currently supported are `all` and `transforms`.
  }];
  let constructor = "mlir::createLibOptPass()";
  let dependentDialects = ["cir::CIRDialect"];

  let options = [
    ListOption<"remarksList", "remarks", "std::string",
               "Diagnostic remarks to enable"
               " Supported styles: {all|transforms}", "llvm::cl::ZeroOrMore">,
  ];
}

def CIRInliner : Pass<"cir-inline", "mlir::ModuleOp"> {
  let summary = "Inline small and single-use functions on flat CIR";
  let description = [{
    Runs MLIR's inliner over a flat, ABI-lowered CIR module, after mem2reg.
    A call is inlined when the callee is small -- a weighted count of its ops
    of at most `threshold`, which covers the getters, `operator[]` and
    forwarding wrappers behind most C++ calls -- or when the call is the
    callee's only use and the callee can then be dropped -- except around a
    recursive cycle, where only the size rule applies, and into a caller
    grown past `max-caller-size`, where nothing is inlined. Afterwards it drops
    the functions left without a use that nothing outside the module can
    still reference.

    The legality rules are conservative: no invoke-style call sites, and no
    callee that has EH, varargs, dynamic allocas or by-value arguments.
  }];
  let constructor = "mlir::createCIRInlinerPass()";
  let dependentDialects = ["cir::CIRDialect"];

  // 4: on a large library's relink, 4 cut link time by 17% with .text
  // unchanged; 8 and 16 bought no more time and grew .text by 2.4 and 4.7
  // points.
  let options = [
    Option<"threshold", "threshold", "unsigned", /*default=*/"4",
           "Largest weighted callee size inlined at any call site">,
    // A caller past this weighted size takes no more inlined code, whatever
    // the rule: a bound on growth, which recursive cycles otherwise made
    // exponential (generated serializers in one library reached 12 M instructions).
    Option<"maxCallerSize", "max-caller-size", "unsigned",
           /*default=*/"20000",
           "Weighted size past which a caller takes no more inlined code">,
  ];
}

#endif // CLANG_CIR_DIALECT_PASSES_TD
