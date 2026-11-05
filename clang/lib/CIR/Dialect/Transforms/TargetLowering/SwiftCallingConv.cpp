//===--- SwiftCallingConv.cpp - CIR Swift ABI lowering --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements Swift calling convention lowering for CIR types. Ported from
// clang/lib/CodeGen/SwiftCallingConv.cpp.
//
//===----------------------------------------------------------------------===//

#include "SwiftCallingConv.h"
#include "ABIInfo.h"
#include "LowerTypes.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/RecordLayout.h"
#include "clang/CIR/Dialect/IR/CIRDataLayout.h"
#include "clang/CIR/Dialect/IR/CIRTypes.h"

using namespace clang;

namespace cir {
namespace swiftcall {

//===----------------------------------------------------------------------===//
// Type helpers
//===----------------------------------------------------------------------===//

static bool isCIRFloatingPointType(mlir::Type ty) {
  return mlir::isa<cir::SingleType, cir::DoubleType, cir::LongDoubleType,
                   cir::FP16Type, cir::BF16Type, cir::FP80Type,
                   cir::FP128Type>(ty);
}

static bool isCIRIntegerType(mlir::Type ty) {
  return mlir::isa<IntType>(ty);
}

static bool isCIRPointerType(mlir::Type ty) {
  return mlir::isa<cir::PointerType>(ty);
}

static bool isCIRVectorType(mlir::Type ty) {
  return mlir::isa<VectorType>(ty);
}

static CharUnits getTypeStoreSize(LowerTypes &LT, mlir::Type type) {
  return CharUnits::fromQuantity(LT.getDataLayout().getTypeStoreSize(type));
}

static CharUnits getTypeAllocSize(LowerTypes &LT, mlir::Type type) {
  return CharUnits::fromQuantity(LT.getDataLayout().getTypeAllocSize(type));
}

/// Given two CIR types with the same size, try to find a common type.
static mlir::Type getCommonType(mlir::Type first, mlir::Type second) {
  assert(first != second);

  // Allow pointers to merge with integers, but prefer the integer type.
  if (isCIRIntegerType(first)) {
    if (isCIRPointerType(second))
      return first;
  } else if (isCIRPointerType(first)) {
    if (isCIRIntegerType(second))
      return second;
    if (isCIRPointerType(second))
      return first;
  } else if (auto firstVecTy = mlir::dyn_cast<VectorType>(first)) {
    if (auto secondVecTy = mlir::dyn_cast<VectorType>(second)) {
      if (firstVecTy.getSize() == secondVecTy.getSize()) {
        if (auto commonTy = getCommonType(firstVecTy.getElementType(),
                                          secondVecTy.getElementType()))
          return commonTy == firstVecTy.getElementType() ? first : second;
      }
    }
  }

  return nullptr;
}

//===----------------------------------------------------------------------===//
// Public helpers
//===----------------------------------------------------------------------===//

CharUnits getMaximumVoluntaryIntegerSize(LowerTypes &LT) {
  // Currently always the size of an ordinary pointer.
  unsigned ptrWidth = LT.getTarget().getPointerWidth(clang::LangAS::Default);
  return CharUnits::fromQuantity(ptrWidth / 8);
}

CharUnits getNaturalAlignment(LowerTypes &LT, mlir::Type type) {
  auto size = (unsigned long long)getTypeStoreSize(LT, type).getQuantity();
  size = llvm::bit_ceil(size);
  return CharUnits::fromQuantity(size);
}

bool isLegalIntegerType(LowerTypes &LT, unsigned bitWidth) {
  switch (bitWidth) {
  case 1:
  case 8:
  case 16:
  case 32:
  case 64:
    return true;
  case 128:
    return LT.getTarget().hasInt128Type();
  default:
    return false;
  }
}

bool isLegalVectorType(LowerTypes &LT, CharUnits vectorSize,
                       mlir::Type eltTy, unsigned numElts) {
  assert(numElts > 1 && "illegal vector length");
  const llvm::Triple &triple = LT.getTarget().getTriple();
  switch (triple.getArch()) {
  case llvm::Triple::aarch64:
  case llvm::Triple::aarch64_32:
    if (!llvm::isPowerOf2_32(numElts))
      return false;
    return vectorSize.getQuantity() == 8 ||
           (vectorSize.getQuantity() == 16 && numElts != 1);
  case llvm::Triple::arm:
  case llvm::Triple::thumb:
    if (!llvm::isPowerOf2_32(numElts))
      return false;
    if (getTypeStoreSize(LT, eltTy).getQuantity() * 8 > 64)
      return false;
    return vectorSize.getQuantity() == 8 ||
           (vectorSize.getQuantity() == 16 && numElts != 1);
  default:
    // Default: assume 128-bit SIMD support but nothing more.
    return (vectorSize.getQuantity() > 8 && vectorSize.getQuantity() <= 16);
  }
}

bool occupiesMoreThan(LowerTypes &LT,
                      llvm::ArrayRef<mlir::Type> scalarTypes,
                      unsigned maxAllRegisters) {
  unsigned intCount = 0, fpCount = 0;
  unsigned ptrWidth = LT.getTarget().getPointerWidth(clang::LangAS::Default);
  for (mlir::Type type : scalarTypes) {
    if (isCIRPointerType(type)) {
      intCount++;
    } else if (auto intTy = mlir::dyn_cast<IntType>(type)) {
      intCount += (intTy.getWidth() + ptrWidth - 1) / ptrWidth;
    } else {
      assert(isCIRVectorType(type) || isCIRFloatingPointType(type));
      fpCount++;
    }
  }
  return (intCount + fpCount > maxAllRegisters);
}

static std::pair<mlir::Type, unsigned>
splitLegalVectorType(LowerTypes &LT, CharUnits vectorSize, VectorType vectorTy) {
  unsigned numElts = vectorTy.getSize();
  mlir::Type eltTy = vectorTy.getElementType();

  if (numElts >= 4 && llvm::isPowerOf2_32(numElts)) {
    if (isLegalVectorType(LT, vectorSize / 2, eltTy, numElts / 2))
      return {VectorType::get(eltTy, numElts / 2), 2};
  }

  return {eltTy, numElts};
}

static void legalizeVectorType(LowerTypes &LT, CharUnits origVectorSize,
                               VectorType origVectorTy,
                               llvm::SmallVectorImpl<mlir::Type> &components) {
  unsigned numElts = origVectorTy.getSize();
  mlir::Type eltTy = origVectorTy.getElementType();

  if (isLegalVectorType(LT, origVectorSize, eltTy, numElts)) {
    components.push_back(origVectorTy);
    return;
  }

  cir_cconv_assert(numElts != 1 && "illegal single-element vector");

  unsigned logCandidateNumElts = llvm::Log2_32(numElts);
  unsigned candidateNumElts = 1U << logCandidateNumElts;
  cir_cconv_assert(candidateNumElts <= numElts &&
                   candidateNumElts * 2 > numElts);

  if (candidateNumElts == numElts) {
    logCandidateNumElts--;
    candidateNumElts >>= 1;
  }

  CharUnits eltSize = origVectorSize / numElts;
  CharUnits candidateSize = eltSize * candidateNumElts;

  while (logCandidateNumElts > 0) {
    cir_cconv_assert(candidateNumElts == 1U << logCandidateNumElts);
    cir_cconv_assert(candidateNumElts <= numElts);
    cir_cconv_assert(candidateSize == eltSize * candidateNumElts);

    if (!isLegalVectorType(LT, candidateSize, eltTy, candidateNumElts)) {
      logCandidateNumElts--;
      candidateNumElts /= 2;
      candidateSize /= 2;
      continue;
    }

    unsigned numVecs = numElts >> logCandidateNumElts;
    components.append(numVecs, VectorType::get(eltTy, candidateNumElts));
    numElts -= (numVecs << logCandidateNumElts);

    if (numElts == 0)
      return;

    if (numElts > 2 && !llvm::isPowerOf2_32(numElts) &&
        isLegalVectorType(LT, eltSize * numElts, eltTy, numElts)) {
      components.push_back(VectorType::get(eltTy, numElts));
      return;
    }

    do {
      logCandidateNumElts--;
      candidateNumElts /= 2;
      candidateSize /= 2;
    } while (candidateNumElts > numElts);
  }

  components.append(numElts, eltTy);
}

//===----------------------------------------------------------------------===//
// SwiftAggLowering
//===----------------------------------------------------------------------===//

static bool isMergeableEntryType(mlir::Type type) {
  if (!type)
    return true;
  // Floating-point and vector types should never be merged.
  return (!isCIRFloatingPointType(type) && !isCIRVectorType(type));
}

void SwiftAggLowering::addTypedData(mlir::Type type, CharUnits begin) {
  assert(type && "didn't provide type for typed data");
  addTypedData(type, begin, begin + getTypeStoreSize(LT, type));
}

void SwiftAggLowering::addTypedData(mlir::Type type, CharUnits begin,
                                    CharUnits end) {
  assert(type && "didn't provide type for typed data");

  // Record types: recurse into members.
  if (auto recTy = mlir::dyn_cast<RecordType>(type)) {
    if (recTy.isUnion()) {
      // Unions: add all members at the same offset.
      for (auto memberTy : recTy.getMembers())
        addTypedData(memberTy, begin);
      return;
    }

    auto &dl = LT.getDataLayout().layout;
    for (unsigned i = 0, e = recTy.getNumElements(); i != e; ++i) {
      CharUnits memberOffset =
          CharUnits::fromQuantity(recTy.getElementOffset(dl, i));
      addTypedData(recTy.getMembers()[i], begin + memberOffset);
    }
    return;
  }

  // Array types.
  if (auto arrTy = mlir::dyn_cast<ArrayType>(type)) {
    auto eltTy = arrTy.getElementType();
    auto eltSize = getTypeStoreSize(LT, eltTy);
    for (uint64_t i = 0, e = arrTy.getSize(); i != e; ++i)
      addTypedData(eltTy, begin + i * eltSize);
    return;
  }

  // Complex types: add real and imaginary parts separately.
  if (auto complexTy = mlir::dyn_cast<ComplexType>(type)) {
    auto eltTy = complexTy.getElementType();
    auto eltSize = getTypeStoreSize(LT, eltTy);
    addTypedData(eltTy, begin, begin + eltSize);
    addTypedData(eltTy, begin + eltSize, begin + 2 * eltSize);
    return;
  }

  if (auto vecTy = mlir::dyn_cast<VectorType>(type)) {
    llvm::SmallVector<mlir::Type, 4> componentTys;
    legalizeVectorType(LT, end - begin, vecTy, componentTys);
    cir_cconv_assert(!componentTys.empty() &&
                     "swiftcall vector legalization produced no components");

    for (size_t i = 0, e = componentTys.size(); i + 1 != e; ++i) {
      mlir::Type componentTy = componentTys[i];
      CharUnits componentSize = getTypeStoreSize(LT, componentTy);
      cir_cconv_assert(componentSize < end - begin);
      addLegalTypedData(componentTy, begin, begin + componentSize);
      begin += componentSize;
    }

    return addLegalTypedData(componentTys.back(), begin, end);
  }

  // Legalize integer types.
  if (auto intTy = mlir::dyn_cast<IntType>(type)) {
    if (!isLegalIntegerType(LT, intTy.getWidth())) {
      addOpaqueData(begin, end);
      return;
    }
  }

  // All other scalar types (including MethodType, DataMemberType, PointerType)
  // should be legal.
  addLegalTypedData(type, begin, end);
}

void SwiftAggLowering::addLegalTypedData(mlir::Type type, CharUnits begin,
                                         CharUnits end) {
  // Require the type to be naturally aligned.
  if (!begin.isZero() &&
      !begin.isMultipleOf(getNaturalAlignment(LT, type))) {
    // If it's a vector, split it into elements.
    if (auto vecTy = mlir::dyn_cast<VectorType>(type)) {
      auto [eltTy, numElts] = splitLegalVectorType(LT, end - begin, vecTy);
      CharUnits eltSize = (end - begin) / numElts;
      for (unsigned i = 0; i != numElts; ++i) {
        addLegalTypedData(eltTy, begin, begin + eltSize);
        begin += eltSize;
      }
      return;
    }

    addOpaqueData(begin, end);
    return;
  }

  addEntry(type, begin, end);
}

void SwiftAggLowering::addEntry(mlir::Type type, CharUnits begin,
                                CharUnits end) {
  // Fast path: we can just add entries to the end.
  if (Entries.empty() || Entries.back().End <= begin) {
    Entries.push_back({begin, end, type});
    return;
  }

  // Find the first existing entry that ends after the start of the new data.
  size_t index = Entries.size() - 1;
  while (index != 0) {
    if (Entries[index - 1].End <= begin)
      break;
    --index;
  }

  // If the entry starts after the end of the new data, there's no conflict.
  if (Entries[index].Begin >= end) {
    Entries.insert(Entries.begin() + index, {begin, end, type});
    return;
  }

  // Otherwise, the ranges overlap.
restartAfterSplit:

  // Simplest case: an exact overlap.
  if (Entries[index].Begin == begin && Entries[index].End == end) {
    if (Entries[index].Type == type)
      return;

    if (!Entries[index].Type) {
      return;
    } else if (!type) {
      Entries[index].Type = nullptr;
      return;
    }

    if (auto entryType = getCommonType(Entries[index].Type, type)) {
      Entries[index].Type = entryType;
      return;
    }

    Entries[index].Type = nullptr;
    return;
  }

  // Split vector entries for partial overlaps.
  if (auto vecTy = mlir::dyn_cast_or_null<VectorType>(type)) {
    auto eltTy = vecTy.getElementType();
    unsigned numElts = vecTy.getSize();
    CharUnits eltSize = (end - begin) / numElts;
    for (unsigned i = 0; i != numElts; ++i) {
      addEntry(eltTy, begin, begin + eltSize);
      begin += eltSize;
    }
    return;
  }

  if (Entries[index].Type && isCIRVectorType(Entries[index].Type)) {
    splitVectorEntry(index);
    goto restartAfterSplit;
  }

  // Make the existing entry opaque.
  Entries[index].Type = nullptr;

  if (begin < Entries[index].Begin) {
    Entries[index].Begin = begin;
    assert(index == 0 || begin >= Entries[index - 1].End);
  }

  while (end > Entries[index].End) {
    assert(!Entries[index].Type);

    if (index == Entries.size() - 1 || end <= Entries[index + 1].Begin) {
      Entries[index].End = end;
      break;
    }

    Entries[index].End = Entries[index + 1].Begin;
    index++;

    if (!Entries[index].Type)
      continue;

    if (isCIRVectorType(Entries[index].Type) && end < Entries[index].End)
      splitVectorEntry(index);

    Entries[index].Type = nullptr;
  }
}

void SwiftAggLowering::splitVectorEntry(unsigned index) {
  auto vecTy = mlir::cast<VectorType>(Entries[index].Type);
  auto [eltTy, numElts] = splitLegalVectorType(LT, Entries[index].getWidth(),
                                               vecTy);
  CharUnits eltSize = getTypeStoreSize(LT, eltTy);

  Entries.insert(Entries.begin() + index + 1, numElts - 1, StorageEntry());

  CharUnits begin = Entries[index].Begin;
  for (unsigned i = 0; i != numElts; ++i) {
    unsigned idx = index + i;
    Entries[idx].Type = eltTy;
    Entries[idx].Begin = begin;
    Entries[idx].End = begin + eltSize;
    begin += eltSize;
  }
}

static CharUnits getOffsetAtStartOfUnit(CharUnits offset, CharUnits unitSize) {
  assert(llvm::isPowerOf2_64(unitSize.getQuantity()));
  auto unitMask = ~(unitSize.getQuantity() - 1);
  return CharUnits::fromQuantity(offset.getQuantity() & unitMask);
}

static bool areBytesInSameUnit(CharUnits first, CharUnits second,
                               CharUnits chunkSize) {
  return getOffsetAtStartOfUnit(first, chunkSize) ==
         getOffsetAtStartOfUnit(second, chunkSize);
}

bool SwiftAggLowering::shouldMergeEntries(const StorageEntry &first,
                                          const StorageEntry &second,
                                          CharUnits chunkSize) {
  if (!areBytesInSameUnit(first.End - CharUnits::One(), second.Begin,
                          chunkSize))
    return false;

  return (isMergeableEntryType(first.Type) &&
          isMergeableEntryType(second.Type));
}

void SwiftAggLowering::finish() {
  if (Entries.empty()) {
    Finished = true;
    return;
  }

  const CharUnits chunkSize = getMaximumVoluntaryIntegerSize(LT);

  // First pass: merge adjacent integer/opaque entries in the same chunk.
  bool hasOpaqueEntries = (!Entries[0].Type);
  for (size_t i = 1, e = Entries.size(); i != e; ++i) {
    if (shouldMergeEntries(Entries[i - 1], Entries[i], chunkSize)) {
      Entries[i - 1].Type = nullptr;
      Entries[i].Type = nullptr;
      Entries[i - 1].End = Entries[i].Begin;
      hasOpaqueEntries = true;
    } else if (!Entries[i].Type) {
      hasOpaqueEntries = true;
    }
  }

  if (!hasOpaqueEntries) {
    Finished = true;
    return;
  }

  // Second pass: replace opaque ranges with aligned integer types.
  auto orig = std::move(Entries);
  assert(Entries.empty());

  auto *ctx = LT.getMLIRContext();

  for (size_t i = 0, e = orig.size(); i != e; ++i) {
    if (orig[i].Type) {
      Entries.push_back(orig[i]);
      continue;
    }

    auto begin = orig[i].Begin;
    auto end = orig[i].End;
    while (i + 1 != e && !orig[i + 1].Type && end == orig[i + 1].Begin) {
      end = orig[i + 1].End;
      i++;
    }

    // Add an entry per intersected chunk.
    do {
      CharUnits localBegin = begin;
      CharUnits chunkBegin = getOffsetAtStartOfUnit(localBegin, chunkSize);
      CharUnits chunkEnd = chunkBegin + chunkSize;
      CharUnits localEnd = std::min(end, chunkEnd);

      CharUnits unitSize = CharUnits::One();
      CharUnits unitBegin, unitEnd;
      for (;; unitSize *= 2) {
        assert(unitSize <= chunkSize);
        unitBegin = getOffsetAtStartOfUnit(localBegin, unitSize);
        unitEnd = unitBegin + unitSize;
        if (unitEnd >= localEnd)
          break;
      }

      auto entryTy = IntType::get(ctx, unitSize.getQuantity() * 8, false);
      Entries.push_back({unitBegin, unitEnd, entryTy});

      begin = localEnd;
    } while (begin != end);
  }

  Finished = true;
}

bool SwiftAggLowering::shouldPassIndirectly(bool asReturnValue) const {
  assert(Finished && "haven't yet finished lowering");

  if (Entries.empty())
    return false;

  llvm::SmallVector<mlir::Type, 8> componentTys;
  componentTys.reserve(Entries.size());
  for (auto &entry : Entries)
    componentTys.push_back(entry.Type);

  return occupiesMoreThan(LT, componentTys, /*maxAllRegisters=*/4);
}

std::pair<mlir::Type, mlir::Type>
SwiftAggLowering::getCoerceAndExpandTypes() const {
  assert(Finished && "haven't yet finished lowering");

  auto *ctx = LT.getMLIRContext();

  if (Entries.empty()) {
    auto type = RecordType::get(ctx, /*members=*/{}, /*packed=*/false,
                                /*padded=*/false,
                                RecordType::RecordKind::Struct);
    return {type, type};
  }

  llvm::SmallVector<mlir::Type, 8> elts;
  CharUnits lastEnd = CharUnits::Zero();
  bool hasPadding = false;
  bool packed = false;
  for (auto &entry : Entries) {
    if (entry.Begin != lastEnd) {
      auto paddingSize = entry.Begin - lastEnd;
      assert(!paddingSize.isNegative());

      auto padding =
          ArrayType::get(IntType::get(ctx, 8, false), paddingSize.getQuantity());
      elts.push_back(padding);
      hasPadding = true;
    }

    if (!packed && !entry.Begin.isMultipleOf(CharUnits::fromQuantity(
                       LT.getDataLayout().getABITypeAlign(entry.Type).value())))
      packed = true;

    elts.push_back(entry.Type);

    lastEnd = entry.Begin + getTypeAllocSize(LT, entry.Type);
    assert(entry.End <= lastEnd);
  }

  auto coercionType =
      RecordType::get(ctx, elts, packed, /*padded=*/hasPadding,
                      RecordType::RecordKind::Struct);

  mlir::Type unpaddedType = coercionType;
  if (hasPadding) {
    elts.clear();
    for (auto &entry : Entries)
      elts.push_back(entry.Type);
    if (elts.size() == 1) {
      unpaddedType = elts[0];
    } else {
      unpaddedType =
          RecordType::get(ctx, elts, /*packed=*/false, /*padded=*/false,
                          RecordType::RecordKind::Struct);
    }
  } else if (Entries.size() == 1) {
    unpaddedType = Entries[0].Type;
  }

  return {coercionType, unpaddedType};
}

static bool mustPassRecordIndirectly(RecordType recTy) {
  if (auto ast = recTy.getAst())
    return !ast.getRawDecl()->canPassInRegisters();
  return false;
}

static unsigned getSwiftRecordAlignment(LowerTypes &LT, RecordType recTy) {
  if (auto ast = recTy.getAst())
    if (const auto *def = ast.getRawDecl()->getDefinition())
      return def->getASTContext().getASTRecordLayout(def).getAlignment()
          .getQuantity();

  return LT.getContext().getTypeAlign(recTy) / 8;
}

static bool isEmptyRecordType(RecordType recTy) {
  auto ast = recTy.getAst();
  if (!ast)
    return false;

  const auto *RD = ast.getRawDecl();
  if (!RD)
    return false;

  if (const auto *CXXRD = llvm::dyn_cast<CXXRecordDecl>(RD)) {
    const auto &layout = RD->getASTContext().getASTRecordLayout(RD);
    return CXXRD->field_empty() && CXXRD->bases().empty() &&
           CXXRD->vbases().empty() && !layout.hasOwnVFPtr() &&
           !layout.hasOwnVBPtr();
  }

  return RD->field_empty();
}

//===----------------------------------------------------------------------===//
// Type classification
//===----------------------------------------------------------------------===//

ABIArgInfo classifyType(LowerTypes &LT, mlir::Type type,
                        bool forReturn) {
  if (forReturn && mlir::isa<cir::VoidType>(type))
    return ABIArgInfo::getIgnore();

  // Record types: use SwiftAggLowering.
  if (auto recTy = mlir::dyn_cast<RecordType>(type)) {
    if (isEmptyRecordType(recTy))
      return ABIArgInfo::getIgnore();

    // Match OGCG's use of ASTRecordLayout::getAlignment() for source records.
    // The lowered CIR record body can inherit the natural alignment of 64-bit
    // members on ARM32 even when the source ABI only guarantees 4-byte record
    // alignment, which would overstate sret/byref alignments.
    unsigned alignment = getSwiftRecordAlignment(LT, recTy);
    if (mustPassRecordIndirectly(recTy))
      return ABIArgInfo::getIndirect(alignment, /*byval=*/false);

    SwiftAggLowering lowering(LT);
    lowering.addTypedData(type, CharUnits::Zero());
    lowering.finish();

    if (lowering.empty())
      return ABIArgInfo::getIgnore();

    if (lowering.shouldPassIndirectly(forReturn))
      return ABIArgInfo::getIndirect(alignment, /*byval=*/false);

    auto [coercionType, unpaddedType] = lowering.getCoerceAndExpandTypes();
    return ABIArgInfo::getCoerceAndExpand(
        mlir::cast<RecordType>(coercionType), unpaddedType);
  }

  // Complex types: pass directly (both arg and return).
  // OGCG uses Expand for args, but CIR doesn't have Expand — Direct with
  // canBeFlattened achieves the same effect for complex types.
  if (mlir::isa<ComplexType>(type))
    return ABIArgInfo::getDirect();

  // Vector types.
  if (mlir::isa<VectorType>(type)) {
    SwiftAggLowering lowering(LT);
    lowering.addTypedData(type, CharUnits::Zero());
    lowering.finish();

    unsigned alignment =
        LT.getDataLayout().getABITypeAlign(type).value();
    if (lowering.shouldPassIndirectly(forReturn))
      return ABIArgInfo::getIndirect(alignment, /*byval=*/false);

    auto [coercionType, unpaddedType] = lowering.getCoerceAndExpandTypes();
    return ABIArgInfo::getCoerceAndExpand(
        mlir::cast<RecordType>(coercionType), unpaddedType);
  }

  // Void should be ignored.
  if (mlir::isa<cir::VoidType>(type))
    return ABIArgInfo::getIgnore();

  // Everything else (scalars, pointers, etc.) is passed directly.
  return ABIArgInfo::getDirect();
}

void computeABIInfo(LowerTypes &LT, LowerFunctionInfo &FI) {
  auto &retInfo = FI.getReturnInfo();
  retInfo = classifyType(LT, FI.getReturnType(), /*forReturn=*/true);

  for (auto &arg : FI.arguments())
    arg.info = classifyType(LT, arg.type, /*forReturn=*/false);
}

} // namespace swiftcall
} // namespace cir
