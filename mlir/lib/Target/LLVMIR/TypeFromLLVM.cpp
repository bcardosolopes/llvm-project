//===- TypeFromLLVM.cpp - type translation from LLVM to MLIR IR -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/Target/LLVMIR/TypeFromLLVM.h"
#include "mlir/Dialect/LLVMIR/LLVMTypes.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/MLIRContext.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Type.h"

using namespace mlir;

namespace mlir {
namespace LLVM {
namespace detail {
/// Support for translating LLVM IR types to MLIR LLVM dialect types.
class TypeFromLLVMIRTranslatorImpl {
public:
  /// Constructs a class creating types in the given MLIR context.
  TypeFromLLVMIRTranslatorImpl(MLIRContext &context,
                               bool importStructsAsLiterals)
      : context(context), importStructsAsLiterals(importStructsAsLiterals) {}

  /// Translates the given type.
  Type translateType(llvm::Type *type) {
    if (knownTranslations.count(type))
      return knownTranslations.lookup(type);

    Type translated =
        llvm::TypeSwitch<llvm::Type *, Type>(type)
            .Case<llvm::ArrayType, llvm::FunctionType, llvm::IntegerType,
                  llvm::PointerType, llvm::StructType, llvm::FixedVectorType,
                  llvm::ScalableVectorType, llvm::TargetExtType>(
                [this](auto *type) { return this->translate(type); })
            .Default([this](llvm::Type *type) {
              return translatePrimitiveType(type);
            });
    knownTranslations.try_emplace(type, translated);
    return translated;
  }

private:
  /// Translates the given primitive, i.e. non-parametric in MLIR nomenclature,
  /// type.
  Type translatePrimitiveType(llvm::Type *type) {
    if (type->isVoidTy())
      return LLVM::LLVMVoidType::get(&context);
    if (type->isHalfTy())
      return Float16Type::get(&context);
    if (type->isBFloatTy())
      return BFloat16Type::get(&context);
    if (type->isFloatTy())
      return Float32Type::get(&context);
    if (type->isDoubleTy())
      return Float64Type::get(&context);
    if (type->isFP128Ty())
      return Float128Type::get(&context);
    if (type->isX86_FP80Ty())
      return Float80Type::get(&context);
    if (type->isX86_AMXTy())
      return LLVM::LLVMX86AMXType::get(&context);
    if (type->isPPC_FP128Ty())
      return LLVM::LLVMPPCFP128Type::get(&context);
    if (type->isLabelTy())
      return LLVM::LLVMLabelType::get(&context);
    if (type->isMetadataTy())
      return LLVM::LLVMMetadataType::get(&context);
    if (type->isTokenTy())
      return LLVM::LLVMTokenType::get(&context);
    llvm_unreachable("not a primitive type");
  }

  /// Translates the given array type.
  Type translate(llvm::ArrayType *type) {
    return LLVM::LLVMArrayType::get(translateType(type->getElementType()),
                                    type->getNumElements());
  }

  /// Translates the given function type.
  Type translate(llvm::FunctionType *type) {
    SmallVector<Type, 8> paramTypes;
    translateTypes(type->params(), paramTypes);
    return LLVM::LLVMFunctionType::get(translateType(type->getReturnType()),
                                       paramTypes, type->isVarArg());
  }

  /// Translates the given integer type.
  Type translate(llvm::IntegerType *type) {
    return IntegerType::get(&context, type->getBitWidth());
  }

  /// Translates the given pointer type.
  Type translate(llvm::PointerType *type) {
    return LLVM::LLVMPointerType::get(&context, type->getAddressSpace());
  }

  /// Translates the given structure type.
  Type translate(llvm::StructType *type) {
    SmallVector<Type, 8> subtypes;
    if (type->isLiteral() || importStructsAsLiterals) {
      translateTypes(type->subtypes(), subtypes);
      return LLVM::LLVMStructType::getLiteral(&context, subtypes,
                                              type->isPacked());
    }

    // An opaque type imports as the type of its name that is already in the
    // context, or as a new opaque type if there is none. A body that an
    // earlier suffixed type of this module put under its name belongs to a
    // different LLVM type, so it stays opaque, under a new name: giving it that
    // body would make an external global of the type dereferenceable.
    if (type->isOpaque()) {
      StringRef name = type->getName();
      if (!bodiedFromSuffix.contains(name))
        return LLVM::LLVMStructType::getOpaque(name, &context);
      for (unsigned i = 0;; ++i) {
        std::string candidate = (name + "." + Twine(i)).str();
        if (!LLVM::LLVMStructType::getIdentified(&context, candidate)
                 .isInitialized())
          return LLVM::LLVMStructType::getOpaque(candidate, &context);
      }
    }

    // With opaque pointers, types in LLVM can't be recursive anymore. LLVM
    // keeps two types of one name apart with a numeric suffix, so a type is
    // imported under its name without that suffix, reusing a type of that name
    // and body that an earlier type put in the context. Types that one LLVM
    // context kept apart, or that came from two LLVM contexts, thus become
    // one. A type of another body keeps its LLVM name if that name is free in
    // the context or holds the same body, and otherwise gets a new numeric
    // suffix.
    translateTypes(type->subtypes(), subtypes);
    StringRef name = type->getName();
    StringRef unsuffixed = name;
    if (size_t dot = name.rfind('.');
        dot != StringRef::npos && dot + 1 < name.size() &&
        llvm::all_of(name.drop_front(dot + 1), llvm::isDigit))
      unsuffixed = name.take_front(dot);
    LLVM::LLVMStructType translated =
        LLVM::LLVMStructType::getIdentified(&context, unsuffixed);
    if (succeeded(translated.setBody(subtypes, type->isPacked()))) {
      if (unsuffixed != name)
        bodiedFromSuffix.insert(unsuffixed);
    } else {
      translated = LLVM::LLVMStructType::getIdentified(&context, name);
      if (failed(translated.setBody(subtypes, type->isPacked())))
        translated = LLVM::LLVMStructType::getNewIdentified(
            &context, name, subtypes, type->isPacked());
    }
    knownTranslations.try_emplace(type, translated);
    return translated;
  }

  /// Translates the given fixed-vector type.
  Type translate(llvm::FixedVectorType *type) {
    return VectorType::get(type->getNumElements(),
                           translateType(type->getElementType()));
  }

  /// Translates the given scalable-vector type.
  Type translate(llvm::ScalableVectorType *type) {
    return VectorType::get(type->getMinNumElements(),
                           translateType(type->getElementType()),
                           /*scalableDims=*/true);
  }

  /// Translates the given target extension type.
  Type translate(llvm::TargetExtType *type) {
    SmallVector<Type> typeParams;
    translateTypes(type->type_params(), typeParams);

    return LLVM::LLVMTargetExtType::get(&context, type->getName(), typeParams,
                                        type->int_params());
  }

  /// Translates a list of types.
  void translateTypes(ArrayRef<llvm::Type *> types,
                      SmallVectorImpl<Type> &result) {
    result.reserve(result.size() + types.size());
    for (llvm::Type *type : types)
      result.push_back(translateType(type));
  }

  /// Map of known translations. Serves as a cache and as recursion stopper for
  /// translating recursive structs.
  llvm::DenseMap<llvm::Type *, Type> knownTranslations;

  /// The names of this import whose body a suffixed LLVM type supplied.
  llvm::StringSet<> bodiedFromSuffix;

  /// The context in which MLIR types are created.
  MLIRContext &context;

  /// Controls if structs should be imported as literal structs, i.e., nameless
  /// structs.
  bool importStructsAsLiterals;
};

} // namespace detail
} // namespace LLVM
} // namespace mlir

LLVM::TypeFromLLVMIRTranslator::TypeFromLLVMIRTranslator(
    MLIRContext &context, bool importStructsAsLiterals)
    : impl(std::make_unique<detail::TypeFromLLVMIRTranslatorImpl>(
          context, importStructsAsLiterals)) {}

LLVM::TypeFromLLVMIRTranslator::~TypeFromLLVMIRTranslator() = default;

Type LLVM::TypeFromLLVMIRTranslator::translateType(llvm::Type *type) {
  return impl->translateType(type);
}
