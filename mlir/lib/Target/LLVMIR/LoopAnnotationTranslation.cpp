//===- LoopAnnotationTranslation.cpp - Loop annotation export -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LoopAnnotationTranslation.h"
#include "llvm/IR/DebugInfoMetadata.h"

using namespace mlir;
using namespace mlir::LLVM;
using namespace mlir::LLVM::detail;

namespace {
/// Helper class that keeps the state of one attribute to metadata conversion.
struct LoopAnnotationConversion {
  LoopAnnotationConversion(LoopAnnotationAttr attr, Operation *op,
                           LoopAnnotationTranslation &loopAnnotationTranslation,
                           llvm::LLVMContext &ctx,
                           DenseMap<Attribute, llvm::MDNode *> &cache)
      : attr(attr), op(op),
        loopAnnotationTranslation(loopAnnotationTranslation), ctx(ctx),
        cache(cache) {}

  /// Converts this struct's loop annotation into a corresponding LLVMIR
  /// metadata representation.
  llvm::MDNode *convert();

  /// Conversion functions for different payload attribute kinds.
  void addUnitNode(StringRef name);
  void addUnitNode(StringRef name, BoolAttr attr);
  void addI32NodeWithVal(StringRef name, uint32_t val);
  void convertBoolNode(StringRef name, BoolAttr attr, bool negated = false);
  void convertI32Node(StringRef name, IntegerAttr attr);
  void convertFollowupNode(StringRef name, LoopAnnotationAttr attr);
  void convertLocation(LocationAttr attr);

  /// Conversion functions for each for each loop annotation sub-attribute.
  void convertLoopOptions(LoopVectorizeAttr options);
  void convertLoopOptions(LoopInterleaveAttr options);
  void convertLoopOptions(LoopUnrollAttr options);
  void convertLoopOptions(LoopUnrollAndJamAttr options);
  void convertLoopOptions(LoopLICMAttr options);
  void convertLoopOptions(LoopDistributeAttr options);
  void convertLoopOptions(LoopPipelineAttr options);
  void convertLoopOptions(LoopPeeledAttr options);
  void convertLoopOptions(LoopUnswitchAttr options);

  LoopAnnotationAttr attr;
  Operation *op;
  LoopAnnotationTranslation &loopAnnotationTranslation;
  llvm::LLVMContext &ctx;
  llvm::SmallVector<llvm::Metadata *> metadataNodes;
  DenseMap<Attribute, llvm::MDNode *> &cache;
};
} // namespace

void LoopAnnotationConversion::addUnitNode(StringRef name) {
  metadataNodes.push_back(
      llvm::MDNode::get(ctx, {llvm::MDString::get(ctx, name)}));
}

void LoopAnnotationConversion::addUnitNode(StringRef name, BoolAttr attr) {
  if (attr && attr.getValue())
    addUnitNode(name);
}

void LoopAnnotationConversion::addI32NodeWithVal(StringRef name, uint32_t val) {
  llvm::Constant *cstValue = llvm::ConstantInt::get(
      llvm::IntegerType::get(ctx, /*NumBits=*/32), val, /*isSigned=*/false);
  metadataNodes.push_back(
      llvm::MDNode::get(ctx, {llvm::MDString::get(ctx, name),
                              llvm::ConstantAsMetadata::get(cstValue)}));
}

void LoopAnnotationConversion::convertBoolNode(StringRef name, BoolAttr attr,
                                               bool negated) {
  if (!attr)
    return;
  bool val = negated ^ attr.getValue();
  llvm::Constant *cstValue = llvm::ConstantInt::getBool(ctx, val);
  metadataNodes.push_back(
      llvm::MDNode::get(ctx, {llvm::MDString::get(ctx, name),
                              llvm::ConstantAsMetadata::get(cstValue)}));
}

void LoopAnnotationConversion::convertI32Node(StringRef name,
                                              IntegerAttr attr) {
  if (!attr)
    return;
  addI32NodeWithVal(name, attr.getInt());
}

void LoopAnnotationConversion::convertFollowupNode(StringRef name,
                                                   LoopAnnotationAttr attr) {
  if (!attr)
    return;

  // Convert the followup annotation to a flat metadata node matching clang's
  // CGLoopInfo::createFollowupMetadata: !{name, entry1, entry2, ...} instead
  // of !{name, !distinctLoopID}. This avoids creating unnecessary distinct
  // loop ID nodes for simple followup annotations.
  LoopAnnotationConversion followupConversion(attr, op,
                                              loopAnnotationTranslation, ctx,
                                              cache);
  auto followupEntries = followupConversion.convert();

  // Build flat node: skip the self-referencing first operand (operand 0) of
  // the followup loop ID since followup_all entries are not self-referential.
  SmallVector<llvm::Metadata *> args;
  args.push_back(llvm::MDString::get(ctx, name));
  for (unsigned i = 1; i < followupEntries->getNumOperands(); ++i)
    args.push_back(followupEntries->getOperand(i));

  metadataNodes.push_back(llvm::MDNode::get(ctx, args));
}

void LoopAnnotationConversion::convertLoopOptions(LoopVectorizeAttr options) {
  convertBoolNode("llvm.loop.vectorize.enable", options.getDisable(), true);
  convertBoolNode("llvm.loop.vectorize.predicate.enable",
                  options.getPredicateEnable());
  convertBoolNode("llvm.loop.vectorize.scalable.enable",
                  options.getScalableEnable());
  convertI32Node("llvm.loop.vectorize.width", options.getWidth());
  convertFollowupNode("llvm.loop.vectorize.followup_vectorized",
                      options.getFollowupVectorized());
  convertFollowupNode("llvm.loop.vectorize.followup_epilogue",
                      options.getFollowupEpilogue());
  convertFollowupNode("llvm.loop.vectorize.followup_all",
                      options.getFollowupAll());
}

void LoopAnnotationConversion::convertLoopOptions(LoopInterleaveAttr options) {
  convertI32Node("llvm.loop.interleave.count", options.getCount());
}

void LoopAnnotationConversion::convertLoopOptions(LoopUnrollAttr options) {
  if (auto disable = options.getDisable())
    addUnitNode(disable.getValue() ? "llvm.loop.unroll.disable"
                                   : "llvm.loop.unroll.enable");
  convertI32Node("llvm.loop.unroll.count", options.getCount());
  convertBoolNode("llvm.loop.unroll.runtime.disable",
                  options.getRuntimeDisable());
  addUnitNode("llvm.loop.unroll.full", options.getFull());
  convertFollowupNode("llvm.loop.unroll.followup_unrolled",
                      options.getFollowupUnrolled());
  convertFollowupNode("llvm.loop.unroll.followup_remainder",
                      options.getFollowupRemainder());
  convertFollowupNode("llvm.loop.unroll.followup_all",
                      options.getFollowupAll());
}

void LoopAnnotationConversion::convertLoopOptions(
    LoopUnrollAndJamAttr options) {
  if (auto disable = options.getDisable())
    addUnitNode(disable.getValue() ? "llvm.loop.unroll_and_jam.disable"
                                   : "llvm.loop.unroll_and_jam.enable");
  convertI32Node("llvm.loop.unroll_and_jam.count", options.getCount());
  convertFollowupNode("llvm.loop.unroll_and_jam.followup_outer",
                      options.getFollowupOuter());
  convertFollowupNode("llvm.loop.unroll_and_jam.followup_inner",
                      options.getFollowupInner());
  convertFollowupNode("llvm.loop.unroll_and_jam.followup_remainder_outer",
                      options.getFollowupRemainderOuter());
  convertFollowupNode("llvm.loop.unroll_and_jam.followup_remainder_inner",
                      options.getFollowupRemainderInner());
  convertFollowupNode("llvm.loop.unroll_and_jam.followup_all",
                      options.getFollowupAll());
}

void LoopAnnotationConversion::convertLoopOptions(LoopLICMAttr options) {
  addUnitNode("llvm.licm.disable", options.getDisable());
  addUnitNode("llvm.loop.licm_versioning.disable",
              options.getVersioningDisable());
}

void LoopAnnotationConversion::convertLoopOptions(LoopDistributeAttr options) {
  convertBoolNode("llvm.loop.distribute.enable", options.getDisable(), true);
  convertFollowupNode("llvm.loop.distribute.followup_coincident",
                      options.getFollowupCoincident());
  convertFollowupNode("llvm.loop.distribute.followup_sequential",
                      options.getFollowupSequential());
  convertFollowupNode("llvm.loop.distribute.followup_fallback",
                      options.getFollowupFallback());
  convertFollowupNode("llvm.loop.distribute.followup_all",
                      options.getFollowupAll());
}

void LoopAnnotationConversion::convertLoopOptions(LoopPipelineAttr options) {
  convertBoolNode("llvm.loop.pipeline.disable", options.getDisable());
  convertI32Node("llvm.loop.pipeline.initiationinterval",
                 options.getInitiationinterval());
}

void LoopAnnotationConversion::convertLoopOptions(LoopPeeledAttr options) {
  convertI32Node("llvm.loop.peeled.count", options.getCount());
}

void LoopAnnotationConversion::convertLoopOptions(LoopUnswitchAttr options) {
  addUnitNode("llvm.loop.unswitch.partial.disable",
              options.getPartialDisable());
  addUnitNode("llvm.loop.unswitch.injection.disable",
              options.getInjectionDisable());
}

void LoopAnnotationConversion::convertLocation(LocationAttr location) {
  // The scope lives on the FusedLoc; for an inlined location that is the
  // callee side of the CallSiteLoc.
  auto fusedLoc = dyn_cast<FusedLoc>(location);
  if (auto callSiteLoc = dyn_cast<CallSiteLoc>(location))
    fusedLoc = dyn_cast<FusedLoc>(callSiteLoc.getCallee());
  DILocalScopeAttr localScopeAttr;
  if (fusedLoc)
    localScopeAttr = dyn_cast_or_null<DILocalScopeAttr>(fusedLoc.getMetadata());
  if (!localScopeAttr)
    if (auto func = op->getParentOfType<LLVMFuncOp>())
      if (auto subprogramLoc =
              func.getLoc()->findInstanceOf<FusedLocWith<DISubprogramAttr>>())
        localScopeAttr = subprogramLoc.getMetadata();
  if (!localScopeAttr)
    return;
  auto *localScope = dyn_cast<llvm::DILocalScope>(
      loopAnnotationTranslation.moduleTranslation.translateDebugInfo(
          localScopeAttr));
  if (!localScope)
    return;
  llvm::Metadata *loc =
      loopAnnotationTranslation.moduleTranslation.translateLoc(location,
                                                               localScope);
  metadataNodes.push_back(loc);
}

llvm::MDNode *LoopAnnotationConversion::convert() {
  // Reserve operand 0 for loop id self reference.
  auto dummy = llvm::MDNode::getTemporary(ctx, {});
  metadataNodes.push_back(dummy.get());

  if (LocationAttr startLoc = attr.getStartLoc())
    convertLocation(startLoc);

  if (LocationAttr endLoc = attr.getEndLoc())
    convertLocation(endLoc);

  addUnitNode("llvm.loop.disable_nonforced", attr.getDisableNonforced());
  addUnitNode("llvm.loop.mustprogress", attr.getMustProgress());
  // "isvectorized" is a unit node (no value), matching clang's emission.
  addUnitNode("llvm.loop.isvectorized", attr.getIsVectorized());
  convertI32Node("llvm.loop.align", attr.getCodeAlign());

  if (auto options = attr.getVectorize())
    convertLoopOptions(options);
  if (auto options = attr.getInterleave())
    convertLoopOptions(options);
  if (auto options = attr.getUnroll())
    convertLoopOptions(options);
  if (auto options = attr.getUnrollAndJam())
    convertLoopOptions(options);
  if (auto options = attr.getLicm())
    convertLoopOptions(options);
  if (auto options = attr.getDistribute())
    convertLoopOptions(options);
  if (auto options = attr.getPipeline())
    convertLoopOptions(options);
  if (auto options = attr.getPeeled())
    convertLoopOptions(options);
  if (auto options = attr.getUnswitch())
    convertLoopOptions(options);

  ArrayRef<AccessGroupAttr> parallelAccessGroups = attr.getParallelAccesses();
  if (!parallelAccessGroups.empty()) {
    SmallVector<llvm::Metadata *> parallelAccess;
    parallelAccess.push_back(
        llvm::MDString::get(ctx, "llvm.loop.parallel_accesses"));
    for (AccessGroupAttr accessGroupAttr : parallelAccessGroups)
      parallelAccess.push_back(
          loopAnnotationTranslation.getAccessGroup(accessGroupAttr));
    metadataNodes.push_back(llvm::MDNode::get(ctx, parallelAccess));
  }

  // Create loop options and set the first operand to itself.
  llvm::MDNode *loopMD = llvm::MDNode::get(ctx, metadataNodes);
  loopMD->replaceOperandWith(0, loopMD);

  return loopMD;
}

llvm::MDNode *
LoopAnnotationTranslation::translateLoopAnnotation(LoopAnnotationAttr attr,
                                                   Operation *op) {
  if (!attr)
    return nullptr;

  // The module-level map, not a cache created per top-level call. A loop
  // annotation carries an identity of its own, so two of them are equal only
  // when they describe the same loop and one attribute has to come out as one
  // `!llvm.loop` node. A per-call cache gave every carrier of the same loop --
  // the latch branch, and each access naming it in
  // `!llvm.mem.parallel_loop_access` -- a node of its own, and consumers check
  // `is_contained(operands, loopID)`, for which an equal-but-separate node is
  // no use.
  return translateLoopAnnotation(attr, op, loopMetadataMapping);
}

llvm::MDNode *LoopAnnotationTranslation::translateLoopAnnotation(
    LoopAnnotationAttr attr, Operation *op,
    DenseMap<Attribute, llvm::MDNode *> &cache) {
  if (!attr)
    return nullptr;

  // Check the per-invocation cache for followup annotations.
  llvm::MDNode *loopMD = cache.lookup(attr);
  if (loopMD)
    return loopMD;

  loopMD =
      LoopAnnotationConversion(attr, op, *this, this->llvmModule.getContext(),
                               cache)
          .convert();
  cache.try_emplace(attr, loopMD);
  return loopMD;
}

llvm::MDNode *
LoopAnnotationTranslation::getAccessGroup(AccessGroupAttr accessGroupAttr) {
  auto [result, inserted] =
      accessGroupMetadataMapping.try_emplace(accessGroupAttr);
  if (inserted)
    result->second = llvm::MDNode::getDistinct(llvmModule.getContext(), {});
  return result->second;
}

llvm::MDNode *
LoopAnnotationTranslation::getAccessGroups(AccessGroupOpInterface op) {
  ArrayAttr accessGroups = op.getAccessGroupsOrNull();
  if (!accessGroups || accessGroups.empty())
    return nullptr;

  SmallVector<llvm::Metadata *> groupMDs;
  for (AccessGroupAttr group : accessGroups.getAsRange<AccessGroupAttr>())
    groupMDs.push_back(getAccessGroup(group));
  if (groupMDs.size() == 1)
    return llvm::cast<llvm::MDNode>(groupMDs.front());
  return llvm::MDNode::get(llvmModule.getContext(), groupMDs);
}
