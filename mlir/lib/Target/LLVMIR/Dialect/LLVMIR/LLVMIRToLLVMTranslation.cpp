//===- LLVMIRToLLVMTranslation.cpp - Translate LLVM IR to LLVM dialect ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a translation between LLVM IR and the MLIR LLVM dialect.
//
//===----------------------------------------------------------------------===//

#include "mlir/Target/LLVMIR/Dialect/LLVMIR/LLVMIRToLLVMTranslation.h"
#include "mlir/Dialect/LLVMIR/LLVMAttrs.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/LLVMIR/LLVMInterfaces.h"
#include "mlir/Support/LLVM.h"
#include "mlir/Target/LLVMIR/ModuleImport.h"

#include "llvm/ADT/TypeSwitch.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/MemoryModelRelaxationAnnotations.h"

using namespace mlir;
using namespace mlir::LLVM;
using namespace mlir::LLVM::detail;

#include "mlir/Dialect/LLVMIR/LLVMConversionEnumsFromLLVM.inc"

// Neither `srcloc` nor `heapallocsite` is a fixed metadata kind; both are
// registered by name.
static constexpr StringLiteral srclocMDName = "srcloc";
static constexpr StringLiteral heapAllocSiteMDName = "heapallocsite";
static constexpr StringLiteral noObjCARCExceptionsMDName =
    "clang.arc.no_objc_arc_exceptions";

static constexpr StringLiteral vecTypeHintMDName = "vec_type_hint";
static constexpr StringLiteral workGroupSizeHintMDName = "work_group_size_hint";
static constexpr StringLiteral reqdWorkGroupSizeMDName = "reqd_work_group_size";
static constexpr StringLiteral intelReqdSubGroupSizeMDName =
    "intel_reqd_sub_group_size";
static constexpr StringLiteral pgoFuncNameMDName = "PGOFuncName";

/// Returns true if the LLVM IR intrinsic is convertible to an MLIR LLVM dialect
/// intrinsic. Returns false otherwise.
static bool isConvertibleIntrinsic(llvm::Intrinsic::ID id) {
  static const DenseSet<unsigned> convertibleIntrinsics = {
#include "mlir/Dialect/LLVMIR/LLVMConvertibleLLVMIRIntrinsics.inc"
  };
  return convertibleIntrinsics.contains(id);
}

/// Returns the list of LLVM IR intrinsic identifiers that are convertible to
/// MLIR LLVM dialect intrinsics.
static ArrayRef<unsigned> getSupportedIntrinsicsImpl() {
  static const SmallVector<unsigned> convertibleIntrinsics = {
#include "mlir/Dialect/LLVMIR/LLVMConvertibleLLVMIRIntrinsics.inc"
  };
  return convertibleIntrinsics;
}

/// Converts the LLVM intrinsic to an MLIR LLVM dialect operation if a
/// conversion exits. Returns failure otherwise.
static LogicalResult convertIntrinsicImpl(OpBuilder &odsBuilder,
                                          llvm::CallInst *inst,
                                          LLVM::ModuleImport &moduleImport) {
  llvm::Intrinsic::ID intrinsicID = inst->getIntrinsicID();

  // Check if the intrinsic is convertible to an MLIR dialect counterpart and
  // copy the arguments to an an LLVM operands array reference for conversion.
  if (isConvertibleIntrinsic(intrinsicID)) {
    SmallVector<llvm::Value *> args(inst->args());
    ArrayRef<llvm::Value *> llvmOperands(args);

    SmallVector<llvm::OperandBundleUse> llvmOpBundles;
    llvmOpBundles.reserve(inst->getNumOperandBundles());
    for (unsigned i = 0; i < inst->getNumOperandBundles(); ++i)
      llvmOpBundles.push_back(inst->getOperandBundleAt(i));

#include "mlir/Dialect/LLVMIR/LLVMIntrinsicFromLLVMIRConversions.inc"
  }

  return failure();
}

/// Returns the list of LLVM IR metadata kinds that are convertible to MLIR LLVM
/// dialect attributes.
static SmallVector<unsigned>
getSupportedMetadataImpl(llvm::LLVMContext &llvmContext) {
  SmallVector<unsigned> convertibleMetadata = {
      llvm::LLVMContext::MD_prof,
      llvm::LLVMContext::MD_tbaa,
      llvm::LLVMContext::MD_access_group,
      llvm::LLVMContext::MD_loop,
      llvm::LLVMContext::MD_noalias,
      llvm::LLVMContext::MD_alias_scope,
      llvm::LLVMContext::MD_dereferenceable,
      llvm::LLVMContext::MD_dereferenceable_or_null,
      llvm::LLVMContext::MD_nonnull,
      llvm::LLVMContext::MD_callees,
      llvm::LLVMContext::MD_nosanitize,
      llvm::LLVMContext::MD_memprof,
      llvm::LLVMContext::MD_fpmath,
      llvm::LLVMContext::MD_align,
      llvm::LLVMContext::MD_noalias_addrspace,
      llvm::LLVMContext::MD_make_implicit,
      llvm::LLVMContext::MD_coro_outside_frame,
      llvm::LLVMContext::MD_func_sanitize,
      llvm::LLVMContext::MD_mmra,
      llvm::LLVMContext::MD_pcsections,
      llvm::LLVMContext::MD_callback,
      llvm::LLVMContext::MD_mem_parallel_loop_access,
      llvm::LLVMContext::MD_DIAssignID,
      llvm::LLVMContext::MD_preserve_access_index,
      llvm::LLVMContext::MD_callsite,
      llvm::LLVMContext::MD_irr_loop,
      llvm::LLVMContext::MD_exclude,
      llvm::LLVMContext::MD_vcall_visibility,
      llvm::LLVMContext::MD_absolute_symbol,
      llvm::LLVMContext::MD_associated,
      llvm::LLVMContext::MD_section_prefix,
      llvm::LLVMContext::MD_kcfi_type,
      llvm::LLVMContext::MD_tbaa_struct,
      llvm::LLVMContext::MD_annotation,
      llvm::LLVMContext::MD_unpredictable,
      llvmContext.getMDKindID(srclocMDName),
      llvmContext.getMDKindID(heapAllocSiteMDName),
      llvmContext.getMDKindID(noObjCARCExceptionsMDName),
      llvmContext.getMDKindID(vecTypeHintMDName),
      llvmContext.getMDKindID(workGroupSizeHintMDName),
      llvmContext.getMDKindID(reqdWorkGroupSizeMDName),
      llvmContext.getMDKindID(intelReqdSubGroupSizeMDName),
      llvmContext.getMDKindID(pgoFuncNameMDName)};
  return convertibleMetadata;
}

/// Converts the given profiling metadata `node` to an MLIR profiling attribute
/// and attaches it to the imported operation if the translation succeeds.
/// Returns failure otherwise.
static LogicalResult setProfilingAttr(OpBuilder &builder, llvm::MDNode *node,
                                      Operation *op,
                                      LLVM::ModuleImport &moduleImport) {
  // Return failure for empty metadata nodes since there is nothing to import.
  if (!node->getNumOperands())
    return failure();

  auto *name = dyn_cast<llvm::MDString>(node->getOperand(0));
  if (!name)
    return failure();

  // Handle function entry count metadata.
  if (name->getString() == llvm::MDProfLabels::FunctionEntryCount) {

    // TODO support function entry count metadata with GUID fields.
    if (node->getNumOperands() != 2)
      return failure();

    llvm::ConstantInt *entryCount =
        llvm::mdconst::dyn_extract<llvm::ConstantInt>(node->getOperand(1));
    if (!entryCount)
      return failure();
    if (auto funcOp = dyn_cast<LLVMFuncOp>(op)) {
      funcOp.setFunctionEntryCount(entryCount->getZExtValue());
      return success();
    }
    return op->emitWarning()
           << "expected function_entry_count to be attached to a function";
  }

  // Handle value profile metadata: `!{!"VP", i32 kind, i64 total, (i64 value,
  // i64 count)...}`. The full-LTO link's indirect call promotion and memory
  // intrinsic size specialization read it.
  if (name->getString() == "VP") {
    if (node->getNumOperands() < 3 || node->getNumOperands() % 2 == 0)
      return failure();
    SmallVector<int64_t> values;
    values.reserve(node->getNumOperands() - 1);
    for (unsigned i = 1, e = node->getNumOperands(); i != e; ++i) {
      auto *value =
          llvm::mdconst::dyn_extract<llvm::ConstantInt>(node->getOperand(i));
      if (!value || value->getBitWidth() > 64)
        return failure();
      values.push_back(static_cast<int64_t>(value->getZExtValue()));
    }
    op->setAttr(LLVMDialect::getValueProfileAttrName(),
                builder.getDenseI64ArrayAttr(values));
    return success();
  }

  if (name->getString() != llvm::MDProfLabels::BranchWeights)
    return failure();
  // The branch_weights metadata must have at least 2 operands.
  if (node->getNumOperands() < 2)
    return failure();

  ArrayRef<llvm::MDOperand> branchWeightOperands =
      node->operands().drop_front();
  if (auto *mdString = dyn_cast<llvm::MDString>(node->getOperand(1))) {
    if (mdString->getString() != llvm::MDProfLabels::ExpectedBranchWeights)
      return failure();
    // The MLIR WeightedBranchOpInterface does not support the
    // ExpectedBranchWeights field, so it is dropped.
    branchWeightOperands = branchWeightOperands.drop_front();
  }

  // Handle branch weights metadata.
  SmallVector<int32_t> branchWeights;
  branchWeights.reserve(branchWeightOperands.size());
  for (const llvm::MDOperand &operand : branchWeightOperands) {
    llvm::ConstantInt *branchWeight =
        llvm::mdconst::dyn_extract<llvm::ConstantInt>(operand);
    if (!branchWeight)
      return failure();
    branchWeights.push_back(branchWeight->getZExtValue());
  }

  if (auto iface = dyn_cast<WeightedBranchOpInterface>(op)) {
    // LLVM allows attaching a single weight to call instructions.
    // This is used for carrying the execution count information
    // in PGO modes. MLIR WeightedBranchOpInterface does not allow this,
    // so we drop the metadata in this case.
    // LLVM should probably use the VP form of MD_prof metadata
    // for such cases.
    if (op->getNumSuccessors() != 0)
      iface.setWeights(branchWeights);
    return success();
  }
  return failure();
}

/// Searches for the attribute that maps to the given TBAA metadata `node` and
/// attaches it to the imported operation if the lookup succeeds. Returns
/// failure otherwise.
static LogicalResult setTBAAAttr(const llvm::MDNode *node, Operation *op,
                                 LLVM::ModuleImport &moduleImport) {
  Attribute tbaaTagSym = moduleImport.lookupTBAAAttr(node);
  if (!tbaaTagSym)
    return failure();

  auto iface = dyn_cast<AliasAnalysisOpInterface>(op);
  if (!iface)
    return failure();

  iface.setTBAATags(ArrayAttr::get(iface.getContext(), tbaaTagSym));
  return success();
}

/// Looks up all the access group attributes that map to the access group nodes
/// starting from the access group metadata `node`, and attaches all of them to
/// the imported operation if the lookups succeed. Returns failure otherwise.
static LogicalResult setAccessGroupsAttr(const llvm::MDNode *node,
                                         Operation *op,
                                         LLVM::ModuleImport &moduleImport) {
  FailureOr<SmallVector<AccessGroupAttr>> accessGroups =
      moduleImport.lookupAccessGroupAttrs(node);
  if (failed(accessGroups))
    return failure();

  auto iface = dyn_cast<AccessGroupOpInterface>(op);
  if (!iface)
    return failure();

  iface.setAccessGroups(ArrayAttr::get(
      iface.getContext(), llvm::to_vector_of<Attribute>(*accessGroups)));
  return success();
}

/// Converts the given dereferenceable metadata node to a dereferenceable
/// attribute, and attaches it to the imported operation if the translation
/// succeeds. Returns failure if the LLVM IR metadata node is ill-formed.
static LogicalResult setDereferenceableAttr(const llvm::MDNode *node,
                                            unsigned kindID, Operation *op,
                                            LLVM::ModuleImport &moduleImport) {
  auto dereferenceable =
      moduleImport.translateDereferenceableAttr(node, kindID);
  if (failed(dereferenceable))
    return failure();

  auto iface = dyn_cast<DereferenceableOpInterface>(op);
  if (!iface)
    return failure();

  iface.setDereferenceable(*dereferenceable);
  return success();
}

/// Convert the given MMRA metadata (either an MMRA tag or an array of them)
/// into corresponding MLIR attributes and set them on the given operation as a
/// discardable `llvm.mmra` attribute.
static LogicalResult setMmraAttr(llvm::MDNode *node, Operation *op,
                                 LLVM::ModuleImport &moduleImport) {
  if (!node)
    return success();

  // We don't use the LLVM wrappers here becasue we care about the order
  // of the metadata for deterministic roundtripping.
  MLIRContext *ctx = op->getContext();
  auto toAttribute = [&](llvm::MDNode *tag) -> Attribute {
    return LLVM::MMRATagAttr::get(
        ctx, cast<llvm::MDString>(tag->getOperand(0))->getString(),
        cast<llvm::MDString>(tag->getOperand(1))->getString());
  };
  Attribute mlirMmra;
  if (llvm::MMRAMetadata::isTagMD(node)) {
    mlirMmra = toAttribute(node);
  } else {
    SmallVector<Attribute> tags;
    for (const llvm::MDOperand &operand : node->operands()) {
      auto *tagNode = dyn_cast<llvm::MDNode>(operand.get());
      if (!tagNode || !llvm::MMRAMetadata::isTagMD(tagNode))
        return failure();
      tags.push_back(toAttribute(tagNode));
    }
    mlirMmra = ArrayAttr::get(ctx, tags);
  }
  op->setAttr(LLVMDialect::getMmraAttrName(), mlirMmra);
  return success();
}

/// Marks the imported load as `!nonnull`. Like `!noundef` the node is
/// valueless, and LLVM allows the kind only on a load -- a call says the same
/// thing with a `nonnull` return attribute, which travels the
/// parameter-attribute path.
static LogicalResult setNonnullAttr(Operation *op) {
  auto loadOp = dyn_cast<LLVM::LoadOp>(op);
  if (!loadOp)
    return failure();

  loadOp.setNonnull(true);
  return success();
}

/// Marks the imported operation as `!nosanitize`, which suppresses sanitizer
/// instrumentation of the access. LLVM permits the metadata on *any*
/// instruction -- loads, stores, calls and plain arithmetic all carry it in
/// practice -- so there is no single carrier op to hang a typed attribute off.
/// It rides as a discardable dialect attribute, the way `llvm.type_metadata`
/// already does, and the node is valueless so a unit attribute is the whole
/// content.
static LogicalResult setNoSanitizeAttr(OpBuilder &builder, Operation *op) {
  op->setAttr(LLVMDialect::getNoSanitizeAttrName(), builder.getUnitAttr());
  return success();
}

/// Converts `!llvm.preserve.access.index` to the `llvm.preserve_access_index`
/// attribute. The node is the record `DIType` a BPF CO-RE relocation is taken
/// against, so it is kept as a `DITypeAttr` rather than by name -- the name
/// would go stale, and an anonymous record has none.
static LogicalResult
setPreserveAccessIndexAttr(const llvm::MDNode *node, Operation *op,
                           LLVM::ModuleImport &moduleImport) {
  auto *type =
      dyn_cast_if_present<llvm::DIType>(const_cast<llvm::MDNode *>(node));
  if (!type)
    return failure();
  DITypeAttr attr = moduleImport.translateDebugType(type);
  if (!attr)
    return failure();
  op->setAttr(LLVMDialect::getPreserveAccessIndexAttrName(), attr);
  return success();
}

/// Converts `!llvm.mem.parallel_loop_access` to the
/// `llvm.mem_parallel_loop_access` attribute. The node is either a loop's own
/// identifier or a list of them -- nested parallel loops -- so it is kept as a
/// list of the loop annotations it names.
static LogicalResult
setMemParallelLoopAccessAttr(OpBuilder &builder, const llvm::MDNode *node,
                             Operation *op, LLVM::ModuleImport &moduleImport) {
  auto *loopIds = const_cast<llvm::MDNode *>(node);
  // A loop identifier is self-referential -- `!0 = distinct !{!0, ...}` -- so
  // the single-loop case is a node whose first operand is itself, and the
  // nested case is a plain list of such nodes. Testing that first operand is
  // what tells the two apart.
  SmallVector<llvm::MDNode *> loops;
  if (loopIds->getNumOperands() > 0 &&
      loopIds->getOperand(0).get() == loopIds) {
    loops.push_back(loopIds);
  } else {
    for (const llvm::MDOperand &operand : loopIds->operands()) {
      auto *loop = dyn_cast_if_present<llvm::MDNode>(operand.get());
      if (!loop)
        return failure();
      loops.push_back(loop);
    }
  }
  if (loops.empty())
    return failure();

  SmallVector<Attribute> annotations;
  for (llvm::MDNode *loop : loops) {
    LoopAnnotationAttr attr =
        moduleImport.translateLoopAnnotationAttr(loop, op->getLoc());
    if (!attr)
      return failure();
    annotations.push_back(attr);
  }
  op->setAttr(LLVMDialect::getMemParallelLoopAccessAttrName(),
              builder.getArrayAttr(annotations));
  return success();
}

/// Converts `!DIAssignID` to the `llvm.dbg_assign_id` attribute. Assignment
/// tracking links a store to its `#dbg_assign` by both naming this node, so
/// what has to survive is the identity, not any content -- the node has none.
static LogicalResult setDIAssignIDAttr(const llvm::MDNode *node, Operation *op,
                                       LLVM::ModuleImport &moduleImport) {
  auto *assignID =
      dyn_cast_if_present<llvm::DIAssignID>(const_cast<llvm::MDNode *>(node));
  if (!assignID)
    return failure();
  DIAssignIDAttr attr = moduleImport.translateDIAssignID(assignID);
  if (!attr)
    return failure();
  op->setAttr(LLVMDialect::getDIAssignIDAttrName(), attr);
  return success();
}

/// Converts `!pcsections` to the `llvm.pcsections` attribute. The node is a
/// flat list of section names, each optionally followed by a node of auxiliary
/// constants: `!{!"s1", !{i32 42}, !"s2"}`. Each name is paired with its aux
/// list so the association survives, which a flat list would lose.
static LogicalResult
setPCSectionsAttr(OpBuilder &builder, const llvm::MDNode *node, Operation *op) {
  if (node->getNumOperands() == 0)
    return failure();
  SmallVector<Attribute> sections;
  for (unsigned i = 0, e = node->getNumOperands(); i < e;) {
    auto *name = dyn_cast_if_present<llvm::MDString>(node->getOperand(i));
    if (!name)
      return failure();
    ++i;
    SmallVector<int32_t> aux;
    if (i < e) {
      if (auto *auxNode =
              dyn_cast_if_present<llvm::MDNode>(node->getOperand(i))) {
        for (const llvm::MDOperand &operand : auxNode->operands()) {
          auto *value =
              llvm::mdconst::dyn_extract_or_null<llvm::ConstantInt>(operand);
          if (!value || value->getBitWidth() > 32)
            return failure();
          aux.push_back(static_cast<int32_t>(value->getZExtValue()));
        }
        ++i;
      }
    }
    sections.push_back(
        builder.getArrayAttr({builder.getStringAttr(name->getString()),
                              builder.getDenseI32ArrayAttr(aux)}));
  }
  op->setAttr(LLVMDialect::getPCSectionsAttrName(),
              builder.getArrayAttr(sections));
  return success();
}

/// Converts `!callback` to the `llvm.callback` attribute. Each entry is
/// `!{i64 calleeIdx, i64 payloadIdx..., i1 varargs}` -- the trailing operand is
/// the varargs flag, not another index, so it is kept apart from them.
static LogicalResult setCallbackAttr(OpBuilder &builder,
                                     const llvm::MDNode *node, Operation *op) {
  if (node->getNumOperands() == 0)
    return failure();
  // Function-only: nothing else in LLVM carries `!callback`.
  if (!isa<LLVM::LLVMFuncOp>(op))
    return failure();
  SmallVector<Attribute> encodings;
  for (const llvm::MDOperand &operand : node->operands()) {
    auto *encoding = dyn_cast_if_present<llvm::MDNode>(operand);
    if (!encoding || encoding->getNumOperands() < 2)
      return failure();
    unsigned last = encoding->getNumOperands() - 1;
    SmallVector<int64_t> indices;
    for (unsigned i = 0; i < last; ++i) {
      auto *index = llvm::mdconst::dyn_extract_or_null<llvm::ConstantInt>(
          encoding->getOperand(i));
      if (!index || index->getBitWidth() > 64)
        return failure();
      indices.push_back(static_cast<int64_t>(index->getZExtValue()));
    }
    auto *varargs = llvm::mdconst::dyn_extract_or_null<llvm::ConstantInt>(
        encoding->getOperand(last));
    if (!varargs || varargs->getBitWidth() != 1)
      return failure();
    encodings.push_back(
        builder.getArrayAttr({builder.getDenseI64ArrayAttr(indices),
                              builder.getBoolAttr(varargs->isOne())}));
  }
  op->setAttr(LLVMDialect::getCallbackAttrName(),
              builder.getArrayAttr(encodings));
  return success();
}

/// Converts a valueless node to a unit attribute. `!make.implicit` and
/// `!coro.outside.frame` are both `!{}`: the presence of the metadata is the
/// whole content, the way `!nosanitize` already is.
static LogicalResult setUnitMetadataAttr(OpBuilder &builder, StringRef name,
                                         Operation *op) {
  op->setAttr(name, builder.getUnitAttr());
  return success();
}

/// Converts `!func_sanitize` to the `llvm.func_sanitize` attribute. The node
/// is the two words a function's sanitizer prologue compares against, so both
/// are kept rather than collapsed.
static LogicalResult setFuncSanitizeAttr(OpBuilder &builder,
                                         const llvm::MDNode *node,
                                         Operation *op) {
  if (node->getNumOperands() != 2)
    return failure();
  SmallVector<int32_t, 2> words;
  for (unsigned i = 0; i < 2; ++i) {
    auto *word = llvm::mdconst::dyn_extract_or_null<llvm::ConstantInt>(
        node->getOperand(i));
    if (!word || word->getBitWidth() != 32)
      return failure();
    words.push_back(static_cast<int32_t>(word->getZExtValue()));
  }
  // Function-only, so it goes in a typed slot rather than riding as a
  // discardable attribute: `convertDialectAttributes` hands a function an
  // empty instruction list, so a discardable attribute here would import
  // cleanly and then never be written back out.
  auto funcOp = dyn_cast<LLVM::LLVMFuncOp>(op);
  if (!funcOp)
    return failure();
  funcOp.setFuncSanitizeAttr(builder.getDenseI32ArrayAttr(words));
  return success();
}

/// Converts `!fpmath` to the `llvm.fpmath` attribute. The node is a single
/// `float` giving the ULP accuracy the result may lose. LLVM permits it on any
/// FP-typed instruction, which is far more carriers than the dialect has typed
/// slots for, so it rides as a discardable dialect attribute.
static LogicalResult setFPMathAttr(OpBuilder &builder, const llvm::MDNode *node,
                                   Operation *op) {
  if (node->getNumOperands() != 1)
    return failure();
  auto *accuracy =
      llvm::mdconst::dyn_extract_or_null<llvm::ConstantFP>(node->getOperand(0));
  // The verifier requires IEEE single precision, so anything else would not
  // survive the trip back out.
  if (!accuracy ||
      &accuracy->getValueAPF().getSemantics() != &llvm::APFloat::IEEEsingle())
    return failure();
  op->setAttr(
      LLVMDialect::getFPMathAttrName(),
      builder.getF32FloatAttr(accuracy->getValueAPF().convertToFloat()));
  return success();
}

/// Converts `!align` to the `llvm.align_metadata` attribute. The node is a
/// single `i64`, and the verifier only allows it on a load that produces a
/// pointer.
static LogicalResult setAlignMetadataAttr(OpBuilder &builder,
                                          const llvm::MDNode *node,
                                          Operation *op) {
  if (node->getNumOperands() != 1)
    return failure();
  auto *align = llvm::mdconst::dyn_extract_or_null<llvm::ConstantInt>(
      node->getOperand(0));
  if (!align || align->getBitWidth() != 64)
    return failure();
  op->setAttr(LLVMDialect::getAlignMetadataAttrName(),
              builder.getI64IntegerAttr(align->getZExtValue()));
  return success();
}

/// Converts `!noalias.addrspace` to the `llvm.noalias_addrspace` attribute.
///
/// The node is a range list over address spaces, and the ranges need not be
/// contiguous -- `!{i32 1, i32 2, i32 5, i32 6}` is address space 1 and 5, not
/// 1 through 5. Each range is therefore kept separately rather than being run
/// through `getConstantRangeFromMetadata`, which unions them: that would widen
/// the claim to address spaces the metadata never named, and a wrong noalias
/// claim miscompiles rather than merely losing information.
static LogicalResult setNoaliasAddrspaceAttr(OpBuilder &builder,
                                             const llvm::MDNode *node,
                                             Operation *op) {
  unsigned numOperands = node->getNumOperands();
  if (numOperands == 0 || numOperands % 2 != 0)
    return failure();
  SmallVector<Attribute> ranges;
  for (unsigned i = 0; i < numOperands; i += 2) {
    auto *lower = llvm::mdconst::dyn_extract_or_null<llvm::ConstantInt>(
        node->getOperand(i));
    auto *upper = llvm::mdconst::dyn_extract_or_null<llvm::ConstantInt>(
        node->getOperand(i + 1));
    if (!lower || !upper || lower->getBitWidth() != upper->getBitWidth())
      return failure();
    ranges.push_back(LLVM::ConstantRangeAttr::get(
        op->getContext(), lower->getValue(), upper->getValue()));
  }
  op->setAttr(LLVMDialect::getNoaliasAddrspaceAttrName(),
              builder.getArrayAttr(ranges));
  return success();
}

/// Reads a MemProf call stack: a node of one or more constant integers, each a
/// hash of a source location. Shared by `!callsite`, which is exactly one of
/// these, and by the first operand of every `!memprof` MemInfoBlock.
static DenseI64ArrayAttr getCallStackAttr(OpBuilder &builder,
                                          const llvm::MDNode *node) {
  if (node->getNumOperands() == 0)
    return {};
  SmallVector<int64_t> ids;
  ids.reserve(node->getNumOperands());
  for (const llvm::MDOperand &operand : node->operands()) {
    auto *id = llvm::mdconst::dyn_extract_or_null<llvm::ConstantInt>(operand);
    // The ids are hashes, so the whole 64-bit pattern is meaningful and is
    // kept as-is rather than being range-checked as a signed quantity.
    if (!id || id->getBitWidth() > 64)
      return {};
    ids.push_back(static_cast<int64_t>(id->getZExtValue()));
  }
  return builder.getDenseI64ArrayAttr(ids);
}

/// Converts `!callsite` to the `llvm.callsite` attribute. The node is the
/// partial allocation call stack this call sits on, which is what ties it to a
/// `!memprof` record elsewhere in the profile.
static LogicalResult setCallsiteAttr(OpBuilder &builder,
                                     const llvm::MDNode *node, Operation *op) {
  DenseI64ArrayAttr callStack = getCallStackAttr(builder, node);
  if (!callStack)
    return failure();
  op->setAttr(LLVMDialect::getCallsiteAttrName(), callStack);
  return success();
}

/// Converts `!memprof` to the `llvm.memprof` attribute. The node is a list of
/// MemInfoBlocks; each is an allocation call stack, then one or more
/// allocation-type tag strings, then optionally `(context id, total size)`
/// pairs. The trailing pairs only appear when the profile was gathered with
/// full context-size information, so their absence is not a loss.
static LogicalResult setMemProfAttr(OpBuilder &builder,
                                    const llvm::MDNode *node, Operation *op) {
  if (node->getNumOperands() == 0)
    return failure();
  SmallVector<Attribute> mibs;
  mibs.reserve(node->getNumOperands());
  for (const llvm::MDOperand &mibOperand : node->operands()) {
    auto *mib = dyn_cast_if_present<llvm::MDNode>(mibOperand);
    if (!mib || mib->getNumOperands() < 2)
      return failure();
    auto *stack = dyn_cast_if_present<llvm::MDNode>(mib->getOperand(0));
    if (!stack)
      return failure();
    DenseI64ArrayAttr callStack = getCallStackAttr(builder, stack);
    if (!callStack)
      return failure();

    unsigned index = 1;
    SmallVector<Attribute> tags;
    for (; index < mib->getNumOperands(); ++index) {
      auto *tag = dyn_cast_if_present<llvm::MDString>(mib->getOperand(index));
      if (!tag)
        break;
      tags.push_back(builder.getStringAttr(tag->getString()));
    }
    if (tags.empty())
      return failure();

    SmallVector<Attribute> contextSizes;
    for (; index < mib->getNumOperands(); ++index) {
      auto *pair = dyn_cast_if_present<llvm::MDNode>(mib->getOperand(index));
      if (!pair || pair->getNumOperands() != 2)
        return failure();
      auto *id = llvm::mdconst::dyn_extract_or_null<llvm::ConstantInt>(
          pair->getOperand(0));
      auto *size = llvm::mdconst::dyn_extract_or_null<llvm::ConstantInt>(
          pair->getOperand(1));
      if (!id || !size || id->getBitWidth() > 64 || size->getBitWidth() > 64)
        return failure();
      contextSizes.push_back(MemProfContextSizeAttr::get(
          op->getContext(), id->getZExtValue(), size->getZExtValue()));
    }

    mibs.push_back(MemProfMIBAttr::get(
        op->getContext(), callStack, builder.getArrayAttr(tags),
        contextSizes.empty() ? ArrayAttr()
                             : builder.getArrayAttr(contextSizes)));
  }
  op->setAttr(LLVMDialect::getMemProfAttrName(), builder.getArrayAttr(mibs));
  return success();
}

/// Converts `!tbaa.struct` to the `tbaa_struct` attribute on the imported
/// aggregate copy. The node is a flat list of (offset, size, tag) triples: it
/// says which fields of the aggregate the copy actually touches, so the copy is
/// not treated as one opaque access and the gaps are known to be padding.
static LogicalResult setTBAAStructAttr(OpBuilder &builder,
                                       const llvm::MDNode *node, Operation *op,
                                       LLVM::ModuleImport &moduleImport) {
  // Operands come in conceptual groups of three; anything else is malformed.
  if (node->getNumOperands() == 0 || node->getNumOperands() % 3 != 0)
    return failure();

  SmallVector<Attribute> fields;
  fields.reserve(node->getNumOperands() / 3);
  for (unsigned i = 0, e = node->getNumOperands(); i != e; i += 3) {
    auto *offset = llvm::mdconst::dyn_extract_or_null<llvm::ConstantInt>(
        node->getOperand(i));
    auto *size = llvm::mdconst::dyn_extract_or_null<llvm::ConstantInt>(
        node->getOperand(i + 1));
    if (!offset || !size)
      return failure();
    auto *tagNode = dyn_cast_if_present<llvm::MDNode>(node->getOperand(i + 2));
    if (!tagNode)
      return failure();
    auto tag =
        dyn_cast_if_present<TBAATagAttr>(moduleImport.lookupTBAAAttr(tagNode));
    if (!tag)
      return failure();
    fields.push_back(builder.getAttr<TBAAStructFieldAttr>(
        offset->getSExtValue(), size->getSExtValue(), tag));
  }

  return TypeSwitch<Operation *, LogicalResult>(op)
      .Case<LLVM::MemcpyOp, LLVM::MemcpyInlineOp, LLVM::MemmoveOp>(
          [&](auto copyOp) {
            copyOp.setTbaaStructAttr(builder.getArrayAttr(fields));
            return success();
          })
      .Default([](auto) { return failure(); });
}

/// Marks the imported call or invoke as `!clang.arc.no_objc_arc_exceptions`,
/// which tells the ObjC ARC optimizer it may ignore the unwind edge. The node
/// is valueless -- its presence is the whole content -- and clang attaches it
/// only under ObjC ARC, always to a `CallBase`.
static LogicalResult setNoObjCARCExceptionsAttr(Operation *op) {
  return TypeSwitch<Operation *, LogicalResult>(op)
      .Case<LLVM::CallOp, LLVM::InvokeOp>([](auto callOp) {
        callOp.setNoObjcArcExceptions(true);
        return success();
      })
      .Default([](auto) { return failure(); });
}

/// Converts `!callees` to the `callees` attribute on the imported call or
/// invoke. The node lists the functions an indirect call may reach;
/// ModuleSummaryAnalysis reads it to drive indirect call promotion, which is
/// exactly the kind of cross-library information this pipeline exists to keep.
///
/// Each callee becomes a `FlatSymbolRefAttr` rather than a copied name, so the
/// reference participates in MLIR symbol renaming instead of going stale.
static LogicalResult setCalleesAttr(OpBuilder &builder,
                                    const llvm::MDNode *node, Operation *op) {
  SmallVector<Attribute> callees;
  callees.reserve(node->getNumOperands());
  for (const llvm::MDOperand &operand : node->operands()) {
    // A null operand is what RAUW leaves behind when a listed callee goes away,
    // and LTO does that constantly, so it is the common case rather than a
    // malformed one. Skip it and keep the rest: the node is a hint to indirect
    // call promotion, and a shorter list is still correct.
    auto *value = dyn_cast_if_present<llvm::ValueAsMetadata>(operand.get());
    if (!value)
      continue;
    auto *func = dyn_cast<llvm::Function>(value->getValue());
    if (!func)
      continue;
    callees.push_back(
        FlatSymbolRefAttr::get(op->getContext(), func->getName()));
  }
  if (callees.empty())
    return failure();

  ArrayAttr attr = builder.getArrayAttr(callees);
  return TypeSwitch<Operation *, LogicalResult>(op)
      .Case<LLVM::CallOp, LLVM::InvokeOp>([&](auto callOp) {
        callOp.setCalleesAttr(attr);
        return success();
      })
      .Default([](auto) { return failure(); });
}

/// Converts `!annotation` to the `annotations` attribute on the imported
/// operation. The node is a tuple whose operands are either strings or nested
/// tuples of strings; both spellings are preserved.
static LogicalResult
setAnnotationAttr(OpBuilder &builder, const llvm::MDNode *node, Operation *op) {
  auto iface = dyn_cast<AnnotationOpInterface>(op);
  if (!iface)
    return failure();

  SmallVector<Attribute> entries;
  entries.reserve(node->getNumOperands());
  for (const llvm::MDOperand &operand : node->operands()) {
    if (auto *str = dyn_cast<llvm::MDString>(operand.get())) {
      entries.push_back(builder.getStringAttr(str->getString()));
      continue;
    }
    auto *tuple = dyn_cast<llvm::MDTuple>(operand.get());
    if (!tuple)
      return failure();
    SmallVector<Attribute> nested;
    nested.reserve(tuple->getNumOperands());
    for (const llvm::MDOperand &inner : tuple->operands()) {
      auto *innerStr = dyn_cast<llvm::MDString>(inner.get());
      if (!innerStr)
        return failure();
      nested.push_back(builder.getStringAttr(innerStr->getString()));
    }
    entries.push_back(builder.getArrayAttr(nested));
  }
  if (entries.empty())
    return failure();

  iface.setAnnotations(builder.getArrayAttr(entries));
  return success();
}

/// Converts `!srcloc` to the `srcloc` attribute on `llvm.inline_asm` and
/// `llvm.callbr`. The node is a tuple of i64 cookies, one per line of a
/// multi-line asm string.
static LogicalResult setSrclocAttr(const llvm::MDNode *node, Operation *op) {
  if (!isa<LLVM::InlineAsmOp, LLVM::CallBrOp>(op))
    return failure();

  SmallVector<int64_t> cookies;
  cookies.reserve(node->getNumOperands());
  for (const llvm::MDOperand &operand : node->operands()) {
    // A null operand is legal in an MDNode -- RAUW leaves them behind -- so the
    // read has to tolerate one rather than assert on it. The node is declined
    // whole: a cookie list with a hole in it would misattribute the remaining
    // lines of the asm string.
    auto *cst = dyn_cast_if_present<llvm::ConstantAsMetadata>(operand.get());
    if (!cst)
      return failure();
    auto *value = dyn_cast<llvm::ConstantInt>(cst->getValue());
    if (!value)
      return failure();
    cookies.push_back(value->getSExtValue());
  }
  if (cookies.empty())
    return failure();

  auto attr = DenseI64ArrayAttr::get(op->getContext(), cookies);
  if (auto asmOp = dyn_cast<LLVM::InlineAsmOp>(op))
    asmOp.setSrclocAttr(attr);
  else
    cast<LLVM::CallBrOp>(op).setSrclocAttr(attr);
  return success();
}

/// Converts `!PGOFuncName` to the `llvm.pgo_func_name` attribute. Indirect
/// call promotion in a full-LTO link maps a value profile's target hashes to
/// local functions through it.
static LogicalResult setPGOFuncNameAttr(OpBuilder &builder,
                                        const llvm::MDNode *node,
                                        Operation *op) {
  auto *name = node->getNumOperands() == 1
                   ? dyn_cast_if_present<llvm::MDString>(node->getOperand(0))
                   : nullptr;
  if (!name || !isa<LLVM::LLVMFuncOp>(op))
    return failure();
  op->setAttr(LLVMDialect::getPGOFuncNameAttrName(),
              builder.getStringAttr(name->getString()));
  return success();
}

/// Converts `!heapallocsite` to the `heapallocsite` attribute on the imported
/// call or invoke. The node is the `DIType` of the allocated object, or an
/// empty tuple when clang could not name one -- CodeViewDebug reads it with a
/// `dyn_cast<DIType>`, so the empty node means "allocated type unknown" rather
/// than "no metadata". `DINullTypeAttr` carries that state, which is why the
/// attribute is a `DITypeAttr` rather than a concrete type attribute.
static LogicalResult setHeapAllocSiteAttr(llvm::MDNode *node, Operation *op,
                                          LLVM::ModuleImport &moduleImport) {
  DITypeAttr attr;
  if (node->getNumOperands() == 0) {
    attr = DINullTypeAttr::get(op->getContext());
  } else {
    auto *diType = dyn_cast<llvm::DIType>(node);
    if (!diType)
      return failure();
    attr = moduleImport.translateDebugType(diType);
    if (!attr)
      return failure();
  }

  return TypeSwitch<Operation *, LogicalResult>(op)
      .Case<LLVM::CallOp, LLVM::InvokeOp>([&](auto callOp) {
        callOp.setHeapallocsiteAttr(attr);
        return success();
      })
      .Default([](auto) { return failure(); });
}

/// Marks the imported operation as `!unpredictable`. The node is valueless --
/// its presence is the whole content -- and it tells the backend the branch is
/// not worth predicting, which is what CodeGenPrepare and SelectOptimize read
/// when choosing between a branch and a conditional move.
static LogicalResult setUnpredictableAttr(Operation *op) {
  return TypeSwitch<Operation *, LogicalResult>(op)
      .Case<LLVM::CondBrOp, LLVM::SwitchOp, LLVM::SelectOp>([](auto carrierOp) {
        carrierOp.setUnpredictable(true);
        return success();
      })
      .Default([](auto) { return failure(); });
}

/// Converts `!irr_loop` to the `irr_loop_header_weight` attribute on the
/// imported terminator. The node is `!{!"loop_header_weight", i64 N}` -- the
/// first operand is a fixed tag, so the weight alone is lossless. LLVM reads it
/// off the block's terminator (`BasicBlock::getIrrLoopHeaderWeight`), and
/// `MachineBasicBlock` carries it into block placement, so it is profile data
/// that reaches codegen rather than a hint.
static LogicalResult setIrrLoopAttr(OpBuilder &builder,
                                    const llvm::MDNode *node, Operation *op) {
  if (node->getNumOperands() != 2)
    return failure();
  auto *tag = dyn_cast_if_present<llvm::MDString>(node->getOperand(0));
  if (!tag || tag->getString() != "loop_header_weight")
    return failure();
  auto *weight = llvm::mdconst::dyn_extract_or_null<llvm::ConstantInt>(
      node->getOperand(1));
  if (!weight)
    return failure();

  IntegerAttr attr = builder.getI64IntegerAttr(weight->getZExtValue());
  return TypeSwitch<Operation *, LogicalResult>(op)
      .Case<LLVM::BrOp, LLVM::CondBrOp, LLVM::SwitchOp, LLVM::InvokeOp>(
          [&](auto termOp) {
            termOp.setIrrLoopHeaderWeightAttr(attr);
            return success();
          })
      .Default([](auto) { return failure(); });
}

/// The GlobalObject-level kinds below all attach to a global variable or a
/// function. Neither is special-cased: `setOnGlobalObject` dispatches over both
/// so a carrier LLVM allows is never declined for want of a slot.
template <typename SetterT>
static LogicalResult setOnGlobalObject(Operation *op, SetterT &&setter) {
  return TypeSwitch<Operation *, LogicalResult>(op)
      .Case<LLVM::GlobalOp, LLVM::LLVMFuncOp>([&](auto globalObject) {
        setter(globalObject);
        return success();
      })
      .Default([](auto) { return failure(); });
}

/// `!exclude` keeps a global out of the emitted symbol table. The node is
/// valueless, so its presence is the whole content.
static LogicalResult setExcludeAttr(Operation *op) {
  return setOnGlobalObject(op, [](auto go) { go.setExclude(true); });
}

/// `!vcall_visibility` is what whole-program devirtualization reads to decide
/// whether a vtable can be assumed complete. The node is `!{i64 N}`.
static LogicalResult setVCallVisibilityAttr(OpBuilder &builder,
                                            const llvm::MDNode *node,
                                            Operation *op) {
  if (node->getNumOperands() != 1)
    return failure();
  auto *value = llvm::mdconst::dyn_extract_or_null<llvm::ConstantInt>(
      node->getOperand(0));
  if (!value)
    return failure();
  IntegerAttr attr = builder.getI64IntegerAttr(value->getZExtValue());
  return setOnGlobalObject(op,
                           [&](auto go) { go.setVcallVisibilityAttr(attr); });
}

/// `!kcfi_type` is the type identifier a KCFI check compares against. The node
/// is a single 32-bit constant integer; the width is what the verifier
/// requires, and checking it here keeps a wider operand from being truncated
/// into a type identifier that would no longer match.
static LogicalResult setKCFITypeAttr(OpBuilder &builder,
                                     const llvm::MDNode *node, Operation *op) {
  if (node->getNumOperands() != 1)
    return failure();
  auto *value = llvm::mdconst::dyn_extract_or_null<llvm::ConstantInt>(
      node->getOperand(0));
  if (!value || value->getBitWidth() != 32)
    return failure();
  IntegerAttr attr = builder.getI32IntegerAttr(value->getZExtValue());
  return setOnGlobalObject(op, [&](auto go) { go.setKcfiTypeAttr(attr); });
}

/// `!absolute_symbol` gives the range a symbol's address is known to lie in.
/// Read it the way `GlobalValue::getAbsoluteSymbolRange` does, so the
/// full-set spelling LLVM allows here is handled by the same code.
/// `getConstantRangeFromMetadata` asserts rather than declining on a node that
/// is not a sequence of same-width integer pairs, so the shape is checked
/// before it is handed over: a malformed node is reported as dropped, not a
/// crash.
static LogicalResult setAbsoluteSymbolAttr(const llvm::MDNode *node,
                                           Operation *op) {
  unsigned numOperands = node->getNumOperands();
  if (numOperands == 0 || numOperands % 2 != 0)
    return failure();
  unsigned bitWidth = 0;
  for (unsigned i = 0; i < numOperands; ++i) {
    auto *bound = llvm::mdconst::dyn_extract_or_null<llvm::ConstantInt>(
        node->getOperand(i));
    if (!bound)
      return failure();
    // `ConstantRange` pairs the bounds, and `unionWith` pairs the ranges, so a
    // single mismatched width is enough to trip an assertion downstream.
    if (i == 0)
      bitWidth = bound->getBitWidth();
    else if (bound->getBitWidth() != bitWidth)
      return failure();
  }
  llvm::ConstantRange range =
      llvm::getConstantRangeFromMetadata(*const_cast<llvm::MDNode *>(node));
  auto attr = LLVM::ConstantRangeAttr::get(op->getContext(), range.getLower(),
                                           range.getUpper());
  return setOnGlobalObject(op,
                           [&](auto go) { go.setAbsoluteSymbolAttr(attr); });
}

/// `!associated` ties a global's liveness to another symbol so the linker keeps
/// or drops them together. The operand becomes a `FlatSymbolRefAttr` rather
/// than a copied name, for the same reason `!callees` does: a string would go
/// stale under `NamespaceSymbolsPass`.
static LogicalResult setAssociatedAttr(const llvm::MDNode *node,
                                       Operation *op) {
  if (node->getNumOperands() != 1)
    return failure();
  auto *value = dyn_cast_if_present<llvm::ValueAsMetadata>(node->getOperand(0));
  if (!value)
    return failure();
  auto *global = dyn_cast<llvm::GlobalValue>(value->getValue());
  if (!global)
    return failure();
  auto attr = FlatSymbolRefAttr::get(op->getContext(), global->getName());
  return setOnGlobalObject(op, [&](auto go) { go.setAssociatedAttr(attr); });
}

/// `!section_prefix` drives hot/cold section placement. The node is
/// `!{!"section_prefix", !"<prefix>"}`, or the older
/// `!{!"function_section_prefix", !"<prefix>"}` that LLVM still accepts on a
/// function only. LLVM reads both tags alike, so the prefix alone is enough;
/// export writes `section_prefix`.
static LogicalResult setSectionPrefixAttr(OpBuilder &builder,
                                          const llvm::MDNode *node,
                                          Operation *op) {
  if (node->getNumOperands() != 2)
    return failure();
  auto *tag = dyn_cast_if_present<llvm::MDString>(node->getOperand(0));
  auto *prefix = dyn_cast_if_present<llvm::MDString>(node->getOperand(1));
  if (!tag || !prefix)
    return failure();
  if (tag->getString() != "section_prefix" &&
      !(isa<LLVM::LLVMFuncOp>(op) &&
        tag->getString() == "function_section_prefix"))
    return failure();
  StringAttr attr = builder.getStringAttr(prefix->getString());
  return setOnGlobalObject(op, [&](auto go) { go.setSectionPrefixAttr(attr); });
}

/// Converts the given loop metadata node to an MLIR loop annotation attribute
/// and attaches it to the imported operation if the translation succeeds.
/// Returns failure otherwise.
static LogicalResult setLoopAttr(const llvm::MDNode *node, Operation *op,
                                 LLVM::ModuleImport &moduleImport) {
  LoopAnnotationAttr attr =
      moduleImport.translateLoopAnnotationAttr(node, op->getLoc());
  if (!attr)
    return failure();

  // A loop latch is usually a conditional branch, but it can be a switch --
  // a `do`/`while` around a switch-based state machine produces one -- or an
  // invoke, when the last thing a loop body does is a call that can unwind and
  // the normal destination branches back. Those four have a typed slot.
  //
  // They are not the whole story: LLVM permits `!llvm.loop` on *any*
  // instruction, and real IR has it on plain calls -- SimplifyCFG turning an
  // invoke latch into a call leaves the node behind on a non-terminator.
  // Anything without a typed slot keeps the same LoopAnnotationAttr as a
  // discardable dialect attribute so that no carrier can silently drop it.
  return TypeSwitch<Operation *, LogicalResult>(op)
      .Case<LLVM::BrOp, LLVM::CondBrOp, LLVM::SwitchOp, LLVM::InvokeOp>(
          [&](auto branchOp) {
            branchOp.setLoopAnnotationAttr(attr);
            return success();
          })
      .Default([&](Operation *other) {
        other->setAttr(LLVMDialect::getLoopAnnotationAttrName(), attr);
        return success();
      });
}

/// Looks up all the alias scope attributes that map to the alias scope nodes
/// starting from the alias scope metadata `node`, and attaches all of them to
/// the imported operation if the lookups succeed. Returns failure otherwise.
static LogicalResult setAliasScopesAttr(const llvm::MDNode *node, Operation *op,
                                        LLVM::ModuleImport &moduleImport) {
  FailureOr<SmallVector<AliasScopeAttr>> aliasScopes =
      moduleImport.lookupAliasScopeAttrs(node);
  if (failed(aliasScopes))
    return failure();

  auto iface = dyn_cast<AliasAnalysisOpInterface>(op);
  if (!iface)
    return failure();

  iface.setAliasScopes(ArrayAttr::get(
      iface.getContext(), llvm::to_vector_of<Attribute>(*aliasScopes)));
  return success();
}

/// Looks up all the alias scope attributes that map to the alias scope nodes
/// starting from the noalias metadata `node`, and attaches all of them to the
/// imported operation if the lookups succeed. Returns failure otherwise.
static LogicalResult setNoaliasScopesAttr(const llvm::MDNode *node,
                                          Operation *op,
                                          LLVM::ModuleImport &moduleImport) {
  FailureOr<SmallVector<AliasScopeAttr>> noAliasScopes =
      moduleImport.lookupAliasScopeAttrs(node);
  if (failed(noAliasScopes))
    return failure();

  auto iface = dyn_cast<AliasAnalysisOpInterface>(op);
  if (!iface)
    return failure();

  iface.setNoAliasScopes(ArrayAttr::get(
      iface.getContext(), llvm::to_vector_of<Attribute>(*noAliasScopes)));
  return success();
}

/// Extracts an integer from the provided metadata `md` if possible. Returns
/// nullopt otherwise.
static std::optional<int32_t> parseIntegerMD(llvm::Metadata *md) {
  auto *constant = dyn_cast_if_present<llvm::ConstantAsMetadata>(md);
  if (!constant)
    return {};

  auto *intConstant = dyn_cast<llvm::ConstantInt>(constant->getValue());
  if (!intConstant)
    return {};

  return intConstant->getValue().getSExtValue();
}

/// Converts the provided metadata node `node` to an LLVM dialect
/// VecTypeHintAttr if possible.
static VecTypeHintAttr convertVecTypeHint(Builder builder, llvm::MDNode *node,
                                          ModuleImport &moduleImport) {
  if (!node || node->getNumOperands() != 2)
    return {};

  auto *hintMD = dyn_cast<llvm::ValueAsMetadata>(node->getOperand(0).get());
  if (!hintMD)
    return {};
  TypeAttr hint = TypeAttr::get(moduleImport.convertType(hintMD->getType()));

  std::optional<int32_t> optIsSigned =
      parseIntegerMD(node->getOperand(1).get());
  if (!optIsSigned)
    return {};
  bool isSigned = *optIsSigned != 0;

  return builder.getAttr<VecTypeHintAttr>(hint, isSigned);
}

/// Converts the provided metadata node `node` to an MLIR DenseI32ArrayAttr if
/// possible.
static DenseI32ArrayAttr convertDenseI32Array(Builder builder,
                                              llvm::MDNode *node) {
  if (!node)
    return {};
  SmallVector<int32_t> vals;
  for (const llvm::MDOperand &op : node->operands()) {
    std::optional<int32_t> mdValue = parseIntegerMD(op.get());
    if (!mdValue)
      return {};
    vals.push_back(*mdValue);
  }
  return builder.getDenseI32ArrayAttr(vals);
}

/// Convert an `MDNode` to an MLIR `IntegerAttr` if possible.
static IntegerAttr convertIntegerMD(Builder builder, llvm::MDNode *node) {
  if (!node || node->getNumOperands() != 1)
    return {};
  std::optional<int32_t> val = parseIntegerMD(node->getOperand(0));
  if (!val)
    return {};
  return builder.getI32IntegerAttr(*val);
}

static LogicalResult setVecTypeHintAttr(Builder &builder, llvm::MDNode *node,
                                        Operation *op,
                                        LLVM::ModuleImport &moduleImport) {
  auto funcOp = dyn_cast<LLVM::LLVMFuncOp>(op);
  if (!funcOp)
    return failure();

  VecTypeHintAttr attr = convertVecTypeHint(builder, node, moduleImport);
  if (!attr)
    return failure();

  funcOp.setVecTypeHintAttr(attr);
  return success();
}

static LogicalResult
setWorkGroupSizeHintAttr(Builder &builder, llvm::MDNode *node, Operation *op) {
  auto funcOp = dyn_cast<LLVM::LLVMFuncOp>(op);
  if (!funcOp)
    return failure();

  DenseI32ArrayAttr attr = convertDenseI32Array(builder, node);
  if (!attr)
    return failure();

  funcOp.setWorkGroupSizeHintAttr(attr);
  return success();
}

static LogicalResult
setReqdWorkGroupSizeAttr(Builder &builder, llvm::MDNode *node, Operation *op) {
  auto funcOp = dyn_cast<LLVM::LLVMFuncOp>(op);
  if (!funcOp)
    return failure();

  DenseI32ArrayAttr attr = convertDenseI32Array(builder, node);
  if (!attr)
    return failure();

  funcOp.setReqdWorkGroupSizeAttr(attr);
  return success();
}

/// Converts the given intel required subgroup size metadata node to an MLIR
/// attribute and attaches it to the imported operation if the translation
/// succeeds. Returns failure otherwise.
static LogicalResult setIntelReqdSubGroupSizeAttr(Builder &builder,
                                                  llvm::MDNode *node,
                                                  Operation *op) {
  auto funcOp = dyn_cast<LLVM::LLVMFuncOp>(op);
  if (!funcOp)
    return failure();

  IntegerAttr attr = convertIntegerMD(builder, node);
  if (!attr)
    return failure();

  funcOp.setIntelReqdSubGroupSizeAttr(attr);
  return success();
}

namespace {

/// Implementation of the dialect interface that converts operations belonging
/// to the LLVM dialect to LLVM IR.
class LLVMDialectLLVMIRImportInterface : public LLVMImportDialectInterface {
public:
  using LLVMImportDialectInterface::LLVMImportDialectInterface;

  /// Converts the LLVM intrinsic to an MLIR LLVM dialect operation if a
  /// conversion exits. Returns failure otherwise.
  LogicalResult convertIntrinsic(OpBuilder &builder, llvm::CallInst *inst,
                                 LLVM::ModuleImport &moduleImport) const final {
    return convertIntrinsicImpl(builder, inst, moduleImport);
  }

  /// Attaches the given LLVM metadata to the imported operation if a conversion
  /// to an LLVM dialect attribute exists and succeeds. Returns failure
  /// otherwise.
  LogicalResult setMetadataAttrs(OpBuilder &builder, unsigned kind,
                                 llvm::MDNode *node, Operation *op,
                                 LLVM::ModuleImport &moduleImport) const final {
    // Call metadata specific handlers.
    if (kind == llvm::LLVMContext::MD_prof)
      return setProfilingAttr(builder, node, op, moduleImport);
    if (kind == llvm::LLVMContext::MD_tbaa)
      return setTBAAAttr(node, op, moduleImport);
    if (kind == llvm::LLVMContext::MD_access_group)
      return setAccessGroupsAttr(node, op, moduleImport);
    if (kind == llvm::LLVMContext::MD_loop)
      return setLoopAttr(node, op, moduleImport);
    if (kind == llvm::LLVMContext::MD_alias_scope)
      return setAliasScopesAttr(node, op, moduleImport);
    if (kind == llvm::LLVMContext::MD_noalias)
      return setNoaliasScopesAttr(node, op, moduleImport);
    if (kind == llvm::LLVMContext::MD_dereferenceable)
      return setDereferenceableAttr(node, llvm::LLVMContext::MD_dereferenceable,
                                    op, moduleImport);
    if (kind == llvm::LLVMContext::MD_dereferenceable_or_null)
      return setDereferenceableAttr(
          node, llvm::LLVMContext::MD_dereferenceable_or_null, op,
          moduleImport);
    if (kind == llvm::LLVMContext::MD_mmra)
      return setMmraAttr(node, op, moduleImport);
    if (kind == llvm::LLVMContext::MD_nonnull)
      return setNonnullAttr(op);
    if (kind == llvm::LLVMContext::MD_callees)
      return setCalleesAttr(builder, node, op);
    if (kind == llvm::LLVMContext::MD_exclude)
      return setExcludeAttr(op);
    if (kind == llvm::LLVMContext::MD_vcall_visibility)
      return setVCallVisibilityAttr(builder, node, op);
    if (kind == llvm::LLVMContext::MD_kcfi_type)
      return setKCFITypeAttr(builder, node, op);
    if (kind == llvm::LLVMContext::MD_absolute_symbol)
      return setAbsoluteSymbolAttr(node, op);
    if (kind == llvm::LLVMContext::MD_associated)
      return setAssociatedAttr(node, op);
    if (kind == llvm::LLVMContext::MD_section_prefix)
      return setSectionPrefixAttr(builder, node, op);
    if (kind == llvm::LLVMContext::MD_irr_loop)
      return setIrrLoopAttr(builder, node, op);
    if (kind == llvm::LLVMContext::MD_nosanitize)
      return setNoSanitizeAttr(builder, op);
    if (kind == llvm::LLVMContext::MD_memprof)
      return setMemProfAttr(builder, node, op);
    if (kind == llvm::LLVMContext::MD_fpmath)
      return setFPMathAttr(builder, node, op);
    if (kind == llvm::LLVMContext::MD_align)
      return setAlignMetadataAttr(builder, node, op);
    if (kind == llvm::LLVMContext::MD_noalias_addrspace)
      return setNoaliasAddrspaceAttr(builder, node, op);
    if (kind == llvm::LLVMContext::MD_make_implicit)
      return setUnitMetadataAttr(builder,
                                 LLVMDialect::getMakeImplicitAttrName(), op);
    if (kind == llvm::LLVMContext::MD_coro_outside_frame)
      return setUnitMetadataAttr(
          builder, LLVMDialect::getCoroOutsideFrameAttrName(), op);
    if (kind == llvm::LLVMContext::MD_func_sanitize)
      return setFuncSanitizeAttr(builder, node, op);
    if (kind == llvm::LLVMContext::MD_pcsections)
      return setPCSectionsAttr(builder, node, op);
    if (kind == llvm::LLVMContext::MD_callback)
      return setCallbackAttr(builder, node, op);
    if (kind == llvm::LLVMContext::MD_preserve_access_index)
      return setPreserveAccessIndexAttr(node, op, moduleImport);
    if (kind == llvm::LLVMContext::MD_DIAssignID)
      return setDIAssignIDAttr(node, op, moduleImport);
    if (kind == llvm::LLVMContext::MD_mem_parallel_loop_access)
      return setMemParallelLoopAccessAttr(builder, node, op, moduleImport);
    if (kind == llvm::LLVMContext::MD_callsite)
      return setCallsiteAttr(builder, node, op);
    if (kind == llvm::LLVMContext::MD_tbaa_struct)
      return setTBAAStructAttr(builder, node, op, moduleImport);
    if (kind == llvm::LLVMContext::MD_annotation)
      return setAnnotationAttr(builder, node, op);
    if (kind == llvm::LLVMContext::MD_unpredictable)
      return setUnpredictableAttr(op);

    llvm::LLVMContext &context = node->getContext();
    if (kind == context.getMDKindID(srclocMDName))
      return setSrclocAttr(node, op);
    if (kind == context.getMDKindID(noObjCARCExceptionsMDName))
      return setNoObjCARCExceptionsAttr(op);
    if (kind == context.getMDKindID(heapAllocSiteMDName))
      return setHeapAllocSiteAttr(node, op, moduleImport);
    if (kind == context.getMDKindID(vecTypeHintMDName))
      return setVecTypeHintAttr(builder, node, op, moduleImport);
    if (kind == context.getMDKindID(workGroupSizeHintMDName))
      return setWorkGroupSizeHintAttr(builder, node, op);
    if (kind == context.getMDKindID(reqdWorkGroupSizeMDName))
      return setReqdWorkGroupSizeAttr(builder, node, op);
    if (kind == context.getMDKindID(intelReqdSubGroupSizeMDName))
      return setIntelReqdSubGroupSizeAttr(builder, node, op);
    if (kind == context.getMDKindID(pgoFuncNameMDName))
      return setPGOFuncNameAttr(builder, node, op);

    // A handler for a supported metadata kind is missing.
    llvm_unreachable("unknown metadata type");
  }

  /// Returns the list of LLVM IR intrinsic identifiers that are convertible to
  /// MLIR LLVM dialect intrinsics.
  ArrayRef<unsigned> getSupportedIntrinsics() const final {
    return getSupportedIntrinsicsImpl();
  }

  /// Returns the list of LLVM IR metadata kinds that are convertible to MLIR
  /// LLVM dialect attributes.
  SmallVector<unsigned>
  getSupportedMetadata(llvm::LLVMContext &llvmContext) const final {
    return getSupportedMetadataImpl(llvmContext);
  }
};
} // namespace

void mlir::registerLLVMDialectImport(DialectRegistry &registry) {
  registry.insert<LLVM::LLVMDialect>();
  registry.addExtension(+[](MLIRContext *ctx, LLVM::LLVMDialect *dialect) {
    dialect->addInterfaces<LLVMDialectLLVMIRImportInterface>();
  });
}

void mlir::registerLLVMDialectImport(MLIRContext &context) {
  DialectRegistry registry;
  registerLLVMDialectImport(registry);
  context.appendDialectRegistry(registry);
}
