#include "LowerToLLVM.h"
#include "mlir/Dialect/LLVMIR/LLVMAttrs.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Support/LLVM.h"
#include "clang/CIR/Dialect/IR/CIRAttrs.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/ErrorHandling.h"
#include <cstdint>

namespace cir {
namespace direct {

class CIRToLLVMTBAAAttrLowering {
public:
  CIRToLLVMTBAAAttrLowering(mlir::MLIRContext *mlirContext, bool isCPlusPlus,
                             bool newStructPathTBAA)
      : mlirContext(mlirContext), isCPlusPlus(isCPlusPlus),
        newStructPathTBAA(newStructPathTBAA) {}

  mlir::LLVM::TBAATypeDescriptorAttr
  lowerCIRTBAAAttrToLLVMTBAAAttr(mlir::Attribute tbaa) {
    if (auto charAttr = mlir::dyn_cast<cir::TBAAOmnipotentCharAttr>(tbaa)) {
      return getChar();
    }
    if (auto vptrAttr = mlir::dyn_cast<cir::TBAAVTablePointerAttr>(tbaa)) {
      return createScalarTypeNode("vtable pointer", getRoot(),
                                  vptrAttr.getSize());
    }
    if (auto scalarAttr = mlir::dyn_cast<cir::TBAAScalarAttr>(tbaa)) {
      mlir::LLVM::TBAANodeAttr parent =
          scalarAttr.getParent()
              ? lowerCIRTBAAAttrToLLVMTBAAAttr(scalarAttr.getParent())
              : getChar();
      return createScalarTypeNode(scalarAttr.getId(), parent,
                                  scalarAttr.getSize());
    }
    if (auto structAttr = mlir::dyn_cast<cir::TBAAStructAttr>(tbaa)) {
      llvm::SmallVector<mlir::LLVM::TBAAMemberAttr, 4> members;
      for (const auto &member : structAttr.getMembers()) {
        auto memberTypeDesc =
            lowerCIRTBAAAttrToLLVMTBAAAttr(member.getTypeDesc());
        auto memberAttr = mlir::LLVM::TBAAMemberAttr::get(
            mlirContext, memberTypeDesc, member.getOffset(), member.getSize());
        members.push_back(memberAttr);
      }
      if (newStructPathTBAA) {
        return mlir::LLVM::TBAATypeDescriptorAttr::get(
            mlirContext, structAttr.getId(), members,
            /*parent=*/getChar(), structAttr.getSize());
      }
      return mlir::LLVM::TBAATypeDescriptorAttr::get(
          mlirContext, structAttr.getId(), members,
          /*parent=*/nullptr, /*size=*/0);
    }
    return nullptr;
  }

private:
  mlir::LLVM::TBAARootAttr getRoot() {
    if (isCPlusPlus)
      return createTBAARoot("Simple C++ TBAA");

    return createTBAARoot("Simple C/C++ TBAA");
  }

  mlir::LLVM::TBAATypeDescriptorAttr getChar() {
    return createScalarTypeNode("omnipotent char", getRoot(), 1);
  }

  mlir::LLVM::TBAATypeDescriptorAttr
  createScalarTypeNode(llvm::StringRef typeName,
                       mlir::LLVM::TBAANodeAttr parent, int64_t size) {
    if (newStructPathTBAA) {
      // New struct-path format: no members, parent and size on the descriptor.
      llvm::SmallVector<mlir::LLVM::TBAAMemberAttr, 0> members;
      return mlir::LLVM::TBAATypeDescriptorAttr::get(
          mlirContext, typeName, members, /*parent=*/parent, size);
    }
    // Old format: one member with (parent, offset=0).
    llvm::SmallVector<mlir::LLVM::TBAAMemberAttr, 2> members;
    members.push_back(mlir::LLVM::TBAAMemberAttr::get(parent, 0));
    return mlir::LLVM::TBAATypeDescriptorAttr::get(
        mlirContext, typeName,
        llvm::ArrayRef<mlir::LLVM::TBAAMemberAttr>(members),
        /*parent=*/nullptr, /*size=*/0);
  }

  mlir::LLVM::TBAARootAttr createTBAARoot(llvm::StringRef name) {
    return mlir::LLVM::TBAARootAttr::get(
        mlirContext, mlir::StringAttr::get(mlirContext, name));
  }

  mlir::MLIRContext *mlirContext;
  bool isCPlusPlus;
  bool newStructPathTBAA;
};

mlir::LLVM::TBAATypeDescriptorAttr
lowerCIRTBAATypeDescAttr(mlir::Attribute tbaa, mlir::MLIRContext *ctx,
                         bool isCPlusPlus) {
  CIRToLLVMTBAAAttrLowering lower(ctx, isCPlusPlus,
                                   /*newStructPathTBAA=*/false);
  return lower.lowerCIRTBAAAttrToLLVMTBAAAttr(tbaa);
}

mlir::ArrayAttr lowerCIRTBAAAttr(mlir::Attribute tbaa,
                                 mlir::ConversionPatternRewriter &rewriter,
                                 cir::LowerModule *lowerMod) {
  auto *ctx = rewriter.getContext();
  // Some generic DirectToLLVM patterns run without a LowerModule, but TBAA
  // only needs the source-language module attribute.
  mlir::ModuleOp module =
      lowerMod ? lowerMod->getModule() : mlir::ModuleOp{};
  if (!module) {
    if (mlir::Block *block = rewriter.getInsertionBlock())
      module = block->getParentOp()->getParentOfType<mlir::ModuleOp>();
  }
  if (!module)
    return mlir::ArrayAttr();

  auto langAttr = module->getAttrOfType<cir::SourceLanguageAttr>(
      cir::CIRDialect::getSourceLanguageAttrName());
  if (!langAttr)
    return mlir::ArrayAttr();

  bool newStructPathTBAA =
      module->hasAttr(cir::CIRDialect::getNewStructPathTBAAAttrName());

  CIRToLLVMTBAAAttrLowering lower(ctx, langAttr.isCXX(), newStructPathTBAA);
  if (auto tbaaTag = mlir::dyn_cast<cir::TBAATagAttr>(tbaa)) {
    mlir::LLVM::TBAATypeDescriptorAttr accessType =
        lower.lowerCIRTBAAAttrToLLVMTBAAAttr(tbaaTag.getAccess());
    if (auto structAttr =
            mlir::dyn_cast<cir::TBAAStructAttr>(tbaaTag.getBase())) {
      auto baseType = lower.lowerCIRTBAAAttrToLLVMTBAAAttr(structAttr);
      if (newStructPathTBAA) {
        auto tag = mlir::LLVM::TBAATagAttr::get(
            baseType.getContext(), baseType, accessType, tbaaTag.getOffset(),
            /*constant=*/false, tbaaTag.getSize());
        return mlir::ArrayAttr::get(ctx, {tag});
      }
      auto tag = mlir::LLVM::TBAATagAttr::get(baseType, accessType,
                                              tbaaTag.getOffset());
      return mlir::ArrayAttr::get(ctx, {tag});
    }
    // Handle scalar base types (e.g., TBAAScalarAttr). When the base is not
    // a struct, lower it as a scalar type descriptor and create a tag with
    // it. Without this, all TBAA metadata for scalar loads/stores is silently
    // dropped, preventing LLVM from performing type-based alias analysis.
    auto baseType =
        lower.lowerCIRTBAAAttrToLLVMTBAAAttr(tbaaTag.getBase());
    if (baseType && accessType) {
      if (newStructPathTBAA) {
        auto tag = mlir::LLVM::TBAATagAttr::get(
            baseType.getContext(), baseType, accessType, tbaaTag.getOffset(),
            /*constant=*/false, tbaaTag.getSize());
        return mlir::ArrayAttr::get(ctx, {tag});
      }
      auto tag = mlir::LLVM::TBAATagAttr::get(baseType, accessType,
                                              tbaaTag.getOffset());
      return mlir::ArrayAttr::get(ctx, {tag});
    }
  } else {
    auto accessType = lower.lowerCIRTBAAAttrToLLVMTBAAAttr(tbaa);
    if (accessType) {
      auto tag = mlir::LLVM::TBAATagAttr::get(accessType, accessType, 0);
      return mlir::ArrayAttr::get(ctx, {tag});
    }
  }
  return mlir::ArrayAttr();
}

} // namespace direct
} // namespace cir
