//===- LoweringPrepareX86CXXABI.cpp - X86 ABI specific code ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides X86_64 C++ ABI specific code that is used during
// LLVMIR lowering prepare.
//
//===----------------------------------------------------------------------===//

#include "../LoweringPrepareCXXABI.h"
#include "../LoweringPrepareVAArgStack.h"
#include "ABIInfoImpl.h"
#include "LowerModule.h"
#include "Targets/X86_64ABIInfo.h"
#include "clang/CIR/Dialect/IR/CIRDataLayout.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"

#include "llvm/ADT/DenseMap.h"
#include <numeric>

using namespace mlir;
using namespace cir;

namespace {

class LoweringPrepareX86CXXABI : public cir::LoweringPrepareCXXABI {
  struct VAArgClassification {
    ABIArgInfo argInfo;
    unsigned neededInt = 0;
    unsigned neededSSE = 0;
  };
  struct VAArgMemoryLayout {
    uint64_t alignment = 1;
    uint64_t stride = 0;
    bool alignOverflowArgArea = false;
  };

  bool is64;
  mlir::ModuleOp cachedModule = nullptr;
  std::unique_ptr<mlir::PatternRewriter> moduleRewriter;
  std::unique_ptr<cir::LowerModule> lowerModuleCache;
  std::unique_ptr<X86_64ABIInfo> x86_64ABIInfoCache;
  llvm::DenseMap<mlir::Type, VAArgClassification> vaArgClassificationCache;
  llvm::DenseMap<mlir::Type, VAArgMemoryLayout> vaArgMemoryLayoutCache;
  llvm::DenseMap<mlir::Type, uint64_t> abiAlignCache;
  llvm::DenseMap<mlir::Type, uint64_t> typeStoreSizeCache;
  llvm::DenseMap<mlir::Type, uint64_t> typeAllocSizeCache;
  X86AVXABILevel cachedAVXABILevel = X86AVXABILevel::None;
  bool hasCachedAVXABILevel = false;

public:
  LoweringPrepareX86CXXABI(bool is64) : is64(is64) {}

  mlir::Value lowerDynamicCast(cir::CIRBaseBuilderTy &builder,
                               clang::ASTContext &astCtx,
                               cir::DynamicCastOp op) override;

  mlir::Value lowerVAArg(cir::CIRBaseBuilderTy &builder, cir::VAArgOp op,
                         const cir::CIRDataLayout &datalayout) override {
    if (is64)
      return lowerVAArgX86_64(builder, op, datalayout);

    return lowerVAArgX86_32(builder, op, datalayout);
  }

  mlir::Value lowerVAArgX86_64(cir::CIRBaseBuilderTy &builder,
                               cir::VAArgOp op,
                               const cir::CIRDataLayout &datalayout);
  mlir::Value lowerVAArgX86_32(cir::CIRBaseBuilderTy &builder,
                               cir::VAArgOp op,
                               const cir::CIRDataLayout &datalayout);

private:
  cir::LowerModule *getOrCreateLowerModule(cir::VAArgOp op);
  const X86_64ABIInfo *getOrCreateX86_64ABIInfo(cir::LowerModule &lowerModule);
  const VAArgClassification &getOrClassifyVAArgType(cir::LowerModule &lowerModule,
                                                    mlir::Type ty);
  const VAArgMemoryLayout &
  getOrComputeVAArgMemoryLayout(const cir::CIRDataLayout &datalayout,
                                mlir::Type ty, uint64_t typeAlignHint);
  uint64_t getOrComputeABITypeAlign(const cir::CIRDataLayout &datalayout,
                                    mlir::Type ty);
  uint64_t getOrComputeTypeStoreSize(const cir::CIRDataLayout &datalayout,
                                     mlir::Type ty);
  uint64_t getOrComputeTypeAllocSize(const cir::CIRDataLayout &datalayout,
                                     mlir::Type ty);
};

cir::LowerModule *LoweringPrepareX86CXXABI::getOrCreateLowerModule(
    cir::VAArgOp op) {
  mlir::ModuleOp mo = op->getParentOfType<mlir::ModuleOp>();
  if (!mo)
    return nullptr;

  if (lowerModuleCache && cachedModule == mo)
    return lowerModuleCache.get();

  cachedModule = mo;
  moduleRewriter = std::make_unique<mlir::PatternRewriter>(mo.getContext());
  lowerModuleCache = cir::createLowerModule(mo, *moduleRewriter);
  x86_64ABIInfoCache.reset();
  vaArgClassificationCache.clear();
  vaArgMemoryLayoutCache.clear();
  abiAlignCache.clear();
  typeStoreSizeCache.clear();
  typeAllocSizeCache.clear();
  hasCachedAVXABILevel = false;
  return lowerModuleCache.get();
}

const X86_64ABIInfo *LoweringPrepareX86CXXABI::getOrCreateX86_64ABIInfo(
    cir::LowerModule &lowerModule) {
  // Derive AVX ABI level from the target's ABI string.
  llvm::StringRef abi = lowerModule.getTarget().getABI();
  X86AVXABILevel avxLevel =
      (abi == "avx512"  ? X86AVXABILevel::AVX512
       : abi == "avx"   ? X86AVXABILevel::AVX
                        : X86AVXABILevel::None);

  if (x86_64ABIInfoCache && hasCachedAVXABILevel &&
      cachedAVXABILevel == avxLevel)
    return x86_64ABIInfoCache.get();

  x86_64ABIInfoCache =
      std::make_unique<X86_64ABIInfo>(lowerModule.getTypes(), avxLevel);
  vaArgClassificationCache.clear();
  vaArgMemoryLayoutCache.clear();
  abiAlignCache.clear();
  typeStoreSizeCache.clear();
  typeAllocSizeCache.clear();
  cachedAVXABILevel = avxLevel;
  hasCachedAVXABILevel = true;
  return x86_64ABIInfoCache.get();
}

const LoweringPrepareX86CXXABI::VAArgClassification &
LoweringPrepareX86CXXABI::getOrClassifyVAArgType(cir::LowerModule &lowerModule,
                                                 mlir::Type ty) {
  auto it = vaArgClassificationCache.find(ty);
  if (it != vaArgClassificationCache.end())
    return it->second;

  const X86_64ABIInfo *abiInfo = getOrCreateX86_64ABIInfo(lowerModule);
  auto [insertedIt, inserted] =
      vaArgClassificationCache.try_emplace(ty, VAArgClassification{});
  assert(inserted && "new va_arg type classification should be inserted");

  VAArgClassification &classification = insertedIt->second;
  classification.argInfo = abiInfo->classifyArgumentType(
      ty, /*isBitIntType=*/false, /*isMatrixType=*/false, 0,
      classification.neededInt,
      classification.neededSSE,
      /*isNamedArg=*/false, /*IsRegCall=*/false);
  return classification;
}

const LoweringPrepareX86CXXABI::VAArgMemoryLayout &
LoweringPrepareX86CXXABI::getOrComputeVAArgMemoryLayout(
    const cir::CIRDataLayout &datalayout, mlir::Type ty,
    uint64_t typeAlignHint) {
  auto it = vaArgMemoryLayoutCache.find(ty);
  if (it != vaArgMemoryLayoutCache.end())
    return it->second;

  auto [insertedIt, inserted] =
      vaArgMemoryLayoutCache.try_emplace(ty, VAArgMemoryLayout{});
  assert(inserted && "new va_arg memory layout should be inserted");

  VAArgMemoryLayout &layout = insertedIt->second;
  // The overflow-area round-up alignment must be the x86-64 psABI alignment,
  // which OGCG's X86_64ABIInfo::EmitVAArg takes from
  // getContext().getTypeAlignInChars(Ty). The CIR data layout can diverge from
  // it in BOTH directions: it under-reports a C-level aligned(N) (modelled as a
  // padded record kept at its members' natural align) and over-reports a
  // _BitInt(N>64) (modelled as an i128-shaped 16-byte type though its psABI
  // align is 8). CIRGen threads the authoritative psABI alignment through
  // typeAlignHint whenever it differs from the CIR data layout alignment, so
  // prefer it; fall back to the CIR data layout alignment only when no hint is
  // present (i.e. the two already agree). Do NOT max() the two: for _BitInt the
  // CIR data layout value is the phantom-high 16 and would over-round.
  layout.alignment =
      typeAlignHint ? typeAlignHint : getOrComputeABITypeAlign(datalayout, ty);
  layout.alignOverflowArgArea = layout.alignment > 8;
  const uint64_t sizeInBytes = getOrComputeTypeStoreSize(datalayout, ty);
  layout.stride = llvm::alignTo(sizeInBytes, 8);
  return layout;
}

uint64_t LoweringPrepareX86CXXABI::getOrComputeABITypeAlign(
    const cir::CIRDataLayout &datalayout, mlir::Type ty) {
  auto it = abiAlignCache.find(ty);
  if (it != abiAlignCache.end())
    return it->second;

  uint64_t align = datalayout.getABITypeAlign(ty).value();
  abiAlignCache.try_emplace(ty, align);
  return align;
}

uint64_t LoweringPrepareX86CXXABI::getOrComputeTypeStoreSize(
    const cir::CIRDataLayout &datalayout, mlir::Type ty) {
  auto it = typeStoreSizeCache.find(ty);
  if (it != typeStoreSizeCache.end())
    return it->second;

  uint64_t size = datalayout.getTypeStoreSize(ty).getFixedValue();
  typeStoreSizeCache.try_emplace(ty, size);
  return size;
}

uint64_t LoweringPrepareX86CXXABI::getOrComputeTypeAllocSize(
    const cir::CIRDataLayout &datalayout, mlir::Type ty) {
  auto it = typeAllocSizeCache.find(ty);
  if (it != typeAllocSizeCache.end())
    return it->second;

  uint64_t size = datalayout.getTypeAllocSize(ty).getFixedValue();
  typeAllocSizeCache.try_emplace(ty, size);
  return size;
}

/// Helper to create a GetMemberOp with automatic result type derivation.
mlir::Value createGetMemberOp(cir::CIRBaseBuilderTy &builder,
                              mlir::Location loc, mlir::Value recordPtr,
                              const char *fldName, unsigned idx) {
  auto recordBaseTy =
      mlir::cast<cir::PointerType>(recordPtr.getType()).getPointee();
  auto fldTy = mlir::cast<cir::RecordType>(recordBaseTy).getMembers()[idx];
  auto fldPtrTy = cir::PointerType::get(fldTy);
  return builder.createGetMember(loc, fldPtrTy, recordPtr, fldName, idx);
}

static cir::TBAAAttr getScalarVAArgTBAATag(cir::VAArgOp op) {
  auto tbaaStruct = op->getAttrOfType<mlir::ArrayAttr>("cir.tbaa_struct");
  if (!tbaaStruct || tbaaStruct.size() != 1)
    return {};

  auto triple = mlir::dyn_cast<mlir::ArrayAttr>(tbaaStruct[0]);
  if (!triple || triple.size() != 3)
    return {};

  auto offset = mlir::dyn_cast<mlir::IntegerAttr>(triple[0]);
  if (!offset || offset.getInt() != 0)
    return {};

  return mlir::dyn_cast<cir::TBAAAttr>(triple[2]);
}

static cir::LoadOp createVAArgResultLoad(cir::CIRBaseBuilderTy &builder,
                                         cir::VAArgOp op,
                                         mlir::Location loc, mlir::Value ptr,
                                         uint64_t alignment = 0) {
  cir::LoadOp load =
      builder.createLoad(loc, ptr, /*isVolatile=*/false, alignment);
  if (auto tbaa = getScalarVAArgTBAATag(op))
    load.setTbaaAttr(tbaa);
  return load;
}

static cir::IntType getVAArgIntStorageType(cir::CIRBaseBuilderTy &builder,
                                           cir::LowerModule &lowerModule,
                                           mlir::Type ty) {
  auto intTy = mlir::dyn_cast<cir::IntType>(ty);
  if (!intTy)
    return {};

  unsigned storageBits = lowerModule.getTarget().getBitIntWidth(intTy.getWidth());
  if (storageBits == intTy.getWidth())
    return {};

  return intTy.isSigned() ? builder.getSIntNTy(storageBits)
                          : builder.getUIntNTy(storageBits);
}

static mlir::Value createVAArgIntegerStorageLoad(
    cir::CIRBaseBuilderTy &builder, cir::VAArgOp op, mlir::Location loc,
    mlir::Value ptr, mlir::Type semanticTy, cir::IntType storageTy,
    uint64_t alignment = 0) {
  auto storagePtrTy = cir::PointerType::get(storageTy);
  auto storagePtr = builder.createBitcast(ptr, storagePtrTy);
  auto storageLoad =
      createVAArgResultLoad(builder, op, loc, storagePtr, alignment);
  return cir::CastOp::create(builder, loc, semanticTy, cir::CastKind::integral,
                             storageLoad);
}

/// Build va_arg lowering for arguments passed in memory on x86-64.
mlir::Value buildX86_64VAArgFromMemory(cir::CIRBaseBuilderTy &builder,
                                       uint64_t alignment, uint64_t stride,
                                       bool alignOverflowArgArea,
                                       mlir::Value valist, mlir::Type Ty,
                                       mlir::Location loc) {
  mlir::Value overflow_arg_area_p =
      createGetMemberOp(builder, loc, valist, "overflow_arg_area", 2);
  mlir::Value overflow_arg_area =
      builder.createLoad(loc, overflow_arg_area_p);

  // AMD64-ABI 3.5.7p5: Step 7. Align l->overflow_arg_area upwards to a 16
  // byte boundary if alignment needed by type exceeds 8 byte boundary.
  if (alignOverflowArgArea)
    overflow_arg_area =
        emitRoundPointerUpToAlignment(builder, overflow_arg_area, alignment);

  // AMD64-ABI 3.5.7p5: Step 8. Fetch type from l->overflow_arg_area.
  mlir::Value res = overflow_arg_area;

  // AMD64-ABI 3.5.7p5: Step 9. Set l->overflow_arg_area to:
  // l->overflow_arg_area + sizeof(type).
  // AMD64-ABI 3.5.7p5: Step 10. Align l->overflow_arg_area upwards to
  // an 8 byte boundary.
  mlir::Value strideVal = builder.getSignedInt(loc, stride, 32);
  mlir::Value castedPtr =
      builder.createPtrBitcast(overflow_arg_area, builder.getSIntNTy(8));
  overflow_arg_area = builder.createPtrStride(loc, castedPtr, strideVal);
  // Cast back to void* to match overflow_arg_area_p's pointee type.
  overflow_arg_area =
      builder.createBitcast(overflow_arg_area, builder.getVoidPtrTy());
  builder.createStore(loc, overflow_arg_area, overflow_arg_area_p);

  return res;
}

static bool tryLowerRecordVAArgMemcpyPattern(cir::CIRBaseBuilderTy &builder,
                                             cir::VAArgOp op,
                                             mlir::Value memAddr,
                                             uint64_t memcpyLen,
                                             uint64_t dstAlignment,
                                             uint64_t srcAlignment) {
  if (!op->hasOneUse())
    return false;

  auto store = mlir::dyn_cast<cir::StoreOp>(*op->user_begin());
  if (!store || store.getValue() != op.getResult())
    return false;

  mlir::Value tmpAddr = store.getAddr();
  cir::CopyOp copy;
  for (mlir::Operation *user : tmpAddr.getUsers()) {
    if (user == store.getOperation())
      continue;

    auto currentCopy = mlir::dyn_cast<cir::CopyOp>(user);
    if (!currentCopy || currentCopy.getSrc() != tmpAddr || copy)
      return false;
    copy = currentCopy;
  }

  if (!copy)
    return false;

  auto loc = store.getLoc();
  mlir::Value dstVoid = builder.createBitcast(copy.getDst(),
                                              builder.getVoidPtrTy());
  mlir::Value len = builder.getUnsignedInt(loc, memcpyLen, /*numBits=*/64);
  auto memcpy = cir::MemCpyOp::create(builder, loc, dstVoid, memAddr, len);

  uint64_t actualDstAlign = dstAlignment;
  if (auto attr = copy.getDstAlignmentAttr())
    actualDstAlign = attr.getInt();
  memcpy->setAttr("cir.dst_align", builder.getI64IntegerAttr(actualDstAlign));
  memcpy->setAttr("cir.src_align", builder.getI64IntegerAttr(srcAlignment));

  copy.erase();
  store.erase();
  if (auto alloca = tmpAddr.getDefiningOp<cir::AllocaOp>();
      alloca && tmpAddr.use_empty())
    alloca.erase();
  return true;
}

/// Lower MS ABI va_arg using the Win64 pointer-bump pattern.
/// MS x64 ABI: va_list is char*, each arg occupies an 8-byte slot.
/// Args > 64 bits or with non-power-of-2 size are passed by reference.
/// Matches OGCG's X86_64ABIInfo::EmitMSVAArg + emitVoidPtrVAArg.
static mlir::Value lowerMSVAArg(cir::CIRBaseBuilderTy &builder,
                                cir::VAArgOp op,
                                const cir::CIRDataLayout &datalayout) {
  mlir::Location loc = op.getLoc();
  mlir::Type ty = op.getType();
  mlir::Value valist = op.getOperand();

  // Compute type size to determine if the argument is passed indirectly.
  uint64_t tyBitWidth = datalayout.getTypeSizeInBits(ty);
  bool isIndirect = tyBitWidth > 64 || !llvm::isPowerOf2_64(tyBitWidth);

  // The slot size is always 8 bytes on Win64.
  constexpr uint64_t slotSize = 8;

  // Load the current va_list pointer value.
  mlir::Value argPtr = builder.createLoad(loc, valist);

  // Advance the va_list pointer by slot size and store it back.
  mlir::Value strideVal = builder.getSignedInt(loc, slotSize, 32);
  mlir::Value castedPtr =
      builder.createPtrBitcast(argPtr, builder.getSIntNTy(8));
  mlir::Value nextPtr = builder.createPtrStride(loc, castedPtr, strideVal);
  nextPtr = builder.createBitcast(nextPtr, builder.getVoidPtrTy());
  builder.createStore(loc, nextPtr, valist);

  // If indirect, the slot contains a pointer to the actual value.
  if (isIndirect) {
    mlir::Value indirectPtr = builder.createLoad(
        loc, builder.createPtrBitcast(argPtr, builder.getVoidPtrTy()));

    if (mlir::isa<cir::RecordType>(ty)) {
      uint64_t tyAlign = datalayout.getABITypeAlign(ty).value();
      uint64_t tyAllocSize = datalayout.getTypeAllocSize(ty).getFixedValue();
      if (tryLowerRecordVAArgMemcpyPattern(builder, op, indirectPtr,
                                           tyAllocSize, tyAlign, tyAlign)) {
        auto undef = cir::ConstantOp::create(builder, loc,
                                             cir::UndefAttr::get(ty));
        return undef.getRes();
      }
    }

    return createVAArgResultLoad(builder, op, loc,
                                 builder.createPtrBitcast(indirectPtr, ty));
  }

  return createVAArgResultLoad(builder, op, loc,
                               builder.createPtrBitcast(argPtr, ty), slotSize);
}

mlir::Value LoweringPrepareX86CXXABI::lowerVAArgX86_64(
    cir::CIRBaseBuilderTy &builder, cir::VAArgOp op,
    const cir::CIRDataLayout &datalayout) {
  // Handle MS ABI va_arg (Win64 pointer-bump pattern).
  if (op->hasAttr("cir.ms_abi"))
    return lowerMSVAArg(builder, op, datalayout);

  // Some CIR types still fall back to LLVM's raw va_arg handling here; keep
  // this guard for those until the shared x86_64 classifier covers everything.
  if (!mlir::isa<VoidType, IntType, SingleType, DoubleType, BoolType,
                 cir::PointerType, LongDoubleType, cir::FP128Type,
                 cir::VectorType, cir::RecordType, cir::ComplexType>(
          op.getType()))
    return nullptr;

  // Assume that va_list type is correct; should be pointer to LLVM type:
  // struct {
  //   i32 gp_offset;
  //   i32 fp_offset;
  //   i8* overflow_arg_area;
  //   i8* reg_save_area;
  // };
  cir::LowerModule *lowerModule = getOrCreateLowerModule(op);
  if (!lowerModule)
    return nullptr;
  mlir::Type ty = op.getType();
  const VAArgClassification &classification =
      getOrClassifyVAArgType(*lowerModule, ty);
  const ABIArgInfo &ai = classification.argInfo;
  unsigned neededInt = classification.neededInt;
  unsigned neededSSE = classification.neededSSE;

  // Empty records are ignored for parameter passing purposes.
  // Return an undef value without touching the va_list, matching OGCG.
  if (ai.isIgnore()) {
    auto undef = cir::ConstantOp::create(builder, op.getLoc(),
                                         cir::UndefAttr::get(ty));
    return undef.getRes();
  }

  mlir::Location loc = op.getLoc();
  mlir::Value valist = op.getOperand();
  // CIRGen records the authoritative psABI alignment (getTypeAlignInChars) here
  // whenever it differs from the CIR data layout alignment — in either
  // direction (aligned(N) raises it, _BitInt(N>64) lowers it). Use it so a
  // memory-class aggregate rounds the overflow area up exactly as classic clang
  // does. (The op's plain `alignment` attr is the *unadjusted* alignment, which
  // the AArch64 AAPCS path deliberately uses; do not use it here.)
  uint64_t typeAlignHint = 0;
  if (auto alignAttr =
          op->getAttrOfType<mlir::IntegerAttr>("cir.x86_overflow_align"))
    typeAlignHint = alignAttr.getInt();
  const VAArgMemoryLayout &memoryLayout =
      getOrComputeVAArgMemoryLayout(datalayout, ty, typeAlignHint);

  // AMD64-ABI 3.5.7p5: Step 1. Determine whether type may be passed
  // in the registers. If not go to step 7.
  if (!neededInt && !neededSSE) {
    mlir::Value memAddr = buildX86_64VAArgFromMemory(
        builder, memoryLayout.alignment, memoryLayout.stride,
        memoryLayout.alignOverflowArgArea, valist, ty, loc);

    // Match OGCG's EmitLoadOfAnyValue handling for large _BitInt memory
    // objects: load the widened storage integer from the ABI slot and then
    // cast back to the semantic result type.
    if (!mlir::isa<cir::RecordType>(ty)) {
      if (auto storageTy = getVAArgIntStorageType(builder, *lowerModule, ty))
        return createVAArgIntegerStorageLoad(builder, op, loc, memAddr, ty,
                                             storageTy,
                                             /*alignment=*/memoryLayout.alignment);
      return createVAArgResultLoad(builder, op, loc,
                                   builder.createPtrBitcast(memAddr, ty));
    }

    // Use an explicit byte copy in the memory-only path, matching OGCG's
    // memcpy-based strategy. Copy only the type's alloc size (not the ABI
    // slot stride), matching OGCG's EmitAggFinalDestCopy which copies
    // getTypeInfoInChars(Ty).Width bytes. Using the stride would overflow
    // the destination for types whose size is not a multiple of 8.
    uint64_t tyAlign = getOrComputeABITypeAlign(datalayout, ty);
    uint64_t tyAllocSize = getOrComputeTypeAllocSize(datalayout, ty);
    if (tryLowerRecordVAArgMemcpyPattern(builder, op, memAddr, tyAllocSize,
                                         tyAlign,
                                         memoryLayout.alignment)) {
      auto undef = cir::ConstantOp::create(builder, loc,
                                           cir::UndefAttr::get(ty));
      return undef.getRes();
    }

    auto addrTy = builder.getPointerTo(ty);
    mlir::Value tmp = builder.createAlloca(
        loc, addrTy, ty, "vaarg.mem.tmp",
        builder.getAlignmentAttr(clang::CharUnits::fromQuantity(tyAlign)));

    mlir::Value tmpVoid = builder.createBitcast(tmp, builder.getVoidPtrTy());
    auto memcpyLen =
        builder.getUnsignedInt(loc, tyAllocSize, /*numBits=*/64);
    auto memcpy = cir::MemCpyOp::create(builder, loc, tmpVoid, memAddr,
                                        memcpyLen);
    memcpy->setAttr("cir.dst_align", builder.getI64IntegerAttr(tyAlign));
    memcpy->setAttr("cir.src_align",
                    builder.getI64IntegerAttr(memoryLayout.alignment));
    return builder.createAlignedLoad(loc, tmp, tyAlign);
  }

  mlir::OpBuilder::InsertPoint scopeIP;
  auto scopeOp = cir::ScopeOp::create(
      builder, loc,
      [&](mlir::OpBuilder &opBuilder, mlir::Type &yieldTy,
          mlir::Location loc) {
        scopeIP = opBuilder.saveInsertionPoint();
        yieldTy = op.getType();
      });

  mlir::Block *contBlock = scopeIP.getBlock();

  mlir::Block *currentBlock = builder.createBlock(contBlock);
  builder.setInsertionPointToEnd(currentBlock);

  // AMD64-ABI 3.5.7p5: Step 2. Compute num_gp to hold the number of
  // general purpose registers needed to pass type and num_fp to hold
  // the number of floating point registers needed.

  // AMD64-ABI 3.5.7p5: Step 3. Verify whether arguments fit into
  // registers. In the case: l->gp_offset > 48 - num_gp * 8 or
  // l->fp_offset > 304 - num_fp * 16 go to step 7.
  //
  // NOTE: 304 is a typo, there are (6 * 8 + 8 * 16) = 176 bytes of
  // register save space).

  mlir::Value inRegs;
  mlir::Value gp_offset_p, fp_offset_p;
  mlir::Value gp_offset, fp_offset;

  // Compute the actual alignment of the va_list pointer.  On x86_64,
  // __va_list_tag is { i32, i32, ptr, ptr } with ABI alignment 8, but
  // va_list allocas are typically 16-byte aligned (Clang decorates
  // __va_list_tag with __attribute__((aligned(16)))).  We trace the
  // SSA def chain (through casts) to find the alloca and use its alignment,
  // falling back to the ABI type alignment if the alloca is not reachable.
  // Per-field alignment is then computed as gcd(base, fieldOffset),
  // matching OGCG's CreateStructGEP / alignmentAtOffset behaviour.
  auto valistPointeeTy =
      mlir::cast<cir::PointerType>(valist.getType()).getPointee();
  uint64_t valistAlign = getOrComputeABITypeAlign(datalayout, valistPointeeTy);
  // Try to recover the actual alloca alignment from the SSA def chain.
  {
    mlir::Value v = valist;
    while (auto castOp = v.getDefiningOp<cir::CastOp>())
      v = castOp.getSrc();
    if (auto allocaOp = v.getDefiningOp<cir::AllocaOp>()) {
      uint64_t allocaAlign = allocaOp.getAlignment().value_or(0);
      if (allocaAlign > valistAlign)
        valistAlign = allocaAlign;
    }
  }

  if (neededInt) {
    gp_offset_p =
        createGetMemberOp(builder, loc, valist, "gp_offset", 0);
    // gp_offset is at offset 0 — alignment equals the struct's alignment.
    gp_offset = builder.createLoad(loc, gp_offset_p, /*isVolatile=*/false,
                                   valistAlign);
    inRegs = builder.getUnsignedInt(loc, 48 - neededInt * 8, 32);
    inRegs =
        builder.createCompare(loc, cir::CmpOpKind::le, gp_offset, inRegs);
  }

  // fp_offset is at offset 4: alignment = gcd(valistAlign, 4).
  uint64_t fpOffsetAlign =
      valistAlign > 0 ? std::gcd(valistAlign, (uint64_t)4) : 4;
  if (neededSSE) {
    fp_offset_p =
        createGetMemberOp(builder, loc, valist, "fp_offset", 1);
    fp_offset = builder.createLoad(loc, fp_offset_p, /*isVolatile=*/false,
                                   fpOffsetAlign);
    mlir::Value fitsInFP =
        builder.getUnsignedInt(loc, 176 - neededSSE * 16, 32);
    fitsInFP =
        builder.createCompare(loc, cir::CmpOpKind::le, fp_offset, fitsInFP);
    inRegs = inRegs ? builder.createAnd(inRegs.getLoc(), inRegs, fitsInFP)
                    : fitsInFP;
  }

  mlir::Block *inRegBlock = builder.createBlock(contBlock);
  mlir::Block *inMemBlock = builder.createBlock(contBlock);
  builder.setInsertionPointToEnd(currentBlock);
  cir::BrCondOp::create(builder, loc, inRegs, inRegBlock, inMemBlock);

  // Emit code to load the value if it was passed in registers.
  builder.setInsertionPointToStart(inRegBlock);

  // AMD64-ABI 3.5.7p5: Step 4. Fetch type from l->reg_save_area with
  // an offset of l->gp_offset and/or l->fp_offset. This may require
  // copying to a temporary location in case the parameter is passed
  // in different register classes or requires an alignment greater
  // than 8 for general purpose registers and 16 for XMM registers.
  // reg_save_area is at offset 16: alignment = gcd(valistAlign, 16).
  uint64_t regSaveAreaAlign =
      valistAlign > 0 ? std::gcd(valistAlign, (uint64_t)16) : 8;
  mlir::Value regSaveArea = builder.createLoad(
      loc, createGetMemberOp(builder, loc, valist, "reg_save_area", 3),
      /*isVolatile=*/false, regSaveAreaAlign);
  mlir::Value regAddr;

  uint64_t tyAlign = getOrComputeABITypeAlign(datalayout, ty);
  // The alignment of result address.
  uint64_t alignment = 0;
  if (neededInt && neededSSE) {
    // FIXME: Cleanup.
    assert(ai.isDirect() && "Unexpected ABI info for mixed regs");
    auto recordTy = mlir::cast<cir::RecordType>(ai.getCoerceToType());
    cir::PointerType addrTy = builder.getPointerTo(ty);

    mlir::Value tmp = builder.createAlloca(
        loc, addrTy, ty, "tmp",
        builder.getAlignmentAttr(clang::CharUnits::fromQuantity(tyAlign)));
    tmp = builder.createPtrBitcast(tmp, recordTy);
    assert(recordTy.getNumElements() == 2 &&
           "Unexpected ABI info for mixed regs");
    mlir::Type tyLo = recordTy.getMembers()[0];
    mlir::Type tyHi = recordTy.getMembers()[1];
    assert((isFPOrVectorOfFPType(tyLo) ^ isFPOrVectorOfFPType(tyHi)) &&
           "Unexpected ABI info for mixed regs");
    mlir::Value gpAddr =
        builder.createPtrStride(loc, regSaveArea, gp_offset);
    mlir::Value fpAddr =
        builder.createPtrStride(loc, regSaveArea, fp_offset);
    mlir::Value regLoAddr = isFPOrVectorOfFPType(tyLo) ? fpAddr : gpAddr;
    mlir::Value regHiAddr = isFPOrVectorOfFPType(tyHi) ? fpAddr : gpAddr;

    // Copy the first element.
    // Store alignment is derived from the tmp alloca's alignment (tyAlign)
    // at field offset 0, matching OGCG's CreateStructGEP alignment logic.
    mlir::Value v = builder.createAlignedLoad(
        loc, builder.createPtrBitcast(regLoAddr, tyLo),
        getOrComputeABITypeAlign(datalayout, tyLo));
    builder.createStore(
        loc, v,
        builder.createGetMember(
            loc, builder.getPointerTo(tyLo), tmp, "gp_offset", 0),
        /*isVolatile=*/false, builder.getAlignmentAttr(tyAlign));

    // Copy the second element.
    v = builder.createAlignedLoad(
        loc, builder.createPtrBitcast(regHiAddr, tyHi),
        getOrComputeABITypeAlign(datalayout, tyHi));
    // Use the actual coerced-record field offset. getTypeAllocSize(tyLo) can
    // undercount when the low field carries trailing padding, as in { i32,
    // double } where field 1 starts at offset 8 rather than 4.
    uint64_t field1Offset = recordTy.getElementOffset(datalayout.layout, 1);
    uint64_t field1Align =
        field1Offset == 0 ? tyAlign : std::gcd(tyAlign, field1Offset);
    builder.createStore(
        loc, v,
        builder.createGetMember(
            loc, builder.getPointerTo(tyHi), tmp, "fp_offset", 1),
        /*isVolatile=*/false, builder.getAlignmentAttr(field1Align));

    tmp = builder.createPtrBitcast(tmp, ty);
    regAddr = tmp;
  } else if (neededInt || neededSSE == 1) {
    uint64_t tySize = getOrComputeTypeStoreSize(datalayout, ty);

    mlir::Type coTy;
    if (ai.isDirect())
      coTy = ai.getCoerceToType();

    mlir::Value gpOrFpOffset = neededInt ? gp_offset : fp_offset;
    // Use the struct's natural alignment for the merged address, matching
    // OGCG which conservatively uses tyAlign for the overflow area address.
    alignment = tyAlign;
    uint64_t regSize = neededInt ? neededInt * 8 : 16;
    if (coTy && (ai.getDirectOffset() == 8 || regSize < tySize)) {
      cir::PointerType addrTy = builder.getPointerTo(ty);
      mlir::Value tmp = builder.createAlloca(
          loc, addrTy, ty, "tmp",
          builder.getAlignmentAttr(clang::CharUnits::fromQuantity(tyAlign)));
      mlir::Value addr =
          builder.createPtrStride(loc, regSaveArea, gpOrFpOffset);
      mlir::Value src = builder.createAlignedLoad(
          loc, builder.createPtrBitcast(addr, coTy), tyAlign);
      // Use a byte-level GEP to reach the correct offset within the
      // temporary, matching OGCG's CreateGEP(Int8Ty, Tmp, PtrOffset).
      mlir::Value tmpBytes =
          builder.createPtrBitcast(tmp, builder.getUIntNTy(8));
      mlir::Value ptrOffset =
          builder.getUnsignedInt(loc, ai.getDirectOffset(), 32);
      mlir::Value dst = builder.createPtrStride(loc, tmpBytes, ptrOffset);
      dst = builder.createPtrBitcast(dst, coTy);
      builder.createStore(loc, src, dst, /*isVolatile=*/false,
                          builder.getAlignmentAttr(
                              clang::CharUnits::fromQuantity(tyAlign)));
      regAddr = tmp;
    } else {
      regAddr = builder.createPtrStride(loc, regSaveArea, gpOrFpOffset);

      // Copy into a temporary if the type is more aligned than the
      // register save area.
      if (neededInt && tyAlign > 8) {
        cir::PointerType addrTy = builder.getPointerTo(ty);
        mlir::Value tmp = builder.createAlloca(
            loc, addrTy, ty, "tmp",
            builder.getAlignmentAttr(clang::CharUnits::fromQuantity(tyAlign)));
        mlir::Value srcAddr = builder.createPtrBitcast(regAddr, ty);
        builder.createCopy(
            tmp, srcAddr, /*isVolatile=*/false,
            builder.getAlignmentAttr(clang::CharUnits::fromQuantity(tyAlign)),
            builder.getAlignmentAttr(clang::CharUnits::fromQuantity(8)));
        regAddr = tmp;
      }
    }

  } else {
    assert(neededSSE == 2 && "Invalid number of needed registers!");
    // SSE registers are spaced 16 bytes apart in the register save
    // area, we need to collect the two eightbytes together.
    constexpr uint64_t xmmSlotAlign = 16;
    mlir::Value regAddrLo =
        builder.createPtrStride(loc, regSaveArea, fp_offset);
    mlir::Value regAddrHi = builder.createPtrStride(
        loc, regAddrLo, builder.getUnsignedInt(loc, 16, /*numBits=*/32));

    mlir::MLIRContext *context = builder.getContext();
    cir::RecordType recordTy =
        ai.canHaveCoerceToType()
            ? cast<cir::RecordType>(ai.getCoerceToType())
            : cir::RecordType::get(
                  context,
                  {DoubleType::get(context), DoubleType::get(context)},
                  /*packed=*/false, /*padded=*/false,
                  cir::RecordType::Struct);
    cir::PointerType addrTy = builder.getPointerTo(ty);
    mlir::Value tmp = builder.createAlloca(
        loc, addrTy, ty, "tmp",
        builder.getAlignmentAttr(clang::CharUnits::fromQuantity(tyAlign)));
    tmp = builder.createPtrBitcast(tmp, recordTy);
    const CIRRecordLayout &layout =
        lowerModule->getTypes().getContext().getCIRRecordLayout(recordTy);
    uint64_t field0Align = tyAlign;
    uint64_t field1Offset = layout.getFieldOffset(1) / 8;
    uint64_t field1Align =
        field1Offset == 0 ? tyAlign : std::gcd(tyAlign, field1Offset);
    mlir::Value v = builder.createAlignedLoad(
        loc, builder.createPtrBitcast(regAddrLo, recordTy.getMembers()[0]),
        xmmSlotAlign);
    builder.createStore(
        loc, v,
        builder.createGetMember(
            loc, builder.getPointerTo(recordTy.getMembers()[0]),
            tmp, "", 0),
        /*isVolatile=*/false, builder.getAlignmentAttr(field0Align));
    v = builder.createAlignedLoad(
        loc, builder.createPtrBitcast(regAddrHi, recordTy.getMembers()[1]),
        xmmSlotAlign);
    builder.createStore(
        loc, v,
        builder.createGetMember(
            loc, builder.getPointerTo(recordTy.getMembers()[1]),
            tmp, "", 1),
        /*isVolatile=*/false, builder.getAlignmentAttr(field1Align));

    tmp = builder.createPtrBitcast(tmp, ty);
    regAddr = tmp;
  }

  // AMD64-ABI 3.5.7p5: Step 5. Set:
  // l->gp_offset = l->gp_offset + num_gp * 8
  // l->fp_offset = l->fp_offset + num_fp * 16.
  if (neededInt) {
    mlir::Value offset = builder.getUnsignedInt(loc, neededInt * 8, 32);
    builder.createStore(loc, builder.createAdd(loc, gp_offset, offset),
                        gp_offset_p, /*isVolatile=*/false,
                        builder.getAlignmentAttr(valistAlign));
  }

  if (neededSSE) {
    mlir::Value offset = builder.getUnsignedInt(loc, neededSSE * 16, 32);
    builder.createStore(loc, builder.createAdd(loc, fp_offset, offset),
                        fp_offset_p, /*isVolatile=*/false,
                        builder.getAlignmentAttr(fpOffsetAlign));
  }

  cir::BrOp::create(builder, loc, mlir::ValueRange{regAddr}, contBlock);

  // Emit code to load the value if it was passed in memory.
  builder.setInsertionPointToStart(inMemBlock);
  mlir::Value memAddr = buildX86_64VAArgFromMemory(
      builder, memoryLayout.alignment, memoryLayout.stride,
      memoryLayout.alignOverflowArgArea, valist, ty, loc);
  // Ensure memAddr type matches regAddr type for the block argument.
  if (memAddr.getType() != regAddr.getType())
    memAddr = builder.createPtrBitcast(memAddr, ty);
  cir::BrOp::create(builder, loc, mlir::ValueRange{memAddr}, contBlock);

  // Yield the appropriate result.
  builder.setInsertionPointToStart(contBlock);
  mlir::Value res_addr = contBlock->addArgument(regAddr.getType(), loc);

  if (mlir::isa<cir::RecordType>(ty)) {
    uint64_t memcpyLen = getOrComputeTypeAllocSize(datalayout, ty);
    if (tryLowerRecordVAArgMemcpyPattern(builder, op, res_addr, memcpyLen,
                                         tyAlign, tyAlign)) {
      auto undef = cir::ConstantOp::create(builder, loc,
                                           cir::UndefAttr::get(ty));
      cir::YieldOp::create(builder, loc, undef.getRes());
      return scopeOp.getResult(0);
    }
  }

  mlir::Value result = createVAArgResultLoad(
      builder, op, loc, builder.createPtrBitcast(res_addr, ty), alignment);

  cir::YieldOp::create(builder, loc, result);

  return scopeOp.getResult(0);
}

mlir::Value LoweringPrepareX86CXXABI::lowerVAArgX86_32(
    cir::CIRBaseBuilderTy &builder, cir::VAArgOp op,
    const cir::CIRDataLayout &datalayout) {
  mlir::Type ty = op.getType();

  // Only aggregate va_arg needs CIR-level lowering here. Scalars fall back to
  // the generic LLVM va_arg lowering, matching pre-existing behavior.
  if (!mlir::isa<cir::RecordType>(ty))
    return {};

  builder.setInsertionPoint(op);

  uint64_t tyStoreSize = getOrComputeTypeStoreSize(datalayout, ty);

  // X86_32 (SysV, non-Darwin): aggregates are passed directly on the stack at
  // the natural 4-byte slot alignment. classic X86_32ABIInfo::getTypeStackAlign
  // InBytes yields the default (no over-alignment) for all non-SIMD aggregates,
  // so the slot pointer is never rounded up. Match that by using the 4-byte
  // slot alignment as the direct alignment (no align-up path taken).
  constexpr uint64_t slotSize = 4;
  uint64_t directAlign = slotSize;

  return vaarg_stack::lowerAggregateStackVAArg(builder, op, tyStoreSize,
                                               directAlign, slotSize,
                                               /*ptrIntBits=*/32);
}

// Delegate dynamic cast to the Itanium ABI implementation (which is
// shared across Itanium-derived ABIs including x86).
mlir::Value LoweringPrepareX86CXXABI::lowerDynamicCast(
    cir::CIRBaseBuilderTy &builder, clang::ASTContext &astCtx,
    cir::DynamicCastOp op) {
  // Create a temporary Itanium ABI instance and delegate.
  std::unique_ptr<cir::LoweringPrepareCXXABI> itaniumABI(
      cir::LoweringPrepareCXXABI::createItaniumABI());
  return itaniumABI->lowerDynamicCast(builder, astCtx, op);
}

} // namespace

cir::LoweringPrepareCXXABI *cir::LoweringPrepareCXXABI::createX86ABI(
    bool is64Bit) {
  return new LoweringPrepareX86CXXABI(is64Bit);
}
