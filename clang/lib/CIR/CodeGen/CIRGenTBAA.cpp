#include "CIRGenTBAA.h"
#include "CIRGenCXXABI.h"
#include "CIRGenTypes.h"
#include "TargetInfo.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Interfaces/DataLayoutInterfaces.h"
#include "mlir/Support/LLVM.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/Type.h"
#include "clang/CIR/Dialect/IR/CIRAttrs.h"
#include "clang/CIR/Dialect/IR/CIRTypes.h"
#include "clang/CIR/MissingFeatures.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
namespace clang::CIRGen {

cir::TBAAAttr tbaa_NYI(mlir::MLIRContext *mlirContext) {
  return cir::TBAAAttr();
}

CIRGenTBAA::CIRGenTBAA(mlir::MLIRContext *mlirContext,
                       clang::ASTContext &astContext, CIRGenTypes &types,
                       mlir::ModuleOp moduleOp,
                       const clang::CodeGenOptions &codeGenOpts,
                       const clang::LangOptions &features)
    : mlirContext(mlirContext), astContext(astContext), types(types),
      moduleOp(moduleOp), codeGenOpts(codeGenOpts), features(features) {}

cir::TBAAAttr CIRGenTBAA::getChar() {
  return cir::TBAAOmnipotentCharAttr::get(mlirContext);
}

static bool typeHasMayAlias(clang::QualType qty) {
  // Tagged types have declarations, and therefore may have attributes.
  if (auto *td = qty->getAsTagDecl())
    if (td->hasAttr<MayAliasAttr>())
      return true;

  // Check array element types for may_alias (matches OGCG).
  if (auto *arrayTy = qty->getAsArrayTypeUnsafe())
    if (typeHasMayAlias(arrayTy->getElementType()))
      return true;

  // Also look for may_alias as a declaration attribute on a typedef.
  // FIXME: We should follow GCC and model may_alias as a type attribute
  // rather than as a declaration attribute.
  while (auto *tt = qty->getAs<TypedefType>()) {
    if (tt->getDecl()->hasAttr<MayAliasAttr>())
      return true;
    qty = tt->desugar();
  }
  return false;
}

/// Check if the given type is a valid base type to be used in access tags.
static bool isValidBaseType(clang::QualType qty) {
  if (const clang::RecordType *tty = qty->getAs<clang::RecordType>()) {
    const clang::RecordDecl *rd = tty->getDecl()->getDefinition();
    // Incomplete types are not valid base access types.
    if (!rd)
      return false;
    if (rd->hasFlexibleArrayMember())
      return false;
    // rd can be struct, union, class, interface or enum.
    // For now, we only handle struct and class.
    if (rd->isStruct() || rd->isClass())
      return true;
  }
  return false;
}

cir::TBAAScalarAttr CIRGenTBAA::getScalarTypeInfo(clang::QualType qty) {
  const clang::Type *ty = astContext.getCanonicalType(qty).getTypePtr();
  assert(mlir::isa<clang::BuiltinType>(ty));
  const clang::BuiltinType *bty = mlir::dyn_cast<BuiltinType>(ty);
  uint64_t size = codeGenOpts.NewStructPathTBAA
                      ? astContext.getTypeSizeInChars(qty).getQuantity()
                      : 0;
  return cir::TBAAScalarAttr::get(mlirContext, bty->getName(features),
                                  types.convertType(qty), /*parent=*/nullptr,
                                  size);
}

cir::TBAAScalarAttr CIRGenTBAA::getAnyPtr(unsigned ptrDepth) {
  assert(ptrDepth >= 1 && "Pointer must have some depth");
  // Populate at least ptrDepth elements in anyPtrs. These are the type nodes
  // for "any" pointers of increasing pointer depth, organized:
  //   any pointer <- any p2 pointer <- any p3 pointer <- ...
  // Note that anyPtrs[idx] is the node for pointer depth (idx+1).
  if (anyPtrs.size() < ptrDepth) {
    anyPtrs.reserve(ptrDepth);
    uint64_t ptrSize = codeGenOpts.NewStructPathTBAA
                           ? astContext.getTypeSizeInChars(
                                 astContext.VoidPtrTy).getQuantity()
                           : 0;
    // Use a generic pointer type for the "any pointer" nodes.
    auto ptrTy = cir::PointerType::get(mlirContext, cir::VoidType::get(mlirContext));
    // Populate first element if needed.
    if (anyPtrs.empty())
      anyPtrs.push_back(cir::TBAAScalarAttr::get(
          mlirContext, "any pointer", ptrTy, /*parent=*/nullptr, ptrSize));
    // Populate further elements.
    for (size_t idx = anyPtrs.size(); idx < ptrDepth; ++idx) {
      auto name = ("any p" + llvm::Twine(idx + 1) + " pointer").str();
      anyPtrs.push_back(cir::TBAAScalarAttr::get(
          mlirContext, name, ptrTy, anyPtrs[idx - 1], ptrSize));
    }
  }
  return anyPtrs[ptrDepth - 1];
}

cir::TBAAAttr CIRGenTBAA::getTypeInfoHelper(clang::QualType qty) {
  const clang::Type *ty = astContext.getCanonicalType(qty).getTypePtr();
  // Handle builtin types.
  if (const clang::BuiltinType *bty = mlir::dyn_cast<BuiltinType>(ty)) {
    switch (bty->getKind()) {
    // Character types are special and can alias anything.
    // In C++, this technically only includes "char" and "unsigned char",
    // and not "signed char". In C, it includes all three. For now,
    // the risk of exploiting this detail in C++ seems likely to outweigh
    // the benefit.
    case BuiltinType::Char_U:
    case BuiltinType::Char_S:
    case BuiltinType::UChar:
    case BuiltinType::SChar:
      return getChar();

    // Unsigned types can alias their corresponding signed types.
    case BuiltinType::UShort:
      return getScalarTypeInfo(astContext.ShortTy);
    case BuiltinType::UInt:
      return getScalarTypeInfo(astContext.IntTy);
    case BuiltinType::ULong:
      return getScalarTypeInfo(astContext.LongTy);
    case BuiltinType::ULongLong:
      return getScalarTypeInfo(astContext.LongLongTy);
    case BuiltinType::UInt128:
      return getScalarTypeInfo(astContext.Int128Ty);

    case BuiltinType::UShortFract:
      return getScalarTypeInfo(astContext.ShortFractTy);
    case BuiltinType::UFract:
      return getScalarTypeInfo(astContext.FractTy);
    case BuiltinType::ULongFract:
      return getScalarTypeInfo(astContext.LongFractTy);

    case BuiltinType::SatUShortFract:
      return getScalarTypeInfo(astContext.SatShortFractTy);
    case BuiltinType::SatUFract:
      return getScalarTypeInfo(astContext.SatFractTy);
    case BuiltinType::SatULongFract:
      return getScalarTypeInfo(astContext.SatLongFractTy);

    case BuiltinType::UShortAccum:
      return getScalarTypeInfo(astContext.ShortAccumTy);
    case BuiltinType::UAccum:
      return getScalarTypeInfo(astContext.AccumTy);
    case BuiltinType::ULongAccum:
      return getScalarTypeInfo(astContext.LongAccumTy);

    case BuiltinType::SatUShortAccum:
      return getScalarTypeInfo(astContext.SatShortAccumTy);
    case BuiltinType::SatUAccum:
      return getScalarTypeInfo(astContext.SatAccumTy);
    case BuiltinType::SatULongAccum:
      return getScalarTypeInfo(astContext.SatLongAccumTy);

    // Treat all other builtin types as distinct types. This includes
    // treating wchar_t, char16_t, and char32_t as distinct from their
    // "underlying types".
    default:
      return getScalarTypeInfo(qty);
    }
  }
  // C++1z [basic.lval]p10: "If a program attempts to access the stored value of
  // an object through a glvalue of other than one of the following types the
  // behavior is undefined: [...] a char, unsigned char, or std::byte type."
  if (ty->isStdByteType())
    return getChar();

  // Handle pointers and references.
  if (ty->isPointerType() || ty->isReferenceType()) {
    if (!codeGenOpts.PointerTBAA)
      return getAnyPtr(1);

    unsigned ptrDepth = 0;
    do {
      ptrDepth++;
      ty = ty->getPointeeType()->getBaseElementTypeUnsafe();
    } while (ty->isPointerType());
    assert(!isa<VariableArrayType>(ty));

    // void pointers get the depth-appropriate "any pointer" tag.
    if (ty->isVoidType())
      return getAnyPtr(ptrDepth);

    SmallString<256> tyName;

    if (isa<BuiltinType>(ty)) {
      // Recursively compute the pointee's TBAA type info so that
      // canonicalization rules are applied (e.g. unsigned int → int,
      // char → omnipotent char).  This matches OGCG behavior.
      auto pointeeTBAA = getTypeInfoHelper(QualType(ty, 0));
      if (auto scalarAttr = mlir::dyn_cast<cir::TBAAScalarAttr>(pointeeTBAA))
        tyName = scalarAttr.getId();
      else if (mlir::isa<cir::TBAAOmnipotentCharAttr>(pointeeTBAA))
        tyName = "omnipotent char";
      else
        return getAnyPtr(ptrDepth);
    } else {
      const auto *rt = ty->getAs<RecordType>();
      if (!rt)
        return getAnyPtr(ptrDepth);

      if (!rt->getDecl()->getDeclName())
        return getAnyPtr(ptrDepth);

      // CIRGenTBAA can be queried late in teardown-adjacent contexts where
      // CXX ABI helpers may not be available; conservatively fall back.
      if (!types.getCGModule().hasCXXABI())
        return getAnyPtr(ptrDepth);

      llvm::raw_svector_ostream tyOut(tyName);
      types.getCGModule()
          .getCXXABI()
          .getMangleContext()
          .mangleCanonicalTypeName(QualType(ty, 0), tyOut);
    }

    uint64_t ptrSize = codeGenOpts.NewStructPathTBAA
                           ? astContext.getTypeSizeInChars(qty).getQuantity()
                           : 0;
    SmallString<256> outName("p");
    outName += std::to_string(ptrDepth);
    outName += " ";
    outName += tyName;
    return cir::TBAAScalarAttr::get(mlirContext, outName,
                                    types.convertType(qty),
                                    getAnyPtr(ptrDepth), ptrSize);
  }
  // Accesses to arrays are accesses to objects of their element types.
  if (codeGenOpts.NewStructPathTBAA && ty->isArrayType())
    return getTypeInfo(cast<ArrayType>(ty)->getElementType());
  // Enum types are distinct types. In C++ they have "underlying types",
  // however they aren't related for TBAA.
  if (const EnumType *ety = dyn_cast<EnumType>(ty)) {
    // Use getDefinitionOrSelf() to handle forward-declared enums.
    const EnumDecl *enumDecl = ety->getDecl()->getDefinitionOrSelf();
    if (!features.CPlusPlus) {
      // A forward-declared enum without a definition (or without a fixed
      // underlying type) has a null integer type.  Fall back to the
      // conservative "char" alias set instead of crashing.
      QualType underlyingTy = enumDecl->getIntegerType();
      if (underlyingTy.isNull())
        return getChar();
      return getTypeInfo(underlyingTy);
    }

    // In C++ mode, types have linkage, so we can rely on the ODR and
    // on their mangled names, if they're external.
    if (!enumDecl->isExternallyVisible())
      return getChar();
    if (!types.getCGModule().hasCXXABI())
      return getChar();

    SmallString<256> outName;
    llvm::raw_svector_ostream out(outName);
    types.getCGModule().getCXXABI().getMangleContext().mangleCanonicalTypeName(
        QualType(ety, 0), out);
    uint64_t enumSize = codeGenOpts.NewStructPathTBAA
                            ? astContext.getTypeSizeInChars(qty).getQuantity()
                            : 0;
    return cir::TBAAScalarAttr::get(mlirContext, outName,
                                    types.convertType(qty), /*parent=*/nullptr,
                                    enumSize);
  }
  if (const auto *eit = dyn_cast<BitIntType>(ty)) {
    SmallString<256> outName;
    llvm::raw_svector_ostream out(outName);
    // Don't specify signed/unsigned since integer types can alias despite sign
    // differences.
    out << "_BitInt(" << eit->getNumBits() << ')';
    uint64_t bitIntSize = codeGenOpts.NewStructPathTBAA
                              ? astContext.getTypeSizeInChars(qty).getQuantity()
                              : 0;
    return cir::TBAAScalarAttr::get(mlirContext, outName,
                                    types.convertType(qty), /*parent=*/nullptr,
                                    bitIntSize);
  }
  // For now, handle any other kind of type conservatively.
  return getChar();
}

cir::TBAAAttr CIRGenTBAA::getTypeInfo(clang::QualType qty) {
  // At -O0 or relaxed aliasing, TBAA is not emitted for regular types unless
  // we're running TypeSanitizer. Match OGCG CodeGenTBAA::getTypeInfo.
  if (!features.Sanitize.has(SanitizerKind::Type) &&
      (codeGenOpts.OptimizationLevel == 0 || codeGenOpts.RelaxedAliasing)) {
    return nullptr;
  }

  // If the type has the may_alias attribute (even on a typedef), it is
  // effectively in the general char alias class.
  if (typeHasMayAlias(qty))
    return getChar();

  // We need this function to not fall back to returning the "omnipotent char"
  // type node for aggregate and union types. Otherwise, any dereference of an
  // aggregate will result into the may-alias access descriptor, meaning all
  // subsequent accesses to direct and indirect members of that aggregate will
  // be considered may-alias too.
  if (isValidBaseType(qty))
    return getValidBaseTypeInfo(qty);

  const clang::Type *ty = astContext.getCanonicalType(qty).getTypePtr();
  if (metadataCache.contains(ty)) {
    return metadataCache[ty];
  }

  // Note that the following helper call is allowed to add new nodes to the
  // cache, which invalidates all its previously obtained iterators. So we
  // first generate the node for the type and then add that node to the
  // cache.
  auto typeNode = getTypeInfoHelper(qty);
  return metadataCache[ty] = typeNode;
}

TBAAAccessInfo CIRGenTBAA::getAccessInfo(clang::QualType accessType) {
  // Pointee values may have incomplete types, but they shall never be
  // dereferenced.
  if (accessType->isIncompleteType())
    return TBAAAccessInfo::getIncompleteInfo();

  // A whole struct/class value has no valid scalar access type: getTypeInfo
  // returns its struct (base-type) node, which the LLVM verifier rejects when
  // used as the access type of a tbaa tag ("Access type node must be a valid
  // scalar type"). This arises for an ABI-coerced by-value aggregate store
  // (e.g. an armv7 hard-float struct return) that flows through the scalar
  // store path. Classic CodeGen never scalar-accesses a whole aggregate (it
  // uses !tbaa.struct for aggregate copies), so emit no access tag here.
  if (isValidBaseType(accessType))
    return TBAAAccessInfo();

  // At -O0 or relaxed aliasing, TBAA is not emitted for regular types unless
  // we're running TypeSanitizer. This must happen before the may_alias check
  // so that may_alias types don't produce tags at O0. Matches OGCG where the
  // TBAA infrastructure is null at O0.
  if (!features.Sanitize.has(SanitizerKind::Type) &&
      (codeGenOpts.OptimizationLevel == 0 || codeGenOpts.RelaxedAliasing))
    return TBAAAccessInfo();

  if (typeHasMayAlias(accessType))
    return TBAAAccessInfo::getMayAliasInfo();

  uint64_t size = astContext.getTypeSizeInChars(accessType).getQuantity();
  return TBAAAccessInfo(getTypeInfo(accessType), size);
}

TBAAAccessInfo CIRGenTBAA::getVTablePtrAccessInfo(mlir::Type vtablePtrType) {
  if (codeGenOpts.OptimizationLevel == 0 || codeGenOpts.RelaxedAliasing)
    return TBAAAccessInfo();
  const mlir::DataLayout dataLayout(moduleOp);
  auto size = dataLayout.getTypeSize(vtablePtrType);
  return TBAAAccessInfo(
      cir::TBAAVTablePointerAttr::get(mlirContext, vtablePtrType, size), size);
}

mlir::ArrayAttr CIRGenTBAA::getTBAAStructInfo(clang::QualType qty) {
  if (codeGenOpts.OptimizationLevel == 0 || codeGenOpts.RelaxedAliasing)
    return {};

  struct TBAAStructFieldInfo {
    uint64_t offset;
    uint64_t size;
    cir::TBAAAttr tag;
  };
  llvm::SmallVector<TBAAStructFieldInfo, 8> fields;

  std::function<bool(uint64_t, clang::QualType, bool)> collectFields =
      [&](uint64_t baseOffset, clang::QualType qTy, bool mayAlias) -> bool {
    if (const auto *recordTy = qTy->getAsCanonical<clang::RecordType>()) {
      if (recordTy->isUnionType()) {
        uint64_t size = astContext.getTypeSizeInChars(qTy).getQuantity();
        auto tag = getAccessTagInfo(TBAAAccessInfo(getChar(), size));
        if (!tag)
          return false;
        fields.push_back({baseOffset, size, tag});
        return true;
      }

      const clang::RecordDecl *rd = recordTy->getDecl()->getDefinition();
      if (!rd || rd->hasFlexibleArrayMember())
        return false;

      if (const auto *cxxrd = llvm::dyn_cast<clang::CXXRecordDecl>(rd))
        if (!cxxrd->bases().empty())
          return false;

      const clang::ASTRecordLayout &layout = astContext.getASTRecordLayout(rd);
      const CIRGenRecordLayout &cgrl = types.getCIRGenRecordLayout(rd);
      unsigned idx = 0;
      for (auto fieldIt = rd->field_begin(), fieldEnd = rd->field_end();
           fieldIt != fieldEnd; ++fieldIt, ++idx) {
        if (isEmptyFieldForLayout(astContext, *fieldIt))
          continue;

        uint64_t offset =
            baseOffset + layout.getFieldOffset(idx) / astContext.getCharWidth();

        if ((*fieldIt)->isBitField()) {
          const CIRGenBitFieldInfo &info = cgrl.getBitFieldInfo(*fieldIt);
          bool isBE = astContext.getTargetInfo().isBigEndian();
          bool isFirst =
              isBE ? info.storageSize - (info.offset + info.size) == 0
                   : info.offset == 0;
          if (!isFirst)
            continue;
          uint64_t size = (info.storageSize + astContext.getCharWidth() - 1) /
                          astContext.getCharWidth();
          auto tag = getAccessTagInfo(TBAAAccessInfo(getChar(), size));
          if (!tag)
            return false;
          fields.push_back({offset, size, tag});
          continue;
        }

        clang::QualType fieldQty = (*fieldIt)->getType();
        if (!collectFields(offset, fieldQty, mayAlias || typeHasMayAlias(fieldQty)))
          return false;
      }
      return true;
    }

    uint64_t size = astContext.getTypeSizeInChars(qTy).getQuantity();
    cir::TBAAAttr type = mayAlias ? getChar() : getTypeInfo(qTy);
    if (!type)
      return false;
    auto tag = getAccessTagInfo(TBAAAccessInfo(type, size));
    if (!tag)
      return false;
    fields.push_back({baseOffset, size, tag});
    return true;
  };

  if (!collectFields(/*baseOffset=*/0, qty, typeHasMayAlias(qty)))
    return {};

  auto int64Ty = mlir::IntegerType::get(mlirContext, 64);
  auto mkI64 = [&](uint64_t v) -> mlir::IntegerAttr {
    return mlir::IntegerAttr::get(int64Ty, v);
  };
  llvm::SmallVector<mlir::Attribute, 8> fieldAttrs;
  fieldAttrs.reserve(fields.size());
  for (const auto &field : fields)
    fieldAttrs.push_back(mlir::ArrayAttr::get(
        mlirContext, {mkI64(field.offset), mkI64(field.size), field.tag}));
  return mlir::ArrayAttr::get(mlirContext, fieldAttrs);
}

cir::TBAAAttr CIRGenTBAA::getBaseTypeInfo(clang::QualType qty) {
  return isValidBaseType(qty) ? getValidBaseTypeInfo(qty) : nullptr;
}

cir::TBAAAttr CIRGenTBAA::getValidBaseTypeInfo(clang::QualType qty) {
  assert(isValidBaseType(qty) && "Must be a valid base type");

  const clang::Type *ty = astContext.getCanonicalType(qty).getTypePtr();

  // nullptr is a valid value in the cache, so use find rather than []
  auto iter = baseTypeMetadataCache.find(ty);
  if (iter != baseTypeMetadataCache.end())
    return iter->second;

  // First calculate the metadata, before recomputing the insertion point, as
  // the helper can recursively call us.
  auto typeNode = getBaseTypeInfoHelper(ty);
  LLVM_ATTRIBUTE_UNUSED auto inserted =
      baseTypeMetadataCache.insert({ty, typeNode});
  assert(inserted.second && "BaseType metadata was already inserted");

  return typeNode;
}
cir::TBAAAttr CIRGenTBAA::getBaseTypeInfoHelper(const clang::Type *ty) {
  using namespace clang;
  if (auto *tty = mlir::dyn_cast<clang::RecordType>(ty)) {
    const clang::RecordDecl *rd = tty->getDecl()->getDefinition();
    const ASTRecordLayout &layout = astContext.getASTRecordLayout(rd);
    SmallVector<cir::TBAAMemberAttr, 4> fields;
    if (const CXXRecordDecl *cxxrd = dyn_cast<CXXRecordDecl>(rd)) {
      // Handle C++ base classes. Non-virtual bases can treated a kind of
      // field. Virtual bases are more complex and omitted, but avoid an
      // incomplete view for NewStructPathTBAA.
      if (codeGenOpts.NewStructPathTBAA && cxxrd->getNumVBases() != 0)
        return nullptr;
      for (const CXXBaseSpecifier &cxxBaseSpecifier : cxxrd->bases()) {
        if (cxxBaseSpecifier.isVirtual())
          continue;
        QualType baseQTy = cxxBaseSpecifier.getType();
        const CXXRecordDecl *baseRD = baseQTy->getAsCXXRecordDecl();
        if (baseRD->isEmpty())
          continue;
        auto typeNode = isValidBaseType(baseQTy) ? getValidBaseTypeInfo(baseQTy)
                                                 : getTypeInfo(baseQTy);
        if (!typeNode)
          return nullptr;
        uint64_t offset = layout.getBaseClassOffset(baseRD).getQuantity();
        uint64_t size = codeGenOpts.NewStructPathTBAA
            ? astContext.getASTRecordLayout(baseRD).getDataSize().getQuantity()
            : 0;
        fields.push_back(
            cir::TBAAMemberAttr::get(mlirContext, typeNode, offset, size));
      }
      // The order in which base class subobjects are allocated is
      // unspecified, so may differ from declaration order. In particular,
      // Itanium ABI will allocate a primary base first. Since we exclude
      // empty subobjects, the objects are not overlapping and their offsets
      // are unique.
      llvm::sort(fields, [](const cir::TBAAMemberAttr &lhs,
                            const cir::TBAAMemberAttr &rhs) {
        return lhs.getOffset() < rhs.getOffset();
      });
    }
    for (FieldDecl *field : rd->fields()) {
      if (field->isZeroSize(astContext) || field->isUnnamedBitField())
        continue;
      QualType fieldQTy = field->getType();
      auto typeNode = isValidBaseType(fieldQTy) ? getValidBaseTypeInfo(fieldQTy)
                                                : getTypeInfo(fieldQTy);
      if (!typeNode)
        return nullptr;

      uint64_t bitOffset = layout.getFieldOffset(field->getFieldIndex());
      uint64_t offset = astContext.toCharUnitsFromBits(bitOffset).getQuantity();
      uint64_t size = codeGenOpts.NewStructPathTBAA
                          ? astContext.getTypeSizeInChars(fieldQTy).getQuantity()
                          : 0;
      fields.push_back(
          cir::TBAAMemberAttr::get(mlirContext, typeNode, offset, size));
    }

    SmallString<256> outName;
    if (features.CPlusPlus) {
      // Don't use the mangler for C code.
      if (!types.getCGModule().hasCXXABI())
        return nullptr;
      llvm::raw_svector_ostream out(outName);
      types.getCGModule()
          .getCXXABI()
          .getMangleContext()
          .mangleCanonicalTypeName(QualType(ty, 0), out);
    } else {
      outName = rd->getName();
    }

    uint64_t structSize = codeGenOpts.NewStructPathTBAA
        ? astContext.getTypeSizeInChars(QualType(ty, 0)).getQuantity()
        : 0;
    return cir::TBAAStructAttr::get(mlirContext, outName, fields, structSize);
  }
  return nullptr;
}
cir::TBAAAttr CIRGenTBAA::getAccessTagInfo(TBAAAccessInfo tbaaInfo) {
  assert(!tbaaInfo.isIncomplete() &&
         "Access to an object of an incomplete type!");

  if (tbaaInfo.isMayAlias())
    tbaaInfo = TBAAAccessInfo(getChar(), tbaaInfo.size);
  if (!tbaaInfo.accessType) {
    return nullptr;
  }

  if (!codeGenOpts.StructPathTBAA)
    tbaaInfo = TBAAAccessInfo(tbaaInfo.accessType, tbaaInfo.size);

  if (!tbaaInfo.baseType) {
    tbaaInfo.baseType = tbaaInfo.accessType;
    assert(!tbaaInfo.offset &&
           "Nonzero offset for an access with no base type!");
  }
  if (codeGenOpts.NewStructPathTBAA) {
    return cir::TBAATagAttr::get(mlirContext, tbaaInfo.baseType,
                                 tbaaInfo.accessType, tbaaInfo.offset,
                                 tbaaInfo.size);
  }
  if (tbaaInfo.baseType == tbaaInfo.accessType) {
    return tbaaInfo.accessType;
  }
  return cir::TBAATagAttr::get(mlirContext, tbaaInfo.baseType,
                               tbaaInfo.accessType, tbaaInfo.offset,
                               /*size=*/0);
}

TBAAAccessInfo CIRGenTBAA::mergeTBAAInfoForCast(TBAAAccessInfo sourceInfo,
                                                TBAAAccessInfo targetInfo) {
  if (sourceInfo.isMayAlias() || targetInfo.isMayAlias())
    return TBAAAccessInfo::getMayAliasInfo();
  return targetInfo;
}

TBAAAccessInfo
CIRGenTBAA::mergeTBAAInfoForConditionalOperator(TBAAAccessInfo infoA,
                                                TBAAAccessInfo infoB) {
  if (infoA == infoB)
    return infoA;

  if (!infoA || !infoB)
    return TBAAAccessInfo();

  if (infoA.isMayAlias() || infoB.isMayAlias())
    return TBAAAccessInfo::getMayAliasInfo();

  // TODO: Implement the rest of the logic here. For example, two accesses
  // with same final access types result in an access to an object of that
  // final access type regardless of their base types.
  return TBAAAccessInfo::getMayAliasInfo();
}

TBAAAccessInfo
CIRGenTBAA::mergeTBAAInfoForMemoryTransfer(TBAAAccessInfo destInfo,
                                           TBAAAccessInfo srcInfo) {
  if (destInfo == srcInfo)
    return destInfo;

  if (!destInfo || !srcInfo)
    return TBAAAccessInfo();

  if (destInfo.isMayAlias() || srcInfo.isMayAlias())
    return TBAAAccessInfo::getMayAliasInfo();

  // TODO: Implement the rest of the logic here. For example, two accesses
  // with same final access types result in an access to an object of that
  // final access type regardless of their base types.
  return TBAAAccessInfo::getMayAliasInfo();
}

} // namespace clang::CIRGen
