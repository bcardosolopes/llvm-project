#include "CIRGenTypes.h"

#include "CIRGenCXXABI.h"
#include "CIRGenFunctionInfo.h"
#include "CIRGenModule.h"
#include "TargetInfo.h"
#include "mlir/IR/BuiltinTypes.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/GlobalDecl.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/Type.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/CIR/Dialect/IR/CIRTypes.h"
#include "llvm/IR/DataLayout.h"

#include <cassert>

using namespace clang;
using namespace clang::CIRGen;

CIRGenTypes::CIRGenTypes(CIRGenModule &genModule)
    : cgm(genModule), astContext(genModule.getASTContext()),
      builder(cgm.getBuilder()), theCXXABI(cgm.getCXXABI()),
      theABIInfo(cgm.getTargetCIRGenInfo().getABIInfo()) {}

CIRGenTypes::~CIRGenTypes() {
  for (auto i = functionInfos.begin(), e = functionInfos.end(); i != e;)
    delete &*i++;
}

cir::CallingConv
CIRGenTypes::ClangCallConvToCIRCallConv(clang::CallingConv cc) {
  switch (cc) {
  case CC_C:
    return cir::CallingConv::C;
  case CC_X86StdCall:
    return cir::CallingConv::X86StdCall;
  case CC_X86FastCall:
    return cir::CallingConv::X86FastCall;
  case CC_X86RegCall:
    return cir::CallingConv::X86RegCall;
  case CC_X86ThisCall:
    return cir::CallingConv::X86ThisCall;
  case CC_X86VectorCall:
    return cir::CallingConv::X86VectorCall;
  case CC_X86_64SysV:
    return cir::CallingConv::X86_64SysV;
  case CC_Win64:
    return cir::CallingConv::Win64;
  case CC_AAPCS:
    return cir::CallingConv::AAPCS;
  case CC_AAPCS_VFP:
    return cir::CallingConv::AAPCS_VFP;
  case CC_IntelOclBicc:
    return cir::CallingConv::IntelOclBicc;
  case CC_AArch64VectorCall:
    return cir::CallingConv::AArch64VectorCall;
  case CC_AArch64SVEPCS:
    return cir::CallingConv::AArch64SVEPCS;
  case CC_PreserveMost:
    return cir::CallingConv::PreserveMost;
  case CC_PreserveAll:
    return cir::CallingConv::PreserveAll;
  case CC_PreserveNone:
    return cir::CallingConv::PreserveNone;
  case CC_Swift:
    return cir::CallingConv::Swift;
  case CC_SwiftAsync:
    return cir::CallingConv::SwiftAsync;
  case CC_SpirFunction:
    return cir::CallingConv::SpirFunction;
  case CC_DeviceKernel:
    return cgm.getTargetCIRGenInfo().getOpenCLKernelCallingConv();
  case CC_X86Pascal:
    // No LLVM equivalent; map to C like OGCG does.
    return cir::CallingConv::C;
  default:
    cgm.errorNYI("calling convention");
    return {};
  }
}

mlir::MLIRContext &CIRGenTypes::getMLIRContext() const {
  return *builder.getContext();
}

/// Return true if the specified type in a function parameter or result position
/// can be converted to a CIR type at this point. This boils down to being
/// whether it is complete, as well as whether we've temporarily deferred
/// expanding the type because we're in a recursive context.
bool CIRGenTypes::isFuncParamTypeConvertible(clang::QualType type) {
  // Some ABIs cannot have their member pointers represented in LLVM IR unless
  // certain circumstances have been reached, but in CIR we represent member
  // pointer types abstractly at this point so they are always convertible.
  if (type->getAs<MemberPointerType>())
    return true;

  // If this isn't a tag type, we can convert it.
  const TagType *tagType = type->getAs<TagType>();
  if (!tagType)
    return true;

  // Function types involving incomplete class types are problematic in MLIR.
  return !tagType->isIncompleteType();
}

/// Code to verify a given function type is complete, i.e. the return type and
/// all of the parameter types are complete. Also check to see if we are in a
/// RS_StructPointer context, and if so whether any struct types have been
/// pended. If so, we don't want to ask the ABI lowering code to handle a type
/// that cannot be converted to a CIR type.
bool CIRGenTypes::isFuncTypeConvertible(const FunctionType *ft) {
  if (!isFuncParamTypeConvertible(ft->getReturnType()))
    return false;

  if (const auto *fpt = dyn_cast<FunctionProtoType>(ft))
    for (unsigned i = 0, e = fpt->getNumParams(); i != e; i++)
      if (!isFuncParamTypeConvertible(fpt->getParamType(i)))
        return false;

  return true;
}

mlir::Type CIRGenTypes::convertFunctionTypeInternal(QualType qft) {
  assert(qft.isCanonical());
  const FunctionType *ft = cast<FunctionType>(qft.getTypePtr());
  // First, check whether we can build the full function type. If the function
  // type depends on an incomplete type (e.g. a struct or enum), we cannot lower
  // the function type.
  if (!isFuncTypeConvertible(ft)) {
    // This function's type depends on an incomplete tag type.
    // Force conversion of all the relevant record types, to make sure
    // we re-convert the FunctionType when appropriate.
    if (const auto *rd = ft->getReturnType()->getAsRecordDecl())
      convertRecordDeclType(rd);
    if (const auto *fpt = dyn_cast<FunctionProtoType>(ft))
      for (unsigned i = 0, e = fpt->getNumParams(); i != e; i++)
        if (const auto *rd = fpt->getParamType(i)->getAsRecordDecl())
          convertRecordDeclType(rd);

    // Return a placeholder type.
    // Set skippedLayout so that when the record type is eventually completed,
    // all stale cached function types are invalidated. Matches OGCG behavior.
    skippedLayout = true;
    return cir::FuncType::get(SmallVector<mlir::Type, 1>{}, cgm.voidTy);
  }

  const CIRGenFunctionInfo *fi;
  if (const auto *fpt = dyn_cast<FunctionProtoType>(ft)) {
    fi = &arrangeFreeFunctionType(
        CanQual<FunctionProtoType>::CreateUnsafe(QualType(fpt, 0)));
  } else {
    const FunctionNoProtoType *fnpt = cast<FunctionNoProtoType>(ft);
    fi = &arrangeFreeFunctionType(
        CanQual<FunctionNoProtoType>::CreateUnsafe(QualType(fnpt, 0)));
  }

  mlir::Type resultType = getFunctionType(*fi);

  return resultType;
}

// Estimate the cost (in characters) of printing a RecordDecl's type name.
// For deeply nested template specializations with many repeated arguments
// (e.g. Foo<Foo<Foo<Base, Base, ...>, ...>, ...>), QualType::print expands
// every template argument recursively, producing exponentially large names.
// This estimate detects such cases quickly by bailing out at a cost threshold.
static unsigned estimateTemplatePrintCostForArgs(
    llvm::ArrayRef<TemplateArgument> args, unsigned limit);

static unsigned estimateTemplatePrintCost(const RecordDecl *rd,
                                          unsigned limit) {
  auto *spec = dyn_cast<ClassTemplateSpecializationDecl>(rd);
  if (!spec)
    return 1;

  const auto &args = spec->getTemplateArgs();
  unsigned cost = 1;
  cost += estimateTemplatePrintCostForArgs(args.asArray(), limit - cost);
  return cost;
}

static unsigned estimateTemplatePrintCostForArgs(
    llvm::ArrayRef<TemplateArgument> args, unsigned limit) {
  unsigned cost = 0;
  for (const auto &arg : args) {
    if (arg.getKind() == TemplateArgument::Type) {
      if (auto *innerRd = arg.getAsType()->getAsRecordDecl())
        cost += estimateTemplatePrintCost(innerRd, limit - cost);
      else
        cost += 1;
    } else if (arg.getKind() == TemplateArgument::Pack) {
      cost +=
          estimateTemplatePrintCostForArgs(arg.getPackAsArray(), limit - cost);
    } else {
      cost += 1;
    }
    if (cost > limit)
      return cost;
  }
  return cost;
}

// This is CIR's version of CodeGenTypes::addRecordTypeName. It isn't shareable
// because CIR has different uniquing requirements.
std::string CIRGenTypes::getRecordTypeName(const clang::RecordDecl *recordDecl,
                                           StringRef suffix) {
  llvm::SmallString<256> typeName;
  llvm::raw_svector_ostream outStream(typeName);

  PrintingPolicy policy = recordDecl->getASTContext().getPrintingPolicy();
  policy.SuppressInlineNamespace =
      llvm::to_underlying(PrintingPolicy::SuppressInlineNamespaceMode::None);
  policy.AlwaysIncludeTypeForTemplateArgument = true;
  policy.PrintAsCanonical = true;
  policy.SuppressTagKeyword = true;

  if (recordDecl->getIdentifier()) {
    // The (suffix-free) prefix is a pure function of the canonical tag type and
    // is recomputed for the primary name and again for the ".base" variant; for
    // deeply nested template specializations it is the ~11k-char string whose
    // construction dominates StringAttr self-time. Memoize it by canonical type.
    QualType canonTy(astContext.getCanonicalTagType(recordDecl));
    auto cached = recordTypeNamePrefixCache.find(canonTy.getTypePtr());
    if (cached != recordTypeNamePrefixCache.end()) {
      outStream << cached->second;
    } else {
      llvm::SmallString<256> prefix;
      llvm::raw_svector_ostream prefixStream(prefix);
      // For template specializations with deeply nested arguments, printing the
      // type name via QualType::print expands all arguments recursively, causing
      // exponential name length growth (e.g. pr29160). Fall back to the compact
      // Itanium ABI mangled name which uses substitutions to stay linear.
      constexpr unsigned costThreshold = 256;
      unsigned cost = estimateTemplatePrintCost(recordDecl, costThreshold);
      if (cost > costThreshold)
        cgm.getCXXABI().getMangleContext().mangleCanonicalTypeName(canonTy,
                                                                   prefixStream);
      else
        canonTy.print(prefixStream, policy);
      recordTypeNamePrefixCache.try_emplace(canonTy.getTypePtr(),
                                            std::string(prefix.str()));
      outStream << prefix;
    }
  } else if (auto *typedefNameDecl = recordDecl->getTypedefNameForAnonDecl())
    typedefNameDecl->printQualifiedName(outStream, policy);
  else
    outStream << builder.getUniqueAnonRecordName();

  if (!suffix.empty())
    outStream << suffix;

  return builder.getUniqueRecordName(typeName);
}

/// Return true if the specified type is already completely laid out.
bool CIRGenTypes::isRecordLayoutComplete(const Type *ty) const {
  const auto it = recordDeclTypes.find(ty);
  return it != recordDeclTypes.end() && it->second.isComplete();
}

// We have multiple forms of this function that call each other, so we need to
// declare one in advance.
static bool
isSafeToConvert(QualType qt, CIRGenTypes &cgt,
                llvm::SmallPtrSetImpl<const RecordDecl *> &alreadyChecked);

/// Return true if it is safe to convert the specified record decl to CIR and
/// lay it out, false if doing so would cause us to get into a recursive
/// compilation mess.
static bool
isSafeToConvert(const RecordDecl *rd, CIRGenTypes &cgt,
                llvm::SmallPtrSetImpl<const RecordDecl *> &alreadyChecked) {
  // If we have already checked this type (maybe the same type is used by-value
  // multiple times in multiple record fields, don't check again.
  if (!alreadyChecked.insert(rd).second)
    return true;

  assert(rd->isCompleteDefinition() &&
         "Expect RecordDecl to be CompleteDefinition");
  const Type *key = cgt.getASTContext().getCanonicalTagType(rd).getTypePtr();

  // If this type is already laid out, converting it is a noop.
  if (cgt.isRecordLayoutComplete(key))
    return true;

  // Check the cross-call cache. This avoids redundant recursive field walks
  // for the same record types across different convertRecordDeclType calls
  // during a single layout phase.
  if (cgt.isCachedSafeToConvert(key))
    return true;

  // If this type is currently being laid out, we can't recursively compile it.
  if (cgt.isRecordBeingLaidOut(key))
    return false;

  // If this type would require laying out bases that are currently being laid
  // out, don't do it.  This includes virtual base classes which get laid out
  // when a class is translated, even though they aren't embedded by-value into
  // the class.
  if (const CXXRecordDecl *crd = dyn_cast<CXXRecordDecl>(rd)) {
    for (const clang::CXXBaseSpecifier &i : crd->bases())
      if (!isSafeToConvert(i.getType()
                               ->castAs<RecordType>()
                               ->getDecl()
                               ->getDefinitionOrSelf(),
                           cgt, alreadyChecked))
        return false;
  }

  // If this type would require laying out members that are currently being laid
  // out, don't do it. Use rd->fields() directly rather than getCachedFields()
  // because the recursive isSafeToConvert calls can trigger getCachedFields()
  // for other records, which may grow the DenseMap and invalidate the ArrayRef
  // returned by a prior getCachedFields() call still on the stack.
  for (const FieldDecl *field : rd->fields())
    if (!isSafeToConvert(field->getType(), cgt, alreadyChecked))
      return false;

  // Cache the positive result. This will be cleared when recordsBeingLaidOut
  // changes.
  cgt.cacheSafeToConvert(key);

  // If there are no problems, lets do it.
  return true;
}

/// Return true if it is safe to convert this field type, which requires the
/// record elements contained by-value to all be recursively safe to convert.
static bool
isSafeToConvert(QualType qt, CIRGenTypes &cgt,
                llvm::SmallPtrSetImpl<const RecordDecl *> &alreadyChecked) {
  // Strip off atomic type sugar.
  if (const auto *at = qt->getAs<AtomicType>())
    qt = at->getValueType();

  // If this is a record, check it.
  if (const auto *rd = qt->getAsRecordDecl())
    return isSafeToConvert(rd, cgt, alreadyChecked);

  // If this is an array, check the elements, which are embedded inline.
  if (const auto *at = cgt.getASTContext().getAsArrayType(qt))
    return isSafeToConvert(at->getElementType(), cgt, alreadyChecked);

  // Otherwise, there is no concern about transforming this. We only care about
  // things that are contained by-value in a record that can have another
  // record as a member.
  return true;
}

// Return true if it is safe to convert the specified record decl to CIR and lay
// it out, false if doing so would cause us to get into a recursive compilation
// mess.
static bool isSafeToConvert(const RecordDecl *rd, CIRGenTypes &cgt) {
  // If no records are being laid out, we can certainly do this one.
  if (cgt.noRecordsBeingLaidOut())
    return true;

  llvm::SmallPtrSet<const RecordDecl *, 16> alreadyChecked;
  return isSafeToConvert(rd, cgt, alreadyChecked);
}

/// Lay out a tagged decl type like struct or union.
mlir::Type CIRGenTypes::convertRecordDeclType(const clang::RecordDecl *rd) {
  // TagDecl's are not necessarily unique, instead use the (clang) type
  // connected to the decl.
  const Type *key = astContext.getCanonicalTagType(rd).getTypePtr();
  cir::RecordType entry = recordDeclTypes[key];

  // If we don't have an entry for this record yet, create one.
  // We create an incomplete type initially. If `rd` is complete, we will
  // add the members below.
  if (!entry) {
    auto name = getRecordTypeName(rd, "");
    entry = builder.getIncompleteRecordTy(name, rd);
    recordDeclTypes[key] = entry;
  }

  rd = rd->getDefinition();
  if (!rd || !rd->isCompleteDefinition() || entry.isComplete())
    return entry;

  // If converting this type would cause us to infinitely loop, don't do it!
  if (!isSafeToConvert(rd, *this)) {
    deferredRecords.push_back(rd);
    return entry;
  }

  // Okay, this is a definition of a type. Compile the implementation now.
  bool insertResult = recordsBeingLaidOut.insert(key).second;
  (void)insertResult;
  assert(insertResult && "isSafeToCovert() should have caught this.");

  // Invalidate the safety cache since recordsBeingLaidOut grew: a record
  // that was previously safe might now reference the newly-being-laid-out
  // record. Incrementing the generation counter effectively invalidates all
  // existing cache entries without the cost of clearing the DenseMap.
  ++safeToConvertGeneration;

  // Force conversion of non-virtual base classes recursively.
  if (const auto *cxxRecordDecl = dyn_cast<CXXRecordDecl>(rd)) {
    for (const auto &base : cxxRecordDecl->bases()) {
      if (base.isVirtual())
        continue;
      convertRecordDeclType(base.getType()->castAsRecordDecl());
    }
  }

  // Layout fields.
  std::unique_ptr<CIRGenRecordLayout> layout = computeRecordLayout(rd, &entry);
  recordDeclTypes[key] = entry;
  cirGenRecordLayouts[key] = std::move(layout);

  // We're done laying out this record.
  bool eraseResult = recordsBeingLaidOut.erase(key);
  (void)eraseResult;
  assert(eraseResult && "record not in RecordsBeingLaidOut set?");

  // Note: we do NOT clear safeToConvertCache here. Removing a record from
  // recordsBeingLaidOut makes the set smaller, which can only make more
  // types safe to convert (not fewer). A record that was safe when more
  // records were being laid out is still safe when fewer are being laid out.
  // Only the addition of a new record to recordsBeingLaidOut (above) needs
  // to invalidate the cache, since a newly-being-laid-out record could
  // make previously-safe records unsafe (if they reference it as a field).

  // If this record blocked a FunctionType conversion, then recompute whatever
  // was derived from that. Defer the expensive refresh until we're back at the
  // outermost record completion (recordsBeingLaidOut is empty) to avoid
  // redundant O(all-records) refreshes at intermediate completion steps.
  // FIXME: This is hugely overconservative.
  if (skippedLayout && recordsBeingLaidOut.empty()) {
    typeCache.clear();
    funcTypeCache.clear();

    // Reset the flag before refreshing. If a refreshed member type still
    // depends on another incomplete record, convertTypeForMem() will set it
    // again so a later completion can retry.
    skippedLayout = false;

    // Phase 1: Compute new member types for all records and apply them.
    // Collect the set of changed record types so that Phase 2 can fix up
    // GetMemberOp users in a single batched module walk.
    llvm::SmallDenseMap<cir::RecordType, llvm::SmallVector<mlir::Type>>
        changedRecords;

    llvm::SmallVector<const clang::Type *> refreshKeys;
    refreshKeys.reserve(recordDeclTypes.size());
    for (const auto &recordEntry : recordDeclTypes)
      refreshKeys.push_back(recordEntry.first);

    for (const clang::Type *refreshKey : refreshKeys) {
      auto recordIt = recordDeclTypes.find(refreshKey);
      if (recordIt == recordDeclTypes.end())
        continue;

      cir::RecordType recordTy = recordIt->second;
      if (!recordTy.isComplete() || recordTy.getMembers().empty())
        continue;

      auto layoutIt = cirGenRecordLayouts.find(refreshKey);
      if (layoutIt == cirGenRecordLayouts.end())
        continue;

      auto *recordType = llvm::dyn_cast<clang::RecordType>(refreshKey);
      if (!recordType)
        continue;

      const auto *refreshRD = recordType->getDecl()->getDefinitionOrSelf();
      if (!refreshRD)
        continue;

      const CIRGenRecordLayout &layout = *layoutIt->second;
      const bool isUnion = recordTy.getKind() == cir::RecordType::Union;

      llvm::SmallVector<mlir::Type> newMembers(recordTy.getMembers().begin(),
                                               recordTy.getMembers().end());
      bool changed = false;
      for (auto *field : refreshRD->fields()) {
        // Zero-length bitfields and some layout-only AST fields may not have
        // corresponding CIR storage members/indices.
        if (!layout.hasCIRFieldNo(field))
          continue;
        unsigned fieldNo =
            isUnion ? field->getFieldIndex() : layout.getCIRFieldNo(field);
        // Refresh to the same storage type used by record layout. Bitfields can
        // share a storage slot, unions keep one member entry per declared
        // field, and potentially-overlapping fields use their base-subobject
        // type in the record layout.
        mlir::Type freshType;
        if (field->isBitField()) {
          freshType = layout.getBitFieldInfo(field).storageType;
        } else if (!isUnion && field->isPotentiallyOverlapping()) {
          freshType =
              getCIRGenRecordLayout(field->getType()->getAsCXXRecordDecl())
                  .getBaseSubobjectCIRType();
        } else {
          freshType = convertTypeForMemForField(field->getType());
        }
        if (fieldNo < newMembers.size() && newMembers[fieldNo] != freshType) {
          newMembers[fieldNo] = freshType;
          changed = true;
        }
      }

      if (!changed)
        continue;

      recordTy.replaceMemberTypes(newMembers);
      changedRecords[recordTy] = std::move(newMembers);
    }

    // Phase 2: Fix up GetMemberOp users in a single batched module walk.
    // Previously, each changed record triggered its own module.walk(), making
    // the cost O(N_ops * N_changed_records). This batched approach is O(N_ops).
    if (!changedRecords.empty()) {
      if (auto module = cgm.getModule()) {
        module.walk([&](cir::GetMemberOp memberOp) {
          auto basePtrTy =
              mlir::dyn_cast<cir::PointerType>(memberOp.getAddr().getType());
          if (!basePtrTy)
            return;
          auto recordTy =
              mlir::dyn_cast<cir::RecordType>(basePtrTy.getPointee());
          if (!recordTy)
            return;

          auto it = changedRecords.find(recordTy);
          if (it == changedRecords.end())
            return;

          const auto &newMembers = it->second;
          if (memberOp.getIndex() >= newMembers.size())
            return;

          auto resultPtrTy =
              mlir::dyn_cast<cir::PointerType>(memberOp.getType());
          if (!resultPtrTy)
            return;

          mlir::Type freshMemberTy = newMembers[memberOp.getIndex()];
          if (resultPtrTy.getPointee() == freshMemberTy)
            return;

          memberOp.getResult().setType(
              builder.getPointerTo(freshMemberTy, resultPtrTy.getAddrSpace()));

          // Repair loads/stores that still encode the placeholder member type.
          auto freshResultPtrTy =
              mlir::cast<cir::PointerType>(memberOp.getType());
          llvm::SmallVector<mlir::OpOperand *, 4> uses;
          for (mlir::OpOperand &use : memberOp.getResult().getUses())
            uses.push_back(&use);

          for (mlir::OpOperand *use : uses) {
            mlir::Operation *user = use->getOwner();
            mlir::Type staleAddrTy;
            if (auto loadOp = llvm::dyn_cast<cir::LoadOp>(user)) {
              if (use->getOperandNumber() != 0)
                continue;
              if (loadOp.getResult().getType() == freshResultPtrTy.getPointee())
                continue;
              staleAddrTy = builder.getPointerTo(loadOp.getResult().getType(),
                                                 resultPtrTy.getAddrSpace());
            } else if (auto storeOp = llvm::dyn_cast<cir::StoreOp>(user)) {
              if (use->getOperandNumber() != 1)
                continue;
              if (storeOp.getValue().getType() == freshResultPtrTy.getPointee())
                continue;
              staleAddrTy = builder.getPointerTo(storeOp.getValue().getType(),
                                                 resultPtrTy.getAddrSpace());
            } else {
              continue;
            }

            if (staleAddrTy == freshResultPtrTy)
              continue;

            mlir::OpBuilder::InsertionGuard guard(builder);
            builder.setInsertionPoint(user);
            mlir::Value staleAddr = builder.createBitcast(
                user->getLoc(), memberOp.getResult(), staleAddrTy);
            user->setOperand(use->getOperandNumber(), staleAddr);
          }
        });
      }
    }
  }

  // If we're done converting the outer-most record, then convert any deferred
  // records as well.
  if (recordsBeingLaidOut.empty())
    while (!deferredRecords.empty())
      convertRecordDeclType(deferredRecords.pop_back_val());

  return entry;
}

mlir::Type CIRGenTypes::convertType(QualType type) {
  type = astContext.getCanonicalType(type);
  const Type *ty = type.getTypePtr();

  // For the device-side compilation, CUDA device builtin surface/texture types
  // may be represented in different types.
  if (astContext.getLangOpts().CUDAIsDevice) {
    if (type->isCUDADeviceBuiltinSurfaceType()) {
      if (mlir::Type surfTy =
              cgm.getTargetCIRGenInfo().getCUDADeviceBuiltinSurfaceDeviceType())
        return surfTy;
    } else if (type->isCUDADeviceBuiltinTextureType()) {
      if (mlir::Type texTy =
              cgm.getTargetCIRGenInfo().getCUDADeviceBuiltinTextureDeviceType())
        return texTy;
    }
  }

  // Process record types before the type cache lookup.
  if (const auto *recordType = dyn_cast<RecordType>(type)) {
    // Fast-path: if we already have a completed CIR type for this canonical
    // record type, return it directly without re-entering
    // convertRecordDeclType (which would redo getCanonicalTagType,
    // getDefinition, isSafeToConvert, etc.).
    auto it = recordDeclTypes.find(ty);
    if (it != recordDeclTypes.end() && it->second && it->second.isComplete())
      return it->second;
    return convertRecordDeclType(recordType->getDecl()->getDefinitionOrSelf());
  }

  // Has the type already been processed?
  TypeCacheTy::iterator tci = typeCache.find(ty);
  if (tci != typeCache.end())
    return tci->second;

  // For types that haven't been implemented yet or are otherwise unsupported,
  // report an error and return 'int'.

  mlir::Type resultType = nullptr;
  switch (ty->getTypeClass()) {
  case Type::Record:
    llvm_unreachable("Should have been handled above");

  case Type::Builtin: {
    switch (cast<BuiltinType>(ty)->getKind()) {
    // void
    case BuiltinType::Void:
      resultType = cgm.voidTy;
      break;

    // ObjC builtin types — id, Class, SEL. In OGCG these map to i8
    // (same as void), since LLVM's void can't be used as a pointee. CIR
    // follows the same i8 mapping for OGCG parity.
    case BuiltinType::ObjCId:
    case BuiltinType::ObjCClass:
    case BuiltinType::ObjCSel:
      resultType =
          cir::IntType::get(&getMLIRContext(), 8, /*isSigned=*/false);
      break;

    // bool
    case BuiltinType::Bool:
      resultType = cir::BoolType::get(&getMLIRContext());
      break;

    // Signed integral types.
    case BuiltinType::Accum:
    case BuiltinType::Char_S:
    case BuiltinType::Fract:
    case BuiltinType::Int:
    case BuiltinType::Int128:
    case BuiltinType::Long:
    case BuiltinType::LongAccum:
    case BuiltinType::LongFract:
    case BuiltinType::LongLong:
    case BuiltinType::SChar:
    case BuiltinType::Short:
    case BuiltinType::ShortAccum:
    case BuiltinType::ShortFract:
    case BuiltinType::WChar_S:
    // Saturated signed types.
    case BuiltinType::SatAccum:
    case BuiltinType::SatFract:
    case BuiltinType::SatLongAccum:
    case BuiltinType::SatLongFract:
    case BuiltinType::SatShortAccum:
    case BuiltinType::SatShortFract:
      resultType =
          cir::IntType::get(&getMLIRContext(), astContext.getTypeSize(ty),
                            /*isSigned=*/true);
      break;

    // SVE vector and predicate types (including x2, x3, x4 tuples).
#define SVE_VECTOR_TYPE(Name, MangledName, Id, SingletonId)                    \
    case BuiltinType::Id:
#define SVE_PREDICATE_TYPE(Name, MangledName, Id, SingletonId)                 \
    case BuiltinType::Id:
#include "clang/Basic/AArch64ACLETypes.def"
    {
      ASTContext::BuiltinVectorTypeInfo info =
          astContext.getBuiltinVectorTypeInfo(cast<BuiltinType>(ty));
      // MFloat8's convertType maps to <1 x i8>, but as a vector element
      // we need the scalar i8 type, matching OGCG behavior.
      mlir::Type eltTy = info.ElementType->isMFloat8Type()
                             ? builder.getSInt8Ty()
                             : convertType(info.ElementType);
      auto vecTy = cir::VectorType::get(eltTy, info.EC.getKnownMinValue(),
                                        /*is_scalable=*/true);
      switch (info.NumVectors) {
      default:
        llvm_unreachable("Expected 1, 2, 3 or 4 vectors!");
      case 1:
        resultType = vecTy;
        break;
      case 2:
        resultType = cir::RecordType::get(&getMLIRContext(), {vecTy, vecTy},
                                          /*packed=*/false, /*padded=*/false,
                                          cir::RecordType::Struct);
        break;
      case 3:
        resultType = cir::RecordType::get(
            &getMLIRContext(), {vecTy, vecTy, vecTy},
            /*packed=*/false, /*padded=*/false, cir::RecordType::Struct);
        break;
      case 4:
        resultType = cir::RecordType::get(
            &getMLIRContext(), {vecTy, vecTy, vecTy, vecTy},
            /*packed=*/false, /*padded=*/false, cir::RecordType::Struct);
        break;
      }
      break;
    }
    case BuiltinType::SveCount:
      resultType = cir::OpaqueType::get(
          builder.getContext(),
          cgm.getStringAttrCached(cir::OpaqueType::getAArch64SVCountTag()));
      break;

    // Unsigned integral types.
    case BuiltinType::Char8:
    case BuiltinType::Char16:
    case BuiltinType::Char32:
    case BuiltinType::Char_U:
    case BuiltinType::UAccum:
    case BuiltinType::UChar:
    case BuiltinType::UFract:
    case BuiltinType::UInt:
    case BuiltinType::UInt128:
    case BuiltinType::ULong:
    case BuiltinType::ULongAccum:
    case BuiltinType::ULongFract:
    case BuiltinType::ULongLong:
    case BuiltinType::UShort:
    case BuiltinType::UShortAccum:
    case BuiltinType::UShortFract:
    case BuiltinType::WChar_U:
    // Saturated unsigned types.
    case BuiltinType::SatUAccum:
    case BuiltinType::SatUFract:
    case BuiltinType::SatULongAccum:
    case BuiltinType::SatULongFract:
    case BuiltinType::SatUShortAccum:
    case BuiltinType::SatUShortFract:
      resultType =
          cir::IntType::get(&getMLIRContext(), astContext.getTypeSize(ty),
                            /*isSigned=*/false);
      break;

    // Floating-point types
    case BuiltinType::Float16:
      resultType = cgm.fP16Ty;
      break;
    case BuiltinType::Half:
      if (astContext.getLangOpts().NativeHalfType ||
          !astContext.getTargetInfo().useFP16ConversionIntrinsics()) {
        resultType = cgm.fP16Ty;
      } else {
        cgm.errorNYI(SourceLocation(), "processing of built-in type", type);
        return {};
      }
      break;
    case BuiltinType::BFloat16:
      resultType = cgm.bFloat16Ty;
      break;
    case BuiltinType::MFloat8:
      // Match OGCG representation for __mfp8: model as <1 x i8>.
      resultType = cir::VectorType::get(cgm.sInt8Ty, 1);
      break;
    case BuiltinType::Float:
      assert(&astContext.getFloatTypeSemantics(type) ==
                 &llvm::APFloat::IEEEsingle() &&
             "ClangIR NYI: 'float' in a format other than IEEE 32-bit");
      resultType = cgm.floatTy;
      break;
    case BuiltinType::Double:
      assert(&astContext.getFloatTypeSemantics(type) ==
                 &llvm::APFloat::IEEEdouble() &&
             "ClangIR NYI: 'double' in a format other than IEEE 64-bit");
      resultType = cgm.doubleTy;
      break;
    case BuiltinType::LongDouble:
      // OGCG sets LongDoubleReferenced = true here for PPC long double ABI
      // module metadata emission. CIR does not track this per-type boolean;
      // PPC double-double format is guarded by ppcDoubleDoubleLongDouble()
      // and target metadata emission by targetCodeGenInfoEmitTargetGlobals().
      resultType =
          builder.getLongDoubleTy(astContext.getFloatTypeSemantics(type));
      break;
    case BuiltinType::Float128:
      resultType = cgm.fP128Ty;
      break;
    case BuiltinType::Ibm128:
      cgm.errorNYI(SourceLocation(), "processing of built-in type", type);
      return {};
      break;

    case BuiltinType::NullPtr:
      // Add proper CIR type for it? this looks mostly useful for sema related
      // things (like for overloads accepting void), for now, given that
      // `sizeof(std::nullptr_t)` is equal to `sizeof(void *)`, model
      // std::nullptr_t as !cir.ptr<!void>
      resultType = builder.getVoidPtrTy();
      break;

    case BuiltinType::OCLEvent:
      resultType = cir::OpaqueType::get(
          builder.getContext(),
          cgm.getStringAttrCached(cir::OpaqueType::getOpenCLEventTag()));
      break;

#define AMDGPU_OPAQUE_PTR_TYPE(Name, Id, SingletonId, Width, Align, AS)        \
    case BuiltinType::Id: {                                                    \
      mlir::ptr::MemorySpaceAttrInterface addrSpace;                           \
      if (AS != 0)                                                             \
        addrSpace = cir::TargetAddressSpaceAttr::get(&getMLIRContext(), AS);   \
      resultType = builder.getVoidPtrTy(addrSpace);                            \
      break;                                                                   \
    }
#define AMDGPU_NAMED_BARRIER_TYPE(Name, Id, SingletonId, Width, Align, Scope)  \
    case BuiltinType::Id:                                                       \
      resultType = cir::OpaqueType::get(                                        \
          builder.getContext(),                                                 \
          cgm.getStringAttrCached(cir::OpaqueType::getAMDGCNNamedBarrierTag()));\
      break;
#include "clang/Basic/AMDGPUTypes.def"

    default:
      cgm.errorNYI(SourceLocation(), "processing of built-in type", type);
      return {};
      break;
    }
    break;
  }

  case Type::Complex: {
    const auto *ct = cast<clang::ComplexType>(ty);
    mlir::Type elementTy = convertType(ct->getElementType());
    resultType = cir::ComplexType::get(elementTy);
    break;
  }

  case Type::LValueReference:
  case Type::RValueReference: {
    const ReferenceType *refTy = cast<ReferenceType>(ty);
    QualType elemTy = refTy->getPointeeType();
    auto pointeeType = convertTypeForMem(elemTy);
    resultType = builder.getPointerTo(pointeeType, elemTy.getAddressSpace());
    assert(resultType && "Cannot get pointer type?");
    break;
  }

  case Type::Pointer: {
    const PointerType *ptrTy = cast<PointerType>(ty);
    QualType elemTy = ptrTy->getPointeeType();
    mlir::Type pointeeType = elemTy->isConstantMatrixType()
                                 ? convertTypeForMem(elemTy)
                                 : convertType(elemTy);

    resultType = builder.getPointerTo(pointeeType, elemTy.getAddressSpace());
    break;
  }

  case Type::BlockPointer: {
    // Block pointers are opaque pointers (like void*). OGCG lowers them to
    // plain LLVM pointer types. CIR represents them as !cir.ptr<!void>.
    resultType = builder.getVoidPtrTy();
    break;
  }

  case Type::ObjCObjectPointer: {
    // ObjC object pointers are opaque pointers. OGCG maps to
    // llvm::PointerType::getUnqual. CIR uses !cir.ptr<!void>.
    resultType = builder.getVoidPtrTy();
    break;
  }

  case Type::ObjCObject:
    resultType = convertType(cast<ObjCObjectType>(ty)->getBaseType());
    break;

  case Type::ObjCInterface: {
    // ObjC interfaces are always opaque outside of the runtime. OGCG creates
    // unnamed opaque llvm::StructTypes. CIR uses void since the layout is
    // never exposed — objects are always accessed through opaque pointers.
    resultType = cgm.voidTy;
    break;
  }

  case Type::VariableArray: {
    const VariableArrayType *a = cast<VariableArrayType>(ty);
    if (a->getIndexTypeCVRQualifiers() != 0) {
      cgm.errorNYI(SourceLocation(), "non trivial array types", type);
      return {};
    }
    // VLAs resolve to the innermost element type; this matches
    // the return of alloca, and there isn't any obviously better choice.
    resultType = convertTypeForMem(a->getElementType());
    break;
  }

  case Type::IncompleteArray: {
    const IncompleteArrayType *arrTy = cast<IncompleteArrayType>(ty);
    if (arrTy->getIndexTypeCVRQualifiers() != 0) {
      cgm.errorNYI(SourceLocation(), "non trivial array types", type);
      return {};
    }

    mlir::Type elemTy = convertTypeForMem(arrTy->getElementType());
    // int X[] -> [0 x int], unless the element type is not sized.  If it is
    // unsized (e.g. an incomplete record) just use [0 x i8].
    if (!cir::isSized(elemTy)) {
      skippedLayout = true;
      elemTy = cgm.sInt8Ty;
    }

    resultType = cir::ArrayType::get(elemTy, 0);
    break;
  }

  case Type::ConstantArray: {
    const ConstantArrayType *arrTy = cast<ConstantArrayType>(ty);
    mlir::Type elemTy = convertTypeForMem(arrTy->getElementType());

    // TODO(CIR): In LLVM, "lower arrays of undefined struct type to arrays of
    // i8 just to have a concrete type"
    if (!cir::isSized(elemTy)) {
      cgm.errorNYI(SourceLocation(), "arrays of undefined struct type", type);
      return {};
    }

    resultType = cir::ArrayType::get(elemTy, arrTy->getSize().getZExtValue());
    break;
  }

  case Type::ExtVector:
  case Type::Vector: {
    const VectorType *vec = cast<VectorType>(ty);
    // Packed ext_vector_bool memory conversion is handled by
    // convertTypeForMem/emitToMemory/emitFromMemory, so convertType keeps
    // the semantic vector<bool> shape here.
    const mlir::Type elemTy = vec->getElementType()->isMFloat8Type()
                                  ? static_cast<mlir::Type>(cgm.sInt8Ty)
                                  : convertType(vec->getElementType());
    // Preserve SVE fixed-length vector kind for ABI classification.
    // These are fixed-length in memory but need scalable coercion in the ABI
    // (Pure Scalable Type handling per AAPCS64).
    bool isSveFixedLength =
        (vec->getVectorKind() == VectorKind::SveFixedLengthData ||
         vec->getVectorKind() == VectorKind::SveFixedLengthPredicate);
    resultType = cir::VectorType::get(elemTy, vec->getNumElements(),
                                      /*is_scalable=*/false,
                                      isSveFixedLength);
    break;
  }

  case Type::Enum: {
    const auto *ed = ty->castAsEnumDecl();
    if (auto integerType = ed->getIntegerType(); !integerType.isNull())
      return convertType(integerType);
    // Return a placeholder 'i32' type.  This can be changed later when the
    // type is defined (see UpdateCompletedType), but is likely to be the
    // "right" answer.
    resultType = cgm.uInt32Ty;
    break;
  }

  case Type::MemberPointer: {
    const auto *mpt = cast<MemberPointerType>(ty);
    // OGCG checks isMemberPointerConvertible and uses an opaque placeholder
    // for unconvertible member pointer types. CIR always converts directly.
    assert(!cir::MissingFeatures::memberPointerConvertibility());

    mlir::Type memberTy = convertType(mpt->getPointeeType());
    auto clsTy = mlir::cast<cir::RecordType>(
        convertType(QualType(mpt->getQualifier().getAsType(), 0)));
    if (mpt->isMemberDataPointer()) {
      resultType = cir::DataMemberType::get(memberTy, clsTy);
    } else {
      auto memberFuncTy = mlir::cast<cir::FuncType>(memberTy);
      resultType = cir::MethodType::get(memberFuncTy, clsTy);
    }
    break;
  }

  case Type::FunctionNoProto:
  case Type::FunctionProto:
    resultType = convertFunctionTypeInternal(type);
    break;

  case Type::BitInt: {
    const auto *bitIntTy = cast<BitIntType>(type);
    resultType = cir::IntType::get(&getMLIRContext(), bitIntTy->getNumBits(),
                                   bitIntTy->isSigned());
    break;
  }

  case Type::Atomic: {
    QualType valueType = cast<AtomicType>(ty)->getValueType();
    resultType = convertTypeForMem(valueType);

    // Pad out to the inflated size if necessary.
    uint64_t valueSize = astContext.getTypeSize(valueType);
    uint64_t atomicSize = astContext.getTypeSize(ty);
    if (valueSize != atomicSize) {
      assert(valueSize < atomicSize);
      auto paddingSize = (atomicSize - valueSize) / 8;
      auto *ctx = &cgm.getMLIRContext();
      auto u8Ty = cir::IntType::get(ctx, 8, /*isSigned=*/false);
      auto paddingArrayTy = cir::ArrayType::get(u8Ty, paddingSize);
      resultType = cir::RecordType::get(ctx, {resultType, paddingArrayTy},
                                        /*packed=*/false, /*padded=*/false,
                                        cir::RecordType::Struct);
    }

    break;
  }

  case Type::ConstantMatrix: {
    const auto *matrixTy = cast<ConstantMatrixType>(ty);
    resultType = cir::VectorType::get(convertType(matrixTy->getElementType()),
                                      matrixTy->getNumElementsFlattened());
    break;
  }

  case Type::DependentSizedMatrix: {
    cgm.error(SourceLocation(),
              "ClangIR code gen Not Yet Implemented: dependent matrix types");
    resultType = cgm.sInt32Ty;
    break;
  }

  default:
    cgm.errorNYI(SourceLocation(), "processing of type",
                 type->getTypeClassName());
    return {};
    break;
  }

  assert(resultType && "Type conversion not yet implemented");

  typeCache[ty] = resultType;
  return resultType;
}

mlir::Type CIRGenTypes::convertTypeForMem(clang::QualType qualType,
                                          bool forBitField) {
  if (qualType->isConstantMatrixType()) {
    const auto *matrixTy =
        qualType.getCanonicalType()->castAs<ConstantMatrixType>();
    return cir::ArrayType::get(
        convertTypeForMem(matrixTy->getElementType()),
        matrixTy->getNumElementsFlattened());
  }

  mlir::Type convertedType = convertType(qualType);

  // boolZeroExtensionForMemory: OGCG widens _Bool (i1) to its full storage
  // size for memory. CIR uses cir::BoolType abstractly; bool->i8 widening
  // is handled in LowerToLLVM.cpp's emitToMemory/emitFromMemory and
  // convertTypeForMemory.

  // OGCG handles ext_vector_bool by widening bool elements to at least one
  // byte (or to full bool storage size for HLSL). For packed (non-HLSL)
  // targets, the memory representation is an integer type iP where P=max(N,8).
  // CIR mirrors the packed path here. Non-packed/HLSL ext_vector_bool uses the
  // regular converted vector type, and bool storage widening remains deferred
  // to CIR-to-LLVM memory conversion.
  if (qualType->isPackedVectorBoolType(astContext)) {
    const auto *vecTy = qualType->castAs<VectorType>();
    unsigned numElems = vecTy->getNumElements();
    unsigned paddedSize = std::max(numElems, 8u);
    return cir::IntType::get(&getMLIRContext(), paddedSize, /*isSigned=*/false);
  }

  assert(!forBitField && "Bit fields NYI");

  // Handle BitInt memory representation: in memory, _BitInt types must be
  // stored at their full storage size as decided by the AST layout, not at
  // their semantic bit-width. For example, _BitInt(35) has semantic type i35
  // but must be stored as i64 (the AST-determined storage size). This matches
  // OGCG's ConvertTypeForMem (CodeGenTypes.cpp:142-148).
  //
  // Note: For struct field layout, large _BitInt types may require byte-array
  // representation [N x i8] when the LLVM alloc size differs from the AST type
  // size. That is handled separately by typeRequiresSplitIntoByteArray() and
  // applied in the record layout builder. convertTypeForMem always returns the
  // storage-width integer type, matching OGCG's convertTypeForLoadStore
  // (CodeGenTypes.cpp:169-176).
  if (qualType->isBitIntType()) {
    unsigned storageBits =
        static_cast<unsigned>(astContext.getTypeSize(qualType));
    const auto *bitIntTy = qualType->castAs<BitIntType>();
    if (storageBits != bitIntTy->getNumBits()) {
      return cir::IntType::get(&getMLIRContext(), storageBits,
                               bitIntTy->isSigned());
    }
  }

  // If this is a bit-precise integer type in a bitfield representation, map
  // this integer to the target-specified size.
  if (forBitField && qualType->isBitIntType())
    assert(!qualType->isBitIntType() && "Bit field with type _BitInt NYI");

  return convertedType;
}

bool CIRGenTypes::typeRequiresSplitIntoByteArray(clang::QualType qualType) {
  if (!qualType->isBitIntType())
    return false;
  const auto *bitIntTy = qualType->castAs<BitIntType>();
  unsigned storageBits =
      static_cast<unsigned>(astContext.getTypeSize(qualType));
  if (storageBits == bitIntTy->getNumBits())
    return false;
  // Check if the LLVM integer type's alloc size matches the AST type size.
  // When they differ (common for large _BitInt types where the LLVM data
  // layout's integer alignment exceeds the target's _BitInt alignment, e.g.
  // _BitInt(257) has AST size 40 bytes but LLVM's i257 alloc size 48 bytes
  // due to i128:128 alignment), a byte array [N x i8] is needed in struct
  // field layout. Matches OGCG's typeRequiresSplitIntoByteArray
  // (CodeGenTypes.cpp:158-167).
  CharUnits astSize = astContext.getTypeSizeInChars(qualType);
  unsigned semanticBits = bitIntTy->getNumBits();
  llvm::DataLayout llvmDL(astContext.getTargetInfo().getDataLayoutString());
  llvm::Align llvmAlign = llvmDL.getABIIntegerTypeAlignment(semanticBits);
  uint64_t storeSize = llvm::divideCeil(semanticBits, 8u);
  uint64_t llvmAllocSize = llvm::alignTo(storeSize, llvmAlign);
  return astSize != CharUnits::fromQuantity(llvmAllocSize);
}

mlir::Type
CIRGenTypes::convertTypeForMemForField(clang::QualType qualType) {
  if (typeRequiresSplitIntoByteArray(qualType)) {
    CharUnits astSize = astContext.getTypeSizeInChars(qualType);
    auto u8Ty = cir::IntType::get(&getMLIRContext(), 8, /*isSigned=*/false);
    return cir::ArrayType::get(u8Ty, astSize.getQuantity());
  }
  return convertTypeForMem(qualType);
}

/// Return record layout info for the given record decl.
const CIRGenRecordLayout &
CIRGenTypes::getCIRGenRecordLayout(const RecordDecl *rd) {
  const auto *key = astContext.getCanonicalTagType(rd).getTypePtr();

  // If we have already computed the layout, return it.
  auto it = cirGenRecordLayouts.find(key);
  if (it != cirGenRecordLayouts.end())
    return *it->second;

  // Compute the type information.
  convertRecordDeclType(rd);

  // Now try again.
  it = cirGenRecordLayouts.find(key);

  assert(it != cirGenRecordLayouts.end() &&
         "Unable to find record layout information for type");
  return *it->second;
}

bool CIRGenTypes::isZeroInitializable(clang::QualType t) {
  if (t->getAs<PointerType>() || t->isNullPtrType())
    return astContext.getTargetNullPointerValue(t) == 0;

  if (const auto *at = astContext.getAsArrayType(t)) {
    if (isa<IncompleteArrayType>(at))
      return true;

    if (const auto *cat = dyn_cast<ConstantArrayType>(at))
      if (astContext.getConstantArrayElementCount(cat) == 0)
        return true;

    // For non-empty arrays, check the base element type. An array of
    // non-zero-initializable elements (e.g., member pointers with non-zero
    // null representation) is itself not zero-initializable.
    t = astContext.getBaseElementType(t);
  }

  if (const auto *rd = t->getAsRecordDecl())
    return isZeroInitializable(rd);

  // We have to ask the ABI about member pointers.
  if (const MemberPointerType *mpt = t->getAs<MemberPointerType>())
    return theCXXABI.isZeroInitializable(mpt);

  // OGCG returns false for HLSL InlineSpirv types.
  if (t->getAs<HLSLInlineSpirvType>())
    return false;

  if (const auto *builtinTy = t->getAs<BuiltinType>()) {
    switch (builtinTy->getKind()) {
#define AMDGPU_OPAQUE_PTR_TYPE(Name, Id, SingletonId, Width, Align, AS)
#define AMDGPU_NAMED_BARRIER_TYPE(Name, Id, SingletonId, Width, Align, Scope)  \
    case BuiltinType::Id:                                                       \
      return false;
#include "clang/Basic/AMDGPUTypes.def"
    default:
      break;
    }
  }

  return true;
}

bool CIRGenTypes::isZeroInitializable(const RecordDecl *rd) {
  return getCIRGenRecordLayout(rd).isZeroInitializable();
}

const CIRGenFunctionInfo &
CIRGenTypes::arrangeCIRFunctionInfo(CanQualType returnType,
                                    FnInfoOpts opts,
                                    llvm::ArrayRef<CanQualType> argTypes,
                                    RequiredArgs required,
                                    FunctionType::ExtInfo extInfo) {
  assert(llvm::all_of(argTypes,
                      [](CanQualType t) { return t.isCanonicalAsParam(); }));

  bool isInstanceMethod =
      (opts & FnInfoOpts::IsInstanceMethod) == FnInfoOpts::IsInstanceMethod;
  bool isChainCall =
      (opts & FnInfoOpts::IsChainCall) == FnInfoOpts::IsChainCall;
  bool isDelegateCall =
      (opts & FnInfoOpts::IsDelegateCall) == FnInfoOpts::IsDelegateCall;

  // OGCG's arrangeLLVMFunctionInfo also takes
  // ArrayRef<FunctionProtoType::ExtParameterInfo> paramInfos, which affect
  // parameter attributes like noescape, ns_consumed, swift_context, etc.
  // CIR handles these via per-argument attributes on FuncOp (e.g.
  // cir.noescape, cir.swiftself, cir.swiftasync, cir.swifterror) rather
  // than threading ExtParameterInfo through CIRGenFunctionInfo.

  // Lookup or create unique function info.
  llvm::FoldingSetNodeID id;
  CIRGenFunctionInfo::Profile(id, isInstanceMethod, isChainCall, isDelegateCall,
                              required, returnType, argTypes, extInfo);

  void *insertPos = nullptr;
  CIRGenFunctionInfo *fi = functionInfos.FindNodeOrInsertPos(id, insertPos);
  if (fi) {
    // We found a matching function info based on id. These asserts verify that
    // it really is a match.
    assert(
        fi->getReturnType() == returnType &&
        std::equal(fi->argTypesBegin(), fi->argTypesEnd(), argTypes.begin()) &&
        "Bad match based on CIRGenFunctionInfo folding set id");
    return *fi;
  }

  // OGCG's arrangeLLVMFunctionInfo computes the effective calling convention
  // (which may differ from the source CC after ABI adjustments, e.g. CC_C
  // replaced with CC_X86StdCall on certain targets). CIR models effective CC
  // during CIR-to-LLVM ABI lowering.

  // Construction the function info. We co-allocate the ArgInfos.
  fi = CIRGenFunctionInfo::create(returnType, argTypes, required, extInfo,
                                  isInstanceMethod, isChainCall, isDelegateCall);
  functionInfos.InsertNode(fi, insertPos);

  return *fi;
}

/// Determine the calling convention for an ObjC method declaration based
/// on its calling convention attributes. Mirrors OGCG's
/// getCallingConventionForDecl in CGCall.cpp.
static CallingConv
getCallingConventionForDecl(const ObjCMethodDecl *d,
                            bool isTargetDefaultMSABI) {
  if (d->hasAttr<StdCallAttr>())
    return CC_X86StdCall;
  if (d->hasAttr<FastCallAttr>())
    return CC_X86FastCall;
  if (d->hasAttr<RegCallAttr>())
    return CC_X86RegCall;
  if (d->hasAttr<ThisCallAttr>())
    return CC_X86ThisCall;
  if (d->hasAttr<VectorCallAttr>())
    return CC_X86VectorCall;
  if (d->hasAttr<PascalAttr>())
    return CC_X86Pascal;
  if (PcsAttr *pcs = d->getAttr<PcsAttr>())
    return (pcs->getPCS() == PcsAttr::AAPCS ? CC_AAPCS : CC_AAPCS_VFP);
  if (d->hasAttr<AArch64VectorPcsAttr>())
    return CC_AArch64VectorCall;
  if (d->hasAttr<AArch64SVEPcsAttr>())
    return CC_AArch64SVEPCS;
  if (d->hasAttr<DeviceKernelAttr>())
    return CC_DeviceKernel;
  if (d->hasAttr<MSABIAttr>())
    return isTargetDefaultMSABI ? CC_C : CC_Win64;
  if (d->hasAttr<SysVABIAttr>())
    return isTargetDefaultMSABI ? CC_X86_64SysV : CC_C;
  if (d->hasAttr<PreserveMostAttr>())
    return CC_PreserveMost;
  if (d->hasAttr<PreserveAllAttr>())
    return CC_PreserveAll;
  if (d->hasAttr<PreserveNoneAttr>())
    return CC_PreserveNone;
  return CC_C;
}

const CIRGenFunctionInfo &
CIRGenTypes::arrangeObjCMethodDeclaration(const ObjCMethodDecl *md) {
  // Build the argument type list: self, _cmd (if not direct), then params.
  SmallVector<CanQualType, 16> argTys;
  argTys.push_back(
      astContext.getCanonicalParamType(md->getSelfDecl()->getType()));
  if (!md->isDirectMethod())
    argTys.push_back(
        astContext.getCanonicalParamType(astContext.getObjCSelType()));
  for (const auto *param : md->parameters())
    argTys.push_back(astContext.getCanonicalParamType(param->getType()));

  // Determine calling convention from method attributes.
  FunctionType::ExtInfo einfo;
  bool isTargetDefaultMSABI =
      astContext.getTargetInfo().getTriple().isOSWindows() ||
      astContext.getTargetInfo().getTriple().isUEFI();
  einfo = einfo.withCallingConv(
      getCallingConventionForDecl(md, isTargetDefaultMSABI));

  if (astContext.getLangOpts().ObjCAutoRefCount &&
      md->hasAttr<NSReturnsRetainedAttr>())
    einfo = einfo.withProducesResult(true);

  RequiredArgs required =
      (md->isVariadic() ? RequiredArgs(argTys.size()) : RequiredArgs::All);

  CanQualType retTy =
      astContext.getCanonicalType(md->getReturnType()).getUnqualifiedType();
  return arrangeCIRFunctionInfo(retTy, FnInfoOpts::None, argTys, required,
                                einfo);
}

const CIRGenFunctionInfo &CIRGenTypes::arrangeGlobalDeclaration(GlobalDecl gd) {
  // FIXME: Do we need to handle ObjCMethodDecl?
  if (isa<CXXConstructorDecl>(gd.getDecl()) ||
      isa<CXXDestructorDecl>(gd.getDecl()))
    return arrangeCXXStructorDeclaration(gd);

  return arrangeFunctionDeclaration(gd);
}

// When we find the full definition for a TagDecl, replace the 'opaque' type we
// previously made for it if applicable.
void CIRGenTypes::updateCompletedType(const TagDecl *td) {
  // If this is an enum being completed, then we flush all non-struct types
  // from the cache. This allows function types and other things that may be
  // derived from the enum to be recomputed.
  if (const auto *ed = dyn_cast<EnumDecl>(td)) {
    // Only flush the cache if we've actually already converted this type.
    const clang::Type *key =
        ed->getASTContext().getCanonicalTagType(ed)->getTypePtr();
    if (typeCache.count(key)) {
      // We speculated that the enum would be lowered to i32, so we only need
      // to flush the cache if this didn't happen.
      mlir::Type converted = convertType(ed->getIntegerType());
      auto intTy = mlir::dyn_cast<cir::IntType>(converted);
      if (!intTy || intTy.getWidth() != 32) {
        typeCache.clear();
        funcTypeCache.clear();
      }
    }
    // OGCG may complete debug type metadata when enum definitions are
    // finalized. CIR defers debug metadata materialization to lowering.
    return;
  }

  // If we completed a RecordDecl that we previously used and converted to an
  // anonymous type, then go ahead and complete it now.
  const auto *rd = cast<RecordDecl>(td);
  if (rd->isDependentType())
    return;

  // Only complete if we converted it already. If we haven't converted it yet,
  // we'll just do it lazily.
  if (recordDeclTypes.count(astContext.getCanonicalTagType(rd).getTypePtr()))
    convertRecordDeclType(rd);

  // OGCG may complete debug type metadata when record definitions are
  // finalized. CIR defers debug metadata materialization to lowering.
}
