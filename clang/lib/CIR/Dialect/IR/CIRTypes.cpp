//===- CIRTypes.cpp - MLIR CIR Types --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the types in the CIR dialect.
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/Dialect/IR/CIRTypes.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/LLVMIR/LLVMTypes.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/DialectImplementation.h"
#include "mlir/IR/MLIRContext.h"
#include "clang/Basic/AddressSpaces.h"
#include "clang/CIR/Dialect/IR/CIRAttrs.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/IR/CIRTypesDetails.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/TypeSwitch.h"

namespace cir {
class GlobalOp;
}

//===----------------------------------------------------------------------===//
// CIR Helpers
//===----------------------------------------------------------------------===//
bool cir::isSized(mlir::Type ty) {
  if (auto sizedTy = mlir::dyn_cast<cir::SizedTypeInterface>(ty))
    return sizedTy.isSized();
  return false;
}

bool cir::containsMemberPointer(mlir::Type ty) {
  if (mlir::isa<cir::MethodType, cir::DataMemberType>(ty))
    return true;
  if (auto ptrTy = mlir::dyn_cast<cir::PointerType>(ty))
    return containsMemberPointer(ptrTy.getPointee());
  if (auto arrTy = mlir::dyn_cast<cir::ArrayType>(ty))
    return containsMemberPointer(arrTy.getElementType());
  if (auto vecTy = mlir::dyn_cast<cir::VectorType>(ty))
    return containsMemberPointer(vecTy.getElementType());
  if (auto fnTy = mlir::dyn_cast<cir::FuncType>(ty))
    return llvm::any_of(fnTy.getInputs(), containsMemberPointer) ||
           containsMemberPointer(fnTy.getReturnType());
  if (auto recTy = mlir::dyn_cast<cir::RecordType>(ty))
    return !recTy.getName() && !recTy.isIncomplete() &&
           llvm::any_of(recTy.getMembers(), containsMemberPointer);
  return false;
}

llvm::TypeSize
cir::OpaqueType::getTypeSizeInBits(const mlir::DataLayout &dataLayout,
                                   mlir::DataLayoutEntryListRef params) const {
  if (getTag() == getAArch64SVCountTag())
    return llvm::TypeSize::getFixed(16);
  if (getTag() == getAMDGCNNamedBarrierTag())
    return llvm::TypeSize::getFixed(128);
  llvm_unreachable("unsized opaque type has no data layout");
}

uint64_t cir::OpaqueType::getABIAlignment(
    const mlir::DataLayout &dataLayout,
    mlir::DataLayoutEntryListRef params) const {
  if (getTag() == getAArch64SVCountTag())
    return 2;
  if (getTag() == getAMDGCNNamedBarrierTag())
    return 4;
  llvm_unreachable("unsized opaque type has no ABI alignment");
}

//===----------------------------------------------------------------------===//
// CIR Custom Parser/Printer Signatures
//===----------------------------------------------------------------------===//

static mlir::ParseResult
parseFuncTypeParams(mlir::AsmParser &p, llvm::SmallVector<mlir::Type> &params,
                    bool &isVarArg);
static void printFuncTypeParams(mlir::AsmPrinter &p,
                                mlir::ArrayRef<mlir::Type> params,
                                bool isVarArg);
//===----------------------------------------------------------------------===//
// CIR Custom Parser/Printer Signatures
//===----------------------------------------------------------------------===//

static mlir::ParseResult
parseFuncTypeParams(mlir::AsmParser &p, llvm::SmallVector<mlir::Type> &params,
                    bool &isVarArg);

static void printFuncTypeParams(mlir::AsmPrinter &p,
                                mlir::ArrayRef<mlir::Type> params,
                                bool isVarArg);

//===----------------------------------------------------------------------===//
// AddressSpace
//===----------------------------------------------------------------------===//

static mlir::ParseResult
parseAddressSpaceValue(mlir::AsmParser &p,
                       mlir::ptr::MemorySpaceAttrInterface &attr);

mlir::ParseResult
parseAddressSpaceValue(mlir::AsmParser &p,
                       mlir::ptr::MemorySpaceAttrInterface &attr, bool wrapped);

static void printAddressSpaceValue(mlir::AsmPrinter &printer,
                                   mlir::ptr::MemorySpaceAttrInterface attr);

mlir::ParseResult
parseGlobalAddressSpaceValue(mlir::AsmParser &p,
                             mlir::ptr::MemorySpaceAttrInterface &attr);

void printGlobalAddressSpaceValue(mlir::AsmPrinter &printer, cir::GlobalOp,
                                  mlir::ptr::MemorySpaceAttrInterface attr);

//===----------------------------------------------------------------------===//
// Get autogenerated stuff
//===----------------------------------------------------------------------===//

namespace cir {

#include "clang/CIR/Dialect/IR/CIRTypeConstraints.cpp.inc"

} // namespace cir

#define GET_TYPEDEF_CLASSES
#include "clang/CIR/Dialect/IR/CIROpsTypes.cpp.inc"

using namespace mlir;
using namespace cir;

namespace {
static std::pair<uint64_t, uint64_t>
computeStructSizeAndAlignment(llvm::ArrayRef<mlir::Type> members, bool packed,
                              const mlir::DataLayout &dataLayout) {
  uint64_t recordSize = 0;
  uint64_t recordAlignment = 1;
  uint64_t maxMemberABIAlignment = 1;

  for (mlir::Type ty : members) {
    const uint64_t memberABIAlignment = dataLayout.getTypeABIAlignment(ty);
    const uint64_t tyAlign = packed ? 1 : memberABIAlignment;
    // Use the alloc size (store size rounded up to ABI alignment), not the
    // store size, to match LLVM's StructLayout which uses
    // DL.getTypeAllocSize(Ty). For most types store == alloc, but for
    // x86_fp80 (store=10, align=16, alloc=16) the difference matters.
    const uint64_t tyStoreSize = dataLayout.getTypeSize(ty);
    const uint64_t tySize =
        llvm::alignTo(tyStoreSize, memberABIAlignment);

    // Add padding to align the current record size before placing this member.
    recordSize = llvm::alignTo(recordSize, tyAlign);
    recordSize += tySize;

    // Track strictest effective alignment used for size computation.
    recordAlignment = std::max(recordAlignment, tyAlign);
    // Track strictest ABI alignment of members.
    maxMemberABIAlignment = std::max(maxMemberABIAlignment, memberABIAlignment);
  }

  // Final struct size must be aligned to the struct ABI alignment.
  recordSize = llvm::alignTo(recordSize, recordAlignment);
  return {recordSize, maxMemberABIAlignment};
}
} // namespace

//===----------------------------------------------------------------------===//
// General CIR parsing / printing
//===----------------------------------------------------------------------===//

Type CIRDialect::parseType(DialectAsmParser &parser) const {
  llvm::SMLoc typeLoc = parser.getCurrentLocation();
  llvm::StringRef mnemonic;
  Type genType;

  // Try to parse as a tablegen'd type.
  OptionalParseResult parseResult =
      generatedTypeParser(parser, &mnemonic, genType);
  if (parseResult.has_value())
    return genType;

  // Type is not tablegen'd: try to parse as a raw C++ type.
  return StringSwitch<function_ref<Type()>>(mnemonic)
      .Case("record", [&] { return RecordType::parse(parser); })
      .Default([&] {
        parser.emitError(typeLoc) << "unknown CIR type: " << mnemonic;
        return Type();
      })();
}

void CIRDialect::printType(Type type, DialectAsmPrinter &os) const {
  // Try to print as a tablegen'd type.
  if (generatedTypePrinter(type, os).succeeded())
    return;

  // TODO(CIR) Attempt to print as a raw C++ type.
  llvm::report_fatal_error("printer is missing a handler for this type");
}

//===----------------------------------------------------------------------===//
// RecordType Definitions
//===----------------------------------------------------------------------===//

Type RecordType::parse(mlir::AsmParser &parser) {
  FailureOr<AsmParser::CyclicParseReset> cyclicParseGuard;
  const llvm::SMLoc loc = parser.getCurrentLocation();
  const mlir::Location eLoc = parser.getEncodedSourceLoc(loc);
  bool packed = false;
  bool padded = false;
  RecordKind kind;
  mlir::MLIRContext *context = parser.getContext();

  if (parser.parseLess())
    return {};

  // TODO(cir): in the future we should probably separate types for different
  // source language declarations such as cir.record and cir.union
  if (parser.parseOptionalKeyword("struct").succeeded())
    kind = RecordKind::Struct;
  else if (parser.parseOptionalKeyword("union").succeeded())
    kind = RecordKind::Union;
  else if (parser.parseOptionalKeyword("class").succeeded())
    kind = RecordKind::Class;
  else {
    parser.emitError(loc, "unknown record type");
    return {};
  }

  mlir::StringAttr name;
  parser.parseOptionalAttribute(name);

  // Is a self reference: ensure referenced type was parsed.
  if (name && parser.parseOptionalGreater().succeeded()) {
    RecordType type = getChecked(eLoc, context, name, kind);
    if (succeeded(parser.tryStartCyclicParse(type))) {
      parser.emitError(loc, "invalid self-reference within record");
      return {};
    }
    return type;
  }

  // Is a named record definition: ensure name has not been parsed yet.
  if (name) {
    RecordType type = getChecked(eLoc, context, name, kind);
    cyclicParseGuard = parser.tryStartCyclicParse(type);
    if (failed(cyclicParseGuard)) {
      parser.emitError(loc, "record already defined");
      return {};
    }
  }

  if (parser.parseOptionalKeyword("packed").succeeded())
    packed = true;

  if (parser.parseOptionalKeyword("padded").succeeded())
    padded = true;

  // Parse record members or lack thereof.
  bool incomplete = true;
  llvm::SmallVector<mlir::Type> members;
  if (parser.parseOptionalKeyword("incomplete").failed()) {
    incomplete = false;
    const auto delimiter = AsmParser::Delimiter::Braces;
    const auto parseElementFn = [&parser, &members]() {
      return parser.parseType(members.emplace_back());
    };
    if (parser.parseCommaSeparatedList(delimiter, parseElementFn).failed())
      return {};
  }

  if (parser.parseGreater())
    return {};

  // Try to create the proper record type.
  ArrayRef<mlir::Type> membersRef(members); // Needed for template deduction.
  mlir::Type type = {};
  if (name && incomplete) { // Identified & incomplete
    type = getChecked(eLoc, context, name, kind);
  } else if (!name && !incomplete) { // Anonymous & complete
    type = getChecked(eLoc, context, membersRef, packed, padded, kind);
  } else if (!incomplete) { // Identified & complete
    type = getChecked(eLoc, context, membersRef, name, packed, padded, kind);
    // If the record has a self-reference, its type already exists in a
    // incomplete state. In this case, we must complete it.
    if (mlir::cast<RecordType>(type).isIncomplete())
      mlir::cast<RecordType>(type).complete(membersRef, packed, padded);
    assert(!cir::MissingFeatures::astRecordDeclAttr());
  } else { // anonymous & incomplete
    parser.emitError(loc, "anonymous records must be complete");
    return {};
  }

  return type;
}

void RecordType::print(mlir::AsmPrinter &printer) const {
  FailureOr<AsmPrinter::CyclicPrintReset> cyclicPrintGuard;
  printer << '<';

  switch (getKind()) {
  case RecordKind::Struct:
    printer << "struct ";
    break;
  case RecordKind::Union:
    printer << "union ";
    break;
  case RecordKind::Class:
    printer << "class ";
    break;
  }

  if (getName())
    printer << getName();

  // Current type has already been printed: print as self reference.
  cyclicPrintGuard = printer.tryStartCyclicPrint(*this);
  if (failed(cyclicPrintGuard)) {
    printer << '>';
    return;
  }

  // Type not yet printed: continue printing the entire record.
  printer << ' ';

  if (getPacked())
    printer << "packed ";

  if (getPadded())
    printer << "padded ";

  if (isIncomplete()) {
    printer << "incomplete";
  } else {
    printer << "{";
    llvm::interleaveComma(getMembers(), printer);
    printer << "}";
  }

  printer << '>';
}

mlir::LogicalResult
RecordType::verify(function_ref<mlir::InFlightDiagnostic()> emitError,
                   llvm::ArrayRef<mlir::Type> members, mlir::StringAttr name,
                   bool incomplete, bool packed, bool padded,
                   RecordType::RecordKind kind,
                   cir::ASTRecordDeclInterface ast) {
  if (name && name.getValue().empty())
    return emitError() << "identified records cannot have an empty name";
  return mlir::success();
}

::llvm::ArrayRef<mlir::Type> RecordType::getMembers() const {
  return getImpl()->members;
}

bool RecordType::isIncomplete() const { return getImpl()->incomplete; }

mlir::StringAttr RecordType::getName() const { return getImpl()->name; }

bool RecordType::getIncomplete() const { return getImpl()->incomplete; }

bool RecordType::getPacked() const { return getImpl()->packed; }

bool RecordType::getPadded() const { return getImpl()->padded; }

cir::RecordType::RecordKind RecordType::getKind() const {
  return getImpl()->kind;
}

cir::ASTRecordDeclInterface RecordType::getAst() const {
  return getImpl()->ast;
}

void RecordType::complete(ArrayRef<Type> members, bool packed, bool padded,
                          ASTRecordDeclInterface ast) {
  if (mutate(members, packed, padded, ast).failed())
    llvm_unreachable("failed to complete record");
}

void RecordType::completeIfIncomplete(ArrayRef<Type> members, bool packed,
                                      bool padded) {
  if (mutate(members, packed, padded, cir::detail::RecordTypeStorage::IfIncomplete{})
          .failed())
    llvm_unreachable("failed to complete record");
}

void RecordType::replaceMemberTypes(ArrayRef<Type> newMembers) {
  if (mutate(newMembers).failed())
    llvm_unreachable("failed to replace member types");
}

/// Return the largest member of in the type.
///
/// Recurses into union members never returning a union as the largest member.
Type RecordType::getLargestMember(const ::mlir::DataLayout &dataLayout) const {
  assert(isUnion() && "Only call getLargestMember on unions");
  llvm::ArrayRef<Type> members = getMembers();
  if (members.empty())
    return {};

  // If the union is padded, we need to ignore the last member,
  // which is the padding.
  auto endIt = getPadded() ? std::prev(members.end()) : members.end();
  if (endIt == members.begin())
    return {};

  Type bestMember;
  uint64_t bestAlign = 0;
  uint64_t bestSize = 0;
  for (auto it = members.begin(); it != endIt; ++it) {
    uint64_t align = dataLayout.getTypeABIAlignment(*it);
    uint64_t size = dataLayout.getTypeSize(*it);
    if (!bestMember || align > bestAlign ||
        (align == bestAlign && size > bestSize)) {
      bestMember = *it;
      bestAlign = align;
      bestSize = size;
    }
  }
  return bestMember;
}

/// Return the member type that CIRGen selected as the union body
/// (the "storage type"). For non-padded unions, this is the largest member.
/// For padded unions, we derive the body size from the padding:
///   padding = layoutSize - storageSize
///   layoutSize = alignTo(max(field_sizes), max(field_alignments))
/// When this formula works (no explicit alignment attribute), we find the
/// member matching the derived body size (handles non-zero-init overrides).
/// When the formula fails (explicit __aligned__ inflates padding beyond
/// member-derived layout size), we fall back to the lowerUnion() heuristic:
/// highest alignment, breaking ties by largest size.
Type RecordType::getStorageMember(
    const ::mlir::DataLayout &dataLayout) const {
  assert(isUnion() && "Only call getStorageMember on unions");
  llvm::ArrayRef<Type> members = getMembers();
  if (members.empty())
    return {};

  // For non-padded unions, the storage type is the largest member.
  if (!getPadded())
    return getLargestMember(dataLayout);

  // Exclude the padding (last member) from the field list.
  auto endIt = std::prev(members.end());
  if (endIt == members.begin())
    return {};

  struct MemberLayoutInfo {
    Type ty;
    uint64_t size;
    uint64_t align;
  };
  llvm::SmallVector<MemberLayoutInfo, 8> memberLayoutInfo;
  memberLayoutInfo.reserve(std::distance(members.begin(), endIt));

  // Compute the union's layout size = alignTo(max(field_sizes), alignment).
  // For packed unions, alignment is 1.
  uint64_t maxFieldSize = 0;
  uint64_t maxFieldAlign = 1;
  for (auto it = members.begin(); it != endIt; ++it) {
    uint64_t size = static_cast<uint64_t>(dataLayout.getTypeSize(*it));
    uint64_t align = static_cast<uint64_t>(dataLayout.getTypeABIAlignment(*it));
    memberLayoutInfo.push_back({*it, size, align});

    maxFieldSize = std::max(maxFieldSize, size);
    if (!getPacked())
      maxFieldAlign = std::max(maxFieldAlign, align);
  }
  uint64_t layoutSize = llvm::alignTo(maxFieldSize, maxFieldAlign);

  // Padding size is the last member's size.
  uint64_t paddingSize = dataLayout.getTypeSize(members.back());

  // Try the size-based approach: bodySize = layoutSize - paddingSize.
  // This correctly handles non-zero-initializable overrides (e.g., member
  // pointers with null=-1) where lowerUnion() forces a specific storage type
  // that differs from the highest-alignment heuristic.
  Type bestMember;
  uint64_t bestMemberAlign = 0;
  if (layoutSize >= paddingSize) {
    uint64_t bodySize = layoutSize - paddingSize;

    // Find the member with this size and highest alignment (matching CIRGen's
    // storageType selection criteria).
    for (const auto &memberInfo : memberLayoutInfo) {
      if (memberInfo.size == bodySize) {
        if (!bestMember || memberInfo.align > bestMemberAlign) {
          bestMember = memberInfo.ty;
          bestMemberAlign = memberInfo.align;
        }
      }
    }
  }

  // Fallback: when layoutSize < paddingSize (explicit alignment attributes
  // like __attribute__((__aligned__(N))) inflate the total size beyond what
  // member alignments predict), or when no member matches bodySize (e.g.,
  // unions with oversized bitfields), use the lowerUnion() heuristic:
  // highest alignment, breaking ties by largest size.
  if (!bestMember) {
    uint64_t bestAlign = 0;
    uint64_t bestSize = 0;
    for (const auto &memberInfo : memberLayoutInfo) {
      if (!bestMember || memberInfo.align > bestAlign ||
          (memberInfo.align == bestAlign && memberInfo.size > bestSize)) {
        bestMember = memberInfo.ty;
        bestAlign = memberInfo.align;
        bestSize = memberInfo.size;
      }
    }
  }

  return bestMember;
}

bool RecordType::isLayoutIdentical(const RecordType &other) {
  if (getImpl() == other.getImpl())
    return true;

  if (getPacked() != other.getPacked())
    return false;

  return getMembers() == other.getMembers();
}

//===----------------------------------------------------------------------===//
// Data Layout information for types
//===----------------------------------------------------------------------===//

namespace {

enum class PointerSpecPos : int64_t {
  Size = 0,
  ABI = 1,
  Preferred = 2,
  Index = 3,
};

static bool samePointerAddrSpace(mlir::ptr::MemorySpaceAttrInterface lhs,
                                 mlir::ptr::MemorySpaceAttrInterface rhs) {
  if (!lhs || !rhs)
    return lhs == rhs;
  if (auto lhsTarget = mlir::dyn_cast<cir::TargetAddressSpaceAttr>(lhs)) {
    auto rhsTarget = mlir::dyn_cast<cir::TargetAddressSpaceAttr>(rhs);
    return rhsTarget && lhsTarget.getValue() == rhsTarget.getValue();
  }
  if (auto lhsLang = mlir::dyn_cast<cir::LangAddressSpaceAttr>(lhs)) {
    auto rhsLang = mlir::dyn_cast<cir::LangAddressSpaceAttr>(rhs);
    return rhsLang && lhsLang.getValue() == rhsLang.getValue();
  }
  return false;
}

static std::optional<uint64_t>
extractCIRPointerSpecValue(mlir::ptr::MemorySpaceAttrInterface addrSpace,
                           mlir::DataLayoutEntryListRef params,
                           PointerSpecPos pos) {
  for (mlir::DataLayoutEntryInterface entry : params) {
    if (!entry.isTypeEntry())
      continue;

    auto keyTy =
        mlir::dyn_cast<cir::PointerType>(entry.getKey().dyn_cast<mlir::Type>());
    if (!keyTy || !samePointerAddrSpace(keyTy.getAddrSpace(), addrSpace))
      continue;

    auto spec = mlir::dyn_cast<mlir::DenseIntElementsAttr>(entry.getValue());
    if (!spec)
      continue;

    auto idx = static_cast<int64_t>(pos);
    if (idx >= static_cast<int64_t>(spec.size()))
      continue;
    return spec.getValues<uint64_t>()[idx];
  }
  return std::nullopt;
}

static std::optional<unsigned>
getTargetPointerAddrSpace(mlir::ptr::MemorySpaceAttrInterface addrSpace,
                          const mlir::DataLayout &dataLayout) {
  if (!addrSpace) {
    if (auto programAS = mlir::cast_if_present<mlir::IntegerAttr>(
            dataLayout.getProgramMemorySpace()))
      return static_cast<unsigned>(programAS.getValue().getZExtValue());
    return 0;
  }

  if (auto targetAS = mlir::dyn_cast<cir::TargetAddressSpaceAttr>(addrSpace))
    return targetAS.getValue();

  return std::nullopt;
}

} // namespace

llvm::TypeSize
PointerType::getTypeSizeInBits(const ::mlir::DataLayout &dataLayout,
                               ::mlir::DataLayoutEntryListRef params) const {
  constexpr uint64_t defaultPtrSizeBits = 64;

  if (auto size = extractCIRPointerSpecValue(getAddrSpace(), params,
                                             PointerSpecPos::Size))
    return llvm::TypeSize::getFixed(*size);

  // CIR currently preserves the translated LLVM pointer layout entries
  // (`!llvm.ptr`) in DLTI. When no explicit `!cir.ptr` entry exists, query
  // the matching LLVM pointer type instead of falling back to a 64-bit default.
  if (getContext()->getLoadedDialect<mlir::LLVM::LLVMDialect>()) {
    if (auto addrSpace =
            getTargetPointerAddrSpace(getAddrSpace(), dataLayout)) {
      auto llvmPtrTy =
          mlir::LLVM::LLVMPointerType::get(getContext(), *addrSpace);
      return llvm::TypeSize::getFixed(dataLayout.getTypeSizeInBits(llvmPtrTy));
    }
  }

  // For non-default address spaces with no explicit entry, use the pointer
  // size of the default address space.
  if (getAddrSpace())
    return llvm::TypeSize::getFixed(
        dataLayout.getTypeSizeInBits(cir::PointerType::get(getPointee())));

  return llvm::TypeSize::getFixed(defaultPtrSizeBits);
}

uint64_t
PointerType::getABIAlignment(const ::mlir::DataLayout &dataLayout,
                             ::mlir::DataLayoutEntryListRef params) const {
  constexpr uint64_t defaultPtrAlignment = 8;

  if (auto align = extractCIRPointerSpecValue(getAddrSpace(), params,
                                              PointerSpecPos::ABI))
    // Pointer ABI alignment is specified in bits, return bytes.
    return *align / 8;

  if (getContext()->getLoadedDialect<mlir::LLVM::LLVMDialect>()) {
    if (auto addrSpace =
            getTargetPointerAddrSpace(getAddrSpace(), dataLayout)) {
      auto llvmPtrTy =
          mlir::LLVM::LLVMPointerType::get(getContext(), *addrSpace);
      return dataLayout.getTypeABIAlignment(llvmPtrTy);
    }
  }

  // For non-default address spaces with no explicit entry, use the ABI
  // alignment of pointers in the default address space.
  if (getAddrSpace())
    return dataLayout.getTypeABIAlignment(cir::PointerType::get(getPointee()));

  return defaultPtrAlignment;
}

llvm::TypeSize
RecordType::getTypeSizeInBits(const mlir::DataLayout &dataLayout,
                              mlir::DataLayoutEntryListRef params) const {
  // Guard against incomplete types — return 0 size for types still being
  // laid out to avoid infinite recursion.
  if (!isComplete())
    return llvm::TypeSize::getFixed(0);

  if (isUnion())
    return llvm::TypeSize::getFixed(
        static_cast<uint64_t>(computeUnionSize(dataLayout)) * 8);

  auto recordSize = static_cast<uint64_t>(computeStructSize(dataLayout));
  return llvm::TypeSize::getFixed(recordSize * 8);
}

uint64_t
RecordType::getABIAlignment(const ::mlir::DataLayout &dataLayout,
                            ::mlir::DataLayoutEntryListRef params) const {
  // Guard against incomplete types — during record layout, nested record
  // types may not have their body set yet. Return alignment of 1 for
  // incomplete types to avoid infinite recursion in getLargestMember.
  if (!isComplete())
    return 1;

  // Packed records always have an ABI alignment of 1.
  if (getPacked())
    return 1;

  if (isUnion()) {
    Type largest = getLargestMember(dataLayout);
    if (!largest)
      return 1;
    return dataLayout.getTypeABIAlignment(largest);
  }

  return computeStructAlignment(dataLayout);
}

uint64_t
RecordType::computeStructSize(const mlir::DataLayout &dataLayout) const {
  assert(isComplete() && "Cannot get layout of incomplete records");
  return computeStructSizeAndAlignment(getMembers(), getPacked(), dataLayout)
      .first;
}

uint64_t
RecordType::computeUnionSize(const mlir::DataLayout &dataLayout) const {
  assert(isComplete() && "Cannot get layout of incomplete records");
  assert(isUnion() && "computeUnionSize called for non-union record");

  uint64_t recordSize = 0;

  // Use getStorageMember (not getLargestMember) to match the member that
  // CIRGen selected as the union body. For padded unions, the storage member
  // may differ from the largest member (e.g., when the first named field is
  // not zero-initializable).
  Type storageMember = getStorageMember(dataLayout);
  if (storageMember) {
    // Use the alloc size (store size rounded up to the storage member's ABI
    // alignment), not the raw store size, so the union type size matches
    // sizeof() and LLVM's getTypeAllocSize. For most types store == alloc,
    // but for x86_fp80 (store=10, align=16, alloc=16) the difference is
    // load-bearing: a by-value union copy sized off the store size would
    // drop the union's tail (e.g. the second double of an aliased struct).
    // This mirrors computeStructSizeAndAlignment's per-member alignTo.
    uint64_t storeSize = dataLayout.getTypeSize(storageMember);
    uint64_t memberABIAlignment =
        dataLayout.getTypeABIAlignment(storageMember);
    recordSize = llvm::alignTo(storeSize, memberABIAlignment);
  }

  // If the union is padded, add the padding to the size.
  if (getPadded()) {
    unsigned numElements = getNumElements();
    auto ty = getMembers()[numElements - 1];
    recordSize += dataLayout.getTypeSize(ty);
  }

  return recordSize;
}

// We also compute the alignment as part of computeStructSize, but this is more
// efficient. Ideally, we'd like to compute both at once and cache the result,
// but that's implemented yet.
// TODO(CIR): Implement a way to cache the result.
uint64_t
RecordType::computeStructAlignment(const mlir::DataLayout &dataLayout) const {
  assert(isComplete() && "Cannot get layout of incomplete records");
  return computeStructSizeAndAlignment(getMembers(), getPacked(), dataLayout)
      .second;
}

uint64_t RecordType::getElementOffset(const ::mlir::DataLayout &dataLayout,
                                      unsigned idx) const {
  assert(idx < getMembers().size() && "access not valid");

  // All union elements are at offset zero.
  if (isUnion() || idx == 0)
    return 0;

  assert(isComplete() && "Cannot get layout of incomplete records");
  assert(idx < getNumElements());
  llvm::ArrayRef<mlir::Type> members = getMembers();

  uint64_t offset = 0;

  for (mlir::Type ty :
       llvm::make_range(members.begin(), std::next(members.begin(), idx))) {
    // This matches LLVM since it uses the ABI instead of preferred alignment.
    const llvm::Align tyAlign =
        llvm::Align(getPacked() ? 1 : dataLayout.getTypeABIAlignment(ty));

    // Add padding if necessary to align the data element properly.
    offset = llvm::alignTo(offset, tyAlign);

    // Consume space for this data item. Use the alloc size (store size
    // rounded up to ABI alignment) to match LLVM's StructLayout which uses
    // DL.getTypeAllocSize(Ty). For most types store == alloc, but for types
    // like <3 x i32> (store=12, align=16, alloc=16) the difference matters.
    const uint64_t tyStoreSize = dataLayout.getTypeSize(ty);
    const uint64_t tyAllocSize =
        llvm::alignTo(tyStoreSize, dataLayout.getTypeABIAlignment(ty));
    offset += tyAllocSize;
  }

  // Account for padding, if necessary, for the alignment of the field whose
  // offset we are calculating.
  const llvm::Align tyAlign = llvm::Align(
      getPacked() ? 1 : dataLayout.getTypeABIAlignment(members[idx]));
  offset = llvm::alignTo(offset, tyAlign);

  return offset;
}

//===----------------------------------------------------------------------===//
// IntType Definitions
//===----------------------------------------------------------------------===//

Type IntType::parse(mlir::AsmParser &parser) {
  mlir::MLIRContext *context = parser.getBuilder().getContext();
  llvm::SMLoc loc = parser.getCurrentLocation();
  bool isSigned;
  unsigned width;

  if (parser.parseLess())
    return {};

  // Fetch integer sign.
  llvm::StringRef sign;
  if (parser.parseKeyword(&sign))
    return {};
  if (sign == "s")
    isSigned = true;
  else if (sign == "u")
    isSigned = false;
  else {
    parser.emitError(loc, "expected 's' or 'u'");
    return {};
  }

  if (parser.parseComma())
    return {};

  // Fetch integer size.
  if (parser.parseInteger(width))
    return {};
  if (width < IntType::minBitwidth() || width > IntType::maxBitwidth()) {
    parser.emitError(loc, "expected integer width to be from ")
        << IntType::minBitwidth() << " up to " << IntType::maxBitwidth();
    return {};
  }

  if (parser.parseGreater())
    return {};

  return IntType::get(context, width, isSigned);
}

void IntType::print(mlir::AsmPrinter &printer) const {
  char sign = isSigned() ? 's' : 'u';
  printer << '<' << sign << ", " << getWidth() << '>';
}

llvm::TypeSize
IntType::getTypeSizeInBits(const mlir::DataLayout &dataLayout,
                           mlir::DataLayoutEntryListRef params) const {
  return llvm::TypeSize::getFixed(getWidth());
}

uint64_t IntType::getABIAlignment(const mlir::DataLayout &dataLayout,
                                  mlir::DataLayoutEntryListRef params) const {
  unsigned width = getWidth();

  // For standard power-of-2 widths (8, 16, 32, 64, 128) — the standard C
  // integer types (__int128, long long, int, ...) — consult the target data
  // layout spec, keyed by the builtin integer type (CIR's !cir.int carries no
  // dl_spec entry of its own, so `params` here is always empty). This honors
  // target ABI alignments that are smaller than the natural width/8, most
  // importantly i386, where i64 has ABI alignment 4 (spec entry
  // `i64 = [32,64]`). Hardcoding width/8 over-aligned such members and made
  // record lowering drop the padding OGCG emits, miscompiling i386 structs.
  // The spec is always present during CIRGen; when absent (standalone cir-opt
  // on a module with no dl_spec) getTypeABIAlignment falls back to MLIR's
  // builtin-integer default, which matches width/8 for widths below 64.
  if (llvm::isPowerOf2_64(width) && width >= 8)
    return dataLayout.getTypeABIAlignment(
        mlir::IntegerType::get(getContext(), width));

  // For non-power-of-2 widths (which must be _BitInt types), Clang's ABI
  // caps alignment at getBitIntMaxAlign(), which defaults to LongLongAlign
  // (64 bits = 8 bytes on most targets). Match this behavior by capping the
  // natural alignment at the alignment of long long (i64).
  uint64_t sizeInBytes = llvm::divideCeil(width, 8);
  uint64_t naturalAlign = llvm::PowerOf2Ceil(sizeInBytes);
  auto longLongTy = mlir::IntegerType::get(getContext(), 64);
  uint64_t maxAlign = dataLayout.getTypeABIAlignment(longLongTy);
  return std::min(naturalAlign, maxAlign);
}

mlir::LogicalResult
IntType::verify(llvm::function_ref<mlir::InFlightDiagnostic()> emitError,
                unsigned width, bool isSigned) {
  if (width < IntType::minBitwidth() || width > IntType::maxBitwidth())
    return emitError() << "IntType only supports widths from "
                       << IntType::minBitwidth() << " up to "
                       << IntType::maxBitwidth();
  return mlir::success();
}

bool cir::isValidFundamentalIntWidth(unsigned width) {
  return width == 8 || width == 16 || width == 32 || width == 64;
}

//===----------------------------------------------------------------------===//
// Floating-point type definitions
//===----------------------------------------------------------------------===//

const llvm::fltSemantics &SingleType::getFloatSemantics() const {
  return llvm::APFloat::IEEEsingle();
}

llvm::TypeSize
SingleType::getTypeSizeInBits(const mlir::DataLayout &dataLayout,
                              mlir::DataLayoutEntryListRef params) const {
  return llvm::TypeSize::getFixed(getWidth());
}

uint64_t
SingleType::getABIAlignment(const mlir::DataLayout &dataLayout,
                            mlir::DataLayoutEntryListRef params) const {
  // Delegate to the builtin float type so the target dl_spec ABI alignment is
  // honored (e.g. f32 on targets that under-align it). !cir.float carries no
  // dl_spec entry, so params here is always empty.
  return dataLayout.getTypeABIAlignment(mlir::Float32Type::get(getContext()));
}

const llvm::fltSemantics &DoubleType::getFloatSemantics() const {
  return llvm::APFloat::IEEEdouble();
}

llvm::TypeSize
DoubleType::getTypeSizeInBits(const mlir::DataLayout &dataLayout,
                              mlir::DataLayoutEntryListRef params) const {
  return llvm::TypeSize::getFixed(getWidth());
}

uint64_t
DoubleType::getABIAlignment(const mlir::DataLayout &dataLayout,
                            mlir::DataLayoutEntryListRef params) const {
  // Delegate to the builtin f64 so the target dl_spec ABI alignment is
  // honored — on i386 double has ABI alignment 4 (spec entry `f64 = [32,64]`),
  // not 8. !cir.double carries no dl_spec entry, so params is always empty.
  return dataLayout.getTypeABIAlignment(mlir::Float64Type::get(getContext()));
}

const llvm::fltSemantics &FP16Type::getFloatSemantics() const {
  return llvm::APFloat::IEEEhalf();
}

llvm::TypeSize
FP16Type::getTypeSizeInBits(const mlir::DataLayout &dataLayout,
                            mlir::DataLayoutEntryListRef params) const {
  return llvm::TypeSize::getFixed(getWidth());
}

uint64_t FP16Type::getABIAlignment(const mlir::DataLayout &dataLayout,
                                   mlir::DataLayoutEntryListRef params) const {
  // Delegate to the builtin f16 so the target dl_spec ABI alignment is
  // honored. !cir.f16 carries no dl_spec entry, so params is always empty.
  return dataLayout.getTypeABIAlignment(mlir::Float16Type::get(getContext()));
}

const llvm::fltSemantics &BF16Type::getFloatSemantics() const {
  return llvm::APFloat::BFloat();
}

llvm::TypeSize
BF16Type::getTypeSizeInBits(const mlir::DataLayout &dataLayout,
                            mlir::DataLayoutEntryListRef params) const {
  return llvm::TypeSize::getFixed(getWidth());
}

uint64_t BF16Type::getABIAlignment(const mlir::DataLayout &dataLayout,
                                   mlir::DataLayoutEntryListRef params) const {
  // Delegate to the builtin bf16 so the target dl_spec ABI alignment is
  // honored. !cir.bf16 carries no dl_spec entry, so params is always empty.
  return dataLayout.getTypeABIAlignment(mlir::BFloat16Type::get(getContext()));
}

const llvm::fltSemantics &FP80Type::getFloatSemantics() const {
  return llvm::APFloat::x87DoubleExtended();
}

llvm::TypeSize
FP80Type::getTypeSizeInBits(const mlir::DataLayout &dataLayout,
                            mlir::DataLayoutEntryListRef params) const {
  // x87 extended precision uses 80 bits. The alloc size (including tail padding)
  // depends on alignment from the target's data layout: alignTo(10, alignment).
  // For x86_64 (f80:128:128), allocSize=16. For i386 (f80:32), allocSize=12.
  return llvm::TypeSize::getFixed(80);
}

uint64_t FP80Type::getABIAlignment(const mlir::DataLayout &dataLayout,
                                   mlir::DataLayoutEntryListRef params) const {
  // Delegate to MLIR's builtin Float80Type which receives the target-specific
  // alignment from the dlti.dl_spec (e.g., f80:32 on i386, f80:128:128 on
  // x86_64). CIR's !cir.f80 type doesn't have its own dl_spec entry, so
  // params here are always empty.
  return dataLayout.getTypeABIAlignment(
      mlir::Float80Type::get(getContext()));
}

const llvm::fltSemantics &FP128Type::getFloatSemantics() const {
  return llvm::APFloat::IEEEquad();
}

llvm::TypeSize
FP128Type::getTypeSizeInBits(const mlir::DataLayout &dataLayout,
                             mlir::DataLayoutEntryListRef params) const {
  return llvm::TypeSize::getFixed(getWidth());
}

uint64_t FP128Type::getABIAlignment(const mlir::DataLayout &dataLayout,
                                    mlir::DataLayoutEntryListRef params) const {
  // Delegate to the builtin f128 so the target dl_spec ABI alignment is
  // honored. !cir.f128 carries no dl_spec entry, so params is always empty.
  return dataLayout.getTypeABIAlignment(mlir::Float128Type::get(getContext()));
}

const llvm::fltSemantics &LongDoubleType::getFloatSemantics() const {
  return mlir::cast<cir::FPTypeInterface>(getUnderlying()).getFloatSemantics();
}

llvm::TypeSize
LongDoubleType::getTypeSizeInBits(const mlir::DataLayout &dataLayout,
                                  mlir::DataLayoutEntryListRef params) const {
  return mlir::cast<mlir::DataLayoutTypeInterface>(getUnderlying())
      .getTypeSizeInBits(dataLayout, params);
}

uint64_t
LongDoubleType::getABIAlignment(const mlir::DataLayout &dataLayout,
                                mlir::DataLayoutEntryListRef params) const {
  return mlir::cast<mlir::DataLayoutTypeInterface>(getUnderlying())
      .getABIAlignment(dataLayout, params);
}

//===----------------------------------------------------------------------===//
// ComplexType Definitions
//===----------------------------------------------------------------------===//

llvm::TypeSize
cir::ComplexType::getTypeSizeInBits(const mlir::DataLayout &dataLayout,
                                    mlir::DataLayoutEntryListRef params) const {
  // C17 6.2.5p13:
  //   Each complex type has the same representation and alignment requirements
  //   as an array type containing exactly two elements of the corresponding
  //   real type.
  //
  // Use the element's alloc size (store size padded to ABI alignment) rather
  // than its type size, because inter-element padding must be included. For
  // example, x86_fp80 on i386 has type size 80 bits (store size 10 bytes) but
  // alloc size 12 bytes (4-byte alignment). _Complex long double is 24 bytes,
  // not 20.
  llvm::TypeSize elemBits = dataLayout.getTypeSizeInBits(getElementType());
  uint64_t elemStoreSize = llvm::divideCeil(elemBits.getFixedValue(), 8u);
  uint64_t elemAlign = dataLayout.getTypeABIAlignment(getElementType());
  uint64_t elemAllocSize = llvm::alignTo(elemStoreSize, elemAlign);
  return llvm::TypeSize::getFixed(elemAllocSize * 2 * 8);
}

uint64_t
cir::ComplexType::getABIAlignment(const mlir::DataLayout &dataLayout,
                                  mlir::DataLayoutEntryListRef params) const {
  // C17 6.2.5p13:
  //   Each complex type has the same representation and alignment requirements
  //   as an array type containing exactly two elements of the corresponding
  //   real type.

  return dataLayout.getTypeABIAlignment(getElementType());
}

FuncType FuncType::clone(TypeRange inputs, TypeRange results) const {
  assert(results.size() == 1 && "expected exactly one result type");
  return get(llvm::to_vector(inputs), results[0], isVarArg());
}

// Custom parser that parses function parameters of form `(<type>*, ...)`.
static mlir::ParseResult
parseFuncTypeParams(mlir::AsmParser &p, llvm::SmallVector<mlir::Type> &params,
                    bool &isVarArg) {
  isVarArg = false;
  return p.parseCommaSeparatedList(
      AsmParser::Delimiter::Paren, [&]() -> mlir::ParseResult {
        if (isVarArg)
          return p.emitError(p.getCurrentLocation(),
                             "variadic `...` must be the last parameter");
        if (succeeded(p.parseOptionalEllipsis())) {
          isVarArg = true;
          return success();
        }
        mlir::Type type;
        if (failed(p.parseType(type)))
          return failure();
        params.push_back(type);
        return success();
      });
}

static void printFuncTypeParams(mlir::AsmPrinter &p,
                                mlir::ArrayRef<mlir::Type> params,
                                bool isVarArg) {
  p << '(';
  llvm::interleaveComma(params, p,
                        [&p](mlir::Type type) { p.printType(type); });
  if (isVarArg) {
    if (!params.empty())
      p << ", ";
    p << "...";
  }
  p << ')';
}

/// Get the C-style return type of the function, which is !cir.void if the
/// function returns nothing and the actual return type otherwise.
mlir::Type FuncType::getReturnType() const {
  if (hasVoidReturn())
    return cir::VoidType::get(getContext());
  return getOptionalReturnType();
}

/// Get the MLIR-style return type of the function, which is an empty
/// ArrayRef if the function returns nothing and a single-element ArrayRef
/// with the actual return type otherwise.
llvm::ArrayRef<mlir::Type> FuncType::getReturnTypes() const {
  if (hasVoidReturn())
    return {};
  // Can't use getOptionalReturnType() here because llvm::ArrayRef hold a
  // pointer to its elements and doesn't do lifetime extension.  That would
  // result in returning a pointer to a temporary that has gone out of scope.
  return getImpl()->optionalReturnType;
}

// Does the fuction type return nothing?
bool FuncType::hasVoidReturn() const { return !getOptionalReturnType(); }

mlir::LogicalResult
FuncType::verify(llvm::function_ref<mlir::InFlightDiagnostic()> emitError,
                 llvm::ArrayRef<mlir::Type> argTypes, mlir::Type returnType,
                 bool isVarArg) {
  if (mlir::isa_and_nonnull<cir::VoidType>(returnType))
    return emitError()
           << "!cir.func cannot have an explicit 'void' return type";
  return mlir::success();
}

//===----------------------------------------------------------------------===//
// MethodType Definitions
//===----------------------------------------------------------------------===//

static mlir::Type getMethodLayoutType(mlir::MLIRContext *ctx) {
  // With Itanium ABI, member function pointers have the same layout as the
  // following struct: struct { fnptr_t, ptrdiff_t }, where fnptr_t is a
  // function pointer type.
  // TODO: consider member function pointer layout in other ABIs
  auto voidPtrTy = cir::PointerType::get(cir::VoidType::get(ctx));
  mlir::Type fields[2]{voidPtrTy, voidPtrTy};
  return cir::RecordType::get(ctx, fields, /*packed=*/false,
                              /*padded=*/false, cir::RecordType::Struct);
}

llvm::TypeSize
MethodType::getTypeSizeInBits(const mlir::DataLayout &dataLayout,
                              mlir::DataLayoutEntryListRef params) const {
  return dataLayout.getTypeSizeInBits(getMethodLayoutType(getContext()));
}

uint64_t
MethodType::getABIAlignment(const mlir::DataLayout &dataLayout,
                            mlir::DataLayoutEntryListRef params) const {
  return dataLayout.getTypeABIAlignment(getMethodLayoutType(getContext()));
}

//===----------------------------------------------------------------------===//
// BoolType
//===----------------------------------------------------------------------===//

llvm::TypeSize
BoolType::getTypeSizeInBits(const ::mlir::DataLayout &dataLayout,
                            ::mlir::DataLayoutEntryListRef params) const {
  return llvm::TypeSize::getFixed(8);
}

uint64_t
BoolType::getABIAlignment(const ::mlir::DataLayout &dataLayout,
                          ::mlir::DataLayoutEntryListRef params) const {
  return 1;
}

//===----------------------------------------------------------------------===//
//  DataMemberType Definitions
//===----------------------------------------------------------------------===//

llvm::TypeSize
DataMemberType::getTypeSizeInBits(const ::mlir::DataLayout &dataLayout,
                                  ::mlir::DataLayoutEntryListRef params) const {
  // An Itanium pointer-to-data-member is a ptrdiff_t, i.e. the target pointer
  // width. Size it like a pointer (mirroring PointerType) so 32-bit targets get
  // 4 bytes instead of a hardcoded 8 — otherwise a data-member-pointer field
  // shifts the offset of every field after it in a record.
  if (getContext()->getLoadedDialect<mlir::LLVM::LLVMDialect>()) {
    auto llvmPtrTy = mlir::LLVM::LLVMPointerType::get(getContext(), 0);
    return llvm::TypeSize::getFixed(dataLayout.getTypeSizeInBits(llvmPtrTy));
  }
  return llvm::TypeSize::getFixed(64);
}

uint64_t
DataMemberType::getABIAlignment(const ::mlir::DataLayout &dataLayout,
                                ::mlir::DataLayoutEntryListRef params) const {
  if (getContext()->getLoadedDialect<mlir::LLVM::LLVMDialect>()) {
    auto llvmPtrTy = mlir::LLVM::LLVMPointerType::get(getContext(), 0);
    return dataLayout.getTypeABIAlignment(llvmPtrTy);
  }
  return 8;
}

//===----------------------------------------------------------------------===//
//  VPtrType Definitions
//===----------------------------------------------------------------------===//

llvm::TypeSize
VPtrType::getTypeSizeInBits(const mlir::DataLayout &dataLayout,
                            mlir::DataLayoutEntryListRef params) const {
  // A vptr is an ordinary default-address-space pointer into the vtable. Size it
  // like one (mirroring PointerType) so targets with a non-64-bit pointer width
  // (e.g. i386/arm32) get the correct width instead of a hardcoded 64. The
  // member-data-pointer offset lowering derives field offsets by walking a
  // record's members via this interface, so a wrong vptr width shifts every
  // field offset in a polymorphic class.
  if (getContext()->getLoadedDialect<mlir::LLVM::LLVMDialect>()) {
    auto llvmPtrTy = mlir::LLVM::LLVMPointerType::get(getContext(), 0);
    return llvm::TypeSize::getFixed(dataLayout.getTypeSizeInBits(llvmPtrTy));
  }
  return llvm::TypeSize::getFixed(64);
}

uint64_t VPtrType::getABIAlignment(const mlir::DataLayout &dataLayout,
                                   mlir::DataLayoutEntryListRef params) const {
  if (getContext()->getLoadedDialect<mlir::LLVM::LLVMDialect>()) {
    auto llvmPtrTy = mlir::LLVM::LLVMPointerType::get(getContext(), 0);
    return dataLayout.getTypeABIAlignment(llvmPtrTy);
  }
  return 8;
}

//===----------------------------------------------------------------------===//
//  ArrayType Definitions
//===----------------------------------------------------------------------===//

llvm::TypeSize
ArrayType::getTypeSizeInBits(const ::mlir::DataLayout &dataLayout,
                             ::mlir::DataLayoutEntryListRef params) const {
  // Array elements are laid out using their alloc size, not their raw store
  // size. This preserves inter-element padding for types such as x86_fp80 and
  // short vectors whose ABI size is larger than their payload width.
  uint64_t elemStoreSize = dataLayout.getTypeSize(getElementType());
  uint64_t elemAlign = dataLayout.getTypeABIAlignment(getElementType());
  uint64_t elemAllocSize = llvm::alignTo(elemStoreSize, elemAlign);
  return llvm::TypeSize::getFixed(getSize() * elemAllocSize * 8);
}

uint64_t
ArrayType::getABIAlignment(const ::mlir::DataLayout &dataLayout,
                           ::mlir::DataLayoutEntryListRef params) const {
  return dataLayout.getTypeABIAlignment(getElementType());
}

//===----------------------------------------------------------------------===//
// VectorType Definitions
//===----------------------------------------------------------------------===//

llvm::TypeSize cir::VectorType::getTypeSizeInBits(
    const ::mlir::DataLayout &dataLayout,
    ::mlir::DataLayoutEntryListRef params) const {
  // Boolean vector elements are 1-bit packed (matching Clang's ext_vector_type
  // semantics for bool). All other element types use their full data layout
  // size.
  uint64_t elementBits = mlir::isa<cir::BoolType>(getElementType())
                             ? 1
                             : dataLayout.getTypeSizeInBits(getElementType());
  return llvm::TypeSize::getFixed(getSize() * elementBits);
}

uint64_t
cir::VectorType::getABIAlignment(const ::mlir::DataLayout &dataLayout,
                                 ::mlir::DataLayoutEntryListRef params) const {
  // Compute alignment from the byte size (not bit size) of the vector.
  // Vector alignment is the next power of 2 >= its byte size.
  uint64_t sizeInBytes =
      llvm::divideCeil(dataLayout.getTypeSizeInBits(*this), 8);
  return llvm::PowerOf2Ceil(sizeInBytes);
}

mlir::LogicalResult cir::VectorType::verify(
    llvm::function_ref<mlir::InFlightDiagnostic()> emitError,
    mlir::Type elementType, uint64_t size, bool scalable,
    bool sveFixedLength) {
  if (size == 0)
    return emitError() << "the number of vector elements must be non-zero";
  if (scalable && sveFixedLength)
    return emitError()
           << "a vector cannot be both scalable and SVE fixed-length";
  return success();
}

mlir::Type cir::VectorType::parse(::mlir::AsmParser &odsParser) {

  llvm::SMLoc odsLoc = odsParser.getCurrentLocation();
  mlir::Builder odsBuilder(odsParser.getContext());
  mlir::FailureOr<::mlir::Type> elementType;
  mlir::FailureOr<uint64_t> size;
  bool isScalable = false;
  bool isSveFixedLength = false;

  // Parse literal '<'
  if (odsParser.parseLess())
    return {};

  // Parse literal '[', if present, and set the scalability flag accordingly
  if (odsParser.parseOptionalLSquare().succeeded())
    isScalable = true;

  // Parse variable 'size'
  size = mlir::FieldParser<uint64_t>::parse(odsParser);
  if (mlir::failed(size)) {
    odsParser.emitError(odsParser.getCurrentLocation(),
                        "failed to parse CIR_VectorType parameter 'size' which "
                        "is to be a `uint64_t`");
    return {};
  }

  // Parse literal ']', which is expected when dealing with scalable
  // dim sizes
  if (isScalable && odsParser.parseRSquare().failed()) {
    odsParser.emitError(odsParser.getCurrentLocation(),
                        "missing closing `]` for scalable dim size");
    return {};
  }

  // Parse literal 'x'
  if (odsParser.parseKeyword("x"))
    return {};

  // Parse variable 'elementType'
  elementType = mlir::FieldParser<::mlir::Type>::parse(odsParser);
  if (mlir::failed(elementType)) {
    odsParser.emitError(odsParser.getCurrentLocation(),
                        "failed to parse CIR_VectorType parameter "
                        "'elementType' which is to be a `mlir::Type`");
    return {};
  }

  // Parse optional ', sve_fixed' keyword for SVE fixed-length vectors.
  if (odsParser.parseOptionalComma().succeeded()) {
    if (odsParser.parseKeyword("sve_fixed").failed()) {
      odsParser.emitError(odsParser.getCurrentLocation(),
                          "expected 'sve_fixed' keyword after ','");
      return {};
    }
    isSveFixedLength = true;
  }

  // Parse literal '>'
  if (odsParser.parseGreater())
    return {};
  return odsParser.getChecked<VectorType>(odsLoc, odsParser.getContext(),
                                          mlir::Type((*elementType)),
                                          uint64_t((*size)), isScalable,
                                          isSveFixedLength);
}

void cir::VectorType::print(mlir::AsmPrinter &odsPrinter) const {
  mlir::Builder odsBuilder(getContext());
  odsPrinter << "<";
  if (this->getIsScalable())
    odsPrinter << "[";

  odsPrinter.printStrippedAttrOrType(getSize());
  if (this->getIsScalable())
    odsPrinter << "]";
  odsPrinter << ' ' << "x";
  odsPrinter << ' ';
  odsPrinter.printStrippedAttrOrType(getElementType());
  if (this->getIsSveFixedLength())
    odsPrinter << ", sve_fixed";
  odsPrinter << ">";
}

//===----------------------------------------------------------------------===//
// AddressSpace definitions
//===----------------------------------------------------------------------===//

bool cir::isSupportedCIRMemorySpaceAttr(
    mlir::ptr::MemorySpaceAttrInterface memorySpace) {
  return mlir::isa<cir::LangAddressSpaceAttr, cir::TargetAddressSpaceAttr>(
      memorySpace);
}

cir::LangAddressSpace cir::toCIRLangAddressSpace(clang::LangAS langAS) {
  using clang::LangAS;
  switch (langAS) {
  case LangAS::Default:
    return LangAddressSpace::Default;
  case LangAS::opencl_global:
  case LangAS::opencl_global_device:
  case LangAS::opencl_global_host:
  case LangAS::sycl_global:
  case LangAS::sycl_global_device:
  case LangAS::sycl_global_host:
    return LangAddressSpace::OffloadGlobal;
  case LangAS::opencl_local:
  case LangAS::cuda_shared:
  case LangAS::sycl_local:
    return LangAddressSpace::OffloadLocal;
  case LangAS::cuda_device:
    return LangAddressSpace::OffloadGlobal;
  case LangAS::opencl_constant:
  case LangAS::cuda_constant:
    return LangAddressSpace::OffloadConstant;
  case LangAS::opencl_private:
  case LangAS::sycl_private:
    return LangAddressSpace::OffloadPrivate;
  case LangAS::opencl_generic:
    return LangAddressSpace::OffloadGeneric;
  default:
    llvm_unreachable("unsupported language address space");
  }
}

mlir::ptr::MemorySpaceAttrInterface
cir::toCIRLangAddressSpaceAttr(mlir::MLIRContext *ctx, clang::LangAS langAS) {
  using clang::LangAS;

  if (langAS == LangAS::Default)
    return {}; // Default address space is represented as an empty attribute.

  if (clang::isTargetAddressSpace(langAS)) {
    unsigned targetAS = clang::toTargetAddressSpace(langAS);
    return cir::TargetAddressSpaceAttr::get(ctx, targetAS);
  }

  return cir::LangAddressSpaceAttr::get(ctx, toCIRLangAddressSpace(langAS));
}

cir::TargetAddressSpaceAttr
cir::toCIRTargetAddressSpace(mlir::MLIRContext &context, clang::LangAS langAS) {
  return cir::TargetAddressSpaceAttr::get(&context,
                                          clang::toTargetAddressSpace(langAS));
}

bool cir::isMatchingAddressSpace(mlir::ptr::MemorySpaceAttrInterface cirAS,
                                 clang::LangAS as) {
  // If there is no CIR address space attr, consider it "default" and only match
  // when the AST address space is LangAS::Default.
  if (!cirAS)
    return as == clang::LangAS::Default;

  // Handle target address spaces.
  if (auto targetAS = mlir::dyn_cast<cir::TargetAddressSpaceAttr>(cirAS)) {
    if (!isTargetAddressSpace(as))
      return false;
    return targetAS.getValue() == toTargetAddressSpace(as);
  }

  // Handle language address spaces.
  if (auto langAS = mlir::dyn_cast<cir::LangAddressSpaceAttr>(cirAS)) {
    auto expected = toCIRLangAddressSpace(as);
    return langAS.getValue() == expected;
  }

  return false;
}

static mlir::ParseResult
parseAddressSpaceValue(mlir::AsmParser &p,
                       mlir::ptr::MemorySpaceAttrInterface &attr) {
  llvm::SMLoc loc = p.getCurrentLocation();

  // Try to parse target address space: target_address_space(N)
  attr = nullptr;
  if (p.parseOptionalKeyword("target_address_space").succeeded()) {
    unsigned val;
    if (p.parseLParen())
      return p.emitError(loc, "expected '(' after target address space");

    if (p.parseInteger(val) || p.parseRParen())
      return p.emitError(loc, "expected target address space value");

    attr = cir::TargetAddressSpaceAttr::get(p.getContext(), val);
    return mlir::success();
  }

  // Try to parse language specific address space: lang_address_space(keyword)
  if (p.parseOptionalKeyword("lang_address_space").succeeded()) {
    if (p.parseLParen())
      return p.emitError(loc, "expected '(' after clang address space");
    mlir::FailureOr<cir::LangAddressSpace> result =
        mlir::FieldParser<cir::LangAddressSpace>::parse(p);

    if (mlir::failed(result) || p.parseRParen())
      return p.emitError(loc, "expected language address space keyword");

    attr = cir::LangAddressSpaceAttr::get(p.getContext(), result.value());
    return mlir::success();
  }

  return p.emitError(p.getCurrentLocation(),
                     "unexpected address-space attribute kind");
}

mlir::ParseResult
parseAddressSpaceValue(mlir::AsmParser &p,
                       mlir::ptr::MemorySpaceAttrInterface &attr,
                       bool wrapped) {
  if (wrapped) {
    if (failed(p.parseKeyword("addrspace")))
      return mlir::failure();
    if (failed(p.parseLParen()))
      return mlir::failure();
  }

  if (parseAddressSpaceValue(p, attr).failed())
    return mlir::failure();

  if (wrapped) {
    if (failed(p.parseRParen()))
      return p.emitError(p.getCurrentLocation(),
                         "expected ')' after address space value");
  }

  return mlir::success();
}

static void printAddressSpaceValue(mlir::AsmPrinter &printer,
                                   mlir::ptr::MemorySpaceAttrInterface attr) {
  if (!attr)
    return;

  if (auto langAS = mlir::dyn_cast<cir::LangAddressSpaceAttr>(attr)) {
    printer << "lang_address_space("
            << cir::stringifyLangAddressSpace(langAS.getValue()) << ')';
  } else if (auto targetAS =
                 mlir::dyn_cast<cir::TargetAddressSpaceAttr>(attr)) {
    printer << "target_address_space(" << targetAS.getValue() << ")";
  } else {
    llvm_unreachable("unexpected address-space attribute kind");
  }
}

mlir::ParseResult
parseGlobalAddressSpaceValue(mlir::AsmParser &p,
                             mlir::ptr::MemorySpaceAttrInterface &attr) {
  // Try to parse target address space: target_address_space(N)
  attr = nullptr;
  if (p.parseOptionalKeyword("target_address_space").succeeded()) {
    unsigned val;
    mlir::SMLoc loc = p.getCurrentLocation();
    if (p.parseLParen())
      return p.emitError(loc, "expected '(' after target address space");

    if (p.parseInteger(val) || p.parseRParen())
      return p.emitError(loc, "expected target address space value");

    attr = cir::TargetAddressSpaceAttr::get(p.getContext(), val);
    return mlir::success();
  }

  // Try to parse language specific address space: lang_address_space(keyword)
  if (p.parseOptionalKeyword("lang_address_space").succeeded()) {
    mlir::SMLoc loc = p.getCurrentLocation();
    if (p.parseLParen())
      return p.emitError(loc, "expected '(' after clang address space");
    mlir::FailureOr<cir::LangAddressSpace> result =
        mlir::FieldParser<cir::LangAddressSpace>::parse(p);

    if (mlir::failed(result) || p.parseRParen())
      return p.emitError(loc, "expected language address space keyword");

    attr = cir::LangAddressSpaceAttr::get(p.getContext(), result.value());
    return mlir::success();
  }

  // No address space found - that's fine for optional addr_space.
  return mlir::success();
}

void printGlobalAddressSpaceValue(mlir::AsmPrinter &printer, cir::GlobalOp,
                                  mlir::ptr::MemorySpaceAttrInterface attr) {
  if (attr)
    printAddressSpaceValue(printer, attr);
}

mlir::LogicalResult cir::PointerType::verify(
    llvm::function_ref<mlir::InFlightDiagnostic()> emitError,
    mlir::Type pointee, mlir::ptr::MemorySpaceAttrInterface addrSpace) {
  if (addrSpace) {
    if (!isSupportedCIRMemorySpaceAttr(addrSpace)) {
      return emitError() << "CIR Address spaces must be either target_address "
                            "space or lang_address_space";
    }
  }

  return mlir::success();
}

//===----------------------------------------------------------------------===//
// CIR Dialect
//===----------------------------------------------------------------------===//

void CIRDialect::registerTypes() {
  // Register tablegen'd types.
  addTypes<
#define GET_TYPEDEF_LIST
#include "clang/CIR/Dialect/IR/CIROpsTypes.cpp.inc"
      >();

  // Register raw C++ types.
  // TODO(CIR) addTypes<RecordType>();
}
