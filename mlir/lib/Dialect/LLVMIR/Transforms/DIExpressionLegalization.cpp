//===- DIExpressionLegalization.cpp - DIExpression Legalization Patterns --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/LLVMIR/Transforms/DIExpressionLegalization.h"

#include "llvm/BinaryFormat/Dwarf.h"

using namespace mlir;
using namespace LLVM;

//===----------------------------------------------------------------------===//
// MergeFragments
//===----------------------------------------------------------------------===//

MergeFragments::OpIterT MergeFragments::match(OpIterRange operators) const {
  OpIterT it = operators.begin();
  if (it == operators.end() ||
      it->getOpcode() != llvm::dwarf::DW_OP_LLVM_fragment)
    return operators.begin();

  ++it;
  if (it == operators.end() ||
      it->getOpcode() != llvm::dwarf::DW_OP_LLVM_fragment)
    return operators.begin();

  return ++it;
}

SmallVector<MergeFragments::OperatorT>
MergeFragments::replace(OpIterRange operators) const {
  OpIterT it = operators.begin();
  OperatorT first = *(it++);
  OperatorT second = *it;
  // Add offsets & select the size of the earlier operator (the one closer to
  // the IR value).
  uint64_t offset = first.getArguments()[0] + second.getArguments()[0];
  uint64_t size = first.getArguments()[1];
  OperatorT newOp = OperatorT::get(
      first.getContext(), llvm::dwarf::DW_OP_LLVM_fragment, {offset, size});
  return SmallVector<OperatorT>{newOp};
}

//===----------------------------------------------------------------------===//
// Runner
//===----------------------------------------------------------------------===//

void mlir::LLVM::legalizeDIExpressionsRecursively(Operation *op) {
  LLVM::DIExpressionRewriter rewriter;
  rewriter.addPattern(std::make_unique<MergeFragments>());

  AttrTypeReplacer replacer;
  replacer.addReplacement([&rewriter](LLVM::DIExpressionAttr expr) {
    return rewriter.simplify(expr);
  });
  // Each attribute is replaced on its own: `recursivelyReplaceElementsIn`
  // replaces `Operation::getAttrDictionary()`, which for an op that keeps
  // inherent attributes in properties is a new dictionary of all of them that
  // the context keeps for good, one per distinct op.
  op->walk([&](Operation *nested) {
    NamedAttrList attrs(nested->getRawDictionaryAttrs());
    nested->getName().populateInherentAttrs(nested, attrs);
    for (NamedAttribute attr : attrs) {
      Attribute replaced = replacer.replace(attr.getValue());
      if (replaced && replaced != attr.getValue())
        nested->setAttr(attr.getName(), replaced);
    }
  });
}
