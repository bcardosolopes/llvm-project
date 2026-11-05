//=== RecordLayoutBuilder.cpp - Helper class for building record layouts ---==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file partially mimics clang/lib/AST/CGRecordLayoutBuilder.cpp. The
// queries are adapted to operate on the CIR dialect, however.
//
//===----------------------------------------------------------------------===//

#include "CIRLowerContext.h"
#include "CIRRecordLayout.h"
#include "mlir/IR/Types.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/RecordLayout.h"
#include "clang/CIR/Dialect/IR/CIRTypes.h"
#include "clang/CIR/MissingFeatures.h"

using namespace mlir;
using namespace cir;

namespace {

//===-----------------------------------------------------------------------==//
// EmptySubobjectMap Implementation
//===----------------------------------------------------------------------===//

/// Keeps track of which empty subobjects exist at different offsets while
/// laying out a C++ class.
class EmptySubobjectMap {
  [[maybe_unused]] const CIRLowerContext &Context;
  uint64_t CharWidth;

  /// The class whose empty entries we're keeping track of.
  const RecordType Class;

  /// The highest offset known to contain an empty base subobject.
  clang::CharUnits MaxEmptyClassOffset;

  /// Compute the size of the largest base or member subobject that is empty.
  void ComputeEmptySubobjectSizes();

public:
  /// This holds the size of the largest empty subobject (either a base
  /// or a member). Will be zero if the record being built doesn't contain
  /// any empty classes.
  clang::CharUnits SizeOfLargestEmptySubobject;

  EmptySubobjectMap(const CIRLowerContext &Context, const RecordType Class)
      : Context(Context), CharWidth(Context.getCharWidth()), Class(Class) {
    ComputeEmptySubobjectSizes();
  }

  /// Return whether a field can be placed at the given offset.
  bool canPlaceFieldAtOffset(const Type Ty, clang::CharUnits Offset);
};

void EmptySubobjectMap::ComputeEmptySubobjectSizes() {
  // Check the bases.
  if (auto ast = Class.getAst()) {
    const clang::RecordDecl *RD = ast.getRawDecl();
    if (const auto *CXXRD = llvm::dyn_cast<clang::CXXRecordDecl>(RD)) {
      for (const auto &Base : CXXRD->bases()) {
        const auto *BaseRD = Base.getType()->getAsCXXRecordDecl();
        if (!BaseRD)
          continue;

        const clang::CXXRecordDecl *BaseDef = BaseRD->getDefinition();
        if (!BaseDef)
          continue;

        const clang::ASTRecordLayout &Layout =
            RD->getASTContext().getASTRecordLayout(BaseDef);
        clang::CharUnits EmptySize = BaseDef->isEmpty()
                                         ? Layout.getSize()
                                         : Layout.getSizeOfLargestEmptySubobject();

        if (EmptySize > SizeOfLargestEmptySubobject)
          SizeOfLargestEmptySubobject = EmptySize;
      }
    }
  }

  // Check the fields.
  for (const auto FT : Class.getMembers()) {
    // qualifiedTypes is no longer tracked as a MissingFeatures guard.
    const auto RT = dyn_cast<RecordType>(FT);

    // We only care about record types.
    if (!RT)
      continue;

    clang::CharUnits EmptySize;
    const CIRRecordLayout &Layout = Context.getCIRRecordLayout(RT);

    // Check if the nested record is empty via AST info.
    bool IsEmpty = false;
    if (auto ast = RT.getAst()) {
      if (auto *cxxDecl =
              clang::dyn_cast<clang::CXXRecordDecl>(ast.getRawDecl()))
        IsEmpty = cxxDecl->isEmpty();
    }

    if (IsEmpty) {
      // If the class decl is empty, get its size.
      EmptySize = Layout.getSize();
    } else {
      // Otherwise, we get the largest empty subobject for the decl.
      EmptySize = Layout.getSizeOfLargestEmptySubobject();
    }

    if (EmptySize > SizeOfLargestEmptySubobject)
      SizeOfLargestEmptySubobject = EmptySize;
  }
}

bool EmptySubobjectMap::canPlaceFieldAtOffset(const Type Ty,
                                              clang::CharUnits Offset) {
  cir_cconv_unreachable("NYI");
}

//===-----------------------------------------------------------------------==//
// ItaniumRecordLayoutBuilder Implementation
//===----------------------------------------------------------------------===//

class ItaniumRecordLayoutBuilder {
protected:
  // FIXME(cir):  Remove this and make the appropriate fields public.
  friend class cir::CIRLowerContext;

  const CIRLowerContext &Context;

  EmptySubobjectMap *EmptySubobjects;

  /// Size - The current size of the record layout.
  uint64_t Size;

  /// Alignment - The current alignment of the record layout.
  clang::CharUnits Alignment;

  /// PreferredAlignment - The preferred alignment of the record layout.
  clang::CharUnits PreferredAlignment;

  /// The alignment if attribute packed is not used.
  clang::CharUnits UnpackedAlignment;

  /// \brief The maximum of the alignments of top-level members.
  clang::CharUnits UnadjustedAlignment;

  llvm::SmallVector<uint64_t, 16> FieldOffsets;

  /// Whether the external AST source has provided a layout for this
  /// record.
  unsigned UseExternalLayout : 1;

  /// Whether we need to infer alignment, even when we have an
  /// externally-provided layout.
  unsigned InferAlignment : 1;

  /// Packed - Whether the record is packed or not.
  unsigned Packed : 1;

  unsigned IsUnion : 1;

  unsigned IsMac68kAlign : 1;

  unsigned IsNaturalAlign : 1;

  unsigned IsMsStruct : 1;

  /// UnfilledBitsInLastUnit - If the last field laid out was a bitfield,
  /// this contains the number of bits in the last unit that can be used for
  /// an adjacent bitfield if necessary.  The unit in question is usually
  /// a byte, but larger units are used if IsMsStruct.
  unsigned char UnfilledBitsInLastUnit;

  /// LastBitfieldStorageUnitSize - If IsMsStruct, represents the size of the
  /// storage unit of the previous field if it was a bitfield.
  unsigned char LastBitfieldStorageUnitSize;

  /// MaxFieldAlignment - The maximum allowed field alignment. This is set by
  /// #pragma pack.
  clang::CharUnits MaxFieldAlignment;

  /// DataSize - The data size of the record being laid out.
  uint64_t DataSize;

  clang::CharUnits NonVirtualSize;
  clang::CharUnits NonVirtualAlignment;
  clang::CharUnits PreferredNVAlignment;

  /// If we've laid out a field but not included its tail padding in Size yet,
  /// this is the size up to the end of that field.
  clang::CharUnits PaddedFieldSize;

  /// The primary base class (if one exists) of the class we're laying out.
  const RecordType PrimaryBase;

  /// Whether the primary base of the class we're laying out is virtual.
  bool PrimaryBaseIsVirtual;

  /// Whether the class provides its own vtable/vftbl pointer, as opposed to
  /// inheriting one from a primary base class.
  bool HasOwnVFPtr;

  /// the flag of field offset changing due to packed attribute.
  bool HasPackedField;

  /// An auxiliary field used for AIX. When there are OverlappingEmptyFields
  /// existing in the aggregate, the flag shows if the following first non-empty
  /// or empty-but-non-overlapping field has been handled, if any.
  bool HandledFirstNonOverlappingEmptyField;

public:
  ItaniumRecordLayoutBuilder(const CIRLowerContext &Context,
                             EmptySubobjectMap *EmptySubobjects)
      : Context(Context), EmptySubobjects(EmptySubobjects), Size(0),
        Alignment(clang::CharUnits::One()),
        PreferredAlignment(clang::CharUnits::One()),
        UnpackedAlignment(clang::CharUnits::One()),
        UnadjustedAlignment(clang::CharUnits::One()), UseExternalLayout(false),
        InferAlignment(false), Packed(false), IsUnion(false),
        IsMac68kAlign(false),
        IsNaturalAlign(!Context.getTargetInfo().getTriple().isOSAIX()),
        IsMsStruct(false), UnfilledBitsInLastUnit(0),
        LastBitfieldStorageUnitSize(0),
        MaxFieldAlignment(clang::CharUnits::Zero()), DataSize(0),
        NonVirtualSize(clang::CharUnits::Zero()),
        NonVirtualAlignment(clang::CharUnits::One()),
        PreferredNVAlignment(clang::CharUnits::One()),
        PaddedFieldSize(clang::CharUnits::Zero()), PrimaryBaseIsVirtual(false),
        HasOwnVFPtr(false), HasPackedField(false),
        HandledFirstNonOverlappingEmptyField(false) {}

  void layout(const RecordType D);

  void layoutFields(const RecordType D);
  void layoutField(const Type Ty, bool InsertExtraPadding);

  void UpdateAlignment(clang::CharUnits NewAlignment,
                       clang::CharUnits UnpackedNewAlignment,
                       clang::CharUnits PreferredAlignment);

  void checkFieldPadding(uint64_t Offset, uint64_t UnpaddedOffset,
                         uint64_t UnpackedOffset, unsigned UnpackedAlign,
                         bool isPacked, const Type Ty);

  clang::CharUnits getSize() const {
    cir_cconv_assert(Size % Context.getCharWidth() == 0);
    return Context.toCharUnitsFromBits(Size);
  }
  uint64_t getSizeInBits() const { return Size; }

  void setSize(clang::CharUnits NewSize) { Size = Context.toBits(NewSize); }
  void setSize(uint64_t NewSize) { Size = NewSize; }

  clang::CharUnits getDataSize() const {
    cir_cconv_assert(DataSize % Context.getCharWidth() == 0);
    return Context.toCharUnitsFromBits(DataSize);
  }

  /// Initialize record layout for the given record decl.
  void initializeLayout(const Type Ty);

  /// Finalize record layout. Adjust record size based on the alignment.
  void finishLayout(const RecordType D);

  uint64_t getDataSizeInBits() const { return DataSize; }

  void setDataSize(clang::CharUnits NewSize) {
    DataSize = Context.toBits(NewSize);
  }
  void setDataSize(uint64_t NewSize) { DataSize = NewSize; }
};

void ItaniumRecordLayoutBuilder::layout(const RecordType RT) {
  initializeLayout(RT);

  // CIR's RecordType already has base subobjects and vtable pointers flattened
  // into its member list, so layoutFields handles field offsets correctly for
  // both C and C++ records. For C++ records, we query the AST to populate
  // HasOwnVFPtr, NonVirtualSize, and NonVirtualAlignment metadata.
  if (auto ast = RT.getAst()) {
    if (const auto *CXXRD =
            llvm::dyn_cast<clang::CXXRecordDecl>(ast.getRawDecl())) {
      const clang::ASTRecordLayout &ASTLayout =
          CXXRD->getASTContext().getASTRecordLayout(CXXRD);
      HasOwnVFPtr = ASTLayout.hasOwnVFPtr();
    }
  }

  layoutFields(RT);

  // For C++ records, set NonVirtualSize and NonVirtualAlignment from the AST
  // layout. These values are used by ABI classification code downstream.
  if (auto ast = RT.getAst()) {
    if (const auto *CXXRD =
            llvm::dyn_cast<clang::CXXRecordDecl>(ast.getRawDecl())) {
      const clang::ASTRecordLayout &ASTLayout =
          CXXRD->getASTContext().getASTRecordLayout(CXXRD);
      NonVirtualSize = ASTLayout.getNonVirtualSize();
      NonVirtualAlignment = ASTLayout.getNonVirtualAlignment();
      PreferredNVAlignment = ASTLayout.getPreferredNVAlignment();
    }
  }

  finishLayout(RT);
}

void ItaniumRecordLayoutBuilder::initializeLayout(const mlir::Type Ty) {
  if (const auto RT = dyn_cast<RecordType>(Ty)) {
    IsUnion = RT.isUnion();
    Packed = RT.getPacked();
    // recordDeclIsMSStruct is no longer tracked as a MissingFeatures guard.
  }

  // Non-record cases (e.g. ObjC) are not lowered through this path yet.
  if (!isa<RecordType>(Ty))
    cir_cconv_unreachable("NYI: non-record layout in ItaniumRecordLayoutBuilder");

  // Honor the default struct packing maximum alignment flag.
  if ([[maybe_unused]] unsigned DefaultMaxFieldAlignment =
          Context.getLangOpts().PackStruct) {
    cir_cconv_unreachable("NYI");
  }

  // OGCG checks mac68k alignment (#pragma options align=mac68k) here.
  // Mac68k alignment is an old Apple-specific mode, not relevant for CIR
  // targets (arm64/x86_64 Linux, Android, GPUs).
  {
    if (const auto RT = dyn_cast<RecordType>(Ty)) {
      if (auto ast = RT.getAst()) {
        if (ast.getRawDecl()->hasAttr<clang::AlignNaturalAttr>())
          IsNaturalAlign = true;
      }
    }

    if (cir::MissingFeatures::declHasMaxFieldAlignmentAttr())
      cir_cconv_unreachable("NYI");

    // Apply __attribute__((aligned(N))) / alignas(N) alignment override.
    // Matches OGCG's RecordLayoutBuilder::initializeLayout which calls
    // D->getMaxAlignment() to get the maximum alignment from attributes.
    // Also check field-level alignment attributes, since CIR's member types
    // don't carry __attribute__((aligned(N))) from individual fields.
    if (const auto RT = dyn_cast<RecordType>(Ty)) {
      if (auto ast = RT.getAst()) {
        const clang::RecordDecl *RD = ast.getRawDecl();
        // Check record-level alignment attribute.
        unsigned maxAlign = RD->getMaxAlignment();
        // Check field-level alignment attributes. OGCG handles this in
        // LayoutField via FieldDecl::getMaxAlignment(), but CIR's member
        // types don't carry field attributes, so we check them upfront.
        for (const auto *FD : RD->fields()) {
          if (unsigned FA = FD->getMaxAlignment())
            maxAlign = std::max(maxAlign, FA);
        }
        if (maxAlign) {
          auto maxAlignChars =
              clang::CharUnits::fromQuantity(maxAlign / 8);
          UpdateAlignment(maxAlignChars, maxAlignChars, maxAlignChars);
        }
      }
    }
  }

  HandledFirstNonOverlappingEmptyField =
      !Context.getTargetInfo().defaultsToAIXPowerAlignment() || IsNaturalAlign;

  // OGCG queries an external AST source (DWARF/XCOFF) for layout overrides
  // here. CIR's target lowering doesn't support external layout sources.
}

void ItaniumRecordLayoutBuilder::layoutField(const Type D,
                                             bool InsertExtraPadding) {
  // OGCG checks isEmpty() and [[no_unique_address]] here to set
  // IsOverlappingEmptyField. CIR's RecordType already accounts for
  // [[no_unique_address]] in its flattened member list during CIRGen,
  // so overlapping empty fields are handled at the CIR type level.
  bool IsOverlappingEmptyField = false;

  clang::CharUnits FieldOffset = (IsUnion || IsOverlappingEmptyField)
                                     ? clang::CharUnits::Zero()
                                     : getDataSize();

  const bool DefaultsToAIXPowerAlignment =
      Context.getTargetInfo().defaultsToAIXPowerAlignment();
  bool FoundFirstNonOverlappingEmptyFieldForAIX = false;
  if (DefaultsToAIXPowerAlignment && !HandledFirstNonOverlappingEmptyField) {
    cir_cconv_unreachable("NYI");
  }

  // CIR record lowering pre-packs bitfield declarations into concrete storage
  // members (integers/byte arrays), so this layout path operates on storage
  // types and does not need FieldDecl::isBitField checks.

  uint64_t UnpaddedFieldOffset = getDataSizeInBits() - UnfilledBitsInLastUnit;
  // Reset the unfilled bits.
  UnfilledBitsInLastUnit = 0;
  LastBitfieldStorageUnitSize = 0;

  llvm::Triple Target = Context.getTargetInfo().getTriple();

  clang::AlignRequirementKind AlignRequirement =
      clang::AlignRequirementKind::None;
  clang::CharUnits FieldSize;
  clang::CharUnits FieldAlign;
  // The amount of this class's dsize occupied by the field.
  // This is equal to FieldSize unless we're permitted to pack
  // into the field's tail padding.
  clang::CharUnits EffectiveFieldSize;

  auto setDeclInfo = [&](bool IsIncompleteArrayType) {
    auto TI = Context.getTypeInfoInChars(D);
    FieldAlign = TI.Align;
    // Flexible array members don't have any size, but they have to be
    // aligned appropriately for their element type.
    EffectiveFieldSize = FieldSize =
        IsIncompleteArrayType ? clang::CharUnits::Zero() : TI.Width;
    AlignRequirement = TI.AlignRequirement;
  };

  if (isa<ArrayType>(D) && cast<ArrayType>(D).getSize() == 0) {
    setDeclInfo(true /* IsIncompleteArrayType */);
  } else {
    setDeclInfo(false /* IsIncompleteArrayType */);

    if (IsMsStruct)
      cir_cconv_unreachable("NYI");
  }

  // In CIR, per-field packed attributes are not tracked separately;
  // the record-level Packed flag applies to all fields. However, non-POD
  // C++ record fields in packed structs retain their natural alignment
  // (Clang ABI > v15 on Linux/Android targets).
  bool FieldPacked = Packed;
  if (Packed) {
    if (auto RT = mlir::dyn_cast<RecordType>(D)) {
      if (auto ast = RT.getAst()) {
        if (const auto *CXXRD =
                llvm::dyn_cast<clang::CXXRecordDecl>(ast.getRawDecl())) {
          if (!CXXRD->isPOD())
            FieldPacked = false;
        }
      }
    }
  }

  // When used as part of a typedef, or together with a 'packed' attribute, the
  // 'aligned' attribute can be used to decrease alignment. In that case, it
  // overrides any computed alignment we have, and there is no need to upgrade
  // the alignment.
  auto alignedAttrCanDecreaseAIXAlignment = [AlignRequirement, FieldPacked] {
    // Enum alignment sources can be safely ignored here, because this only
    // helps decide whether we need the AIX alignment upgrade, which only
    // applies to floating-point types.
    return AlignRequirement == clang::AlignRequirementKind::RequiredByTypedef ||
           (AlignRequirement == clang::AlignRequirementKind::RequiredByRecord &&
            FieldPacked);
  };

  // The AIX `power` alignment rules apply the natural alignment of the
  // "first member" if it is of a floating-point data type (or is an aggregate
  // whose recursively "first" member or element is such a type). The alignment
  // associated with these types for subsequent members use an alignment value
  // where the floating-point data type is considered to have 4-byte alignment.
  //
  // For the purposes of the foregoing: vtable pointers, non-empty base classes,
  // and zero-width bit-fields count as prior members; members of empty class
  // types marked `no_unique_address` are not considered to be prior members.
  clang::CharUnits PreferredAlign = FieldAlign;
  if (DefaultsToAIXPowerAlignment && !alignedAttrCanDecreaseAIXAlignment() &&
      (FoundFirstNonOverlappingEmptyFieldForAIX || IsNaturalAlign)) {
    cir_cconv_unreachable("NYI");
  }

  // The align if the field is not packed. This is to check if the attribute
  // was unnecessary (-Wpacked).
  clang::CharUnits UnpackedFieldAlign = FieldAlign;
  clang::CharUnits PackedFieldAlign = clang::CharUnits::One();
  clang::CharUnits UnpackedFieldOffset = FieldOffset;
  // clang::CharUnits OriginalFieldAlign = UnpackedFieldAlign;

  // OGCG applies FieldDecl::getMaxAlignment() at this point. CIR member types
  // do not carry per-field attribute metadata, so initializeLayout pre-applies
  // the maximum alignment constraints discovered from the backing AST record
  // and fields. No additional per-field max-alignment adjustment is needed
  // here.
  clang::CharUnits MaxAlignmentInChars = clang::CharUnits::Zero();
  PackedFieldAlign = std::max(PackedFieldAlign, MaxAlignmentInChars);
  PreferredAlign = std::max(PreferredAlign, MaxAlignmentInChars);
  UnpackedFieldAlign = std::max(UnpackedFieldAlign, MaxAlignmentInChars);

  // The maximum field alignment overrides the aligned attribute.
  if (!MaxFieldAlignment.isZero()) {
    cir_cconv_unreachable("NYI");
  }

  if (!FieldPacked)
    FieldAlign = UnpackedFieldAlign;
  if (DefaultsToAIXPowerAlignment)
    cir_cconv_unreachable("NYI");
  if (FieldPacked) {
    PreferredAlign = PackedFieldAlign;
    FieldAlign = PackedFieldAlign;
  }

  clang::CharUnits AlignTo =
      !DefaultsToAIXPowerAlignment ? FieldAlign : PreferredAlign;
  // Round up the current record size to the field's alignment boundary.
  FieldOffset = FieldOffset.alignTo(AlignTo);
  UnpackedFieldOffset = UnpackedFieldOffset.alignTo(UnpackedFieldAlign);

  if (UseExternalLayout) {
    cir_cconv_unreachable("NYI");
  } else {
    if (!IsUnion && EmptySubobjects) {
      // Check if we can place the field at this offset.
      while (/*!EmptySubobjects->CanPlaceFieldAtOffset(D, FieldOffset)*/
             false) {
        cir_cconv_unreachable("NYI");
      }
    }
  }

  // Place this field at the current location.
  FieldOffsets.push_back(Context.toBits(FieldOffset));

  if (!UseExternalLayout)
    checkFieldPadding(Context.toBits(FieldOffset), UnpaddedFieldOffset,
                      Context.toBits(UnpackedFieldOffset),
                      Context.toBits(UnpackedFieldAlign), FieldPacked, D);

  if (InsertExtraPadding) {
    cir_cconv_unreachable("NYI");
  }

  // Reserve space for this field.
  if (!IsOverlappingEmptyField) {
    uint64_t EffectiveFieldSizeInBits = Context.toBits(EffectiveFieldSize);
    if (IsUnion)
      setDataSize(std::max(getDataSizeInBits(), EffectiveFieldSizeInBits));
    else
      setDataSize(FieldOffset + EffectiveFieldSize);

    PaddedFieldSize = std::max(PaddedFieldSize, FieldOffset + FieldSize);
    setSize(std::max(getSizeInBits(), getDataSizeInBits()));
  } else {
    cir_cconv_unreachable("NYI");
  }

  // Remember max struct/class ABI-specified alignment.
  UnadjustedAlignment = std::max(UnadjustedAlignment, FieldAlign);
  UpdateAlignment(FieldAlign, UnpackedFieldAlign, PreferredAlign);

  // For checking the alignment of inner fields against the alignment of the
  // parent record, this lowering path now relies on record-layout context
  // tracked by the builder/AST mapping rather than a standalone FieldDecl
  // abstraction hook.

  // OGCG emits a warn_unpacked_field diagnostic here when Packed &&
  // !FieldPacked && PackedFieldAlign < FieldAlign. CIR's target lowering
  // doesn't emit clang diagnostics.
}

void ItaniumRecordLayoutBuilder::layoutFields(const RecordType D) {
  // Layout each field, for now, just sequentially, respecting alignment.  In
  // the future, this will need to be tweakable by targets.
  cir_cconv_assert(!Context.getLangOpts().SanitizeAddressFieldPadding);
  bool InsertExtraPadding = false;
  // Flexible-array-member checks are handled directly via AST queries.
  bool HasFlexibleArrayMember = false;
  for (const auto FT : D.getMembers()) {
    layoutField(FT, InsertExtraPadding && (FT != D.getMembers().back() ||
                                           !HasFlexibleArrayMember));
  }
}

void ItaniumRecordLayoutBuilder::finishLayout(const RecordType D) {
  // If we have any remaining field tail padding, include that in the overall
  // size.
  setSize(std::max(getSizeInBits(), (uint64_t)Context.toBits(PaddedFieldSize)));

  // Finally, round the size of the record up to the alignment of the
  // record itself.
  uint64_t roundedSize = llvm::alignTo(
      getSizeInBits(),
      Context.toBits(!Context.getTargetInfo().defaultsToAIXPowerAlignment()
                         ? Alignment
                         : PreferredAlignment));

  if (UseExternalLayout) {
    cir_cconv_unreachable("NYI");
  }

  // Set the size to the final size.
  setSize(roundedSize);
}

void ItaniumRecordLayoutBuilder::UpdateAlignment(
    clang::CharUnits NewAlignment, clang::CharUnits UnpackedNewAlignment,
    clang::CharUnits PreferredNewAlignment) {
  // The alignment is not modified when using 'mac68k' alignment or when
  // we have an externally-supplied layout that also provides overall alignment.
  if (IsMac68kAlign || (UseExternalLayout && !InferAlignment))
    return;

  if (NewAlignment > Alignment) {
    cir_cconv_assert(llvm::isPowerOf2_64(NewAlignment.getQuantity()) &&
                     "Alignment not a power of 2");
    Alignment = NewAlignment;
  }

  if (UnpackedNewAlignment > UnpackedAlignment) {
    cir_cconv_assert(llvm::isPowerOf2_64(UnpackedNewAlignment.getQuantity()) &&
                     "Alignment not a power of 2");
    UnpackedAlignment = UnpackedNewAlignment;
  }

  if (PreferredNewAlignment > PreferredAlignment) {
    cir_cconv_assert(llvm::isPowerOf2_64(PreferredNewAlignment.getQuantity()) &&
                     "Alignment not a power of 2");
    PreferredAlignment = PreferredNewAlignment;
  }
}

void ItaniumRecordLayoutBuilder::checkFieldPadding(
    uint64_t Offset, uint64_t UnpaddedOffset, uint64_t UnpackedOffset,
    unsigned UnpackedAlign, bool isPacked, const Type Ty) {
  // We let objc ivars without warning, objc interfaces generally are not used
  // for padding tricks.
  // CIR record-layout lowering does not currently route ObjC ivar diagnostics
  // through this path; this hook is effectively a no-op, matching the existing
  // behavior of not issuing padding diagnostics here.

  // FIXME(cir): Should the following be skiped in CIR?
  // Don't warn about records created without a SourceLocation.  This can
  // be done by clients of the AST, such as codegen.

  // OGCG uses CharBitNum here for -Wpadded diagnostics. CIR's target lowering
  // doesn't emit clang diagnostics from this path, so no byte-size conversion
  // is needed.
  if (isPacked && Offset != UnpackedOffset) {
    HasPackedField = true;
  }
}

//===-----------------------------------------------------------------------==//
// Misc. Helper Functions
//===----------------------------------------------------------------------===//

bool isMsLayout(const CIRLowerContext &Context) {
  return Context.getTargetInfo().getCXXABI().isMicrosoft();
}

/// Does the target C++ ABI require us to skip over the tail-padding
/// of the given class (considering it as a base class) when allocating
/// objects?
static bool mustSkipTailPadding(clang::TargetCXXABI ABI, const RecordType RD) {
  switch (ABI.getTailPaddingUseRules()) {
  case clang::TargetCXXABI::AlwaysUseTailPadding:
    return false;

  case clang::TargetCXXABI::UseTailPaddingUnlessPOD03:
    // http://itanium-cxx-abi.github.io/cxx-abi/abi.html#POD :
    //   In general, a type is considered a POD for the purposes of
    //   layout if it is a POD type (in the sense of ISO C++
    //   [basic.types]). However, a POD-struct or POD-union (in the
    //   sense of ISO C++ [class]) with a bitfield member whose
    //   declared width is wider than the declared type of the
    //   bitfield is not a POD for the purpose of layout.  Similarly,
    //   an array type is not a POD for the purpose of layout if the
    //   element type of the array is not a POD for the purpose of
    //   layout.
    //
    //   Where references to the ISO C++ are made in this paragraph,
    //   the Technical Corrigendum 1 version of the standard is
    //   intended.
    if (auto ast = RD.getAst()) {
      if (const auto *CXXRD =
              llvm::dyn_cast<clang::CXXRecordDecl>(ast.getRawDecl()))
        return CXXRD->isPOD();
    }
    // C records are always POD — must skip tail padding (no reuse).
    return true;

  case clang::TargetCXXABI::UseTailPaddingUnlessPOD11:
    // This is equivalent to RD->getTypeForDecl().isCXX11PODType(),
    // but with a lot of abstraction penalty stripped off.  This does
    // assume that these properties are set correctly even in C++98
    // mode; fortunately, that is true because we want to assign
    // consistently semantics to the type-traits intrinsics (or at
    // least as many of them as possible).
    if (auto ast = RD.getAst()) {
      if (const auto *CXXRD =
              llvm::dyn_cast<clang::CXXRecordDecl>(ast.getRawDecl()))
        return CXXRD->isTrivial() && CXXRD->isCXX11StandardLayout();
    }
    // C records are always trivial and standard-layout — skip tail padding.
    return true;
  }

  cir_cconv_unreachable("bad tail-padding use kind");
}

} // namespace

/// Get or compute information about the layout of the specified record
/// (struct/union/class), which indicates its size and field position
/// information.
const CIRRecordLayout &CIRLowerContext::getCIRRecordLayout(const Type D) const {
  cir_cconv_assert(isa<RecordType>(D) && "Not a record type");
  auto RT = dyn_cast<RecordType>(D);

  cir_cconv_assert(RT.isComplete() &&
                   "Cannot get layout of forward declarations!");

  // Check the cache first.
  auto it = CIRRecordLayoutMap.find(D);
  if (it != CIRRecordLayoutMap.end())
    return *it->second;

  // FIXME(cir): Use a more MLIR-based approach by using it's buitin data layout
  // features, such as interfaces, cacheing, and the DLTI dialect.

  const CIRRecordLayout *NewEntry = nullptr;

  bool needsMicrosoftCXXLayout = false;
  if (isMsLayout(*this)) {
    if (auto ast = RT.getAst())
      needsMicrosoftCXXLayout =
          llvm::isa<clang::CXXRecordDecl>(ast.getRawDecl());
  }

  if (needsMicrosoftCXXLayout) {
    cir_cconv_unreachable("NYI");
  } else {
    EmptySubobjectMap EmptySubobjects(*this, RT);
    ItaniumRecordLayoutBuilder Builder(*this, &EmptySubobjects);
    Builder.layout(RT);

    // CIR's record types may not accurately reflect C-level packing: the
    // CIR 'packed' flag indicates LLVM-level struct packing, not C-level
    // __attribute__((packed)). This can cause the builder to compute a
    // different alignment than the AST. Always sync with the AST's
    // alignment which correctly accounts for packed, aligned, vtable
    // pointers, base classes, and virtual bases.
    if (auto ast = RT.getAst()) {
      const clang::RecordDecl *RD = ast.getRawDecl();
      const clang::ASTRecordLayout &ASTLayout =
          RD->getASTContext().getASTRecordLayout(RD);
      clang::CharUnits ASTAlign = ASTLayout.getAlignment();
      if (ASTAlign != Builder.Alignment) {
        Builder.Alignment = ASTAlign;
        Builder.PreferredAlignment =
            std::max(Builder.PreferredAlignment, ASTAlign);
        // Re-round from the data size (not the already-rounded size) to the
        // new alignment, since finishLayout already rounded to the builder's
        // alignment which may differ from the AST's.
        uint64_t RoundedSize =
            llvm::alignTo(Builder.getDataSizeInBits(), toBits(ASTAlign));
        Builder.setSize(RoundedSize);
      }
      // Always sync UnadjustedAlignment from AST. CIR's initializeLayout
      // pre-applies field-level __aligned__ attributes to Alignment (since
      // CIR member types don't carry per-field attributes), but this
      // doesn't update UnadjustedAlignment. OGCG applies field-level
      // __aligned__ in LayoutField which naturally propagates to
      // UnadjustedAlignment. Syncing here ensures UnadjustedAlignment
      // includes field-level alignment attributes, which is needed for
      // AAPCS ABI classification (e.g. HFA alignstack computation).
      Builder.UnadjustedAlignment =
          std::max(Builder.UnadjustedAlignment,
                   ASTLayout.getUnadjustedAlignment());
    }

    // In certain situations, we are allowed to lay out objects in the
    // tail-padding of base classes.  This is ABI-dependent.
    // FIXME: this should be stored in the record layout.
    bool skipTailPadding = mustSkipTailPadding(getTargetInfo().getCXXABI(), RT);

    // FIXME: This should be done in FinalizeLayout.
    clang::CharUnits DataSize =
        skipTailPadding ? Builder.getSize() : Builder.getDataSize();
    clang::CharUnits NonVirtualSize =
        skipTailPadding ? DataSize : Builder.NonVirtualSize;
    // Determine if this is a dynamic C++ class (has vtable that derived
    // classes could extend). Query the AST since CIR RecordType doesn't
    // carry this directly.
    bool isDynamicClass = false;
    if (auto ast = RT.getAst()) {
      if (const auto *CXXRD =
              llvm::dyn_cast<clang::CXXRecordDecl>(ast.getRawDecl()))
        isDynamicClass = CXXRD->isDynamicClass();
    }

    // Owned by CIRLowerContext via CIRRecordLayoutMap; freed in its destructor.
    NewEntry = new CIRRecordLayout(
        *this, Builder.getSize(), Builder.Alignment, Builder.PreferredAlignment,
        Builder.UnadjustedAlignment,
        /*RequiredAlignment : used by MS-ABI)*/
        Builder.Alignment, Builder.HasOwnVFPtr, isDynamicClass,
        clang::CharUnits::fromQuantity(-1), DataSize, Builder.FieldOffsets,
        NonVirtualSize, Builder.NonVirtualAlignment,
        Builder.PreferredNVAlignment,
        EmptySubobjects.SizeOfLargestEmptySubobject, Builder.PrimaryBase,
        Builder.PrimaryBaseIsVirtual, nullptr, false, false);
  }

  // Cache the layout and return.
  CIRRecordLayoutMap[D] = NewEntry;
  return *NewEntry;
}
