//===- CIRDialect.cpp - MLIR CIR ops implementation -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the CIR dialect and its operations.
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/Dialect/IR/CIRDialect.h"

#include "CIRDialectBytecode.h"

#include "clang/CIR/Dialect/IR/CIRAttrs.h"
#include "clang/CIR/Dialect/IR/CIROpsEnums.h"
#include "clang/CIR/Dialect/IR/CIRStage.h"
#include "clang/CIR/Dialect/IR/CIRTypes.h"

#include "mlir/IR/Attributes.h"
#include "mlir/IR/DialectImplementation.h"
#include "mlir/Interfaces/CallInterfaces.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Interfaces/FunctionImplementation.h"
#include "mlir/Support/LLVM.h"

#include "clang/CIR/Dialect/IR/CIROpsDialect.cpp.inc"
#include "clang/CIR/Dialect/IR/CIROpsEnums.cpp.inc"
#include "clang/CIR/MissingFeatures.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/Support/LogicalResult.h"

using namespace mlir;
using namespace cir;

//===----------------------------------------------------------------------===//
// CIR Dialect
//===----------------------------------------------------------------------===//
namespace {
struct CIROpAsmDialectInterface : public OpAsmDialectInterface {
  using OpAsmDialectInterface::OpAsmDialectInterface;

  AliasResult getAlias(Type type, raw_ostream &os) const final {
    if (auto recordType = dyn_cast<cir::RecordType>(type)) {
      StringAttr nameAttr = recordType.getName();
      if (!nameAttr)
        os << "rec_anon_" << recordType.getKindAsStr();
      else
        os << "rec_" << nameAttr.getValue();
      return AliasResult::OverridableAlias;
    }
    if (auto intType = dyn_cast<cir::IntType>(type)) {
      // We only provide alias for standard integer types (i.e. integer types
      // whose width is a power of 2 and at least 8).
      unsigned width = intType.getWidth();
      if (width < 8 || !llvm::isPowerOf2_32(width))
        return AliasResult::NoAlias;
      os << intType.getAlias();
      return AliasResult::OverridableAlias;
    }
    if (auto voidType = dyn_cast<cir::VoidType>(type)) {
      os << voidType.getAlias();
      return AliasResult::OverridableAlias;
    }

    return AliasResult::NoAlias;
  }

  AliasResult getAlias(Attribute attr, raw_ostream &os) const final {
    if (auto boolAttr = mlir::dyn_cast<cir::BoolAttr>(attr)) {
      os << (boolAttr.getValue() ? "true" : "false");
      return AliasResult::FinalAlias;
    }
    if (auto bitfield = mlir::dyn_cast<cir::BitfieldInfoAttr>(attr)) {
      os << "bfi_" << bitfield.getName().str();
      return AliasResult::FinalAlias;
    }
    if (auto dynCastInfoAttr = mlir::dyn_cast<cir::DynamicCastInfoAttr>(attr)) {
      os << dynCastInfoAttr.getAlias();
      return AliasResult::FinalAlias;
    }
    if (auto cmp3wayInfoAttr = mlir::dyn_cast<cir::CmpThreeWayInfoAttr>(attr)) {
      os << cmp3wayInfoAttr.getAlias();
      return AliasResult::FinalAlias;
    }
    if (auto extraFuncAttr =
            mlir::dyn_cast<cir::ExtraFuncAttributesAttr>(attr)) {
      os << "fn_attr";
      return AliasResult::FinalAlias;
    }
    return TypeSwitch<Attribute, AliasResult>(attr)
        .Case<cir::TBAAOmnipotentCharAttr, cir::TBAAVTablePointerAttr,
              cir::TBAAScalarAttr, cir::TBAAStructAttr, cir::TBAATagAttr>(
            [&](auto attr) {
              os << decltype(attr)::getMnemonic();
              return AliasResult::OverridableAlias;
            })
        .Default([](Attribute) { return AliasResult::NoAlias; });
  }
};
} // namespace

void cir::CIRDialect::initialize() {
  registerTypes();
  registerAttributes();
  addOperations<
#define GET_OP_LIST
#include "clang/CIR/Dialect/IR/CIROps.cpp.inc"
      >();
  addInterfaces<CIROpAsmDialectInterface>();
  detail::addBytecodeInterface(this);
}

Operation *cir::CIRDialect::materializeConstant(mlir::OpBuilder &builder,
                                                mlir::Attribute value,
                                                mlir::Type type,
                                                mlir::Location loc) {
  return cir::ConstantOp::create(builder, loc, type,
                                 mlir::cast<mlir::TypedAttr>(value));
}

//===----------------------------------------------------------------------===//
// Helpers
//===----------------------------------------------------------------------===//

// Parses one of the keywords provided in the list `keywords` and returns the
// position of the parsed keyword in the list. If none of the keywords from the
// list is parsed, returns -1.
static int parseOptionalKeywordAlternative(AsmParser &parser,
                                           ArrayRef<llvm::StringRef> keywords) {
  for (auto en : llvm::enumerate(keywords)) {
    if (succeeded(parser.parseOptionalKeyword(en.value())))
      return en.index();
  }
  return -1;
}

namespace {
template <typename Ty> struct EnumTraits {};

#define REGISTER_ENUM_TYPE(Ty)                                                 \
  template <> struct EnumTraits<cir::Ty> {                                     \
    static llvm::StringRef stringify(cir::Ty value) {                          \
      return stringify##Ty(value);                                             \
    }                                                                          \
    static unsigned getMaxEnumVal() { return cir::getMaxEnumValFor##Ty(); }    \
  }

REGISTER_ENUM_TYPE(GlobalLinkageKind);
REGISTER_ENUM_TYPE(VisibilityKind);
REGISTER_ENUM_TYPE(SideEffect);
REGISTER_ENUM_TYPE(CallingConv);
} // namespace

/// Parse an enum from the keyword, or default to the provided default value.
/// The return type is the enum type by default, unless overriden with the
/// second template argument.
template <typename EnumTy, typename RetTy = EnumTy>
static RetTy parseOptionalCIRKeyword(AsmParser &parser, EnumTy defaultValue) {
  llvm::SmallVector<llvm::StringRef, 10> names;
  for (unsigned i = 0, e = EnumTraits<EnumTy>::getMaxEnumVal(); i <= e; ++i)
    names.push_back(EnumTraits<EnumTy>::stringify(static_cast<EnumTy>(i)));

  int index = parseOptionalKeywordAlternative(parser, names);
  if (index == -1)
    return static_cast<RetTy>(defaultValue);
  return static_cast<RetTy>(index);
}

/// Parse an enum from the keyword, return failure if the keyword is not found.
template <typename EnumTy, typename RetTy = EnumTy>
static ParseResult parseCIRKeyword(AsmParser &parser, RetTy &result) {
  llvm::SmallVector<llvm::StringRef, 10> names;
  for (unsigned i = 0, e = EnumTraits<EnumTy>::getMaxEnumVal(); i <= e; ++i)
    names.push_back(EnumTraits<EnumTy>::stringify(static_cast<EnumTy>(i)));

  int index = parseOptionalKeywordAlternative(parser, names);
  if (index == -1)
    return failure();
  result = static_cast<RetTy>(index);
  return success();
}

// Check if a region's termination omission is valid and, if so, creates and
// inserts the omitted terminator into the region.
static LogicalResult ensureRegionTerm(OpAsmParser &parser, Region &region,
                                      SMLoc errLoc) {
  Location eLoc = parser.getEncodedSourceLoc(parser.getCurrentLocation());
  OpBuilder builder(parser.getBuilder().getContext());

  // Insert empty block in case the region is empty to ensure the terminator
  // will be inserted
  if (region.empty())
    builder.createBlock(&region);

  Block &block = region.back();
  // Region is properly terminated: nothing to do.
  if (!block.empty() && block.back().hasTrait<OpTrait::IsTerminator>())
    return success();

  // Check for invalid terminator omissions.
  if (!region.hasOneBlock())
    return parser.emitError(errLoc,
                            "multi-block region must not omit terminator");

  // Terminator was omitted correctly: recreate it.
  builder.setInsertionPointToEnd(&block);
  cir::YieldOp::create(builder, eLoc);
  return success();
}

// True if the region's terminator should be omitted.
static bool omitRegionTerm(mlir::Region &r) {
  const auto singleNonEmptyBlock = r.hasOneBlock() && !r.back().empty();
  const auto yieldsNothing = [&r]() {
    auto y = dyn_cast<cir::YieldOp>(r.back().getTerminator());
    return y && y.getArgs().empty();
  };
  return singleNonEmptyBlock && yieldsNothing();
}

void printVisibilityAttr(OpAsmPrinter &printer,
                         cir::VisibilityAttr &visibility) {
  switch (visibility.getValue()) {
  case cir::VisibilityKind::Hidden:
    printer << "hidden";
    break;
  case cir::VisibilityKind::Protected:
    printer << "protected";
    break;
  case cir::VisibilityKind::Default:
    break;
  }
}

void parseVisibilityAttr(OpAsmParser &parser, cir::VisibilityAttr &visibility) {
  cir::VisibilityKind visibilityKind =
      parseOptionalCIRKeyword(parser, cir::VisibilityKind::Default);
  visibility = cir::VisibilityAttr::get(parser.getContext(), visibilityKind);
}

// Custom directive for GlobalOp's visibility attribute
static void printCIRVisibilityAttr(OpAsmPrinter &printer, mlir::Operation *,
                                   cir::VisibilityAttr visibility) {
  if (visibility && !visibility.isDefault()) {
    printer << stringifyVisibilityKind(visibility.getValue()) << ' ';
  }
}

static ParseResult parseCIRVisibilityAttr(OpAsmParser &parser,
                                          cir::VisibilityAttr &visibility) {
  cir::VisibilityKind visibilityKind =
      parseOptionalCIRKeyword(parser, cir::VisibilityKind::Default);
  visibility = cir::VisibilityAttr::get(parser.getContext(), visibilityKind);
  return success();
}

//===----------------------------------------------------------------------===//
// InlineKindAttr (FIXME: remove once FuncOp uses assembly format)
//===----------------------------------------------------------------------===//

ParseResult parseInlineKindAttr(OpAsmParser &parser,
                                cir::InlineKindAttr &inlineKindAttr) {
  // Static list of possible inline kind keywords
  static constexpr llvm::StringRef keywords[] = {"no_inline", "always_inline",
                                                 "inline_hint"};

  // Parse the inline kind keyword (optional)
  llvm::StringRef keyword;
  if (parser.parseOptionalKeyword(&keyword, keywords).failed()) {
    // Not an inline kind keyword, leave inlineKindAttr empty
    return success();
  }

  // Parse the enum value from the keyword
  auto inlineKindResult = ::cir::symbolizeEnum<::cir::InlineKind>(keyword);
  if (!inlineKindResult) {
    return parser.emitError(parser.getCurrentLocation(), "expected one of [")
           << llvm::join(llvm::ArrayRef(keywords), ", ")
           << "] for inlineKind, got: " << keyword;
  }

  inlineKindAttr =
      ::cir::InlineKindAttr::get(parser.getContext(), *inlineKindResult);
  return success();
}

void printInlineKindAttr(OpAsmPrinter &p, cir::InlineKindAttr inlineKindAttr) {
  if (inlineKindAttr) {
    p << " " << stringifyInlineKind(inlineKindAttr.getValue());
  }
}
//===----------------------------------------------------------------------===//
// CIR Custom Parsers/Printers
//===----------------------------------------------------------------------===//

static mlir::ParseResult parseOmittedTerminatorRegion(mlir::OpAsmParser &parser,
                                                      mlir::Region &scopeRegion,
                                                      mlir::Region &cleanupRegion) {
  auto regionLoc = parser.getCurrentLocation();
  if (parser.parseRegion(scopeRegion))
    return failure();
  if (ensureRegionTerm(parser, scopeRegion, regionLoc).failed())
    return failure();

  // Parse optional cleanup region.
  if (parser.parseOptionalKeyword("cleanup").succeeded()) {
    if (parser.parseRegion(cleanupRegion, /*arguments=*/{}, /*argTypes=*/{}))
      return failure();
    if (ensureRegionTerm(parser, cleanupRegion, regionLoc).failed())
      return failure();
  }

  return success();
}

static void printOmittedTerminatorRegion(mlir::OpAsmPrinter &printer,
                                         cir::ScopeOp &op,
                                         mlir::Region &scopeRegion,
                                         mlir::Region &cleanupRegion) {
  printer.printRegion(scopeRegion,
                      /*printEntryBlockArgs=*/false,
                      /*printBlockTerminators=*/!omitRegionTerm(scopeRegion));
  if (!op.getCleanupRegion().empty()) {
    printer << " cleanup ";
    printer.printRegion(
        cleanupRegion,
        /*printEntryBlockArgs=*/false,
        /*printBlockTerminators=*/!omitRegionTerm(cleanupRegion));
  }
}

//===----------------------------------------------------------------------===//
// AllocaOp
//===----------------------------------------------------------------------===//

/// Parse the optional `, init` and `, const` flags of a cir.alloca.
///
/// These cannot be two adjacent optional groups in the declarative format.
/// Both would begin with a literal `,`, so the generated parser commits to the
/// first group the moment it sees one and then insists on `init` -- which
/// means `cir.alloca ... ["x", const]`, a form the *printer* produces whenever
/// a variable is const but not initialised in place, could not be parsed back.
/// A caught-exception binding is exactly that shape, so any function with a
/// `catch` emitted CIR that CIR itself rejected.
///
/// Accepting the flags in a loop also makes the parser order-insensitive,
/// which costs nothing and removes a second way to get this wrong.
static ParseResult parseAllocaFlags(OpAsmParser &parser, StringAttr &name,
                                    UnitAttr &init, UnitAttr &constant) {
  std::string parsed;
  if (parser.parseString(&parsed))
    return failure();
  name = parser.getBuilder().getStringAttr(parsed);

  while (succeeded(parser.parseOptionalComma())) {
    StringRef keyword;
    llvm::SMLoc loc = parser.getCurrentLocation();
    if (parser.parseKeyword(&keyword))
      return failure();
    if (keyword == "init") {
      if (init)
        return parser.emitError(loc, "'init' specified more than once");
      init = parser.getBuilder().getUnitAttr();
    } else if (keyword == "const") {
      if (constant)
        return parser.emitError(loc, "'const' specified more than once");
      constant = parser.getBuilder().getUnitAttr();
    } else {
      return parser.emitError(loc, "expected 'init' or 'const'");
    }
  }
  return success();
}

static void printAllocaFlags(OpAsmPrinter &printer, Operation *,
                             StringAttr name, UnitAttr init,
                             UnitAttr constant) {
  // The name is printed here rather than left as a `$name` literal in the
  // format so that this directive owns the whole bracketed group. Otherwise
  // the framework inserts a space between the name and the flags and every
  // alloca in the tree reprints as `["x" , const]`.
  //
  // printString, not raw quotes: it escapes, and the parser above unescapes.
  // Writing the quotes by hand would leave a name containing a `"` or a `\`
  // printing in a form that reads back as a different name -- the same
  // print/parse asymmetry this directive exists to fix, just moved.
  printer.printString(name.getValue());
  if (init)
    printer << ", init";
  if (constant)
    printer << ", const";
}

void cir::AllocaOp::build(mlir::OpBuilder &odsBuilder,
                          mlir::OperationState &odsState, mlir::Type addr,
                          mlir::Type allocaType, llvm::StringRef name,
                          mlir::IntegerAttr alignment) {
  odsState.addAttribute(getAllocaTypeAttrName(odsState.name),
                        mlir::TypeAttr::get(allocaType));
  odsState.addAttribute(getNameAttrName(odsState.name),
                        odsBuilder.getStringAttr(name));
  if (alignment) {
    odsState.addAttribute(getAlignmentAttrName(odsState.name), alignment);
  }
  odsState.addTypes(addr);
}

//===----------------------------------------------------------------------===//
// BreakOp
//===----------------------------------------------------------------------===//

LogicalResult cir::BreakOp::verify() {
  if (!getOperation()->getParentOfType<LoopOpInterface>() &&
      !getOperation()->getParentOfType<SwitchOp>())
    return emitOpError("must be within a loop or switch");
  return success();
}

//===----------------------------------------------------------------------===//
// ConditionOp
//===----------------------------------------------------------------------===//

//===----------------------------------
// BranchOpTerminatorInterface Methods
//===----------------------------------

void cir::ConditionOp::getSuccessorRegions(
    ArrayRef<Attribute> operands, SmallVectorImpl<RegionSuccessor> &regions) {
  // TODO(cir): The condition value may be folded to a constant, narrowing
  // down its list of possible successors.

  // Parent is a loop: condition may branch to the body or to the parent op.
  if (auto loopOp = dyn_cast<LoopOpInterface>(getOperation()->getParentOp())) {
    regions.emplace_back(&loopOp.getBody());
    regions.push_back(RegionSuccessor(loopOp.getOperation(), loopOp->getResults()));
  }

  // Parent is an await: condition may branch to resume or suspend regions.
  auto await = cast<AwaitOp>(getOperation()->getParentOp());
  regions.emplace_back(&await.getResume());
  regions.emplace_back(&await.getSuspend());
}

MutableOperandRange
cir::ConditionOp::getMutableSuccessorOperands(RegionSuccessor point) {
  // No values are yielded to the successor region.
  return MutableOperandRange(getOperation(), 0, 0);
}

LogicalResult cir::ConditionOp::verify() {
  if (!isa<LoopOpInterface, AwaitOp>(getOperation()->getParentOp()))
    return emitOpError("condition must be within a conditional region");
  return success();
}

//===----------------------------------------------------------------------===//
// ConstantOp
//===----------------------------------------------------------------------===//

static LogicalResult checkConstantTypes(mlir::Operation *op, mlir::Type opType,
                                        mlir::Attribute attrType) {
  if (isa<cir::ConstPtrAttr>(attrType)) {
    if (!mlir::isa<cir::PointerType>(opType))
      return op->emitOpError(
          "pointer constant initializing a non-pointer type");
    return success();
  }

  if (isa<cir::DataMemberAttr, cir::MethodAttr>(attrType)) {
    // More detailed type verifications are already done in
    // DataMemberAttr::verify or MethodAttr::verify. Don't need to repeat here.
    return success();
  }

  if (isa<cir::ZeroAttr>(attrType)) {
    if (isa<cir::RecordType, cir::ArrayType, cir::VectorType, cir::ComplexType,
            cir::OpaqueType>(opType))
      return success();
    return op->emitOpError(
        "zero expects struct, array, vector, complex, or opaque type");
  }

  if (isa<cir::PatternInitAttr>(attrType)) {
    if (isa<cir::VPtrType, cir::DataMemberType, cir::MethodType>(opType))
      return success();
    return op->emitOpError("pattern_init expects vtable pointer, data member "
                           "pointer, or method pointer type");
  }

  if (mlir::isa<cir::UndefAttr>(attrType)) {
    if (!mlir::isa<cir::VoidType>(opType))
      return success();
    return op->emitOpError("undef expects non-void type");
  }

  if (mlir::isa<cir::BoolAttr>(attrType)) {
    if (!mlir::isa<cir::BoolType>(opType))
      return op->emitOpError("result type (")
             << opType << ") must be '!cir.bool' for '" << attrType << "'";
    return success();
  }

  if (mlir::isa<cir::IntAttr, cir::FPAttr, cir::BlockAddrDiffAttr,
                cir::GlobalViewDiffAttr>(attrType)) {
    auto at = cast<TypedAttr>(attrType);
    if (at.getType() != opType) {
      return op->emitOpError("result type (")
             << opType << ") does not match value type (" << at.getType()
             << ")";
    }
    return success();
  }

  if (mlir::isa<cir::PoisonAttr>(attrType)) {
    if (!mlir::isa<cir::VoidType>(opType))
      return success();
    return op->emitOpError("poison expects non-void type");
  }

  if (mlir::isa<cir::ConstArrayAttr, cir::ConstVectorAttr,
                cir::ConstComplexAttr, cir::ConstRecordAttr,
                cir::GlobalViewAttr, cir::TypeInfoAttr,
                cir::VTableAttr, cir::BlockAddrAttr,
                cir::GlobalViewDiffAttr>(attrType))
    return success();

  assert(isa<TypedAttr>(attrType) && "What else could we be looking at here?");
  return op->emitOpError("global with type ")
         << cast<TypedAttr>(attrType).getType() << " not yet supported";
}

LogicalResult cir::ConstantOp::verify() {
  // ODS already generates checks to make sure the result type is valid. We just
  // need to additionally check that the value's attribute type is consistent
  // with the result type.
  return checkConstantTypes(getOperation(), getType(), getValue());
}

OpFoldResult cir::ConstantOp::fold(FoldAdaptor /*adaptor*/) {
  return getValue();
}

//===----------------------------------------------------------------------===//
// ContinueOp
//===----------------------------------------------------------------------===//

LogicalResult cir::ContinueOp::verify() {
  if (!getOperation()->getParentOfType<LoopOpInterface>())
    return emitOpError("must be within a loop");
  return success();
}

//===----------------------------------------------------------------------===//
// CastOp
//===----------------------------------------------------------------------===//

LogicalResult cir::CastOp::verify() {
  mlir::Type resType = getType();
  mlir::Type srcType = getSrc().getType();

  // Verify address space casts for pointer types. given that
  // casts for within a different address space are illegal.
  auto srcPtrTy = mlir::dyn_cast<cir::PointerType>(srcType);
  auto resPtrTy = mlir::dyn_cast<cir::PointerType>(resType);
  if (srcPtrTy && resPtrTy && (getKind() != cir::CastKind::address_space))
    if (srcPtrTy.getAddrSpace() != resPtrTy.getAddrSpace()) {
      return emitOpError() << "result type address space does not match the "
                              "address space of the operand";
    }

  if (mlir::isa<cir::VectorType>(srcType) &&
      mlir::isa<cir::VectorType>(resType)) {
    // Use the element type of the vector to verify the cast kind. (Except for
    // bitcast, see below.)
    srcType = mlir::cast<cir::VectorType>(srcType).getElementType();
    resType = mlir::cast<cir::VectorType>(resType).getElementType();
  }

  switch (getKind()) {
  case cir::CastKind::int_to_bool: {
    if (!mlir::isa<cir::BoolType>(resType))
      return emitOpError() << "requires !cir.bool type for result";
    if (!mlir::isa<cir::IntType>(srcType))
      return emitOpError() << "requires !cir.int type for source";
    return success();
  }
  case cir::CastKind::ptr_to_bool: {
    if (!mlir::isa<cir::BoolType>(resType))
      return emitOpError() << "requires !cir.bool type for result";
    if (!mlir::isa<cir::PointerType>(srcType))
      return emitOpError() << "requires !cir.ptr type for source";
    return success();
  }
  case cir::CastKind::integral: {
    if (!mlir::isa<cir::IntType>(resType))
      return emitOpError() << "requires !cir.int type for result";
    if (!mlir::isa<cir::IntType>(srcType))
      return emitOpError() << "requires !cir.int type for source";
    return success();
  }
  case cir::CastKind::array_to_ptrdecay: {
    const auto arrayPtrTy = mlir::dyn_cast<cir::PointerType>(srcType);
    const auto flatPtrTy = mlir::dyn_cast<cir::PointerType>(resType);
    if (!arrayPtrTy || !flatPtrTy)
      return emitOpError() << "requires !cir.ptr type for source and result";

    if (arrayPtrTy.getAddrSpace() != flatPtrTy.getAddrSpace()) {
      return emitOpError()
             << "requires same address space for source and result";
    }

    auto arrayTy = mlir::dyn_cast<cir::ArrayType>(arrayPtrTy.getPointee());
    if (!arrayTy)
      return emitOpError() << "requires !cir.array pointee";

    if (arrayTy.getElementType() != flatPtrTy.getPointee())
      return emitOpError()
             << "requires same type for array element and pointee result";
    return success();
  }
  case cir::CastKind::bitcast: {
    // Allow bitcast of records for calling conventions.
    if (isa<RecordType>(srcType) || isa<RecordType>(resType))
      return success();

    // Handle the pointer types first.
    auto srcPtrTy = mlir::dyn_cast<cir::PointerType>(srcType);
    auto resPtrTy = mlir::dyn_cast<cir::PointerType>(resType);

    if (srcPtrTy && resPtrTy) {
      return success();
    }

    // Handle the data member pointer types.
    if (mlir::isa<cir::DataMemberType>(srcType) &&
        mlir::isa<cir::DataMemberType>(resType))
      return success();

    // Handle the pointer to member function types.
    if (mlir::isa<cir::MethodType>(srcType) &&
        mlir::isa<cir::MethodType>(resType))
      return success();

    // Allow casting cir.vptr to pointer types.
    // TODO: Add operations to get object offset and type info and remove this.
    if (mlir::isa<cir::VPtrType>(srcType) &&
        mlir::dyn_cast<cir::PointerType>(resType))
      return success();

    // Handle scalar to vector and vector to scalar conversions.
    // FIXME(cir): The size equality check was removed because the data
    // layout query can fail during ABI lowering when types are being
    // rewritten. The incubator also allows all scalar↔vector bitcasts
    // unconditionally. Restore size checking once the data layout
    // infrastructure is more mature.
    if (mlir::isa<cir::VectorType>(getSrc().getType()) !=
        mlir::isa<cir::VectorType>(getType())) {
      return success();
    }

    // Allow scalar-to-scalar bitcasts if they have the same size
    if (cir::isScalarType(srcType) && cir::isScalarType(resType)) {
      mlir::DataLayout dataLayout(
          getOperation()->getParentOfType<mlir::DataLayoutOpInterface>());
      if (dataLayout.getTypeSize(srcType) == dataLayout.getTypeSize(resType))
        return success();
    }

    // This is the only cast kind where we don't want vector types to decay
    // into the element type.
    if ((!mlir::isa<cir::VectorType>(getSrc().getType()) ||
         !mlir::isa<cir::VectorType>(getType())))
      return emitOpError()
             << "requires !cir.ptr or !cir.vector type for source and result";
    return success();
  }
  case cir::CastKind::floating: {
    if (!mlir::isa<cir::FPTypeInterface>(srcType) ||
        !mlir::isa<cir::FPTypeInterface>(resType))
      return emitOpError() << "requires !cir.float type for source and result";
    return success();
  }
  case cir::CastKind::float_to_int:
  case cir::CastKind::float_to_int_sat: {
    if (!mlir::isa<cir::FPTypeInterface>(srcType))
      return emitOpError() << "requires !cir.float type for source";
    if (!mlir::dyn_cast<cir::IntType>(resType))
      return emitOpError() << "requires !cir.int type for result";
    return success();
  }
  case cir::CastKind::int_to_ptr: {
    if (!mlir::dyn_cast<cir::IntType>(srcType))
      return emitOpError() << "requires !cir.int type for source";
    if (!mlir::dyn_cast<cir::PointerType>(resType))
      return emitOpError() << "requires !cir.ptr type for result";
    return success();
  }
  case cir::CastKind::ptr_to_int: {
    if (!mlir::dyn_cast<cir::PointerType>(srcType))
      return emitOpError() << "requires !cir.ptr type for source";
    if (!mlir::dyn_cast<cir::IntType>(resType))
      return emitOpError() << "requires !cir.int type for result";
    return success();
  }
  case cir::CastKind::float_to_bool: {
    if (!mlir::isa<cir::FPTypeInterface>(srcType))
      return emitOpError() << "requires !cir.float type for source";
    if (!mlir::isa<cir::BoolType>(resType))
      return emitOpError() << "requires !cir.bool type for result";
    return success();
  }
  case cir::CastKind::bool_to_int: {
    if (!mlir::isa<cir::BoolType>(srcType))
      return emitOpError() << "requires !cir.bool type for source";
    if (!mlir::isa<cir::IntType>(resType))
      return emitOpError() << "requires !cir.int type for result";
    return success();
  }
  case cir::CastKind::int_to_float: {
    if (!mlir::isa<cir::IntType>(srcType))
      return emitOpError() << "requires !cir.int type for source";
    if (!mlir::isa<cir::FPTypeInterface>(resType))
      return emitOpError() << "requires !cir.float type for result";
    return success();
  }
  case cir::CastKind::bool_to_float: {
    if (!mlir::isa<cir::BoolType>(srcType))
      return emitOpError() << "requires !cir.bool type for source";
    if (!mlir::isa<cir::FPTypeInterface>(resType))
      return emitOpError() << "requires !cir.float type for result";
    return success();
  }
  case cir::CastKind::address_space: {
    auto srcPtrTy = mlir::dyn_cast<cir::PointerType>(srcType);
    auto resPtrTy = mlir::dyn_cast<cir::PointerType>(resType);
    if (!srcPtrTy || !resPtrTy)
      return emitOpError() << "requires !cir.ptr type for source and result";
    // Address space verification is sufficient here. The pointee types need not
    // be verified as they are handled by bitcast verification logic, which
    // ensures address space compatibility. Verifying pointee types would create
    // a circular dependency between address space and pointee type casting.
    return success();
  }
  case cir::CastKind::float_to_complex: {
    if (!mlir::isa<cir::FPTypeInterface>(srcType))
      return emitOpError() << "requires !cir.float type for source";
    auto resComplexTy = mlir::dyn_cast<cir::ComplexType>(resType);
    if (!resComplexTy)
      return emitOpError() << "requires !cir.complex type for result";
    if (srcType != resComplexTy.getElementType())
      return emitOpError() << "requires source type match result element type";
    return success();
  }
  case cir::CastKind::int_to_complex: {
    if (!mlir::isa<cir::IntType>(srcType))
      return emitOpError() << "requires !cir.int type for source";
    auto resComplexTy = mlir::dyn_cast<cir::ComplexType>(resType);
    if (!resComplexTy)
      return emitOpError() << "requires !cir.complex type for result";
    if (srcType != resComplexTy.getElementType())
      return emitOpError() << "requires source type match result element type";
    return success();
  }
  case cir::CastKind::float_complex_to_real: {
    auto srcComplexTy = mlir::dyn_cast<cir::ComplexType>(srcType);
    if (!srcComplexTy)
      return emitOpError() << "requires !cir.complex type for source";
    if (!mlir::isa<cir::FPTypeInterface>(resType))
      return emitOpError() << "requires !cir.float type for result";
    if (srcComplexTy.getElementType() != resType)
      return emitOpError() << "requires source element type match result type";
    return success();
  }
  case cir::CastKind::int_complex_to_real: {
    auto srcComplexTy = mlir::dyn_cast<cir::ComplexType>(srcType);
    if (!srcComplexTy)
      return emitOpError() << "requires !cir.complex type for source";
    if (!mlir::isa<cir::IntType>(resType))
      return emitOpError() << "requires !cir.int type for result";
    if (srcComplexTy.getElementType() != resType)
      return emitOpError() << "requires source element type match result type";
    return success();
  }
  case cir::CastKind::float_complex_to_bool: {
    auto srcComplexTy = mlir::dyn_cast<cir::ComplexType>(srcType);
    if (!srcComplexTy || !srcComplexTy.isFloatingPointComplex())
      return emitOpError()
             << "requires floating point !cir.complex type for source";
    if (!mlir::isa<cir::BoolType>(resType))
      return emitOpError() << "requires !cir.bool type for result";
    return success();
  }
  case cir::CastKind::int_complex_to_bool: {
    auto srcComplexTy = mlir::dyn_cast<cir::ComplexType>(srcType);
    if (!srcComplexTy || !srcComplexTy.isIntegerComplex())
      return emitOpError()
             << "requires integer !cir.complex type for source";
    if (!mlir::isa<cir::BoolType>(resType))
      return emitOpError() << "requires !cir.bool type for result";
    return success();
  }
  case cir::CastKind::float_complex: {
    auto srcComplexTy = mlir::dyn_cast<cir::ComplexType>(srcType);
    if (!srcComplexTy || !srcComplexTy.isFloatingPointComplex())
      return emitOpError()
             << "requires floating point !cir.complex type for source";
    auto resComplexTy = mlir::dyn_cast<cir::ComplexType>(resType);
    if (!resComplexTy || !resComplexTy.isFloatingPointComplex())
      return emitOpError()
             << "requires floating point !cir.complex type for result";
    return success();
  }
  case cir::CastKind::float_complex_to_int_complex: {
    auto srcComplexTy = mlir::dyn_cast<cir::ComplexType>(srcType);
    if (!srcComplexTy || !srcComplexTy.isFloatingPointComplex())
      return emitOpError()
             << "requires floating point !cir.complex type for source";
    auto resComplexTy = mlir::dyn_cast<cir::ComplexType>(resType);
    if (!resComplexTy || !resComplexTy.isIntegerComplex())
      return emitOpError() << "requires integer !cir.complex type for result";
    return success();
  }
  case cir::CastKind::int_complex: {
    auto srcComplexTy = mlir::dyn_cast<cir::ComplexType>(srcType);
    if (!srcComplexTy || !srcComplexTy.isIntegerComplex())
      return emitOpError() << "requires integer !cir.complex type for source";
    auto resComplexTy = mlir::dyn_cast<cir::ComplexType>(resType);
    if (!resComplexTy || !resComplexTy.isIntegerComplex())
      return emitOpError() << "requires integer !cir.complex type for result";
    return success();
  }
  case cir::CastKind::int_complex_to_float_complex: {
    auto srcComplexTy = mlir::dyn_cast<cir::ComplexType>(srcType);
    if (!srcComplexTy || !srcComplexTy.isIntegerComplex())
      return emitOpError() << "requires integer !cir.complex type for source";
    auto resComplexTy = mlir::dyn_cast<cir::ComplexType>(resType);
    if (!resComplexTy || !resComplexTy.isFloatingPointComplex())
      return emitOpError()
             << "requires floating point !cir.complex type for result";
    return success();
  }
  case cir::CastKind::member_ptr_to_bool: {
    if (!mlir::isa<cir::DataMemberType, cir::MethodType>(srcType))
      return emitOpError()
             << "requires !cir.data_member or !cir.method type for source";
    if (!mlir::isa<cir::BoolType>(resType))
      return emitOpError() << "requires !cir.bool type for result";
    return success();
  }
  }
  llvm_unreachable("Unknown CastOp kind?");
}

static bool isIntOrBoolCast(cir::CastOp op) {
  auto kind = op.getKind();
  return kind == cir::CastKind::bool_to_int ||
         kind == cir::CastKind::int_to_bool || kind == cir::CastKind::integral;
}

static Value tryFoldCastChain(cir::CastOp op) {
  cir::CastOp head = op, tail = op;

  while (op) {
    if (!isIntOrBoolCast(op))
      break;
    head = op;
    op = head.getSrc().getDefiningOp<cir::CastOp>();
  }

  if (head == tail)
    return {};

  // if bool_to_int -> ...  -> int_to_bool: take the bool
  // as we had it was before all casts
  if (head.getKind() == cir::CastKind::bool_to_int &&
      tail.getKind() == cir::CastKind::int_to_bool)
    return head.getSrc();

  // if int_to_bool -> ...  -> int_to_bool: take the result
  // of the first one, as no other casts (and ext casts as well)
  // don't change the first result
  if (head.getKind() == cir::CastKind::int_to_bool &&
      tail.getKind() == cir::CastKind::int_to_bool)
    return head.getResult();

  return {};
}

OpFoldResult cir::CastOp::fold(FoldAdaptor adaptor) {
  if (mlir::isa_and_present<cir::PoisonAttr>(adaptor.getSrc())) {
    // Propagate poison value
    return cir::PoisonAttr::get(getContext(), getType());
  }

  if (getSrc().getType() == getType()) {
    switch (getKind()) {
    case cir::CastKind::integral:
    case cir::CastKind::bitcast:
    case cir::CastKind::address_space:
    case cir::CastKind::float_complex:
    case cir::CastKind::int_complex: {
      return getSrc();
    }
    default:
      return {};
    }
  }

  // Handle cases where a chain of casts cancel out.
  Value result = tryFoldCastChain(*this);
  if (result)
    return result;

  // Handle simple constant casts.
  if (auto srcConst = getSrc().getDefiningOp<cir::ConstantOp>()) {
    switch (getKind()) {
    case cir::CastKind::integral: {
      mlir::Type srcTy = getSrc().getType();
      // Don't try to fold vector casts for now.
      assert(mlir::isa<cir::VectorType>(srcTy) ==
             mlir::isa<cir::VectorType>(getType()));
      if (mlir::isa<cir::VectorType>(srcTy))
        break;

      auto srcIntAttr = srcConst.getValueAttr<cir::IntAttr>();
      if (!srcIntAttr)
        break;

      auto srcIntTy = mlir::cast<cir::IntType>(srcTy);
      auto dstIntTy = mlir::cast<cir::IntType>(getType());
      APInt newVal =
          srcIntTy.isSigned()
              ? srcIntAttr.getValue().sextOrTrunc(dstIntTy.getWidth())
              : srcIntAttr.getValue().zextOrTrunc(dstIntTy.getWidth());
      return cir::IntAttr::get(dstIntTy, newVal);
    }
    default:
      break;
    }
  }
  return {};
}

//===----------------------------------------------------------------------===//
// CallOp
//===----------------------------------------------------------------------===//

namespace {
struct ModuleFuncSymbolCache {
  llvm::StringMap<cir::FuncOp> funcsByName;
  bool seeded = false;
};

// Per thread: modules are lowered in parallel, each on one thread. The
// lowering drops a module's entry around its lowering; see
// forgetModuleFuncSymbolCache.
thread_local llvm::DenseMap<mlir::Operation *, ModuleFuncSymbolCache>
    moduleFuncSymbolCaches;

static ModuleFuncSymbolCache &getModuleFuncSymbolCache(mlir::ModuleOp module) {
  auto &cache = moduleFuncSymbolCaches[module.getOperation()];
  if (!cache.seeded) {
    for (auto fn : module.getOps<cir::FuncOp>())
      cache.funcsByName[fn.getSymName()] = fn;
    cache.seeded = true;
  }
  return cache;
}

static unsigned getHiddenCallOperands(mlir::Operation *op) {
  unsigned hiddenOperands = 0;
  if (op->hasAttr("cir.static_chain"))
    ++hiddenOperands;
  if (op->hasAttr("cir.runtime_ptrauth"))
    ++hiddenOperands;
  return hiddenOperands;
}

static bool areCallOperandTypesCompatible(mlir::Type argTy,
                                          mlir::Type paramTy) {
  if (argTy == paramTy)
    return true;

  // Allow signedness-compatible integer types (e.g., !cir.int<s,32> vs
  // !cir.int<u,32>). C permits implicit signed/unsigned conversions,
  // and LLVM IR doesn't distinguish signedness (both are i32).
  if (auto argIntTy = mlir::dyn_cast<cir::IntType>(argTy))
    if (auto paramIntTy = mlir::dyn_cast<cir::IntType>(paramTy))
      if (argIntTy.getWidth() == paramIntTy.getWidth())
        return true;

  // For pointer type mismatches (e.g., ptr<!void> vs ptr<!u8i>, or
  // stale placeholder ptr<func<()>> vs resolved ptr<func<(real...)>>),
  // tolerate the difference. CIR uses typed pointers unlike LLVM IR's
  // opaque pointers, so mismatches can arise when record type member
  // replacement (via RecordType::replaceMemberTypes) changes the type
  // of values already used as call arguments without updating the
  // callee's declared parameter types. These mismatches are
  // semantically harmless — they become identical opaque pointers
  // after lowering to LLVM IR.
  return mlir::isa<cir::PointerType>(argTy) &&
         mlir::isa<cir::PointerType>(paramTy);
}

struct CallSignatureView {
  llvm::SmallVector<mlir::Type, 8> paramTypes;
  mlir::Type resultType;
  bool returnsVoid = true;
};

static CallSignatureView getRawCallSignature(cir::FuncType fnType) {
  CallSignatureView signature;
  signature.paramTypes.append(fnType.getInputs().begin(),
                              fnType.getInputs().end());
  signature.resultType = fnType.getReturnType();
  signature.returnsVoid = fnType.hasVoidReturn();
  return signature;
}

static std::optional<CallSignatureView>
getSemanticCallSignature(cir::FuncOp fn, cir::FuncType fnType) {
  if (!fnType.hasVoidReturn())
    return std::nullopt;

  CallSignatureView signature;
  signature.paramTypes.reserve(fnType.getNumInputs());

  bool sawSRet = false;
  for (unsigned i = 0; i < fnType.getNumInputs(); ++i) {
    auto sretAttr =
        mlir::dyn_cast_or_null<mlir::TypeAttr>(fn.getArgAttr(i, "cir.sret"));
    if (!sretAttr) {
      signature.paramTypes.push_back(fnType.getInput(i));
      continue;
    }

    if (sawSRet)
      return std::nullopt;

    sawSRet = true;
    signature.resultType = sretAttr.getValue();
  }

  if (!sawSRet)
    return std::nullopt;

  signature.returnsVoid = false;
  return signature;
}

static bool callResultsMatchSignature(mlir::Operation *op,
                                      const CallSignatureView &signature) {
  if (signature.returnsVoid)
    return op->getNumResults() == 0;
  if (op->getNumResults() != 1)
    return false;
  return op->getResult(0).getType() == signature.resultType;
}

static bool callMatchesSignature(cir::CIRCallOpInterface callIf,
                                 const CallSignatureView &signature,
                                 bool isVarArg,
                                 llvm::ArrayRef<bool> isNestArg) {
  mlir::Operation *op = callIf.getOperation();
  unsigned numCallOperands = callIf.getNumArgOperands();
  unsigned nestArgs = llvm::count(isNestArg, true);

  if (!isVarArg && numCallOperands != signature.paramTypes.size() + nestArgs)
    return false;
  if (isVarArg && numCallOperands < signature.paramTypes.size() + nestArgs)
    return false;

  unsigned paramIdx = 0;
  for (unsigned i = 0;
       i < numCallOperands && paramIdx < signature.paramTypes.size(); ++i) {
    if (isNestArg[i])
      continue;
    if (!areCallOperandTypesCompatible(callIf.getArgOperand(i).getType(),
                                       signature.paramTypes[paramIdx]))
      return false;
    ++paramIdx;
  }

  return callResultsMatchSignature(op, signature);
}
} // namespace

void cir::forgetModuleFuncSymbolCache(mlir::Operation *module) {
  moduleFuncSymbolCaches.erase(module);
}

mlir::OperandRange cir::CallOp::getArgOperands() {
  unsigned hiddenOperands = getHiddenCallOperands(this->getOperation());
  if (isIndirect())
    return getArgs().slice(1, getArgs().size() - 1 - hiddenOperands);
  return getArgs().take_front(getArgs().size() - hiddenOperands);
}

mlir::MutableOperandRange cir::CallOp::getArgOperandsMutable() {
  mlir::MutableOperandRange args = getArgsMutable();
  unsigned hiddenOperands = getHiddenCallOperands(this->getOperation());
  if (isIndirect())
    return args.slice(1, args.size() - 1 - hiddenOperands);
  return args.slice(0, args.size() - hiddenOperands);
}

mlir::Value cir::CallOp::getIndirectCall() {
  assert(isIndirect());
  return getOperand(0);
}

/// Return the operand at index 'i'.
Value cir::CallOp::getArgOperand(unsigned i) {
  if (isIndirect())
    ++i;
  return getOperand(i);
}

/// Return the number of operands.
unsigned cir::CallOp::getNumArgOperands() {
  unsigned hiddenOperands = getHiddenCallOperands(this->getOperation());
  if (isIndirect())
    return this->getOperation()->getNumOperands() - 1 - hiddenOperands;
  return this->getOperation()->getNumOperands() - hiddenOperands;
}

cir::FuncOp cir::CallOp::getDirectCallee(mlir::SymbolTable &symbolTable) {
  if (!getCallee())
    return {};
  llvm::StringRef name = *getCallee();
  return symbolTable.lookup<cir::FuncOp>(name);
}

cir::FuncOp cir::CallOp::getDirectCallee(mlir::ModuleOp module) {
  if (!getCallee())
    return {};
  llvm::StringRef name = *getCallee();
  auto &cache = getModuleFuncSymbolCache(module);
  if (auto it = cache.funcsByName.find(name); it != cache.funcsByName.end())
    return it->second;

  auto fn = module.lookupSymbol<cir::FuncOp>(name);
  if (fn)
    cache.funcsByName[name] = fn;
  return fn;
}

cir::FuncOp cir::TryCallOp::getDirectCallee(mlir::SymbolTable &symbolTable) {
  if (!getCallee())
    return {};
  llvm::StringRef name = *getCallee();
  return symbolTable.lookup<cir::FuncOp>(name);
}

cir::FuncOp cir::TryCallOp::getDirectCallee(mlir::ModuleOp module) {
  if (!getCallee())
    return {};
  llvm::StringRef name = *getCallee();
  auto &cache = getModuleFuncSymbolCache(module);
  if (auto it = cache.funcsByName.find(name); it != cache.funcsByName.end())
    return it->second;

  auto fn = module.lookupSymbol<cir::FuncOp>(name);
  if (fn)
    cache.funcsByName[name] = fn;
  return fn;
}

static mlir::ParseResult
parseTryCallDestinations(mlir::OpAsmParser &parser,
                         mlir::OperationState &result) {
  mlir::Block *normalDestSuccessor;
  if (parser.parseSuccessor(normalDestSuccessor))
    return mlir::failure();

  if (parser.parseComma())
    return mlir::failure();

  mlir::Block *unwindDestSuccessor;
  if (parser.parseSuccessor(unwindDestSuccessor))
    return mlir::failure();

  result.addSuccessors(normalDestSuccessor);
  result.addSuccessors(unwindDestSuccessor);
  return mlir::success();
}

static mlir::ParseResult parseCallCommon(mlir::OpAsmParser &parser,
                                         mlir::OperationState &result,
                                         llvm::StringRef extraAttrsAttrName,
                                         bool hasDestinationBlocks = false) {
  llvm::SmallVector<mlir::OpAsmParser::UnresolvedOperand, 4> ops;
  llvm::SMLoc opsLoc;
  mlir::FlatSymbolRefAttr calleeAttr;
  llvm::ArrayRef<mlir::Type> allResultTypes;

  // Parse optional 'exception' keyword before the callee
  if (parser.parseOptionalKeyword("exception").succeeded())
    result.addAttribute("exception", mlir::UnitAttr::get(parser.getContext()));

  // Parse optional 'tail' keyword before the callee
  if (parser.parseOptionalKeyword("tail").succeeded())
    result.addAttribute("tail", mlir::UnitAttr::get(parser.getContext()));

  // Parse optional 'must_tail' keyword before the callee
  if (parser.parseOptionalKeyword("must_tail").succeeded())
    result.addAttribute("must_tail", mlir::UnitAttr::get(parser.getContext()));

  // Parse optional 'no_tail' keyword before the callee
  if (parser.parseOptionalKeyword("no_tail").succeeded())
    result.addAttribute("no_tail", mlir::UnitAttr::get(parser.getContext()));

  // If we cannot parse a string callee, it means this is an indirect call.
  if (!parser
           .parseOptionalAttribute(calleeAttr, CIRDialect::getCalleeAttrName(),
                                   result.attributes)
           .has_value()) {
    OpAsmParser::UnresolvedOperand indirectVal;
    // Do not resolve right now, since we need to figure out the type
    if (parser.parseOperand(indirectVal).failed())
      return failure();
    ops.push_back(indirectVal);
  }

  if (parser.parseLParen())
    return mlir::failure();

  opsLoc = parser.getCurrentLocation();
  if (parser.parseOperandList(ops))
    return mlir::failure();
  if (parser.parseRParen())
    return mlir::failure();

  if (hasDestinationBlocks &&
      parseTryCallDestinations(parser, result).failed()) {
    return ::mlir::failure();
  }

  // Parse optional nothrow (before the type, upstream format)
  if (parser.parseOptionalKeyword("nothrow").succeeded())
    result.addAttribute(CIRDialect::getNoThrowAttrName(),
                        mlir::UnitAttr::get(parser.getContext()));

  // Parse optional side_effect(...) (before the type, upstream format)
  if (parser.parseOptionalKeyword("side_effect").succeeded()) {
    if (parser.parseLParen().failed())
      return failure();
    cir::SideEffect sideEffect;
    if (parseCIRKeyword<cir::SideEffect>(parser, sideEffect).failed())
      return failure();
    if (parser.parseRParen().failed())
      return failure();
    auto attr = cir::SideEffectAttr::get(parser.getContext(), sideEffect);
    result.addAttribute(CIRDialect::getSideEffectAttrName(), attr);
  }

  if (parser.parseOptionalAttrDict(result.attributes))
    return ::mlir::failure();

  if (parser.parseColon())
    return ::mlir::failure();

  mlir::FunctionType opsFnTy;
  if (parser.parseType(opsFnTy))
    return mlir::failure();

  allResultTypes = opsFnTy.getResults();
  result.addTypes(allResultTypes);

  if (parser.resolveOperands(ops, opsFnTy.getInputs(), opsLoc, result.operands))
    return mlir::failure();

  auto &builder = parser.getBuilder();

  // Parse optional cc(...) (after the type, incubator format)
  if (parser.parseOptionalKeyword("cc").succeeded()) {
    if (parser.parseLParen().failed())
      return failure();
    cir::CallingConv callingConv;
    if (parseCIRKeyword<cir::CallingConv>(parser, callingConv).failed())
      return failure();
    if (parser.parseRParen().failed())
      return failure();
    result.addAttribute("calling_conv", cir::CallingConvAttr::get(
                                            builder.getContext(), callingConv));
  }

  // Parse optional extra(...) (after the type, incubator format)
  Attribute extraAttrs;
  if (parser.parseOptionalKeyword("extra").succeeded()) {
    if (parser.parseLParen().failed())
      return failure();
    if (parser.parseAttribute(extraAttrs).failed())
      return failure();
    if (parser.parseRParen().failed())
      return failure();
  } else {
    NamedAttrList empty;
    extraAttrs = cir::ExtraFuncAttributesAttr::get(
        empty.getDictionary(builder.getContext()));
  }
  result.addAttribute(extraAttrsAttrName, extraAttrs);

  return mlir::success();
}

static mlir::Operation *lookupNearestOrModuleSymbolFastPath(
    mlir::Operation *op, mlir::FlatSymbolRefAttr symbol,
    SymbolTableCollection &symbolTable);

static void printCallCommon(
    mlir::Operation *op, mlir::FlatSymbolRefAttr calleeSym,
    mlir::Value indirectCallee, mlir::OpAsmPrinter &printer,
    cir::ExtraFuncAttributesAttr extraAttrs, cir::CallingConv callingConv,
    cir::SideEffect sideEffect, bool nothrow, bool exception = false,
    bool tail = false, bool mustTail = false, bool noTail = false,
    mlir::Block *normalDest = nullptr, mlir::Block *unwindDest = nullptr) {
  printer << ' ';

  auto callLikeOp = mlir::cast<cir::CIRCallOpInterface>(op);
  auto ops = callLikeOp.getArgOperands();

  // Print 'exception' keyword before the callee
  if (exception)
    printer << "exception ";

  // Print 'tail' keyword before the callee
  if (tail)
    printer << "tail ";

  // Print 'must_tail' keyword before the callee
  if (mustTail)
    printer << "must_tail ";

  // Print 'no_tail' keyword before the callee
  if (noTail)
    printer << "no_tail ";

  if (calleeSym) {
    // Direct calls
    printer.printAttributeWithoutType(calleeSym);
  } else {
    // Indirect calls
    assert(indirectCallee);
    printer << indirectCallee;
  }

  printer << "(" << ops << ")";

  if (normalDest) {
    assert(unwindDest && "expected two successors");
    auto tryCall = cast<cir::TryCallOp>(op);
    printer << ' ' << tryCall.getNormalDest();
    printer << ",";
    printer << ' ';
    printer << tryCall.getUnwindDest();
  }

  // Print optional nothrow before the colon (upstream format)
  if (nothrow)
    printer << " nothrow";

  // Print optional side_effect(...) before the colon (upstream format)
  if (sideEffect != cir::SideEffect::All) {
    printer << " side_effect(";
    printer << stringifySideEffect(sideEffect);
    printer << ")";
  }

  llvm::SmallVector<::llvm::StringRef> elidedAttrs = {
      CIRDialect::getCalleeAttrName(),
      CIRDialect::getSideEffectAttrName(),
      CIRDialect::getNoThrowAttrName(),
      CIRDialect::getOperandSegmentSizesAttrName(),
      "cir.arg_lvalues",
      "calling_conv",
      "extra_attrs",
      "exception",
      "tail",
      "must_tail",
      "no_tail",
      "ast"};
  printer.printOptionalAttrDict(op->getAttrs(), elidedAttrs);
  printer << " : ";
  llvm::SmallVector<mlir::Type, 8> printedOperandTypes;
  if (indirectCallee)
    printedOperandTypes.push_back(indirectCallee.getType());
  for (mlir::Value operand : callLikeOp.getArgOperands())
    printedOperandTypes.push_back(operand.getType());
  printer.printFunctionalType(printedOperandTypes, op->getResultTypes());

  // Print optional cc(...) after the type (incubator format)
  if (callingConv != cir::CallingConv::C) {
    printer << " cc(";
    printer << stringifyCallingConv(callingConv);
    printer << ")";
  }

  // Print optional extra(...) after the type (incubator format)
  if (!extraAttrs.getElements().empty()) {
    printer << " extra(";
    printer.printAttributeWithoutType(extraAttrs);
    printer << ")";
  }
}

mlir::ParseResult cir::CallOp::parse(mlir::OpAsmParser &parser,
                                     mlir::OperationState &result) {
  if (parseCallCommon(parser, result, getExtraAttrsAttrName(result.name)))
    return failure();

  // Parse optional cleanup region
  if (succeeded(parser.parseOptionalKeyword("cleanup"))) {
    if (parser.parseRegion(*result.addRegion()))
      return failure();
  } else {
    result.addRegion(); // Add empty region placeholder
  }

  return mlir::success();
}

void cir::CallOp::print(mlir::OpAsmPrinter &p) {
  mlir::Value indirectCallee = isIndirect() ? getIndirectCall() : nullptr;
  cir::CallingConv callingConv = getCallingConv();
  cir::SideEffect sideEffect = getSideEffect();
  bool nothrow = getNothrowAttr() != nullptr;
  bool exception = getExceptionAttr() != nullptr;
  bool tail = getTailAttr() != nullptr;
  bool mustTail = getMustTailAttr() != nullptr;
  bool noTail = getNoTailAttr() != nullptr;
  cir::ExtraFuncAttributesAttr extraAttrs = getExtraAttrsAttr();
  if (!extraAttrs) {
    NamedAttrList empty;
    extraAttrs =
        cir::ExtraFuncAttributesAttr::get(empty.getDictionary(getContext()));
  }
  printCallCommon(*this, getCalleeAttr(), indirectCallee, p, extraAttrs,
                  callingConv, sideEffect, nothrow, exception, tail, mustTail,
                  noTail);

  // Print cleanup region if not empty
  if (!getCleanup().empty()) {
    p << " cleanup ";
    p.printRegion(getCleanup(), /*printEntryBlockArgs=*/false,
                  /*printBlockTerminators=*/true);
  }
}

static LogicalResult
verifyCallCommInSymbolUses(mlir::Operation *op,
                           SymbolTableCollection &symbolTable) {
  auto fnAttr =
      op->getAttrOfType<FlatSymbolRefAttr>(CIRDialect::getCalleeAttrName());
  if (!fnAttr) {
    // This is an indirect call, thus we don't have to check the symbol uses.
    return mlir::success();
  }

  cir::FuncOp fn = mlir::dyn_cast_or_null<cir::FuncOp>(
      lookupNearestOrModuleSymbolFastPath(op, fnAttr, symbolTable));
  if (!fn)
    return op->emitOpError() << "'" << fnAttr.getValue()
                             << "' does not reference a valid function";

  auto callIf = dyn_cast<cir::CIRCallOpInterface>(op);
  assert(callIf && "expected CIR call interface to be always available");

  // Verify that the operand and result types match the callee. Note that
  // argument-checking is disabled for functions without a prototype.
  auto fnType = fn.getFunctionType();
  CallSignatureView rawSignature = getRawCallSignature(fnType);
  std::optional<CallSignatureView> semanticSignature =
      getSemanticCallSignature(fn, fnType);

  unsigned numCallOperands = callIf.getNumArgOperands();
  llvm::SmallVector<bool> isNestArg(numCallOperands, false);
  unsigned nestArgs = 0;
  if (auto callArgAttrs =
          op->getAttrOfType<mlir::ArrayAttr>("cir.call_arg_attrs")) {
    for (auto [i, attr] : llvm::enumerate(callArgAttrs)) {
      if (i >= numCallOperands)
        break;
      if (auto dict = mlir::dyn_cast<mlir::DictionaryAttr>(attr))
        if (dict.get("cir.nest")) {
          ++nestArgs;
          isNestArg[i] = true;
        }
    }
  }

  if (!fn.getNoProto()) {
    bool matchesRawSignature =
        callMatchesSignature(callIf, rawSignature, fnType.isVarArg(), isNestArg);
    bool matchesSourceSignature =
        semanticSignature &&
        callMatchesSignature(callIf, *semanticSignature, fnType.isVarArg(),
                             isNestArg);

    if (!matchesRawSignature && !matchesSourceSignature) {
      const CallSignatureView &diagSignature =
          semanticSignature && (op->getNumResults() != 0)
              ? *semanticSignature
              : rawSignature;

      if (!fnType.isVarArg() &&
          numCallOperands != diagSignature.paramTypes.size() + nestArgs)
        return op->emitOpError("incorrect number of operands for callee");
      if (fnType.isVarArg() &&
          numCallOperands < diagSignature.paramTypes.size() + nestArgs)
        return op->emitOpError("too few operands for callee");

      unsigned fnParamIdx = 0;
      for (unsigned i = 0;
           i < numCallOperands &&
           fnParamIdx < diagSignature.paramTypes.size(); ++i) {
        if (isNestArg[i])
          continue;
        if (!areCallOperandTypesCompatible(callIf.getArgOperand(i).getType(),
                                           diagSignature.paramTypes[fnParamIdx]))
          return op->emitOpError("operand type mismatch: expected operand type ")
                 << diagSignature.paramTypes[fnParamIdx] << ", but provided "
                 << callIf.getArgOperand(i).getType() << " for operand number "
                 << i;
        fnParamIdx++;
      }
    }
  }

  // Calling convention must match.
  if (callIf.getCallingConv() != fn.getCallingConv())
    return op->emitOpError("calling convention mismatch: expected ")
           << stringifyCallingConv(fn.getCallingConv()) << ", but provided "
           << stringifyCallingConv(callIf.getCallingConv());

  bool matchesRawResults = callResultsMatchSignature(op, rawSignature);
  bool matchesSourceResults =
      semanticSignature && callResultsMatchSignature(op, *semanticSignature);

  if (!matchesRawResults && !matchesSourceResults) {
    const CallSignatureView &diagSignature =
        semanticSignature && (op->getNumResults() != 0) ? *semanticSignature
                                                        : rawSignature;
    // Void function must not return any results.
    if (diagSignature.returnsVoid && op->getNumResults() != 0)
      return op->emitOpError("callee returns void but call has results");

    // Non-void function calls must return exactly one result.
    if (!diagSignature.returnsVoid && op->getNumResults() != 1)
      return op->emitOpError("incorrect number of results for callee");

    // Parent function and return value types must match.
    if (!diagSignature.returnsVoid &&
        op->getResultTypes().front() != diagSignature.resultType) {
      return op->emitOpError("result type mismatch: expected ")
             << diagSignature.resultType << ", but provided "
             << op->getResult(0).getType();
    }
  }

  return mlir::success();
}

static mlir::Operation *lookupNearestOrModuleSymbolFastPath(
    mlir::Operation *op, mlir::FlatSymbolRefAttr symbol,
    SymbolTableCollection &symbolTable) {
  if (!symbol)
    return nullptr;

  // For flat refs resolved against the module symbol table, query the cached
  // SymbolTableCollection directly with the root StringAttr instead of routing
  // through the generic SymbolRefAttr path.
  if (auto module = op->getParentOfType<mlir::ModuleOp>())
    if (mlir::SymbolTable::getNearestSymbolTable(op) == module.getOperation())
      if (auto *fast = symbolTable.lookupSymbolIn(module, symbol.getAttr()))
        return fast;
  return symbolTable.lookupNearestSymbolFrom(op, symbol);
}

LogicalResult
cir::CallOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  return verifyCallCommInSymbolUses(*this, symbolTable);
}

//===----------------------------------------------------------------------===//
// TryCallOp
//===----------------------------------------------------------------------===//

mlir::OperandRange cir::TryCallOp::getArgOperands() {
  unsigned hiddenOperands = getHiddenCallOperands(this->getOperation());
  if (isIndirect())
    return getArgs().slice(1, getArgs().size() - 1 - hiddenOperands);
  return getArgs().take_front(getArgs().size() - hiddenOperands);
}

mlir::MutableOperandRange cir::TryCallOp::getArgOperandsMutable() {
  mlir::MutableOperandRange args = getArgsMutable();
  unsigned hiddenOperands = getHiddenCallOperands(this->getOperation());
  if (isIndirect())
    return args.slice(1, args.size() - 1 - hiddenOperands);
  return args.slice(0, args.size() - hiddenOperands);
}

mlir::Value cir::TryCallOp::getIndirectCall() {
  assert(isIndirect());
  return getOperand(0);
}

/// Return the operand at index 'i'.
Value cir::TryCallOp::getArgOperand(unsigned i) {
  if (isIndirect())
    ++i;
  return getOperand(i);
}

/// Return the number of operands.
unsigned cir::TryCallOp::getNumArgOperands() {
  unsigned hiddenOperands = getHiddenCallOperands(this->getOperation());
  if (isIndirect())
    return this->getOperation()->getNumOperands() - 1 - hiddenOperands;
  return this->getOperation()->getNumOperands() - hiddenOperands;
}

LogicalResult
cir::TryCallOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  return verifyCallCommInSymbolUses(*this, symbolTable);
}

mlir::ParseResult cir::TryCallOp::parse(mlir::OpAsmParser &parser,
                                        mlir::OperationState &result) {
  return parseCallCommon(parser, result, getExtraAttrsAttrName(result.name),
                         /*hasDestinationBlocks=*/true);
}

void cir::TryCallOp::print(::mlir::OpAsmPrinter &p) {
  mlir::Value indirectCallee = isIndirect() ? getIndirectCall() : nullptr;
  cir::CallingConv callingConv = getCallingConv();
  cir::SideEffect sideEffect = getSideEffect();
  bool nothrow = getNothrowAttr() != nullptr;
  cir::ExtraFuncAttributesAttr extraAttrs = getExtraAttrsAttr();
  if (!extraAttrs) {
    NamedAttrList empty;
    extraAttrs =
        cir::ExtraFuncAttributesAttr::get(empty.getDictionary(getContext()));
  }
  printCallCommon(*this, getCalleeAttr(), indirectCallee, p, extraAttrs,
                  callingConv, sideEffect, nothrow, /*exception=*/false,
                  /*tail=*/false, /*mustTail=*/false, /*noTail=*/false,
                  getNormalDest(), getUnwindDest());
}

//===----------------------------------------------------------------------===//
// ReturnOp
//===----------------------------------------------------------------------===//

static mlir::LogicalResult checkReturnAndFunction(cir::ReturnOp op,
                                                  cir::FuncOp function) {
  // ReturnOps currently only have a single optional operand.
  if (op.getNumOperands() > 1)
    return op.emitOpError() << "expects at most 1 return operand";

  // Ensure returned type matches the function signature.
  auto expectedTy = function.getFunctionType().getReturnType();
  auto actualTy =
      (op.getNumOperands() == 0 ? cir::VoidType::get(op.getContext())
                                : op.getOperand(0).getType());
  if (actualTy != expectedTy)
    return op.emitOpError() << "returns " << actualTy
                            << " but enclosing function returns " << expectedTy;

  return mlir::success();
}

mlir::LogicalResult cir::ReturnOp::verify() {
  // Returns can be present in multiple different scopes, get the
  // wrapping function and start from there.
  auto *fnOp = getOperation()->getParentOp();
  while (!isa<cir::FuncOp>(fnOp))
    fnOp = fnOp->getParentOp();

  // Make sure return types match function return type.
  if (checkReturnAndFunction(*this, cast<cir::FuncOp>(fnOp)).failed())
    return failure();

  return success();
}

//===----------------------------------------------------------------------===//
// IfOp
//===----------------------------------------------------------------------===//

ParseResult cir::IfOp::parse(OpAsmParser &parser, OperationState &result) {
  // create the regions for 'then'.
  result.regions.reserve(2);
  Region *thenRegion = result.addRegion();
  Region *elseRegion = result.addRegion();

  mlir::Builder &builder = parser.getBuilder();
  OpAsmParser::UnresolvedOperand cond;
  Type boolType = cir::BoolType::get(builder.getContext());

  if (parser.parseOperand(cond) ||
      parser.resolveOperand(cond, boolType, result.operands))
    return failure();

  // Parse 'then' region.
  mlir::SMLoc parseThenLoc = parser.getCurrentLocation();
  if (parser.parseRegion(*thenRegion, /*arguments=*/{}, /*argTypes=*/{}))
    return failure();

  if (ensureRegionTerm(parser, *thenRegion, parseThenLoc).failed())
    return failure();

  // If we find an 'else' keyword, parse the 'else' region.
  if (!parser.parseOptionalKeyword("else")) {
    mlir::SMLoc parseElseLoc = parser.getCurrentLocation();
    if (parser.parseRegion(*elseRegion, /*arguments=*/{}, /*argTypes=*/{}))
      return failure();
    if (ensureRegionTerm(parser, *elseRegion, parseElseLoc).failed())
      return failure();
  }

  // Parse the optional attribute list.
  if (parser.parseOptionalAttrDict(result.attributes))
    return failure();
  return success();
}

void cir::IfOp::print(OpAsmPrinter &p) {
  p << " " << getCondition() << " ";
  mlir::Region &thenRegion = this->getThenRegion();
  p.printRegion(thenRegion,
                /*printEntryBlockArgs=*/false,
                /*printBlockTerminators=*/!omitRegionTerm(thenRegion));

  // Print the 'else' regions if it exists and has a block.
  mlir::Region &elseRegion = this->getElseRegion();
  if (!elseRegion.empty()) {
    p << " else ";
    p.printRegion(elseRegion,
                  /*printEntryBlockArgs=*/false,
                  /*printBlockTerminators=*/!omitRegionTerm(elseRegion));
  }

  p.printOptionalAttrDict(getOperation()->getAttrs());
}

/// Default callback for IfOp builders.
void cir::buildTerminatedBody(OpBuilder &builder, Location loc) {
  // add cir.yield to end of the block
  cir::YieldOp::create(builder, loc);
}

/// Given the region at `index`, or the parent operation if `index` is None,
/// return the successor regions. These are the regions that may be selected
/// during the flow of control. `operands` is a set of optional attributes that
/// correspond to a constant value for each operand, or null if that operand is
/// not a constant.
void cir::IfOp::getSuccessorRegions(mlir::RegionBranchPoint point,
                                    SmallVectorImpl<RegionSuccessor> &regions) {
  // The `then` and the `else` region branch back to the parent operation.
  if (!point.isParent()) {
    regions.push_back(RegionSuccessor(getOperation(), getOperation()->getResults()));
    return;
  }

  // Don't consider the else region if it is empty.
  Region *elseRegion = &this->getElseRegion();
  if (elseRegion->empty())
    elseRegion = nullptr;

  // If the condition isn't constant, both regions may be executed.
  regions.push_back(RegionSuccessor(&getThenRegion()));
  // If the else region does not exist, it is not a viable successor.
  if (elseRegion)
    regions.push_back(RegionSuccessor(elseRegion));

  return;
}

void cir::IfOp::build(OpBuilder &builder, OperationState &result, Value cond,
                      bool withElseRegion, BuilderCallbackRef thenBuilder,
                      BuilderCallbackRef elseBuilder) {
  assert(thenBuilder && "the builder callback for 'then' must be present");
  result.addOperands(cond);

  OpBuilder::InsertionGuard guard(builder);
  Region *thenRegion = result.addRegion();
  builder.createBlock(thenRegion);
  thenBuilder(builder, result.location);

  Region *elseRegion = result.addRegion();
  if (!withElseRegion)
    return;

  builder.createBlock(elseRegion);
  elseBuilder(builder, result.location);
}

//===----------------------------------------------------------------------===//
// ScopeOp
//===----------------------------------------------------------------------===//

/// Given the region at `index`, or the parent operation if `index` is None,
/// return the successor regions. These are the regions that may be selected
/// during the flow of control. `operands` is a set of optional attributes
/// that correspond to a constant value for each operand, or null if that
/// operand is not a constant.
void cir::ScopeOp::getSuccessorRegions(
    mlir::RegionBranchPoint point, SmallVectorImpl<RegionSuccessor> &regions) {
  // The only region always branch back to the parent operation.
  if (!point.isParent()) {
    regions.push_back(RegionSuccessor(getOperation(), getOperation()->getResults()));
    return;
  }

  // If the condition isn't constant, both regions may be executed.
  regions.push_back(RegionSuccessor(&getScopeRegion()));
  regions.push_back(RegionSuccessor(&getCleanupRegion()));
}

void cir::ScopeOp::build(
    OpBuilder &builder, OperationState &result,
    function_ref<void(OpBuilder &, Type &, Location)> scopeBuilder) {
  assert(scopeBuilder && "the builder callback for 'then' must be present");

  OpBuilder::InsertionGuard guard(builder);
  Region *scopeRegion = result.addRegion();
  builder.createBlock(scopeRegion);
  // cleanup region, do not eagerly create blocks, do it upon demand.
  (void)result.addRegion();

  mlir::Type yieldTy;
  scopeBuilder(builder, yieldTy, result.location);

  if (yieldTy)
    result.addTypes(TypeRange{yieldTy});
}

void cir::ScopeOp::build(
    OpBuilder &builder, OperationState &result,
    function_ref<void(OpBuilder &, Location)> scopeBuilder) {
  assert(scopeBuilder && "the builder callback for 'then' must be present");
  OpBuilder::InsertionGuard guard(builder);
  Region *scopeRegion = result.addRegion();
  builder.createBlock(scopeRegion);
  // cleanup region, do not eagerly create blocks, do it upon demand.
  (void)result.addRegion();
  scopeBuilder(builder, result.location);
}

LogicalResult cir::ScopeOp::verify() {
  if (getScopeRegion().empty()) {
    return emitOpError() << "cir.scope must not be empty since it should "
                            "include at least an implicit cir.yield ";
  }

  mlir::Block &lastBlock = getScopeRegion().back();
  if (lastBlock.empty() || !lastBlock.mightHaveTerminator() ||
      !lastBlock.getTerminator()->hasTrait<OpTrait::IsTerminator>())
    return emitOpError() << "last block of cir.scope must be terminated";
  return success();
}

LogicalResult cir::ScopeOp::fold(FoldAdaptor /*adaptor*/,
                                 SmallVectorImpl<OpFoldResult> &results) {
  // Only fold "trivial" scopes: a single block containing only a `cir.yield`.
  if (!getScopeRegion().hasOneBlock())
    return failure();
  Block &block = getScopeRegion().front();
  if (block.getOperations().size() != 1)
    return failure();

  auto yield = dyn_cast<cir::YieldOp>(block.front());
  if (!yield)
    return failure();

  // Only fold when the scope produces a value.
  if (getNumResults() != 1 || yield.getNumOperands() != 1)
    return failure();

  // Don't fold if there's a non-empty cleanup region.
  if (!getCleanupRegion().empty())
    return failure();

  results.push_back(yield.getOperand(0));
  return success();
}

//===----------------------------------------------------------------------===//
// BrOp
//===----------------------------------------------------------------------===//

mlir::SuccessorOperands cir::BrOp::getSuccessorOperands(unsigned index) {
  assert(index == 0 && "invalid successor index");
  return mlir::SuccessorOperands(getDestOperandsMutable());
}

Block *cir::BrOp::getSuccessorForOperands(ArrayRef<Attribute>) {
  return getDest();
}

//===----------------------------------------------------------------------===//
// IndirectBrCondOp
//===----------------------------------------------------------------------===//

mlir::SuccessorOperands
cir::IndirectBrOp::getSuccessorOperands(unsigned index) {
  assert(index < getNumSuccessors() && "invalid successor index");
  return mlir::SuccessorOperands(getSuccOperandsMutable()[index]);
}

ParseResult parseIndirectBrOpSucessors(
    OpAsmParser &parser, Type &flagType,
    SmallVectorImpl<Block *> &succOperandBlocks,
    SmallVectorImpl<SmallVector<OpAsmParser::UnresolvedOperand>> &succOperands,
    SmallVectorImpl<SmallVector<Type>> &succOperandsTypes) {
  if (failed(parser.parseCommaSeparatedList(
          OpAsmParser::Delimiter::Square,
          [&]() {
            Block *destination = nullptr;
            SmallVector<OpAsmParser::UnresolvedOperand> operands;
            SmallVector<Type> operandTypes;

            if (parser.parseSuccessor(destination).failed())
              return failure();

            if (succeeded(parser.parseOptionalLParen())) {
              if (failed(parser.parseOperandList(
                      operands, OpAsmParser::Delimiter::None)) ||
                  failed(parser.parseColonTypeList(operandTypes)) ||
                  failed(parser.parseRParen()))
                return failure();
            }
            succOperandBlocks.push_back(destination);
            succOperands.emplace_back(operands);
            succOperandsTypes.emplace_back(operandTypes);
            return success();
          },
          "successor blocks")))
    return failure();
  return success();
}

void printIndirectBrOpSucessors(OpAsmPrinter &p, cir::IndirectBrOp op,
                                Type flagType, SuccessorRange succs,
                                OperandRangeRange succOperands,
                                const TypeRangeRange &succOperandsTypes) {
  p << "[";
  llvm::interleave(
      llvm::zip(succs, succOperands),
      [&](auto i) {
        p.printNewline();
        p.printSuccessorAndUseList(std::get<0>(i), std::get<1>(i));
      },
      [&] { p << ','; });
  if (!succOperands.empty())
    p.printNewline();
  p << "]";
}

//===----------------------------------------------------------------------===//
// BrCondOp
//===----------------------------------------------------------------------===//

mlir::SuccessorOperands cir::BrCondOp::getSuccessorOperands(unsigned index) {
  assert(index < getNumSuccessors() && "invalid successor index");
  return SuccessorOperands(index == 0 ? getDestOperandsTrueMutable()
                                      : getDestOperandsFalseMutable());
}

Block *cir::BrCondOp::getSuccessorForOperands(ArrayRef<Attribute> operands) {
  if (IntegerAttr condAttr = dyn_cast_if_present<IntegerAttr>(operands.front()))
    return condAttr.getValue().isOne() ? getDestTrue() : getDestFalse();
  return nullptr;
}

//===----------------------------------------------------------------------===//
// CaseOp
//===----------------------------------------------------------------------===//

void cir::CaseOp::getSuccessorRegions(
    mlir::RegionBranchPoint point, SmallVectorImpl<RegionSuccessor> &regions) {
  if (!point.isParent()) {
    regions.push_back(RegionSuccessor(getOperation(), getOperation()->getResults()));
    return;
  }
  regions.push_back(RegionSuccessor(&getCaseRegion()));
}

void cir::CaseOp::build(OpBuilder &builder, OperationState &result,
                        ArrayAttr value, CaseOpKind kind,
                        OpBuilder::InsertPoint &insertPoint) {
  OpBuilder::InsertionGuard guardSwitch(builder);
  result.addAttribute("value", value);
  result.getOrAddProperties<Properties>().kind =
      cir::CaseOpKindAttr::get(builder.getContext(), kind);
  Region *caseRegion = result.addRegion();
  builder.createBlock(caseRegion);

  insertPoint = builder.saveInsertionPoint();
}

//===----------------------------------------------------------------------===//
// SwitchOp
//===----------------------------------------------------------------------===//

void cir::SwitchOp::getSuccessorRegions(
    mlir::RegionBranchPoint point, SmallVectorImpl<RegionSuccessor> &region) {
  if (!point.isParent()) {
    region.push_back(RegionSuccessor(getOperation(), getOperation()->getResults()));
    return;
  }

  region.push_back(RegionSuccessor(&getBody()));
}

void cir::SwitchOp::build(OpBuilder &builder, OperationState &result,
                          Value cond, BuilderOpStateCallbackRef switchBuilder) {
  assert(switchBuilder && "the builder callback for regions must be present");
  OpBuilder::InsertionGuard guardSwitch(builder);
  Region *switchRegion = result.addRegion();
  builder.createBlock(switchRegion);
  result.addOperands({cond});
  switchBuilder(builder, result.location, result);
}

void cir::SwitchOp::collectCases(llvm::SmallVectorImpl<CaseOp> &cases) {
  walk<mlir::WalkOrder::PreOrder>([&](mlir::Operation *op) {
    // Don't walk in nested switch op.
    if (isa<cir::SwitchOp>(op) && op != *this)
      return WalkResult::skip();

    if (auto caseOp = dyn_cast<cir::CaseOp>(op))
      cases.push_back(caseOp);

    return WalkResult::advance();
  });
}

bool cir::SwitchOp::isSimpleForm(llvm::SmallVectorImpl<CaseOp> &cases) {
  collectCases(cases);

  if (getBody().empty())
    return false;

  if (!isa<YieldOp>(getBody().front().back()))
    return false;

  if (!llvm::all_of(getBody().front(),
                    [](Operation &op) { return isa<CaseOp, YieldOp>(op); }))
    return false;

  return llvm::all_of(cases, [this](CaseOp op) {
    return op->getParentOfType<SwitchOp>() == *this;
  });
}

//===----------------------------------------------------------------------===//
// SwitchFlatOp
//===----------------------------------------------------------------------===//

void cir::SwitchFlatOp::build(OpBuilder &builder, OperationState &result,
                              Value value, Block *defaultDestination,
                              ValueRange defaultOperands,
                              ArrayRef<APInt> caseValues,
                              BlockRange caseDestinations,
                              ArrayRef<ValueRange> caseOperands) {

  std::vector<mlir::Attribute> caseValuesAttrs;
  for (const APInt &val : caseValues)
    caseValuesAttrs.push_back(cir::IntAttr::get(value.getType(), val));
  mlir::ArrayAttr attrs = ArrayAttr::get(builder.getContext(), caseValuesAttrs);

  build(builder, result, value, defaultOperands, caseOperands, attrs,
        defaultDestination, caseDestinations);
}

/// <cases> ::= `[` (case (`,` case )* )? `]`
/// <case>  ::= integer `:` bb-id (`(` ssa-use-and-type-list `)`)?
static ParseResult parseSwitchFlatOpCases(
    OpAsmParser &parser, Type flagType, mlir::ArrayAttr &caseValues,
    SmallVectorImpl<Block *> &caseDestinations,
    SmallVectorImpl<llvm::SmallVector<OpAsmParser::UnresolvedOperand>>
        &caseOperands,
    SmallVectorImpl<llvm::SmallVector<Type>> &caseOperandTypes) {
  if (failed(parser.parseLSquare()))
    return failure();
  if (succeeded(parser.parseOptionalRSquare()))
    return success();
  llvm::SmallVector<mlir::Attribute> values;

  auto parseCase = [&]() {
    int64_t value = 0;
    if (failed(parser.parseInteger(value)))
      return failure();

    values.push_back(cir::IntAttr::get(flagType, value));

    Block *destination;
    llvm::SmallVector<OpAsmParser::UnresolvedOperand> operands;
    llvm::SmallVector<Type> operandTypes;
    if (parser.parseColon() || parser.parseSuccessor(destination))
      return failure();
    if (!parser.parseOptionalLParen()) {
      if (parser.parseOperandList(operands, OpAsmParser::Delimiter::None,
                                  /*allowResultNumber=*/false) ||
          parser.parseColonTypeList(operandTypes) || parser.parseRParen())
        return failure();
    }
    caseDestinations.push_back(destination);
    caseOperands.emplace_back(operands);
    caseOperandTypes.emplace_back(operandTypes);
    return success();
  };
  if (failed(parser.parseCommaSeparatedList(parseCase)))
    return failure();

  caseValues = ArrayAttr::get(flagType.getContext(), values);

  return parser.parseRSquare();
}

static void printSwitchFlatOpCases(OpAsmPrinter &p, cir::SwitchFlatOp op,
                                   Type flagType, mlir::ArrayAttr caseValues,
                                   SuccessorRange caseDestinations,
                                   OperandRangeRange caseOperands,
                                   const TypeRangeRange &caseOperandTypes) {
  p << '[';
  p.printNewline();
  if (!caseValues) {
    p << ']';
    return;
  }

  size_t index = 0;
  llvm::interleave(
      llvm::zip(caseValues, caseDestinations),
      [&](auto i) {
        p << "  ";
        mlir::Attribute a = std::get<0>(i);
        p << mlir::cast<cir::IntAttr>(a).getValue();
        p << ": ";
        p.printSuccessorAndUseList(std::get<1>(i), caseOperands[index++]);
      },
      [&] {
        p << ',';
        p.printNewline();
      });
  p.printNewline();
  p << ']';
}

//===----------------------------------------------------------------------===//
// GlobalOp
//===----------------------------------------------------------------------===//

static ParseResult parseConstantValue(OpAsmParser &parser,
                                      mlir::Attribute &valueAttr) {
  NamedAttrList attr;
  return parser.parseAttribute(valueAttr, "value", attr);
}

static void printConstant(OpAsmPrinter &p, Attribute value) {
  p.printAttribute(value);
}

mlir::LogicalResult cir::GlobalOp::verify() {
  // Verify that the initial value, if present, is either a unit attribute or
  // an attribute CIR supports.
  if (getInitialValue().has_value()) {
    if (checkConstantTypes(getOperation(), getSymType(), *getInitialValue())
            .failed())
      return failure();
  }

  // Verify that the constructor region, if present, has only one block which is
  // not empty.
  auto &ctorRegion = getCtorRegion();
  if (!ctorRegion.empty()) {
    if (!ctorRegion.hasOneBlock())
      return emitError() << "ctor region must have exactly one block.";

    auto &block = ctorRegion.front();
    if (block.empty())
      return emitError() << "ctor region shall not be empty.";
  }

  // Verify that the destructor region, if present, has only one block which is
  // not empty.
  auto &dtorRegion = getDtorRegion();
  if (!dtorRegion.empty()) {
    if (!dtorRegion.hasOneBlock())
      return emitError() << "dtor region must have exactly one block.";

    auto &block = dtorRegion.front();
    if (block.empty())
      return emitError() << "dtor region shall not be empty.";
  }

  // Verify visibility/linkage consistency.
  switch (getLinkage()) {
  case GlobalLinkageKind::InternalLinkage:
  case GlobalLinkageKind::PrivateLinkage:
    if (isPublic())
      return emitError() << "public visibility not allowed with '"
                         << stringifyGlobalLinkageKind(getLinkage())
                         << "' linkage";
    break;
  case GlobalLinkageKind::ExternalLinkage:
  case GlobalLinkageKind::ExternalWeakLinkage:
  case GlobalLinkageKind::LinkOnceODRLinkage:
  case GlobalLinkageKind::LinkOnceAnyLinkage:
  case GlobalLinkageKind::CommonLinkage:
  case GlobalLinkageKind::WeakAnyLinkage:
  case GlobalLinkageKind::WeakODRLinkage:
  case GlobalLinkageKind::AvailableExternallyLinkage:
    // FIXME: mlir's concept of visibility gets tricky with LLVM ones,
    // for instance, symbol declarations cannot be "public", so we
    // have to mark them "private" to workaround the symbol verifier.
    if (isPrivate() && !isDeclaration())
      return emitError() << "private visibility not allowed with '"
                         << stringifyGlobalLinkageKind(getLinkage())
                         << "' linkage";
    break;
  }

  return success();
}

void cir::GlobalOp::build(
    OpBuilder &odsBuilder, OperationState &odsState, llvm::StringRef sym_name,
    mlir::Type sym_type, bool isConstant, cir::GlobalLinkageKind linkage,
    function_ref<void(OpBuilder &, Location)> ctorBuilder,
    function_ref<void(OpBuilder &, Location)> dtorBuilder) {
  odsState.addAttribute(getSymNameAttrName(odsState.name),
                        odsBuilder.getStringAttr(sym_name));
  odsState.addAttribute(getSymTypeAttrName(odsState.name),
                        mlir::TypeAttr::get(sym_type));
  if (isConstant)
    odsState.addAttribute(getConstantAttrName(odsState.name),
                          odsBuilder.getUnitAttr());

  cir::GlobalLinkageKindAttr linkageAttr =
      cir::GlobalLinkageKindAttr::get(odsBuilder.getContext(), linkage);
  odsState.addAttribute(getLinkageAttrName(odsState.name), linkageAttr);

  Region *ctorRegion = odsState.addRegion();
  if (ctorBuilder) {
    odsBuilder.createBlock(ctorRegion);
    ctorBuilder(odsBuilder, odsState.location);
  }

  Region *dtorRegion = odsState.addRegion();
  if (dtorBuilder) {
    odsBuilder.createBlock(dtorRegion);
    dtorBuilder(odsBuilder, odsState.location);
  }

  odsState.addAttribute(getGlobalVisibilityAttrName(odsState.name),
                        cir::VisibilityAttr::get(odsBuilder.getContext()));
}

/// Given the region at `index`, or the parent operation if `index` is None,
/// return the successor regions. These are the regions that may be selected
/// during the flow of control. `operands` is a set of optional attributes that
/// correspond to a constant value for each operand, or null if that operand is
/// not a constant.
void cir::GlobalOp::getSuccessorRegions(
    mlir::RegionBranchPoint point, SmallVectorImpl<RegionSuccessor> &regions) {
  // The `ctor` and `dtor` regions always branch back to the parent operation.
  if (!point.isParent()) {
    regions.push_back(RegionSuccessor(getOperation(), getOperation()->getResults()));
    return;
  }

  // Don't consider the ctor region if it is empty.
  Region *ctorRegion = &this->getCtorRegion();
  if (ctorRegion->empty())
    ctorRegion = nullptr;

  // Don't consider the dtor region if it is empty.
  Region *dtorRegion = &this->getDtorRegion();
  if (dtorRegion->empty())
    dtorRegion = nullptr;

  // If the condition isn't constant, both regions may be executed.
  if (ctorRegion)
    regions.push_back(RegionSuccessor(ctorRegion));
  if (dtorRegion)
    regions.push_back(RegionSuccessor(dtorRegion));
}

static void printGlobalOpTypeAndInitialValue(OpAsmPrinter &p, cir::GlobalOp op,
                                             TypeAttr type, Attribute initAttr,
                                             mlir::Region &ctorRegion,
                                             mlir::Region &dtorRegion) {
  auto printType = [&]() { p << ": " << type; };
  if (!op.isDeclaration()) {
    p << "= ";
    if (!ctorRegion.empty()) {
      p << "ctor ";
      printType();
      p << " ";
      p.printRegion(ctorRegion,
                    /*printEntryBlockArgs=*/false,
                    /*printBlockTerminators=*/false);
    } else {
      // This also prints the type...
      if (initAttr)
        printConstant(p, initAttr);
    }

    if (!dtorRegion.empty()) {
      p << " dtor ";
      p.printRegion(dtorRegion,
                    /*printEntryBlockArgs=*/false,
                    /*printBlockTerminators=*/false);
    }
  } else {
    printType();
  }
}

static ParseResult parseGlobalOpTypeAndInitialValue(OpAsmParser &parser,
                                                    TypeAttr &typeAttr,
                                                    Attribute &initialValueAttr,
                                                    mlir::Region &ctorRegion,
                                                    mlir::Region &dtorRegion) {
  mlir::Type opTy;
  if (parser.parseOptionalEqual().failed()) {
    // Absence of equal means a declaration, so we need to parse the type.
    //  cir.global @a : !cir.int<s, 32>
    if (parser.parseColonType(opTy))
      return failure();
  } else {
    // Parse contructor, example:
    //  cir.global @rgb = ctor : type { ... }
    if (!parser.parseOptionalKeyword("ctor")) {
      if (parser.parseColonType(opTy))
        return failure();
      auto parseLoc = parser.getCurrentLocation();
      if (parser.parseRegion(ctorRegion, /*arguments=*/{}, /*argTypes=*/{}))
        return failure();
      if (ensureRegionTerm(parser, ctorRegion, parseLoc).failed())
        return failure();
    } else {
      // Parse constant with initializer, examples:
      //  cir.global @y = 3.400000e+00 : f32
      //  cir.global @rgb = #cir.const_array<[...] : !cir.array<i8 x 3>>
      if (parseConstantValue(parser, initialValueAttr).failed())
        return failure();

      auto typedAttr = mlir::dyn_cast<mlir::TypedAttr>(initialValueAttr);
      if (!typedAttr) {
        parser.emitError(
            parser.getCurrentLocation(),
            "global initializer must be a typed constant attribute");
        return failure();
      }
      opTy = typedAttr.getType();
    }

    // Parse destructor, example:
    //   dtor { ... }
    if (!parser.parseOptionalKeyword("dtor")) {
      auto parseLoc = parser.getCurrentLocation();
      if (parser.parseRegion(dtorRegion, /*arguments=*/{}, /*argTypes=*/{}))
        return failure();
      if (ensureRegionTerm(parser, dtorRegion, parseLoc).failed())
        return failure();
    }
  }

  typeAttr = TypeAttr::get(opTy);
  return success();
}

//===----------------------------------------------------------------------===//
// GetGlobalOp
//===----------------------------------------------------------------------===//

LogicalResult
cir::GetGlobalOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  // Verify that the result type underlying pointer type matches the type of
  // the referenced cir.global, cir.func, or cir.alias op.
  mlir::Operation *op =
      lookupNearestOrModuleSymbolFastPath(*this, getNameAttr(), symbolTable);
  if (op == nullptr ||
      !(isa<GlobalOp>(op) || isa<FuncOp>(op) || isa<AliasOp>(op)))
    return emitOpError("'")
           << getName()
           << "' does not reference a valid cir.global, cir.func, or cir.alias";

  mlir::Type symTy;
  mlir::ptr::MemorySpaceAttrInterface symAddrSpaceAttr{};
  if (auto g = dyn_cast<GlobalOp>(op)) {
    symTy = g.getSymType();
    symAddrSpaceAttr = g.getAddrSpaceAttr();
    // Verify that for thread local global access, the global needs to
    // be marked with tls bits.
    if (getTls() && !g.getTlsModel())
      return emitOpError("access to global not marked thread local");
  } else if (auto f = dyn_cast<FuncOp>(op)) {
    symTy = f.getFunctionType();
    if (getTls())
      return emitOpError("thread local access is not valid for function symbol");
  } else if (auto a = dyn_cast<AliasOp>(op)) {
    symTy = a.getSymType();
    if (getTls())
      return emitOpError("thread local access is not valid for alias symbol");
  } else {
    llvm_unreachable("Unexpected operation for GetGlobalOp");
  }

  auto resultType = dyn_cast<PointerType>(getAddr().getType());
  if (!resultType)
    return emitOpError("result type is not a pointer type");
  // For function references, tolerate pointer type mismatches. CIR codegen
  // creates cir.get_global ops for functions early (e.g., forward declarations
  // with incomplete parameter types) and the function type may change later
  // (e.g., after template instantiation completes types). The lowering handles
  // these mismatches via bitcasts. This matches the call verifier
  // relaxation.
  if (!isa<FuncOp>(op) && symTy != resultType.getPointee())
    return emitOpError("result type pointee type '")
           << resultType.getPointee() << "' does not match type " << symTy
           << " of the global @" << getName();

  if (symAddrSpaceAttr != resultType.getAddrSpace()) {
    return emitOpError()
           << "result type address space does not match the address "
              "space of the global @"
           << getName();
  }

  return success();
}

//===----------------------------------------------------------------------===//
// VTableAddrPointOp
//===----------------------------------------------------------------------===//

/// Follow cir.alias links to the op that actually holds the definition.
///
/// An alias is a second name for one object, so an op that needs a cir.global
/// is satisfied by an alias to one -- cir.get_global already accepts that.
/// Clang reaches this routinely: under -fno-semantic-interposition a vtable is
/// emitted as `@_ZTV....local` with `@_ZTV...` aliased to it, and a TU that
/// only declares the vtable refers to the alias name.
///
/// Chains are followed up to a bound instead of tracking visited symbols: real
/// chains are one or two links, and the point of the bound is that a cyclic
/// alias must not hang the verifier. An unresolved or over-long chain returns
/// null and the caller reports it like any other bad reference.
static mlir::Operation *
resolveThroughAliases(mlir::Operation *user, mlir::Operation *sym,
                      SymbolTableCollection &symbolTable) {
  constexpr unsigned maxAliasHops = 8;
  for (unsigned hop = 0; sym && hop < maxAliasHops; ++hop) {
    auto alias = mlir::dyn_cast<cir::AliasOp>(sym);
    if (!alias)
      return sym;
    sym = lookupNearestOrModuleSymbolFastPath(user, alias.getAliaseeAttr(),
                                              symbolTable);
  }
  return nullptr;
}

LogicalResult
cir::VTableAddrPointOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  StringRef name = getName();

  // Verify that the result type underlying pointer type matches the type of
  // the referenced cir.global.
  cir::GlobalOp op =
      mlir::dyn_cast_or_null<cir::GlobalOp>(resolveThroughAliases(
          *this,
          lookupNearestOrModuleSymbolFastPath(*this, getNameAttr(),
                                              symbolTable),
          symbolTable));
  if (!op)
    return emitOpError("'")
           << name << "' does not reference a valid cir.global";
  std::optional<mlir::Attribute> init = op.getInitialValue();
  if (!init)
    return success();
  if (!isa<cir::VTableAttr>(*init))
    return emitOpError("Expected #cir.vtable in initializer for global '")
           << name << "'";
  return success();
}

//===----------------------------------------------------------------------===//
// VTTAddrPointOp
//===----------------------------------------------------------------------===//

LogicalResult
cir::VTTAddrPointOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  // VTT ptr is not coming from a symbol.
  if (!getName())
    return success();
  StringRef name = *getName();

  // Verify that the result type underlying pointer type matches the type of
  // the referenced cir.global op.
  cir::GlobalOp op =
      mlir::dyn_cast_or_null<cir::GlobalOp>(resolveThroughAliases(
          *this,
          lookupNearestOrModuleSymbolFastPath(*this, getNameAttr(),
                                              symbolTable),
          symbolTable));
  if (!op)
    return emitOpError("'")
           << name << "' does not reference a valid cir.global";
  std::optional<mlir::Attribute> init = op.getInitialValue();
  if (!init)
    return success();
  if (!isa<cir::ConstArrayAttr>(*init))
    return emitOpError(
               "Expected constant array in initializer for global VTT '")
           << name << "'";
  return success();
}

LogicalResult cir::VTTAddrPointOp::verify() {
  // The operation uses either a symbol or a value to operate, but not both
  if (getName() && getSymAddr())
    return emitOpError("should use either a symbol or value, but not both");

  // If not a symbol, stick with the concrete type used for getSymAddr.
  if (getSymAddr())
    return success();

  auto resultPtrTy = mlir::dyn_cast<cir::PointerType>(getAddr().getType());
  auto vttEntryPtrTy =
      resultPtrTy ? mlir::dyn_cast<cir::PointerType>(resultPtrTy.getPointee())
                  : cir::PointerType();
  if (!resultPtrTy || !vttEntryPtrTy ||
      !mlir::isa<cir::VoidType>(vttEntryPtrTy.getPointee()))
    return emitOpError("result type must be a pointer to a void pointer, but "
                       "provided result type is ")
           << getAddr().getType();
  return success();
}

//===----------------------------------------------------------------------===//
// FuncOp
//===----------------------------------------------------------------------===//

/// Returns the name used for the linkage attribute. This *must* correspond to
/// the name of the attribute in ODS.
static llvm::StringRef getLinkageAttrNameString() { return "linkage"; }

void cir::FuncOp::build(OpBuilder &builder, OperationState &result,
                        StringRef name, FuncType type,
                        GlobalLinkageKind linkage) {
  result.addRegion();
  result.addAttribute(SymbolTable::getSymbolAttrName(),
                      builder.getStringAttr(name));
  result.addAttribute(getFunctionTypeAttrName(result.name),
                      TypeAttr::get(type));
  result.addAttribute(
      getLinkageAttrNameString(),
      GlobalLinkageKindAttr::get(builder.getContext(), linkage));
  result.addAttribute(getGlobalVisibilityAttrName(result.name),
                      cir::VisibilityAttr::get(builder.getContext()));
}

ParseResult cir::FuncOp::parse(OpAsmParser &parser, OperationState &state) {
  llvm::SMLoc loc = parser.getCurrentLocation();
  mlir::Builder &builder = parser.getBuilder();

  mlir::StringAttr builtinNameAttr = getBuiltinAttrName(state.name);
  mlir::StringAttr coroutineNameAttr = getCoroutineAttrName(state.name);
  mlir::StringAttr inlineKindNameAttr = getInlineKindAttrName(state.name);
  mlir::StringAttr lambdaNameAttr = getLambdaAttrName(state.name);
  mlir::StringAttr noProtoNameAttr = getNoProtoAttrName(state.name);
  mlir::StringAttr visNameAttr = getSymVisibilityAttrName(state.name);
  mlir::StringAttr visibilityNameAttr = getGlobalVisibilityAttrName(state.name);
  mlir::StringAttr dsoLocalNameAttr = getDsoLocalAttrName(state.name);
  mlir::StringAttr specialMemberAttr = getCxxSpecialMemberAttrName(state.name);

  if (::mlir::succeeded(parser.parseOptionalKeyword(builtinNameAttr.strref())))
    state.addAttribute(builtinNameAttr, parser.getBuilder().getUnitAttr());
  if (::mlir::succeeded(
          parser.parseOptionalKeyword(coroutineNameAttr.strref())))
    state.addAttribute(coroutineNameAttr, parser.getBuilder().getUnitAttr());

  // Parse optional inline kind attribute
  cir::InlineKindAttr inlineKindAttr;
  if (failed(parseInlineKindAttr(parser, inlineKindAttr)))
    return failure();
  if (inlineKindAttr)
    state.addAttribute(inlineKindNameAttr, inlineKindAttr);

  if (::mlir::succeeded(parser.parseOptionalKeyword(lambdaNameAttr.strref())))
    state.addAttribute(lambdaNameAttr, parser.getBuilder().getUnitAttr());
  if (parser.parseOptionalKeyword(noProtoNameAttr).succeeded())
    state.addAttribute(noProtoNameAttr, parser.getBuilder().getUnitAttr());

  mlir::StringAttr optNoneNameAttr = getOptNoneAttrName(state.name);
  if (parser.parseOptionalKeyword("optnone").succeeded())
    state.addAttribute(optNoneNameAttr, parser.getBuilder().getUnitAttr());

  mlir::StringAttr coldNameAttr = getColdAttrName(state.name);
  if (parser.parseOptionalKeyword("cold").succeeded())
    state.addAttribute(coldNameAttr, parser.getBuilder().getUnitAttr());

  mlir::StringAttr comdatNameAttr = getComdatAttrName(state.name);
  bool parsedComdatKeyword = false;
  if (parser.parseOptionalKeyword("comdat").succeeded()) {
    state.addAttribute(comdatNameAttr, parser.getBuilder().getUnitAttr());
    parsedComdatKeyword = true;
  }

  // Default to external linkage if no keyword is provided.
  state.addAttribute(getLinkageAttrNameString(),
                     GlobalLinkageKindAttr::get(
                         parser.getContext(),
                         parseOptionalCIRKeyword<GlobalLinkageKind>(
                             parser, GlobalLinkageKind::ExternalLinkage)));

  ::llvm::StringRef visAttrStr;
  if (parser.parseOptionalKeyword(&visAttrStr, {"private", "public", "nested"})
          .succeeded()) {
    state.addAttribute(visNameAttr,
                       parser.getBuilder().getStringAttr(visAttrStr));
  }

  cir::VisibilityAttr cirVisibilityAttr;
  parseVisibilityAttr(parser, cirVisibilityAttr);
  state.addAttribute(visibilityNameAttr, cirVisibilityAttr);

  if (parser.parseOptionalKeyword(dsoLocalNameAttr).succeeded())
    state.addAttribute(dsoLocalNameAttr, parser.getBuilder().getUnitAttr());

  StringAttr nameAttr;
  if (parser.parseSymbolName(nameAttr, SymbolTable::getSymbolAttrName(),
                             state.attributes))
    return failure();
  llvm::SmallVector<OpAsmParser::Argument, 8> arguments;
  llvm::SmallVector<mlir::Type> resultTypes;
  llvm::SmallVector<DictionaryAttr> resultAttrs;
  bool isVariadic = false;
  if (function_interface_impl::parseFunctionSignatureWithArguments(
          parser, /*allowVariadic=*/true, arguments, isVariadic, resultTypes,
          resultAttrs))
    return failure();
  llvm::SmallVector<mlir::Type> argTypes;
  for (OpAsmParser::Argument &arg : arguments)
    argTypes.push_back(arg.type);

  if (resultTypes.size() > 1) {
    return parser.emitError(
        loc, "functions with multiple return types are not supported");
  }

  mlir::Type returnType =
      (resultTypes.empty() ? cir::VoidType::get(builder.getContext())
                           : resultTypes.front());

  cir::FuncType fnType = cir::FuncType::get(argTypes, returnType, isVariadic);
  if (!fnType)
    return failure();
  state.addAttribute(getFunctionTypeAttrName(state.name),
                     TypeAttr::get(fnType));

  // The annotations array is accepted here and again after cc(...), because
  // the printer emits it here -- ahead of alias/section/align/cc -- while
  // extra(...) is emitted last. Reading it in only one of the two places left
  // the other spelling unparseable.
  auto parseOptionalAnnotations = [&]() -> ParseResult {
    llvm::SMLoc annotationsLoc = parser.getCurrentLocation();
    mlir::ArrayAttr annotations;
    // Gated on '[' inside the parser, so a body or attribute dict is not
    // mistaken for an annotations array.
    OptionalParseResult parsed = parser.parseOptionalAttribute(annotations);
    if (!parsed.has_value())
      return success();
    if (failed(*parsed))
      return failure();
    if (state.attributes.get(getAnnotationsAttrName(state.name)))
      return parser.emitError(annotationsLoc,
                              "annotations already specified for this "
                              "function");
    state.addAttribute(getAnnotationsAttrName(state.name), annotations);
    return success();
  };
  if (parseOptionalAnnotations().failed())
    return failure();

  bool hasAlias = false;
  mlir::StringAttr aliaseeNameAttr = getAliaseeAttrName(state.name);
  if (parser.parseOptionalKeyword("alias").succeeded()) {
    if (parser.parseLParen().failed())
      return failure();
    mlir::StringAttr aliaseeAttr;
    if (parser.parseOptionalSymbolName(aliaseeAttr).failed())
      return failure();
    state.addAttribute(aliaseeNameAttr, FlatSymbolRefAttr::get(aliaseeAttr));
    if (parser.parseRParen().failed())
      return failure();
    hasAlias = true;
  }

  mlir::StringAttr personalityNameAttr = getPersonalityAttrName(state.name);
  if (parser.parseOptionalKeyword("personality").succeeded()) {
    if (parser.parseLParen().failed())
      return failure();
    mlir::StringAttr personalityAttr;
    if (parser.parseOptionalSymbolName(personalityAttr).failed())
      return failure();
    state.addAttribute(personalityNameAttr,
                       FlatSymbolRefAttr::get(personalityAttr));
    if (parser.parseRParen().failed())
      return failure();
  }

  // Parse optional section("...") attribute.
  mlir::StringAttr sectionNameAttr = getSectionAttrName(state.name);
  if (parser.parseOptionalKeyword("section").succeeded()) {
    if (parser.parseLParen().failed())
      return failure();
    std::string sectionStr;
    if (parser.parseString(&sectionStr).failed())
      return failure();
    state.addAttribute(sectionNameAttr,
                       mlir::StringAttr::get(parser.getContext(), sectionStr));
    if (parser.parseRParen().failed())
      return failure();
  }

  // Parse optional align(N) attribute.
  mlir::StringAttr alignmentNameAttr = getAlignmentAttrName(state.name);
  if (parser.parseOptionalKeyword("align").succeeded()) {
    if (parser.parseLParen().failed())
      return failure();
    int64_t alignVal;
    if (parser.parseInteger(alignVal).failed())
      return failure();
    state.addAttribute(alignmentNameAttr,
                       parser.getBuilder().getI64IntegerAttr(alignVal));
    if (parser.parseRParen().failed())
      return failure();
  }

  auto parseGlobalDtorCtor =
      [&](StringRef keyword,
          llvm::function_ref<void(std::optional<int> prio)> createAttr)
      -> mlir::LogicalResult {
    if (mlir::succeeded(parser.parseOptionalKeyword(keyword))) {
      std::optional<int> priority;
      if (mlir::succeeded(parser.parseOptionalLParen())) {
        auto parsedPriority = mlir::FieldParser<int>::parse(parser);
        if (mlir::failed(parsedPriority))
          return parser.emitError(parser.getCurrentLocation(),
                                  "failed to parse 'priority', of type 'int'");
        priority = parsedPriority.value_or(int());
        // Parse literal ')'
        if (parser.parseRParen())
          return failure();
      }
      createAttr(priority);
    }
    return success();
  };

  // Parse CXXSpecialMember attribute
  if (parser.parseOptionalKeyword("special_member").succeeded()) {
    cir::CXXCtorAttr ctorAttr;
    cir::CXXDtorAttr dtorAttr;
    cir::CXXAssignAttr assignAttr;
    if (parser.parseLess().failed())
      return failure();
    if (parser.parseOptionalAttribute(ctorAttr).has_value())
      state.addAttribute(specialMemberAttr, ctorAttr);
    else if (parser.parseOptionalAttribute(dtorAttr).has_value())
      state.addAttribute(specialMemberAttr, dtorAttr);
    else if (parser.parseOptionalAttribute(assignAttr).has_value())
      state.addAttribute(specialMemberAttr, assignAttr);
    if (parser.parseGreater().failed())
      return failure();
  }

  // cc(...) is accepted here and again after global_dtor, because the printer
  // emits it ahead of global_ctor/global_dtor while this parse read it only
  // after. A function carrying both spellings did not read back.
  mlir::StringAttr callingConvNameAttr = getCallingConvAttrName(state.name);
  cir::CallingConv callingConv = cir::CallingConv::C;
  bool hasCallingConv = false;
  auto parseOptionalCallingConv = [&]() -> ParseResult {
    // Taken before the keyword is consumed, so the caret lands on the
    // offending `cc` rather than just past it.
    llvm::SMLoc ccLoc = parser.getCurrentLocation();
    if (parser.parseOptionalKeyword("cc").failed())
      return success();
    // Consumed first, then rejected: short-circuiting on hasCallingConv would
    // leave a second `cc(` for the module parser and report it as an unknown
    // op instead of naming the duplicate.
    if (hasCallingConv)
      return parser.emitError(ccLoc, "cc already specified for this function");
    if (parser.parseLParen().failed())
      return failure();
    llvm::SMLoc convLoc = parser.getCurrentLocation();
    if (parseCIRKeyword<cir::CallingConv>(parser, callingConv).failed())
      return parser.emitError(convLoc) << "unknown calling convention";
    if (parser.parseRParen().failed())
      return failure();
    hasCallingConv = true;
    return success();
  };
  if (parseOptionalCallingConv().failed())
    return failure();

  if (parseGlobalDtorCtor("global_ctor", [&](std::optional<int> priority) {
        mlir::IntegerAttr globalCtorPriorityAttr =
            builder.getI32IntegerAttr(priority.value_or(65535));
        state.addAttribute(getGlobalCtorPriorityAttrName(state.name),
                           globalCtorPriorityAttr);
      }).failed())
    return failure();

  if (parseGlobalDtorCtor("global_dtor", [&](std::optional<int> priority) {
        mlir::IntegerAttr globalDtorPriorityAttr =
            builder.getI32IntegerAttr(priority.value_or(65535));
        state.addAttribute(getGlobalDtorPriorityAttrName(state.name),
                           globalDtorPriorityAttr);
      }).failed())
    return failure();

  if (parseOptionalCallingConv().failed())
    return failure();
  state.addAttribute(
      callingConvNameAttr,
      cir::CallingConvAttr::get(parser.getContext(), callingConv));

  // extra(...) and the annotations array, in either order. Only one of each is
  // accepted, so this is two attempts at extra(...) with an annotations array
  // allowed between them -- which covers both spellings without a loop.
  Attribute extraAttrs;
  auto parseOptionalExtra = [&]() -> ParseResult {
    // Taken before the keyword is consumed, so the caret lands on the
    // offending `extra` rather than just past it.
    llvm::SMLoc extraLoc = parser.getCurrentLocation();
    if (parser.parseOptionalKeyword("extra").failed())
      return success();
    // Consumed first, then rejected: short-circuiting on extraAttrs would
    // leave a second `extra(` for the module parser and reproduce the
    // "custom op 'extra' is unknown" message this parse exists to avoid.
    if (extraAttrs)
      return parser.emitError(extraLoc,
                              "extra already specified for this function");
    if (parser.parseLParen().failed())
      return failure();
    if (parser.parseAttribute(extraAttrs).failed())
      return failure();
    return parser.parseRParen();
  };
  if (parseOptionalExtra().failed())
    return failure();
  if (parseOptionalAnnotations().failed())
    return failure();
  if (parseOptionalExtra().failed())
    return failure();
  if (!extraAttrs) {
    mlir::NamedAttrList empty;
    extraAttrs = cir::ExtraFuncAttributesAttr::get(
        empty.getDictionary(builder.getContext()));
  }
  state.addAttribute(getExtraAttrsAttrName(state.name), extraAttrs);

  // Parse the rest of the attributes.
  if (parser.parseOptionalAttrDictWithKeyword(state.attributes))
    return failure();

  // Add argument and result attributes to the function.
  assert(resultAttrs.size() == resultTypes.size());
  call_interface_impl::addArgAndResultAttrs(
      builder, state, arguments, resultAttrs, getArgAttrsAttrName(state.name),
      getResAttrsAttrName(state.name));

  // Reject attributes that should be specified as keywords, not in the
  // explicit attribute list.
  if (!parsedComdatKeyword && state.attributes.get("comdat"))
    return parser.emitError(loc)
           << "custom op 'cir.func' attribute 'comdat' should not be "
              "specified in the explicit attribute list";

  // Parse the optional function body.
  auto *body = state.addRegion();
  OptionalParseResult parseResult = parser.parseOptionalRegion(
      *body, arguments, /*enableNameShadowing=*/false);
  if (parseResult.has_value()) {
    if (hasAlias)
      return parser.emitError(loc, "function alias shall not have a body");
    if (failed(*parseResult))
      return failure();
    // Function body was parsed, make sure its not empty.
    if (body->empty())
      return parser.emitError(loc, "expected non-empty function body");
  }

  return success();
}

// This function corresponds to `llvm::GlobalValue::isDeclaration` and should
// have a similar implementation. We don't currently ifuncs or materializable
// functions, but those should be handled here as they are implemented.
bool cir::FuncOp::isDeclaration() {
  std::optional<StringRef> aliasee = getAliasee();
  if (!aliasee)
    return getFunctionBody().empty();

  // Aliases are always definitions.
  return false;
}

bool cir::FuncOp::isCXXSpecialMemberFunction() {
  return getCxxSpecialMemberAttr() != nullptr;
}

bool cir::FuncOp::isCxxConstructor() {
  auto attr = getCxxSpecialMemberAttr();
  return attr && dyn_cast<CXXCtorAttr>(attr);
}

bool cir::FuncOp::isCxxDestructor() {
  auto attr = getCxxSpecialMemberAttr();
  return attr && dyn_cast<CXXDtorAttr>(attr);
}

bool cir::FuncOp::isCxxSpecialAssignment() {
  auto attr = getCxxSpecialMemberAttr();
  return attr && dyn_cast<CXXAssignAttr>(attr);
}

std::optional<CtorKind> cir::FuncOp::getCxxConstructorKind() {
  mlir::Attribute attr = getCxxSpecialMemberAttr();
  if (attr) {
    if (auto ctor = dyn_cast<CXXCtorAttr>(attr))
      return ctor.getCtorKind();
  }
  return std::nullopt;
}

std::optional<AssignKind> cir::FuncOp::getCxxSpecialAssignKind() {
  mlir::Attribute attr = getCxxSpecialMemberAttr();
  if (attr) {
    if (auto assign = dyn_cast<CXXAssignAttr>(attr))
      return assign.getAssignKind();
  }
  return std::nullopt;
}

bool cir::FuncOp::isCxxTrivialMemberFunction() {
  mlir::Attribute attr = getCxxSpecialMemberAttr();
  if (attr) {
    if (auto ctor = dyn_cast<CXXCtorAttr>(attr))
      return ctor.getIsTrivial();
    if (auto dtor = dyn_cast<CXXDtorAttr>(attr))
      return dtor.getIsTrivial();
    if (auto assign = dyn_cast<CXXAssignAttr>(attr))
      return assign.getIsTrivial();
  }
  return false;
}

mlir::Region *cir::FuncOp::getCallableRegion() {
  // An alias and an external declaration have no body to call into. Saying so
  // keeps them out of MLIR's call graph as callables: the inliner erases a
  // private callable it sees no use of, and an alias is private and named
  // only by its own symbol.
  if (getAliasee() || getFunctionBody().empty())
    return nullptr;
  return &getBody();
}

void cir::FuncOp::print(OpAsmPrinter &p) {
  if (getBuiltin())
    p << " builtin";

  if (getCoroutine())
    p << " coroutine";

  printInlineKindAttr(p, getInlineKindAttr());

  if (getLambda())
    p << " lambda";

  if (getNoProto())
    p << " no_proto";

  if (getOptNone())
    p << " optnone";

  if (getCold())
    p << " cold";

  if (getComdat())
    p << " comdat";

  if (getLinkage() != GlobalLinkageKind::ExternalLinkage)
    p << ' ' << stringifyGlobalLinkageKind(getLinkage());

  mlir::SymbolTable::Visibility vis = getVisibility();
  if (vis != mlir::SymbolTable::Visibility::Public)
    p << ' ' << vis;

  cir::VisibilityAttr cirVisibilityAttr = getGlobalVisibilityAttr();
  if (!cirVisibilityAttr.isDefault()) {
    p << ' ';
    printVisibilityAttr(p, cirVisibilityAttr);
  }

  if (getDsoLocal())
    p << " dso_local";

  p << ' ';
  p.printSymbolName(getSymName());
  cir::FuncType fnType = getFunctionType();
  function_interface_impl::printFunctionSignature(
      p, *this, fnType.getInputs(), fnType.isVarArg(), fnType.getReturnTypes());

  if (mlir::ArrayAttr annotations = getAnnotationsAttr()) {
    p << ' ';
    p.printAttribute(annotations);
  }

  if (std::optional<StringRef> aliaseeName = getAliasee()) {
    p << " alias(";
    p.printSymbolName(*aliaseeName);
    p << ")";
  }

  if (std::optional<StringRef> personalityName = getPersonality()) {
    p << " personality(";
    p.printSymbolName(*personalityName);
    p << ")";
  }

  if (auto sect = getSection()) {
    p << " section(\"";
    llvm::printEscapedString(*sect, p.getStream());
    p << "\")";
  }

  if (auto alignment = getAlignment()) {
    p << " align(" << *alignment << ")";
  }

  if (auto specialMemberAttr = getCxxSpecialMember()) {
    p << " special_member<";
    p.printAttribute(*specialMemberAttr);
    p << '>';
  }

  if (getCallingConv() != cir::CallingConv::C) {
    p << " cc(";
    p << stringifyCallingConv(getCallingConv());
    p << ")";
  }

  if (auto globalCtorPriority = getGlobalCtorPriority()) {
    p << " global_ctor";
    if (globalCtorPriority.value() != 65535)
      p << "(" << globalCtorPriority.value() << ")";
  }

  if (auto globalDtorPriority = getGlobalDtorPriority()) {
    p << " global_dtor";
    if (globalDtorPriority.value() != 65535)
      p << "(" << globalDtorPriority.value() << ")";
  }

  // Print extra(...) before the generic attribute dict, matching the parser's
  // expected ordering: extra(...) followed by attributes { ... }.
  if (auto extraAttrs = getExtraAttrs()) {
    if (!extraAttrs->getElements().empty()) {
      p << " extra(";
      p.printAttributeWithoutType(*extraAttrs);
      p << ")";
    }
  }

  function_interface_impl::printFunctionAttributes(
      p, *this, cir::FuncOp::getAttributeNames());

  // Print the body if this is not an external function.
  Region &body = getOperation()->getRegion(0);
  if (!body.empty()) {
    p << ' ';
    p.printRegion(body, /*printEntryBlockArgs=*/false,
                  /*printBlockTerminators=*/true);
  }
}

mlir::LogicalResult cir::FuncOp::verify() {
  llvm::DenseSet<llvm::StringRef> labels;
  llvm::SmallVector<llvm::StringRef, 16> gotos;
  llvm::SmallVector<llvm::StringRef, 16> blockAddresses;
  bool invalidBlockAddress = false;
  getOperation()->walk([&](mlir::Operation *op) {
    if (auto lab = dyn_cast<cir::LabelOp>(op)) {
      labels.insert(lab.getLabel());
    } else if (auto goTo = dyn_cast<cir::GotoOp>(op)) {
      gotos.push_back(goTo.getLabel());
    } else if (auto blkAdd = dyn_cast<cir::BlockAddressOp>(op)) {
      if (blkAdd.getBlockAddrInfoAttr().getFunc().getAttr() != getSymName()) {
        // Stop the walk early, no need to continue
        invalidBlockAddress = true;
        return mlir::WalkResult::interrupt();
      }
      blockAddresses.push_back(blkAdd.getBlockAddrInfoAttr().getLabel());
    }
    return mlir::WalkResult::advance();
  });

  if (invalidBlockAddress)
    return emitOpError() << "blockaddress references a different function";

  if (!gotos.empty()) {
    for (auto gotoLabel : gotos) {
      if (!labels.contains(gotoLabel))
        return emitOpError() << "goto/label mismatch";
    }
  }

  if (!blockAddresses.empty()) {
    for (auto blockAddrLabel : blockAddresses) {
      if (!labels.contains(blockAddrLabel))
        return emitOpError()
               << "expects an existing label target in the referenced function";
    }
  }

  return success();
}

//===----------------------------------------------------------------------===//
// BinOp
//===----------------------------------------------------------------------===//
LogicalResult cir::BinOp::verify() {
  bool noWrap = getNoUnsignedWrap() || getNoSignedWrap();
  bool saturated = getSaturated();

  if (!isa<cir::IntType>(getType()) && noWrap)
    return emitError()
           << "only operations on integer values may have nsw/nuw flags";

  bool noWrapOps = getKind() == cir::BinOpKind::Add ||
                   getKind() == cir::BinOpKind::Sub ||
                   getKind() == cir::BinOpKind::Mul;

  bool saturatedOps =
      getKind() == cir::BinOpKind::Add || getKind() == cir::BinOpKind::Sub;

  if (noWrap && !noWrapOps)
    return emitError() << "The nsw/nuw flags are applicable to opcodes: 'add', "
                          "'sub' and 'mul'";
  if (saturated && !saturatedOps)
    return emitError() << "The saturated flag is applicable to opcodes: 'add' "
                          "and 'sub'";
  if (noWrap && saturated)
    return emitError() << "The nsw/nuw flags and the saturated flag are "
                          "mutually exclusive";

  return mlir::success();
}

//===----------------------------------------------------------------------===//
// TernaryOp
//===----------------------------------------------------------------------===//

/// Given the region at `point`, or the parent operation if `point` is None,
/// return the successor regions. These are the regions that may be selected
/// during the flow of control. `operands` is a set of optional attributes that
/// correspond to a constant value for each operand, or null if that operand is
/// not a constant.
void cir::TernaryOp::getSuccessorRegions(
    mlir::RegionBranchPoint point, SmallVectorImpl<RegionSuccessor> &regions) {
  // The `true` and the `false` region branch back to the parent operation.
  if (!point.isParent()) {
    regions.push_back(RegionSuccessor(getOperation(), getOperation()->getResults()));
    return;
  }

  // When branching from the parent operation, both the true and false
  // regions are considered possible successors
  regions.push_back(RegionSuccessor(&getTrueRegion()));
  regions.push_back(RegionSuccessor(&getFalseRegion()));
}

void cir::TernaryOp::build(
    OpBuilder &builder, OperationState &result, Value cond,
    function_ref<void(OpBuilder &, Location)> trueBuilder,
    function_ref<void(OpBuilder &, Location)> falseBuilder) {
  result.addOperands(cond);
  OpBuilder::InsertionGuard guard(builder);
  Region *trueRegion = result.addRegion();
  builder.createBlock(trueRegion);
  trueBuilder(builder, result.location);
  Region *falseRegion = result.addRegion();
  builder.createBlock(falseRegion);
  falseBuilder(builder, result.location);

  // Get result type from whichever branch has a yield (the other may have
  // unreachable from a throw expression, or the yield may be deferred)
  cir::YieldOp yield;
  if (trueRegion->back().mightHaveTerminator())
    yield = dyn_cast<cir::YieldOp>(trueRegion->back().getTerminator());
  if (!yield && falseRegion->back().mightHaveTerminator())
    yield = dyn_cast<cir::YieldOp>(falseRegion->back().getTerminator());

  // If no yield was found (e.g. yields are deferred), the ternary has no
  // result type.
  if (yield && yield.getNumOperands() == 1)
    result.addTypes(TypeRange{yield.getOperandTypes().front()});
}

//===----------------------------------------------------------------------===//
// SelectOp
//===----------------------------------------------------------------------===//

OpFoldResult cir::SelectOp::fold(FoldAdaptor adaptor) {
  mlir::Attribute condition = adaptor.getCondition();
  if (condition) {
    bool conditionValue = mlir::cast<cir::BoolAttr>(condition).getValue();
    return conditionValue ? getTrueValue() : getFalseValue();
  }

  // cir.select if %0 then x else x -> x
  mlir::Attribute trueValue = adaptor.getTrueValue();
  mlir::Attribute falseValue = adaptor.getFalseValue();
  if (trueValue == falseValue)
    return trueValue;
  if (getTrueValue() == getFalseValue())
    return getTrueValue();

  return {};
}

LogicalResult cir::SelectOp::verify() {
  // AllTypesMatch already guarantees trueVal and falseVal have matching types.
  auto condTy = dyn_cast<cir::VectorType>(getCondition().getType());

  // If condition is not a vector, no further checks are needed.
  if (!condTy)
    return success();

  // When condition is a vector, both other operands must also be vectors.
  if (!isa<cir::VectorType>(getTrueValue().getType()) ||
      !isa<cir::VectorType>(getFalseValue().getType())) {
    return emitOpError()
           << "expected both true and false operands to be vector types "
              "when the condition is a vector boolean type";
  }

  return success();
}

//===----------------------------------------------------------------------===//
// ShiftOp
//===----------------------------------------------------------------------===//
LogicalResult cir::ShiftOp::verify() {
  mlir::Operation *op = getOperation();
  auto op0VecTy = mlir::dyn_cast<cir::VectorType>(op->getOperand(0).getType());
  auto op1VecTy = mlir::dyn_cast<cir::VectorType>(op->getOperand(1).getType());
  if (!op0VecTy ^ !op1VecTy)
    return emitOpError() << "input types cannot be one vector and one scalar";

  if (op0VecTy) {
    if (op0VecTy.getSize() != op1VecTy.getSize())
      return emitOpError() << "input vector types must have the same size";

    auto opResultTy = mlir::dyn_cast<cir::VectorType>(getType());
    if (!opResultTy)
      return emitOpError() << "the type of the result must be a vector "
                           << "if it is vector shift";

    auto op0VecEleTy = mlir::cast<cir::IntType>(op0VecTy.getElementType());
    auto op1VecEleTy = mlir::cast<cir::IntType>(op1VecTy.getElementType());
    if (op0VecEleTy.getWidth() != op1VecEleTy.getWidth())
      return emitOpError()
             << "vector operands do not have the same elements sizes";

    auto resVecEleTy = mlir::cast<cir::IntType>(opResultTy.getElementType());
    if (op0VecEleTy.getWidth() != resVecEleTy.getWidth())
      return emitOpError() << "vector operands and result type do not have the "
                              "same elements sizes";
  }

  return mlir::success();
}

//===----------------------------------------------------------------------===//
// LabelOp Definitions
//===----------------------------------------------------------------------===//

LogicalResult cir::LabelOp::verify() {
  // During CIR-to-CIR transformations (FlattenCFG), labels may temporarily
  // not be at block front due to scope inlining. GotoSolver will split
  // blocks at label positions to restore this invariant before LLVM lowering.
  // Relaxing this check allows FlattenCFG to process complex goto patterns
  // (e.g., goto inside compound statement followed by label) without
  // verification failures between pattern applications.
  return mlir::success();
}

//===----------------------------------------------------------------------===//
// UnaryOp
//===----------------------------------------------------------------------===//

LogicalResult cir::UnaryOp::verify() {
  switch (getKind()) {
  case cir::UnaryOpKind::Inc:
  case cir::UnaryOpKind::Dec:
  case cir::UnaryOpKind::Plus:
  case cir::UnaryOpKind::Minus:
  case cir::UnaryOpKind::Not:
    // Nothing to verify.
    return success();
  }

  llvm_unreachable("Unknown UnaryOp kind?");
}

static bool isBoolNot(cir::UnaryOp op) {
  return isa<cir::BoolType>(op.getInput().getType()) &&
         op.getKind() == cir::UnaryOpKind::Not;
}

// This folder simplifies the sequential boolean not operations.
// For instance, the next two unary operations will be eliminated:
//
// ```mlir
// %1 = cir.unary(not, %0) : !cir.bool, !cir.bool
// %2 = cir.unary(not, %1) : !cir.bool, !cir.bool
// ```
//
// and the argument of the first one (%0) will be used instead.
OpFoldResult cir::UnaryOp::fold(FoldAdaptor adaptor) {
  if (auto poison =
          mlir::dyn_cast_if_present<cir::PoisonAttr>(adaptor.getInput())) {
    // Propagate poison values
    return poison;
  }

  if (isBoolNot(*this))
    if (auto previous = getInput().getDefiningOp<cir::UnaryOp>())
      if (isBoolNot(previous))
        return previous.getInput();

  // Avoid introducing unnecessary duplicate constants in cases where we are
  // just folding the operation to its input value. If we return the
  // input attribute from the adapter, a new constant is materialized, but
  // if we return the input value directly, it avoids that.
  if (auto srcConst = getInput().getDefiningOp<cir::ConstantOp>()) {
    if (getKind() == cir::UnaryOpKind::Plus ||
        (mlir::isa<cir::BoolType>(srcConst.getType()) &&
         getKind() == cir::UnaryOpKind::Minus))
      return srcConst.getResult();
  }

  // Fold unary operations with constant inputs. If the input is a ConstantOp,
  // it "folds" to its value attribute. If it was some other operation that
  // was folded, it will be an mlir::Attribute that hasn't yet been
  // materialized. If it was a value that couldn't be folded, it will be null.
  if (mlir::Attribute attr = adaptor.getInput()) {
    // For now, we only attempt to fold simple scalar values.
    OpFoldResult result =
        llvm::TypeSwitch<mlir::Attribute, OpFoldResult>(attr)
            .Case<cir::IntAttr>([&](cir::IntAttr attrT) {
              switch (getKind()) {
              case cir::UnaryOpKind::Not: {
                APInt val = attrT.getValue();
                val.flipAllBits();
                return cir::IntAttr::get(getType(), val);
              }
              case cir::UnaryOpKind::Plus:
                return attrT;
              case cir::UnaryOpKind::Minus: {
                APInt val = attrT.getValue();
                val.negate();
                return cir::IntAttr::get(getType(), val);
              }
              default:
                return cir::IntAttr{};
              }
            })
            .Case<cir::FPAttr>([&](cir::FPAttr attrT) {
              switch (getKind()) {
              case cir::UnaryOpKind::Plus:
                return attrT;
              case cir::UnaryOpKind::Minus: {
                APFloat val = attrT.getValue();
                val.changeSign();
                return cir::FPAttr::get(getType(), val);
              }
              default:
                return cir::FPAttr{};
              }
            })
            .Case<cir::BoolAttr>([&](cir::BoolAttr attrT) {
              switch (getKind()) {
              case cir::UnaryOpKind::Not:
                return cir::BoolAttr::get(getContext(), !attrT.getValue());
              case cir::UnaryOpKind::Plus:
              case cir::UnaryOpKind::Minus:
                return attrT;
              default:
                return cir::BoolAttr{};
              }
            })
            .Default([&](auto attrT) { return mlir::Attribute{}; });
    if (result)
      return result;
  }

  return {};
}

//===----------------------------------------------------------------------===//
// BaseDataMemberOp & DerivedDataMemberOp
//===----------------------------------------------------------------------===//

static LogicalResult verifyMemberPtrCast(Operation *op, mlir::Value src,
                                         mlir::Type resultTy) {
  // Let the operand type be T1 C1::*, let the result type be T2 C2::*.
  // Verify that T1 and T2 are the same type.
  mlir::Type inputMemberTy;
  mlir::Type resultMemberTy;
  if (mlir::isa<cir::DataMemberType>(src.getType())) {
    inputMemberTy =
        mlir::cast<cir::DataMemberType>(src.getType()).getMemberTy();
    resultMemberTy = mlir::cast<cir::DataMemberType>(resultTy).getMemberTy();
  }
  assert(!cir::MissingFeatures::memberFuncPtrCast());
  if (inputMemberTy != resultMemberTy)
    return op->emitOpError()
           << "member types of the operand and the result do not match";

  return mlir::success();
}

LogicalResult cir::BaseDataMemberOp::verify() {
  return verifyMemberPtrCast(getOperation(), getSrc(), getType());
}

LogicalResult cir::DerivedDataMemberOp::verify() {
  return verifyMemberPtrCast(getOperation(), getSrc(), getType());
}

//===----------------------------------------------------------------------===//
// AwaitOp
//===----------------------------------------------------------------------===//

void cir::AwaitOp::build(OpBuilder &builder, OperationState &result,
                         cir::AwaitKind kind, BuilderCallbackRef readyBuilder,
                         BuilderCallbackRef suspendBuilder,
                         BuilderCallbackRef resumeBuilder,
                         BuilderCallbackRef cleanupBuilder) {
  result.addAttribute(getKindAttrName(result.name),
                      cir::AwaitKindAttr::get(builder.getContext(), kind));
  {
    OpBuilder::InsertionGuard guard(builder);
    Region *readyRegion = result.addRegion();
    builder.createBlock(readyRegion);
    readyBuilder(builder, result.location);
  }

  {
    OpBuilder::InsertionGuard guard(builder);
    Region *suspendRegion = result.addRegion();
    builder.createBlock(suspendRegion);
    suspendBuilder(builder, result.location);
  }

  {
    OpBuilder::InsertionGuard guard(builder);
    Region *resumeRegion = result.addRegion();
    builder.createBlock(resumeRegion);
    resumeBuilder(builder, result.location);
  }

  // Optional cleanup/destroy region. The region slot is always added so the op
  // matches its 4-region declaration and getCleanup() is always valid, but it
  // is left empty (0 blocks) when no cleanupBuilder is supplied. An empty
  // cleanup region is omitted by the assembly format and treated by FlattenCFG
  // as "no live local cleanups on the destroy edge" (prior behaviour).
  {
    OpBuilder::InsertionGuard guard(builder);
    Region *cleanupRegion = result.addRegion();
    if (cleanupBuilder) {
      builder.createBlock(cleanupRegion);
      cleanupBuilder(builder, result.location);
    }
  }
}

void cir::AwaitOp::getSuccessorRegions(
    mlir::RegionBranchPoint point, SmallVectorImpl<RegionSuccessor> &regions) {
  // If any index all the underlying regions branch back to the parent
  // operation.
  if (!point.isParent()) {
    regions.push_back(RegionSuccessor(getOperation(), getOperation()->getResults()));
    return;
  }

  // TODO: retrieve information from the promise and only push the
  // necessary ones. Example: `std::suspend_never` on initial or final
  // await's might allow suspend region to be skipped.
  regions.push_back(RegionSuccessor(&this->getReady()));
  regions.push_back(RegionSuccessor(&this->getSuspend()));
  regions.push_back(RegionSuccessor(&this->getResume()));
  // The cleanup region is optional; only branch into it when it has a body.
  if (!this->getCleanup().empty())
    regions.push_back(RegionSuccessor(&this->getCleanup()));
}

LogicalResult cir::AwaitOp::verify() {
  if (!isa<ConditionOp>(this->getReady().back().getTerminator()))
    return emitOpError("ready region must end with cir.condition");
  if (!this->getCleanup().empty() &&
      !isa<YieldOp>(this->getCleanup().back().getTerminator()))
    return emitOpError("cleanup region must end with cir.yield");
  return success();
}

//===----------------------------------------------------------------------===//
// CopyOp Definitions
//===----------------------------------------------------------------------===//

// Assembly format:
//   cir.copy %src to %dst [volatile] {attrs} : !src_ptr_ty [, !dst_ptr_ty]
//
// When src and dst pointer types are the same, only one type is printed:
//   cir.copy %0 to %1 : !cir.ptr<!record_ty>
//
// When they differ (e.g. different address spaces), both types are printed:
//   cir.copy %0 to %1 : !cir.ptr<!record_ty, addrspace(2)>, !cir.ptr<!record_ty>

void cir::CopyOp::print(OpAsmPrinter &p) {
  p << ' ' << getSrc() << " to " << getDst();
  if (getIsVolatile())
    p << " volatile";
  p.printOptionalAttrDict((*this)->getAttrs(),
                          {"is_volatile"});
  p << " : " << getSrc().getType();
  if (getSrc().getType() != getDst().getType())
    p << ", " << getDst().getType();
}

ParseResult cir::CopyOp::parse(OpAsmParser &parser, OperationState &result) {
  OpAsmParser::UnresolvedOperand src, dst;
  mlir::Type srcType, dstType;

  if (parser.parseOperand(src) || parser.parseKeyword("to") ||
      parser.parseOperand(dst))
    return mlir::failure();

  // Parse optional 'volatile'.
  if (succeeded(parser.parseOptionalKeyword("volatile")))
    result.addAttribute("is_volatile", UnitAttr::get(parser.getContext()));

  if (parser.parseOptionalAttrDict(result.attributes) || parser.parseColon() ||
      parser.parseType(srcType))
    return mlir::failure();

  // Parse optional second type for different address spaces.
  if (succeeded(parser.parseOptionalComma())) {
    if (parser.parseType(dstType))
      return mlir::failure();
  } else {
    dstType = srcType;
  }

  if (parser.resolveOperand(dst, dstType, result.operands) ||
      parser.resolveOperand(src, srcType, result.operands))
    return mlir::failure();

  return mlir::success();
}

LogicalResult cir::CopyOp::verify() {
  // A data layout is required for us to know the number of bytes to be copied.
  if (!getDst().getType().getPointee().hasTrait<DataLayoutTypeInterface::Trait>())
    return emitError() << "missing data layout for pointee type";

  // The pointee types must match (ignoring address space).
  if (getSrc().getType().getPointee() != getDst().getType().getPointee())
    return emitError() << "pointee type of source and destination must match";

  // Volatile self-copies are observable side effects (e.g., MMIO) and must
  // not be elided, so only reject non-volatile same-address copies.
  if (getSrc() == getDst() && !getIsVolatile())
    return emitError() << "source and destination are the same";

  return mlir::success();
}

//===----------------------------------------------------------------------===//
// GetRuntimeMemberOp Definitions
//===----------------------------------------------------------------------===//

LogicalResult cir::GetRuntimeMemberOp::verify() {
  auto recordTy = mlir::cast<RecordType>(getAddr().getType().getPointee());
  cir::DataMemberType memberPtrTy = getMember().getType();

  if (recordTy != memberPtrTy.getClassTy())
    return emitError() << "record type does not match the member pointer type";
  if (getType().getPointee() != memberPtrTy.getMemberTy())
    return emitError() << "result type does not match the member pointer type";
  return mlir::success();
}

//===----------------------------------------------------------------------===//
// GetMethodOp Definitions
//===----------------------------------------------------------------------===//

LogicalResult cir::GetMethodOp::verify() {
  cir::MethodType methodTy = getMethod().getType();

  // Assume objectTy is !cir.ptr<!T>
  cir::PointerType objectPtrTy = getObject().getType();
  mlir::Type objectTy = objectPtrTy.getPointee();

  if (methodTy.getClassTy() != objectTy)
    return emitError() << "method class type and object type do not match";

  // Assume methodFuncTy is !cir.func<!Ret (!Args)>
  auto calleeTy = mlir::cast<cir::FuncType>(getCallee().getType().getPointee());
  cir::FuncType methodFuncTy = methodTy.getMemberFuncTy();

  // We verify at here that calleeTy is !cir.func<!Ret (!cir.ptr<!void>, !Args)>
  // Note that the first parameter type of the callee is !cir.ptr<!void> instead
  // of !cir.ptr<!T> because the "this" pointer may be adjusted before calling
  // the callee.

  if (methodFuncTy.getReturnType() != calleeTy.getReturnType())
    return emitError()
           << "method return type and callee return type do not match";

  llvm::ArrayRef<mlir::Type> calleeArgsTy = calleeTy.getInputs();
  llvm::ArrayRef<mlir::Type> methodFuncArgsTy = methodFuncTy.getInputs();

  if (calleeArgsTy.empty())
    return emitError() << "callee parameter list lacks receiver object ptr";

  auto calleeThisArgPtrTy = mlir::dyn_cast<cir::PointerType>(calleeArgsTy[0]);
  if (!calleeThisArgPtrTy ||
      !mlir::isa<cir::VoidType>(calleeThisArgPtrTy.getPointee())) {
    return emitError()
           << "the first parameter of callee must be a void pointer";
  }

  if (calleeArgsTy.slice(1) != methodFuncArgsTy)
    return emitError()
           << "callee parameters and method parameters do not match";

  return mlir::success();
}

//===----------------------------------------------------------------------===//
// GetMemberOp Definitions
//===----------------------------------------------------------------------===//

LogicalResult cir::GetMemberOp::verify() {
  const auto recordTy = dyn_cast<RecordType>(getAddrTy().getPointee());
  if (!recordTy)
    return emitError() << "expected pointer to a record type";

  if (recordTy.getMembers().size() <= getIndex())
    return emitError() << "member index out of bounds";

  if (recordTy.getMembers()[getIndex()] != getType().getPointee())
    return emitError() << "member type mismatch";

  return mlir::success();
}

//===----------------------------------------------------------------------===//
// ExtractMemberOp Definitions
//===----------------------------------------------------------------------===//

LogicalResult cir::ExtractMemberOp::verify() {
  // Allow both RecordType and ArrayType operands. ArrayType can appear
  // when ABI coercion converts a member pointer struct to an array
  // (e.g., {i64, i64} → array<u64, 2> on AArch64).
  if (auto arrayTy = mlir::dyn_cast<cir::ArrayType>(getRecord().getType())) {
    if (arrayTy.getSize() <= getIndex())
      return emitError() << "array index out of bounds";
    return mlir::success();
  }
  auto recordTy = mlir::dyn_cast<cir::RecordType>(getRecord().getType());
  if (!recordTy)
    return emitError() << "operand must be CIR record or array type";
  if (recordTy.getKind() == cir::RecordType::Union)
    return emitError()
           << "cir.extract_member currently does not support unions";
  if (recordTy.getMembers().size() <= getIndex())
    return emitError() << "member index out of bounds";
  if (recordTy.getMembers()[getIndex()] != getType())
    return emitError() << "member type mismatch";
  return mlir::success();
}

//===----------------------------------------------------------------------===//
// InsertMemberOp Definitions
//===----------------------------------------------------------------------===//

LogicalResult cir::InsertMemberOp::verify() {
  auto recordTy = mlir::cast<cir::RecordType>(getRecord().getType());
  if (recordTy.getKind() == cir::RecordType::Union)
    return emitError() << "cir.insert_member currently does not support unions";
  if (recordTy.getMembers().size() <= getIndex())
    return emitError() << "member index out of bounds";
  if (recordTy.getMembers()[getIndex()] != getValue().getType())
    return emitError() << "member type mismatch";
  // The op trait already checks that the types of $result and $record match.
  return mlir::success();
}

//===----------------------------------------------------------------------===//
// VecCreateOp
//===----------------------------------------------------------------------===//

OpFoldResult cir::VecCreateOp::fold(FoldAdaptor adaptor) {
  if (llvm::any_of(getElements(), [](mlir::Value value) {
        return !value.getDefiningOp<cir::ConstantOp>();
      }))
    return {};

  return cir::ConstVectorAttr::get(
      getType(), mlir::ArrayAttr::get(getContext(), adaptor.getElements()));
}

LogicalResult cir::VecCreateOp::verify() {
  // Verify that the number of arguments matches the number of elements in the
  // vector, and that the type of all the arguments matches the type of the
  // elements in the vector.
  const cir::VectorType vecTy = getType();
  if (getElements().size() != vecTy.getSize()) {
    return emitOpError() << "operand count of " << getElements().size()
                         << " doesn't match vector type " << vecTy
                         << " element count of " << vecTy.getSize();
  }

  const mlir::Type elementType = vecTy.getElementType();
  for (const mlir::Value element : getElements()) {
    if (element.getType() != elementType) {
      return emitOpError() << "operand type " << element.getType()
                           << " doesn't match vector element type "
                           << elementType;
    }
  }

  return success();
}

//===----------------------------------------------------------------------===//
// VecExtractOp
//===----------------------------------------------------------------------===//

OpFoldResult cir::VecExtractOp::fold(FoldAdaptor adaptor) {
  const auto vectorAttr =
      llvm::dyn_cast_if_present<cir::ConstVectorAttr>(adaptor.getVec());
  if (!vectorAttr)
    return {};

  const auto indexAttr =
      llvm::dyn_cast_if_present<cir::IntAttr>(adaptor.getIndex());
  if (!indexAttr)
    return {};

  const mlir::ArrayAttr elements = vectorAttr.getElts();
  const uint64_t index = indexAttr.getUInt();
  if (index >= elements.size())
    return {};

  return elements[index];
}

//===----------------------------------------------------------------------===//
// CmpOp
//===----------------------------------------------------------------------===//

LogicalResult cir::CmpOp::verify() {
  auto kind = getKind();
  if (kind == cir::CmpOpKind::fone || kind == cir::CmpOpKind::funo ||
      kind == cir::CmpOpKind::fugt || kind == cir::CmpOpKind::fuge ||
      kind == cir::CmpOpKind::fult || kind == cir::CmpOpKind::fule ||
      kind == cir::CmpOpKind::ford || kind == cir::CmpOpKind::fueq) {
    if (!mlir::isa<cir::SingleType, cir::DoubleType, cir::LongDoubleType,
                   cir::FP16Type, cir::FP80Type, cir::FP128Type, cir::BF16Type>(
            getLhs().getType()))
      return emitOpError(
          "floating point comparison predicate requires floating point operands");
  }
  return success();
}

//===----------------------------------------------------------------------===//
// VecCmpOp
//===----------------------------------------------------------------------===//

OpFoldResult cir::VecCmpOp::fold(FoldAdaptor adaptor) {
  auto lhsVecAttr =
      mlir::dyn_cast_if_present<cir::ConstVectorAttr>(adaptor.getLhs());
  auto rhsVecAttr =
      mlir::dyn_cast_if_present<cir::ConstVectorAttr>(adaptor.getRhs());
  if (!lhsVecAttr || !rhsVecAttr)
    return {};

  mlir::Type inputElemTy =
      mlir::cast<cir::VectorType>(lhsVecAttr.getType()).getElementType();
  if (!isAnyIntegerOrFloatingPointType(inputElemTy))
    return {};

  cir::CmpOpKind opKind = adaptor.getKind();
  mlir::ArrayAttr lhsVecElhs = lhsVecAttr.getElts();
  mlir::ArrayAttr rhsVecElhs = rhsVecAttr.getElts();
  uint64_t vecSize = lhsVecElhs.size();

  SmallVector<mlir::Attribute, 16> elements(vecSize);
  bool isIntAttr = vecSize && mlir::isa<cir::IntAttr>(lhsVecElhs[0]);
  bool isSigned =
      isIntAttr && mlir::cast<cir::IntType>(inputElemTy).isSigned();
  for (uint64_t i = 0; i < vecSize; i++) {
    mlir::Attribute lhsAttr = lhsVecElhs[i];
    mlir::Attribute rhsAttr = rhsVecElhs[i];
    int cmpResult = 0;
    switch (opKind) {
    case cir::CmpOpKind::lt: {
      if (isIntAttr) {
        const APInt &lhs = mlir::cast<cir::IntAttr>(lhsAttr).getValue();
        const APInt &rhs = mlir::cast<cir::IntAttr>(rhsAttr).getValue();
        cmpResult = isSigned ? lhs.slt(rhs) : lhs.ult(rhs);
      } else {
        cmpResult = mlir::cast<cir::FPAttr>(lhsAttr).getValue() <
                    mlir::cast<cir::FPAttr>(rhsAttr).getValue();
      }
      break;
    }
    case cir::CmpOpKind::le: {
      if (isIntAttr) {
        const APInt &lhs = mlir::cast<cir::IntAttr>(lhsAttr).getValue();
        const APInt &rhs = mlir::cast<cir::IntAttr>(rhsAttr).getValue();
        cmpResult = isSigned ? lhs.sle(rhs) : lhs.ule(rhs);
      } else {
        cmpResult = mlir::cast<cir::FPAttr>(lhsAttr).getValue() <=
                    mlir::cast<cir::FPAttr>(rhsAttr).getValue();
      }
      break;
    }
    case cir::CmpOpKind::gt: {
      if (isIntAttr) {
        const APInt &lhs = mlir::cast<cir::IntAttr>(lhsAttr).getValue();
        const APInt &rhs = mlir::cast<cir::IntAttr>(rhsAttr).getValue();
        cmpResult = isSigned ? lhs.sgt(rhs) : lhs.ugt(rhs);
      } else {
        cmpResult = mlir::cast<cir::FPAttr>(lhsAttr).getValue() >
                    mlir::cast<cir::FPAttr>(rhsAttr).getValue();
      }
      break;
    }
    case cir::CmpOpKind::ge: {
      if (isIntAttr) {
        const APInt &lhs = mlir::cast<cir::IntAttr>(lhsAttr).getValue();
        const APInt &rhs = mlir::cast<cir::IntAttr>(rhsAttr).getValue();
        cmpResult = isSigned ? lhs.sge(rhs) : lhs.uge(rhs);
      } else {
        cmpResult = mlir::cast<cir::FPAttr>(lhsAttr).getValue() >=
                    mlir::cast<cir::FPAttr>(rhsAttr).getValue();
      }
      break;
    }
    case cir::CmpOpKind::eq: {
      if (isIntAttr) {
        cmpResult = mlir::cast<cir::IntAttr>(lhsAttr).getValue() ==
                    mlir::cast<cir::IntAttr>(rhsAttr).getValue();
      } else {
        cmpResult = mlir::cast<cir::FPAttr>(lhsAttr).getValue() ==
                    mlir::cast<cir::FPAttr>(rhsAttr).getValue();
      }
      break;
    }
    case cir::CmpOpKind::ne: {
      if (isIntAttr) {
        cmpResult = mlir::cast<cir::IntAttr>(lhsAttr).getValue() !=
                    mlir::cast<cir::IntAttr>(rhsAttr).getValue();
      } else {
        cmpResult = mlir::cast<cir::FPAttr>(lhsAttr).getValue() !=
                    mlir::cast<cir::FPAttr>(rhsAttr).getValue();
      }
      break;
    }
    case cir::CmpOpKind::fone: {
      // Ordered not-equal: returns true if neither is NaN and values differ
      assert(!isIntAttr && "fone is a float-only predicate");
      auto lhsVal = mlir::cast<cir::FPAttr>(lhsAttr).getValue();
      auto rhsVal = mlir::cast<cir::FPAttr>(rhsAttr).getValue();
      cmpResult = !lhsVal.isNaN() && !rhsVal.isNaN() && lhsVal != rhsVal;
      break;
    }
    case cir::CmpOpKind::funo: {
      // Unordered: returns true if either value is NaN
      assert(!isIntAttr && "funo is a float-only predicate");
      cmpResult = mlir::cast<cir::FPAttr>(lhsAttr).getValue().isNaN() ||
                  mlir::cast<cir::FPAttr>(rhsAttr).getValue().isNaN();
      break;
    }
    case cir::CmpOpKind::fugt: {
      // Unordered greater-than: true if NaN or lhs > rhs
      assert(!isIntAttr && "fugt is a float-only predicate");
      const auto &lhs = mlir::cast<cir::FPAttr>(lhsAttr).getValue();
      const auto &rhs = mlir::cast<cir::FPAttr>(rhsAttr).getValue();
      cmpResult = lhs.isNaN() || rhs.isNaN() ||
                  lhs.compare(rhs) == llvm::APFloat::cmpGreaterThan;
      break;
    }
    case cir::CmpOpKind::fuge: {
      // Unordered greater-than-or-equal: true if NaN or lhs >= rhs.
      assert(!isIntAttr && "fuge is a float-only predicate");
      const auto &lhs = mlir::cast<cir::FPAttr>(lhsAttr).getValue();
      const auto &rhs = mlir::cast<cir::FPAttr>(rhsAttr).getValue();
      cmpResult = lhs.isNaN() || rhs.isNaN() || lhs >= rhs;
      break;
    }
    case cir::CmpOpKind::fult: {
      // Unordered less-than: true if NaN or lhs < rhs.
      assert(!isIntAttr && "fult is a float-only predicate");
      const auto &lhs = mlir::cast<cir::FPAttr>(lhsAttr).getValue();
      const auto &rhs = mlir::cast<cir::FPAttr>(rhsAttr).getValue();
      cmpResult = lhs.isNaN() || rhs.isNaN() || lhs < rhs;
      break;
    }
    case cir::CmpOpKind::fule: {
      // Unordered less-than-or-equal: true if NaN or lhs <= rhs.
      assert(!isIntAttr && "fule is a float-only predicate");
      const auto &lhs = mlir::cast<cir::FPAttr>(lhsAttr).getValue();
      const auto &rhs = mlir::cast<cir::FPAttr>(rhsAttr).getValue();
      cmpResult = lhs.isNaN() || rhs.isNaN() || lhs <= rhs;
      break;
    }
    case cir::CmpOpKind::ford: {
      // Ordered: true if neither operand is NaN.
      assert(!isIntAttr && "ford is a float-only predicate");
      const auto &lhs = mlir::cast<cir::FPAttr>(lhsAttr).getValue();
      const auto &rhs = mlir::cast<cir::FPAttr>(rhsAttr).getValue();
      cmpResult = !lhs.isNaN() && !rhs.isNaN();
      break;
    }
    case cir::CmpOpKind::fueq: {
      // Unordered equal: true if NaN or lhs == rhs.
      assert(!isIntAttr && "fueq is a float-only predicate");
      const auto &lhs = mlir::cast<cir::FPAttr>(lhsAttr).getValue();
      const auto &rhs = mlir::cast<cir::FPAttr>(rhsAttr).getValue();
      cmpResult = lhs.isNaN() || rhs.isNaN() || lhs == rhs;
      break;
    }
    }

    // VecCmpOp produces 0 or -1 (all-ones), matching vector comparison
    // semantics and the sign-extension in the LLVM lowering.
    elements[i] =
        cir::IntAttr::get(getType().getElementType(), cmpResult ? -1 : 0);
  }

  return cir::ConstVectorAttr::get(
      getType(), mlir::ArrayAttr::get(getContext(), elements));
}

//===----------------------------------------------------------------------===//
// VecShuffleOp
//===----------------------------------------------------------------------===//

OpFoldResult cir::VecShuffleOp::fold(FoldAdaptor adaptor) {
  auto vec1Attr =
      mlir::dyn_cast_if_present<cir::ConstVectorAttr>(adaptor.getVec1());
  auto vec2Attr =
      mlir::dyn_cast_if_present<cir::ConstVectorAttr>(adaptor.getVec2());
  if (!vec1Attr || !vec2Attr)
    return {};

  mlir::Type vec1ElemTy =
      mlir::cast<cir::VectorType>(vec1Attr.getType()).getElementType();

  mlir::ArrayAttr vec1Elts = vec1Attr.getElts();
  mlir::ArrayAttr vec2Elts = vec2Attr.getElts();
  mlir::ArrayAttr indicesElts = adaptor.getIndices();

  SmallVector<mlir::Attribute, 16> elements;
  elements.reserve(indicesElts.size());

  uint64_t vec1Size = vec1Elts.size();
  for (const auto &idxAttr : indicesElts.getAsRange<cir::IntAttr>()) {
    if (idxAttr.getSInt() == -1) {
      elements.push_back(cir::UndefAttr::get(vec1ElemTy));
      continue;
    }

    uint64_t idxValue = idxAttr.getUInt();
    elements.push_back(idxValue < vec1Size ? vec1Elts[idxValue]
                                           : vec2Elts[idxValue - vec1Size]);
  }

  return cir::ConstVectorAttr::get(
      getType(), mlir::ArrayAttr::get(getContext(), elements));
}

LogicalResult cir::VecShuffleOp::verify() {
  // The number of elements in the indices array must match the number of
  // elements in the result type.
  if (getIndices().size() != getResult().getType().getSize()) {
    return emitOpError() << ": the number of elements in " << getIndices()
                         << " and " << getResult().getType() << " don't match";
  }

  // The element types of the two input vectors and of the result type must
  // match.
  if (getVec1().getType().getElementType() !=
      getResult().getType().getElementType()) {
    return emitOpError() << ": element types of " << getVec1().getType()
                         << " and " << getResult().getType() << " don't match";
  }

  const uint64_t maxValidIndex =
      getVec1().getType().getSize() + getVec2().getType().getSize() - 1;
  if (llvm::any_of(
          getIndices().getAsRange<cir::IntAttr>(), [&](cir::IntAttr idxAttr) {
            return idxAttr.getSInt() != -1 && idxAttr.getUInt() > maxValidIndex;
          })) {
    return emitOpError() << ": index for __builtin_shufflevector must be "
                            "less than the total number of vector elements";
  }
  return success();
}

//===----------------------------------------------------------------------===//
// VecShuffleDynamicOp
//===----------------------------------------------------------------------===//

OpFoldResult cir::VecShuffleDynamicOp::fold(FoldAdaptor adaptor) {
  mlir::Attribute vec = adaptor.getVec();
  mlir::Attribute indices = adaptor.getIndices();
  if (mlir::isa_and_nonnull<cir::ConstVectorAttr>(vec) &&
      mlir::isa_and_nonnull<cir::ConstVectorAttr>(indices)) {
    auto vecAttr = mlir::cast<cir::ConstVectorAttr>(vec);
    auto indicesAttr = mlir::cast<cir::ConstVectorAttr>(indices);

    mlir::ArrayAttr vecElts = vecAttr.getElts();
    mlir::ArrayAttr indicesElts = indicesAttr.getElts();

    const uint64_t numElements = vecElts.size();

    SmallVector<mlir::Attribute, 16> elements;
    elements.reserve(numElements);

    const uint64_t maskBits = llvm::NextPowerOf2(numElements - 1) - 1;
    for (const auto &idxAttr : indicesElts.getAsRange<cir::IntAttr>()) {
      uint64_t idxValue = idxAttr.getUInt();
      uint64_t newIdx = idxValue & maskBits;
      elements.push_back(vecElts[newIdx]);
    }

    return cir::ConstVectorAttr::get(
        getType(), mlir::ArrayAttr::get(getContext(), elements));
  }

  return {};
}

LogicalResult cir::VecShuffleDynamicOp::verify() {
  // The number of elements in the two input vectors must match.
  if (getVec().getType().getSize() !=
      mlir::cast<cir::VectorType>(getIndices().getType()).getSize()) {
    return emitOpError() << ": the number of elements in " << getVec().getType()
                         << " and " << getIndices().getType() << " don't match";
  }
  return success();
}

//===----------------------------------------------------------------------===//
// VecTernaryOp
//===----------------------------------------------------------------------===//

LogicalResult cir::VecTernaryOp::verify() {
  // Verify that the condition operand has the same number of elements as the
  // other operands.  (The automatic verification already checked that all
  // operands are vector types and that the second and third operands are the
  // same type.)
  if (getCond().getType().getSize() != getLhs().getType().getSize()) {
    return emitOpError() << ": the number of elements in "
                         << getCond().getType() << " and " << getLhs().getType()
                         << " don't match";
  }
  return success();
}

OpFoldResult cir::VecTernaryOp::fold(FoldAdaptor adaptor) {
  mlir::Attribute cond = adaptor.getCond();
  mlir::Attribute lhs = adaptor.getLhs();
  mlir::Attribute rhs = adaptor.getRhs();

  if (!mlir::isa_and_nonnull<cir::ConstVectorAttr>(cond) ||
      !mlir::isa_and_nonnull<cir::ConstVectorAttr>(lhs) ||
      !mlir::isa_and_nonnull<cir::ConstVectorAttr>(rhs))
    return {};
  auto condVec = mlir::cast<cir::ConstVectorAttr>(cond);
  auto lhsVec = mlir::cast<cir::ConstVectorAttr>(lhs);
  auto rhsVec = mlir::cast<cir::ConstVectorAttr>(rhs);

  mlir::ArrayAttr condElts = condVec.getElts();

  SmallVector<mlir::Attribute, 16> elements;
  elements.reserve(condElts.size());

  for (const auto &[idx, condAttr] : llvm::enumerate(condElts)) {
    bool isTrue = llvm::TypeSwitch<mlir::Attribute, bool>(condAttr)
                      .Case<cir::IntAttr>(
                          [](cir::IntAttr a) { return a.getSInt() != 0; })
                      .Case<cir::BoolAttr>(
                          [](cir::BoolAttr a) { return a.getValue(); });
    elements.push_back(isTrue ? lhsVec.getElts()[idx]
                              : rhsVec.getElts()[idx]);
  }

  cir::VectorType vecTy = getLhs().getType();
  return cir::ConstVectorAttr::get(
      vecTy, mlir::ArrayAttr::get(getContext(), elements));
}

//===----------------------------------------------------------------------===//
// ComplexCreateOp
//===----------------------------------------------------------------------===//

LogicalResult cir::ComplexCreateOp::verify() {
  if (getType().getElementType() != getReal().getType()) {
    emitOpError()
        << "operand type of cir.complex.create does not match its result type";
    return failure();
  }

  return success();
}

OpFoldResult cir::ComplexCreateOp::fold(FoldAdaptor adaptor) {
  mlir::Attribute real = adaptor.getReal();
  mlir::Attribute imag = adaptor.getImag();
  if (!real || !imag)
    return {};

  // When both of real and imag are constants, we can fold the operation into an
  // `#cir.const_complex` operation.
  auto realAttr = mlir::cast<mlir::TypedAttr>(real);
  auto imagAttr = mlir::cast<mlir::TypedAttr>(imag);
  return cir::ConstComplexAttr::get(realAttr, imagAttr);
}

//===----------------------------------------------------------------------===//
// ComplexRealOp
//===----------------------------------------------------------------------===//

LogicalResult cir::ComplexRealOp::verify() {
  mlir::Type operandTy = getOperand().getType();
  if (auto complexOperandTy = mlir::dyn_cast<cir::ComplexType>(operandTy))
    operandTy = complexOperandTy.getElementType();

  if (getType() != operandTy) {
    emitOpError() << ": result type does not match operand type";
    return failure();
  }

  return success();
}

OpFoldResult cir::ComplexRealOp::fold(FoldAdaptor adaptor) {
  if (!mlir::isa<cir::ComplexType>(getOperand().getType()))
    return nullptr;

  if (auto complexCreateOp = getOperand().getDefiningOp<cir::ComplexCreateOp>())
    return complexCreateOp.getOperand(0);

  auto complex =
      mlir::dyn_cast_if_present<cir::ConstComplexAttr>(adaptor.getOperand());
  return complex ? complex.getReal() : nullptr;
}

//===----------------------------------------------------------------------===//
// ComplexImagOp
//===----------------------------------------------------------------------===//

LogicalResult cir::ComplexImagOp::verify() {
  mlir::Type operandTy = getOperand().getType();
  if (auto complexOperandTy = mlir::dyn_cast<cir::ComplexType>(operandTy))
    operandTy = complexOperandTy.getElementType();

  if (getType() != operandTy) {
    emitOpError() << ": result type does not match operand type";
    return failure();
  }

  return success();
}

OpFoldResult cir::ComplexImagOp::fold(FoldAdaptor adaptor) {
  if (!mlir::isa<cir::ComplexType>(getOperand().getType()))
    return nullptr;

  if (auto complexCreateOp = getOperand().getDefiningOp<cir::ComplexCreateOp>())
    return complexCreateOp.getOperand(1);

  auto complex =
      mlir::dyn_cast_if_present<cir::ConstComplexAttr>(adaptor.getOperand());
  return complex ? complex.getImag() : nullptr;
}

//===----------------------------------------------------------------------===//
// ComplexRealPtrOp
//===----------------------------------------------------------------------===//

LogicalResult cir::ComplexRealPtrOp::verify() {
  mlir::Type resultPointeeTy = getType().getPointee();
  cir::PointerType operandPtrTy = getOperand().getType();
  auto operandPointeeTy =
      mlir::cast<cir::ComplexType>(operandPtrTy.getPointee());

  if (resultPointeeTy != operandPointeeTy.getElementType()) {
    return emitOpError() << ": result type does not match operand type";
  }

  return success();
}

//===----------------------------------------------------------------------===//
// ComplexImagPtrOp
//===----------------------------------------------------------------------===//

LogicalResult cir::ComplexImagPtrOp::verify() {
  mlir::Type resultPointeeTy = getType().getPointee();
  cir::PointerType operandPtrTy = getOperand().getType();
  auto operandPointeeTy =
      mlir::cast<cir::ComplexType>(operandPtrTy.getPointee());

  if (resultPointeeTy != operandPointeeTy.getElementType()) {
    return emitOpError()
           << "cir.complex.imag_ptr result type does not match operand type";
  }
  return success();
}

//===----------------------------------------------------------------------===//
// Bit manipulation operations
//===----------------------------------------------------------------------===//

static OpFoldResult
foldUnaryBitOp(mlir::Attribute inputAttr,
               llvm::function_ref<llvm::APInt(const llvm::APInt &)> func,
               bool poisonZero = false) {
  if (mlir::isa_and_present<cir::PoisonAttr>(inputAttr)) {
    // Propagate poison value
    return inputAttr;
  }

  auto input = mlir::dyn_cast_if_present<IntAttr>(inputAttr);
  if (!input)
    return nullptr;

  llvm::APInt inputValue = input.getValue();
  if (poisonZero && inputValue.isZero())
    return cir::PoisonAttr::get(input.getType());

  llvm::APInt resultValue = func(inputValue);
  return IntAttr::get(input.getType(), resultValue);
}

OpFoldResult BitClrsbOp::fold(FoldAdaptor adaptor) {
  return foldUnaryBitOp(adaptor.getInput(), [](const llvm::APInt &inputValue) {
    unsigned resultValue =
        inputValue.getBitWidth() - inputValue.getSignificantBits();
    return llvm::APInt(inputValue.getBitWidth(), resultValue);
  });
}

OpFoldResult BitClzOp::fold(FoldAdaptor adaptor) {
  return foldUnaryBitOp(
      adaptor.getInput(),
      [](const llvm::APInt &inputValue) {
        unsigned resultValue = inputValue.countLeadingZeros();
        return llvm::APInt(inputValue.getBitWidth(), resultValue);
      },
      getPoisonZero());
}

OpFoldResult BitCtzOp::fold(FoldAdaptor adaptor) {
  return foldUnaryBitOp(
      adaptor.getInput(),
      [](const llvm::APInt &inputValue) {
        return llvm::APInt(inputValue.getBitWidth(),
                           inputValue.countTrailingZeros());
      },
      getPoisonZero());
}

OpFoldResult BitFfsOp::fold(FoldAdaptor adaptor) {
  return foldUnaryBitOp(adaptor.getInput(), [](const llvm::APInt &inputValue) {
    unsigned trailingZeros = inputValue.countTrailingZeros();
    unsigned result =
        trailingZeros == inputValue.getBitWidth() ? 0 : trailingZeros + 1;
    return llvm::APInt(inputValue.getBitWidth(), result);
  });
}

OpFoldResult BitParityOp::fold(FoldAdaptor adaptor) {
  return foldUnaryBitOp(adaptor.getInput(), [](const llvm::APInt &inputValue) {
    return llvm::APInt(inputValue.getBitWidth(), inputValue.popcount() % 2);
  });
}

OpFoldResult BitPopcountOp::fold(FoldAdaptor adaptor) {
  return foldUnaryBitOp(adaptor.getInput(), [](const llvm::APInt &inputValue) {
    return llvm::APInt(inputValue.getBitWidth(), inputValue.popcount());
  });
}

OpFoldResult BitReverseOp::fold(FoldAdaptor adaptor) {
  return foldUnaryBitOp(adaptor.getInput(), [](const llvm::APInt &inputValue) {
    return inputValue.reverseBits();
  });
}

OpFoldResult ByteSwapOp::fold(FoldAdaptor adaptor) {
  return foldUnaryBitOp(adaptor.getInput(), [](const llvm::APInt &inputValue) {
    return inputValue.byteSwap();
  });
}

OpFoldResult RotateOp::fold(FoldAdaptor adaptor) {
  if (mlir::isa_and_present<cir::PoisonAttr>(adaptor.getInput()) ||
      mlir::isa_and_present<cir::PoisonAttr>(adaptor.getAmount())) {
    // Propagate poison values
    return cir::PoisonAttr::get(getType());
  }

  auto input = mlir::dyn_cast_if_present<IntAttr>(adaptor.getInput());
  auto amount = mlir::dyn_cast_if_present<IntAttr>(adaptor.getAmount());
  if (!input && !amount)
    return nullptr;

  // We could fold cir.rotate even if one of its two operands is not a constant:
  //   - `cir.rotate left/right %0, 0` could be folded into just %0 even if %0
  //     is not a constant.
  //   - `cir.rotate left/right 0/0b111...111, %0` could be folded into 0 or
  //     0b111...111 even if %0 is not a constant.

  llvm::APInt inputValue;
  if (input) {
    inputValue = input.getValue();
    if (inputValue.isZero() || inputValue.isAllOnes()) {
      // An input value of all 0s or all 1s will not change after rotation
      return input;
    }
  }

  uint64_t amountValue;
  if (amount) {
    amountValue = amount.getValue().urem(getInput().getType().getWidth());
    if (amountValue == 0) {
      // A shift amount of 0 will not change the input value
      return getInput();
    }
  }

  if (!input || !amount)
    return nullptr;

  assert(inputValue.getBitWidth() == getInput().getType().getWidth() &&
         "input value must have the same bit width as the input type");

  llvm::APInt resultValue;
  if (isRotateLeft())
    resultValue = inputValue.rotl(amountValue);
  else
    resultValue = inputValue.rotr(amountValue);

  return IntAttr::get(input.getContext(), input.getType(), resultValue);
}

//===----------------------------------------------------------------------===//
// InlineAsmOp
//===----------------------------------------------------------------------===//

void cir::InlineAsmOp::print(OpAsmPrinter &p) {
  p << '(' << getAsmFlavor() << ", ";
  p.increaseIndent();
  p.printNewline();

  llvm::SmallVector<std::string, 3> names{"out", "in", "in_out"};
  auto *nameIt = names.begin();
  auto *attrIt = getOperandAttrs().begin();

  for (mlir::OperandRange ops : getAsmOperands()) {
    p << *nameIt << " = ";

    p << '[';
    llvm::interleaveComma(llvm::make_range(ops.begin(), ops.end()), p,
                          [&](Value value) {
                            p.printOperand(value);
                            p << " : " << value.getType();
                            if (mlir::isa<mlir::UnitAttr>(*attrIt))
                              p << " (maybe_memory)";
                            attrIt++;
                          });
    p << "],";
    p.printNewline();
    ++nameIt;
  }

  p << "{";
  p.printString(getAsmString());
  p << " ";
  p.printString(getConstraints());
  p << "}";
  p.decreaseIndent();
  p << ')';
  if (getSideEffects())
    p << " side_effects";

  std::array elidedAttrs{
      llvm::StringRef("asm_flavor"),        llvm::StringRef("asm_string"),
      llvm::StringRef("constraints"),       llvm::StringRef("operand_attrs"),
      llvm::StringRef("operands_segments"), llvm::StringRef("side_effects"),
      llvm::StringRef("cir.asm_srcloc")};
  p.printOptionalAttrDict(getOperation()->getAttrs(), elidedAttrs);

  if (auto v = getRes())
    p << " -> " << v.getType();
}

void cir::InlineAsmOp::build(OpBuilder &odsBuilder, OperationState &odsState,
                             ArrayRef<ValueRange> asmOperands,
                             StringRef asmString, StringRef constraints,
                             bool sideEffects, cir::AsmFlavor asmFlavor,
                             ArrayRef<Attribute> operandAttrs) {
  // Set up the operands_segments for VariadicOfVariadic
  SmallVector<int32_t> segments;
  for (auto operandRange : asmOperands) {
    segments.push_back(operandRange.size());
    odsState.addOperands(operandRange);
  }

  odsState.addAttribute(
      "operands_segments",
      DenseI32ArrayAttr::get(odsBuilder.getContext(), segments));
  odsState.addAttribute("asm_string", odsBuilder.getStringAttr(asmString));
  odsState.addAttribute("constraints", odsBuilder.getStringAttr(constraints));
  odsState.addAttribute("asm_flavor",
                        AsmFlavorAttr::get(odsBuilder.getContext(), asmFlavor));

  if (sideEffects)
    odsState.addAttribute("side_effects", odsBuilder.getUnitAttr());

  odsState.addAttribute("operand_attrs", odsBuilder.getArrayAttr(operandAttrs));
}

ParseResult cir::InlineAsmOp::parse(OpAsmParser &parser,
                                    OperationState &result) {
  llvm::SmallVector<mlir::Attribute> operandAttrs;
  llvm::SmallVector<int32_t> operandsGroupSizes;
  std::string asmString, constraints;
  Type resType;
  MLIRContext *ctxt = parser.getBuilder().getContext();

  auto error = [&](const Twine &msg) -> LogicalResult {
    return parser.emitError(parser.getCurrentLocation(), msg);
  };

  auto expected = [&](const std::string &c) {
    return error("expected '" + c + "'");
  };

  if (parser.parseLParen().failed())
    return expected("(");

  auto flavor = FieldParser<AsmFlavor, AsmFlavor>::parse(parser);
  if (failed(flavor))
    return error("Unknown AsmFlavor");

  if (parser.parseComma().failed())
    return expected(",");

  auto parseValue = [&](Value &v) {
    OpAsmParser::UnresolvedOperand op;

    if (parser.parseOperand(op) || parser.parseColon())
      return error("can't parse operand");

    Type typ;
    if (parser.parseType(typ).failed())
      return error("can't parse operand type");
    llvm::SmallVector<mlir::Value> tmp;
    if (parser.resolveOperand(op, typ, tmp))
      return error("can't resolve operand");
    v = tmp[0];
    return mlir::success();
  };

  auto parseOperands = [&](llvm::StringRef name) {
    if (parser.parseKeyword(name).failed())
      return error("expected " + name + " operands here");
    if (parser.parseEqual().failed())
      return expected("=");
    if (parser.parseLSquare().failed())
      return expected("[");

    int size = 0;
    if (parser.parseOptionalRSquare().succeeded()) {
      operandsGroupSizes.push_back(size);
      if (parser.parseComma())
        return expected(",");
      return mlir::success();
    }

    auto parseOperand = [&]() {
      Value val;
      if (parseValue(val).succeeded()) {
        result.operands.push_back(val);
        size++;

        if (parser.parseOptionalLParen().failed()) {
          operandAttrs.push_back(mlir::DictionaryAttr::get(ctxt));
          return mlir::success();
        }

        if (parser.parseKeyword("maybe_memory").succeeded()) {
          operandAttrs.push_back(mlir::UnitAttr::get(ctxt));
          if (parser.parseRParen())
            return expected(")");
          return mlir::success();
        } else {
          return expected("maybe_memory");
        }
      }
      return mlir::failure();
    };

    if (parser.parseCommaSeparatedList(parseOperand).failed())
      return mlir::failure();

    if (parser.parseRSquare().failed() || parser.parseComma().failed())
      return expected("]");
    operandsGroupSizes.push_back(size);
    return mlir::success();
  };

  if (parseOperands("out").failed() || parseOperands("in").failed() ||
      parseOperands("in_out").failed())
    return error("failed to parse operands");

  if (parser.parseLBrace())
    return expected("{");
  if (parser.parseString(&asmString))
    return error("asm string parsing failed");
  if (parser.parseString(&constraints))
    return error("constraints string parsing failed");
  if (parser.parseRBrace())
    return expected("}");
  if (parser.parseRParen())
    return expected(")");

  if (parser.parseOptionalKeyword("side_effects").succeeded())
    result.attributes.set("side_effects", UnitAttr::get(ctxt));

  // The attribute dictionary comes *before* the result type, matching the
  // printer. Reading them in the other order silently loses the result: the
  // arrow does not match a leading `{`, so resType stays null, the dictionary
  // is then consumed, and an op that was printed as `%1 = cir.asm ... -> !s32i`
  // parses back with no results at all ("cannot name an operation with no
  // results"). Any inline asm carrying a discardable attribute hits this, and
  // cir.asm_readnone and cir.asm_nounwind are attached routinely.
  if (parser.parseOptionalAttrDict(result.attributes).failed())
    return mlir::failure();

  if (parser.parseOptionalArrow().succeeded() &&
      parser.parseType(resType).failed())
    return mlir::failure();

  result.attributes.set("asm_flavor", AsmFlavorAttr::get(ctxt, *flavor));
  result.attributes.set("asm_string", StringAttr::get(ctxt, asmString));
  result.attributes.set("constraints", StringAttr::get(ctxt, constraints));
  result.attributes.set("operand_attrs", ArrayAttr::get(ctxt, operandAttrs));
  result.getOrAddProperties<InlineAsmOp::Properties>().operands_segments =
      parser.getBuilder().getDenseI32ArrayAttr(operandsGroupSizes);
  if (resType)
    result.addTypes(TypeRange{resType});

  return mlir::success();
}

/// Shared checking for the `operand_attrs` array of the two inline-asm ops.
///
/// The array is positional: entry i describes asm operand i, so an operand
/// that carries no attributes still needs a slot. That slot must be an empty
/// dictionary and not a null attribute. A null inside an ArrayAttr is invalid
/// in MLIR generally -- printing it and writing it to bytecode both assert --
/// so an op that holds one cannot be dumped, serialized or lowered.
static llvm::LogicalResult
verifyInlineAsmOperandAttrs(mlir::Operation *op, mlir::ArrayAttr operandAttrs,
                            mlir::OperandRangeRange asmOperands) {
  size_t numAsmOperands = 0;
  for (mlir::OperandRange ops : asmOperands)
    numAsmOperands += ops.size();

  // The printer walks this array in lockstep with the operands, so a short
  // array is an out-of-bounds read rather than a merely odd-looking op.
  if (operandAttrs.size() != numAsmOperands)
    return op->emitOpError()
           << "expects one 'operand_attrs' entry per asm operand, but got "
           << operandAttrs.size() << " for " << numAsmOperands << " operands";

  for (auto [i, attr] : llvm::enumerate(operandAttrs)) {
    if (!attr)
      return op->emitOpError()
             << "'operand_attrs' entry #" << i
             << " is null; use an empty dictionary for an operand that "
                "carries no attributes";
    if (!mlir::isa<mlir::UnitAttr, mlir::DictionaryAttr>(attr))
      return op->emitOpError()
             << "'operand_attrs' entry #" << i
             << " must be a unit or a dictionary attribute, but got " << attr;
  }
  return mlir::success();
}

llvm::LogicalResult cir::InlineAsmOp::verify() {
  return verifyInlineAsmOperandAttrs(*this, getOperandAttrs(),
                                     getAsmOperands());
}

//===----------------------------------------------------------------------===//
// InlineAsmGotoOp
//===----------------------------------------------------------------------===//

mlir::SuccessorOperands cir::InlineAsmGotoOp::getSuccessorOperands(
    unsigned index) {
  assert(index < getNumSuccessors() && "invalid successor index");
  if (index == 0 || !getRes())
    return mlir::SuccessorOperands(
        mlir::MutableOperandRange(getOperation(), /*start=*/0, /*length=*/0));
  return mlir::SuccessorOperands(
      /*producedOperandCount=*/1,
      mlir::MutableOperandRange(getOperation(), /*start=*/0, /*length=*/0));
}

Block *cir::InlineAsmGotoOp::getSuccessorForOperands(ArrayRef<Attribute>) {
  return nullptr;
}

void cir::InlineAsmGotoOp::print(OpAsmPrinter &p) {
  p << '(' << getAsmFlavor() << ", ";
  p.increaseIndent();
  p.printNewline();

  llvm::SmallVector<std::string, 3> names{"out", "in", "in_out"};
  auto *nameIt = names.begin();
  auto *attrIt = getOperandAttrs().begin();

  for (mlir::OperandRange ops : getAsmOperands()) {
    p << *nameIt << " = ";

    p << '[';
    llvm::interleaveComma(llvm::make_range(ops.begin(), ops.end()), p,
                          [&](Value value) {
                            p.printOperand(value);
                            p << " : " << value.getType();
                            if (mlir::isa<mlir::UnitAttr>(*attrIt))
                              p << " (maybe_memory)";
                            attrIt++;
                          });
    p << "],";
    p.printNewline();
    ++nameIt;
  }

  p << "{";
  p.printString(getAsmString());
  p << " ";
  p.printString(getConstraints());
  p << "}";
  p.decreaseIndent();
  p << ')';

  // Print result type if present.
  if (getRes())
    p << " -> " << getRes().getType();

  if (getSideEffects())
    p << " side_effects";

  // Print label names.
  p << " labels [";
  llvm::interleaveComma(getLabelNames(), p, [&](Attribute attr) {
    p.printString(cast<StringAttr>(attr).getValue());
  });
  p << "]";

  // Print successor blocks if any.
  if (!getTargets().empty()) {
    p << " targets [";
    llvm::interleaveComma(getTargets(), p,
                          [&](Block *block) { p.printSuccessor(block); });
    p << "]";
  }

  std::array elidedAttrs{
      llvm::StringRef("asm_flavor"),        llvm::StringRef("asm_string"),
      llvm::StringRef("constraints"),       llvm::StringRef("operand_attrs"),
      llvm::StringRef("operands_segments"), llvm::StringRef("side_effects"),
      llvm::StringRef("label_names"), llvm::StringRef("cir.asm_srcloc")};
  p.printOptionalAttrDict(getOperation()->getAttrs(), elidedAttrs);
}

ParseResult cir::InlineAsmGotoOp::parse(OpAsmParser &parser,
                                        OperationState &result) {
  llvm::SmallVector<mlir::Attribute> operandAttrs;
  llvm::SmallVector<int32_t> operandsGroupSizes;
  std::string asmString, constraints;
  MLIRContext *ctxt = parser.getBuilder().getContext();

  auto error = [&](const Twine &msg) -> LogicalResult {
    return parser.emitError(parser.getCurrentLocation(), msg);
  };

  auto expected = [&](const std::string &c) {
    return error("expected '" + c + "'");
  };

  if (parser.parseLParen().failed())
    return expected("(");

  auto flavor = FieldParser<AsmFlavor, AsmFlavor>::parse(parser);
  if (failed(flavor))
    return error("Unknown AsmFlavor");

  if (parser.parseComma().failed())
    return expected(",");

  auto parseValue = [&](Value &v) {
    OpAsmParser::UnresolvedOperand op;

    if (parser.parseOperand(op) || parser.parseColon())
      return error("can't parse operand");

    Type typ;
    if (parser.parseType(typ).failed())
      return error("can't parse operand type");
    llvm::SmallVector<mlir::Value> tmp;
    if (parser.resolveOperand(op, typ, tmp))
      return error("can't resolve operand");
    v = tmp[0];
    return mlir::success();
  };

  auto parseOperands = [&](llvm::StringRef name) {
    if (parser.parseKeyword(name).failed())
      return error("expected " + name + " operands here");
    if (parser.parseEqual().failed())
      return expected("=");
    if (parser.parseLSquare().failed())
      return expected("[");

    int size = 0;
    if (parser.parseOptionalRSquare().succeeded()) {
      operandsGroupSizes.push_back(size);
      if (parser.parseComma())
        return expected(",");
      return mlir::success();
    }

    auto parseOperand = [&]() {
      Value val;
      if (parseValue(val).succeeded()) {
        result.operands.push_back(val);
        size++;

        if (parser.parseOptionalLParen().failed()) {
          operandAttrs.push_back(mlir::DictionaryAttr::get(ctxt));
          return mlir::success();
        }

        if (parser.parseKeyword("maybe_memory").succeeded()) {
          operandAttrs.push_back(mlir::UnitAttr::get(ctxt));
          if (parser.parseRParen())
            return expected(")");
          return mlir::success();
        } else {
          return expected("maybe_memory");
        }
      }
      return mlir::failure();
    };

    if (parser.parseCommaSeparatedList(parseOperand).failed())
      return mlir::failure();

    if (parser.parseRSquare().failed() || parser.parseComma().failed())
      return expected("]");
    operandsGroupSizes.push_back(size);
    return mlir::success();
  };

  if (parseOperands("out").failed() || parseOperands("in").failed() ||
      parseOperands("in_out").failed())
    return error("failed to parse operands");

  if (parser.parseLBrace())
    return expected("{");
  if (parser.parseString(&asmString))
    return error("asm string parsing failed");
  if (parser.parseString(&constraints))
    return error("constraints string parsing failed");
  if (parser.parseRBrace())
    return expected("}");
  if (parser.parseRParen())
    return expected(")");

  // Parse optional result type: -> type
  if (parser.parseOptionalArrow().succeeded()) {
    Type resultType;
    if (parser.parseType(resultType))
      return error("result type parsing failed");
    result.addTypes(resultType);
  }

  if (parser.parseOptionalKeyword("side_effects").succeeded())
    result.attributes.set("side_effects", UnitAttr::get(ctxt));

  // Parse label names: labels ["label1", "label2"]
  SmallVector<Attribute> labelAttrs;
  if (parser.parseKeyword("labels"))
    return expected("labels");
  if (parser.parseLSquare())
    return expected("[");
  if (parser.parseOptionalRSquare().failed()) {
    auto parseLabelName = [&]() {
      std::string labelName;
      if (parser.parseString(&labelName))
        return mlir::failure();
      labelAttrs.push_back(StringAttr::get(ctxt, labelName));
      return mlir::success();
    };
    if (parser.parseCommaSeparatedList(parseLabelName))
      return mlir::failure();
    if (parser.parseRSquare())
      return expected("]");
  }

  // Parse optional successor blocks: targets [^bb1, ^bb2]
  if (parser.parseOptionalKeyword("targets").succeeded()) {
    if (parser.parseLSquare())
      return expected("[");
    if (parser.parseOptionalRSquare().failed()) {
      auto parseTarget = [&]() {
        Block *target = nullptr;
        if (parser.parseSuccessor(target))
          return mlir::failure();
        result.addSuccessors(target);
        return mlir::success();
      };
      if (parser.parseCommaSeparatedList(parseTarget))
        return mlir::failure();
      if (parser.parseRSquare())
        return expected("]");
    }
  }

  if (parser.parseOptionalAttrDict(result.attributes).failed())
    return mlir::failure();

  result.attributes.set("asm_flavor", AsmFlavorAttr::get(ctxt, *flavor));
  result.attributes.set("asm_string", StringAttr::get(ctxt, asmString));
  result.attributes.set("constraints", StringAttr::get(ctxt, constraints));
  result.attributes.set("operand_attrs", ArrayAttr::get(ctxt, operandAttrs));
  result.getOrAddProperties<InlineAsmGotoOp::Properties>().operands_segments =
      parser.getBuilder().getDenseI32ArrayAttr(operandsGroupSizes);
  result.attributes.set("label_names", ArrayAttr::get(ctxt, labelAttrs));

  return mlir::success();
}

llvm::LogicalResult cir::InlineAsmGotoOp::verify() {
  return verifyInlineAsmOperandAttrs(*this, getOperandAttrs(),
                                     getAsmOperands());
}

//===----------------------------------------------------------------------===//
// ThrowOp
//===----------------------------------------------------------------------===//

mlir::LogicalResult cir::ThrowOp::verify() {
  // For the no-rethrow version, it must have at least the exception pointer.
  if (rethrows())
    return success();

  if (getNumOperands() != 0) {
    if (getTypeInfo())
      return success();
    return emitOpError() << "'type_info' symbol attribute missing";
  }

  return failure();
}

//===----------------------------------------------------------------------===//
// AtomicFetchOp
//===----------------------------------------------------------------------===//

LogicalResult cir::AtomicFetchOp::verify() {
  if (getBinop() != cir::AtomicFetchKind::Add &&
      getBinop() != cir::AtomicFetchKind::Sub &&
      getBinop() != cir::AtomicFetchKind::Max &&
      getBinop() != cir::AtomicFetchKind::Min &&
      !mlir::isa<cir::IntType>(getVal().getType()))
    return emitError("only atomic add, sub, max, and min operation could "
                     "operate on floating-point values");
  return success();
}

//===----------------------------------------------------------------------===//
// TypeInfoAttr
//===----------------------------------------------------------------------===//

LogicalResult cir::TypeInfoAttr::verify(
    ::llvm::function_ref<::mlir::InFlightDiagnostic()> emitError,
    ::mlir::Type type, ::mlir::ArrayAttr typeInfoData) {

  if (cir::ConstRecordAttr::verify(emitError, type, typeInfoData).failed())
    return failure();

  return success();
}

//===----------------------------------------------------------------------===//
// TryOp
//===----------------------------------------------------------------------===//

void cir::TryOp::getSuccessorRegions(
    mlir::RegionBranchPoint point,
    llvm::SmallVectorImpl<mlir::RegionSuccessor> &regions) {
  // The `try` and the `catchers` region branch back to the parent operation.
  if (!point.isParent()) {
    regions.push_back(RegionSuccessor(getOperation(), getOperation()->getResults()));
    return;
  }

  regions.push_back(mlir::RegionSuccessor(&getTryRegion()));

  // TODO(CIR): If we know a target function never throws a specific type, we
  // can remove the catch handler.
  for (mlir::Region &handlerRegion : this->getHandlerRegions())
    regions.push_back(mlir::RegionSuccessor(&handlerRegion));
}

void cir::CleanupScopeOp::getSuccessorRegions(
    mlir::RegionBranchPoint point,
    llvm::SmallVectorImpl<mlir::RegionSuccessor> &regions) {
  // Both the body and the (exception-only) shared cleanup region branch back to
  // the parent operation.
  if (!point.isParent()) {
    regions.push_back(
        RegionSuccessor(getOperation(), getOperation()->getResults()));
    return;
  }
  regions.push_back(mlir::RegionSuccessor(&getBody()));
  regions.push_back(mlir::RegionSuccessor(&getCleanup()));
}

bool cir::TryOp::isCatchAllOnly() {
  mlir::ArrayAttr handlerTypes = getHandlerTypesAttr();
  return handlerTypes.size() == 1 &&
         mlir::isa<cir::CatchAllAttr>(handlerTypes[0]);
}

static void
printTryHandlerRegions(mlir::OpAsmPrinter &printer, cir::TryOp op,
                       mlir::MutableArrayRef<mlir::Region> handlerRegions,
                       mlir::ArrayAttr handlerTypes) {
  if (!handlerTypes)
    return;

  for (const auto [typeIdx, typeAttr] : llvm::enumerate(handlerTypes)) {
    if (typeIdx)
      printer << " ";

    if (mlir::isa<cir::CatchAllAttr>(typeAttr)) {
      printer << "catch all ";
    } else if (mlir::isa<cir::UnwindAttr>(typeAttr)) {
      printer << "unwind ";
    } else {
      printer << "catch [type ";
      printer.printAttribute(typeAttr);
      printer << "] ";
    }

    printer.printRegion(handlerRegions[typeIdx],
                        /*printEntryBLockArgs=*/false,
                        /*printBlockTerminators=*/true);
  }
}

static mlir::ParseResult parseTryHandlerRegions(
    mlir::OpAsmParser &parser,
    llvm::SmallVectorImpl<std::unique_ptr<mlir::Region>> &handlerRegions,
    mlir::ArrayAttr &handlerTypes) {

  auto parseCheckedCatcherRegion = [&]() -> mlir::ParseResult {
    handlerRegions.emplace_back(new mlir::Region);

    mlir::Region &currRegion = *handlerRegions.back();
    mlir::SMLoc regionLoc = parser.getCurrentLocation();
    if (parser.parseRegion(currRegion)) {
      handlerRegions.clear();
      return failure();
    }

    if (currRegion.empty())
      return parser.emitError(regionLoc, "handler region shall not be empty");

    if (!(currRegion.back().mightHaveTerminator() &&
          currRegion.back().getTerminator()))
      return parser.emitError(
          regionLoc, "blocks are expected to be explicitly terminated");

    return success();
  };

  bool hasCatchAll = false;
  llvm::SmallVector<mlir::Attribute, 4> catcherAttrs;
  while (parser.parseOptionalKeyword("catch").succeeded()) {
    bool hasLSquare = parser.parseOptionalLSquare().succeeded();

    llvm::StringRef attrStr;
    if (parser.parseOptionalKeyword(&attrStr, {"all", "type"}).failed())
      return parser.emitError(parser.getCurrentLocation(),
                              "expected 'all' or 'type' keyword");

    bool isCatchAll = attrStr == "all";
    if (isCatchAll) {
      if (hasCatchAll)
        return parser.emitError(parser.getCurrentLocation(),
                                "can't have more than one catch all");
      hasCatchAll = true;
    }

    mlir::Attribute exceptionRTTIAttr;
    if (!isCatchAll && parser.parseAttribute(exceptionRTTIAttr).failed())
      return parser.emitError(parser.getCurrentLocation(),
                              "expected valid RTTI info attribute");

    catcherAttrs.push_back(isCatchAll
                               ? cir::CatchAllAttr::get(parser.getContext())
                               : exceptionRTTIAttr);

    if (hasLSquare && isCatchAll)
      return parser.emitError(parser.getCurrentLocation(),
                              "catch all dosen't need RTTI info attribute");

    if (hasLSquare && parser.parseRSquare().failed())
      return parser.emitError(parser.getCurrentLocation(),
                              "expected `]` after RTTI info attribute");

    if (parseCheckedCatcherRegion().failed())
      return mlir::failure();
  }

  if (parser.parseOptionalKeyword("unwind").succeeded()) {
    if (hasCatchAll)
      return parser.emitError(parser.getCurrentLocation(),
                              "unwind can't be used with catch all");

    catcherAttrs.push_back(cir::UnwindAttr::get(parser.getContext()));
    if (parseCheckedCatcherRegion().failed())
      return mlir::failure();
  }

  handlerTypes = parser.getBuilder().getArrayAttr(catcherAttrs);
  return mlir::success();
}

//===----------------------------------------------------------------------===//
// EhTypeIdOp
//===----------------------------------------------------------------------===//

LogicalResult
cir::EhTypeIdOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  Operation *op =
      lookupNearestOrModuleSymbolFastPath(*this, getTypeSymAttr(), symbolTable);
  if (!isa_and_nonnull<GlobalOp>(op))
    return emitOpError("'")
           << getTypeSym() << "' does not reference a valid cir.global";
  return success();
}

//===----------------------------------------------------------------------===//
// CatchParamOp
//===----------------------------------------------------------------------===//

LogicalResult cir::CatchParamOp::verify() {
  if (getExceptionPtr()) {
    auto kind = getKind();
    if (!kind || (*kind != cir::CatchParamKind::Begin &&
                  *kind != cir::CatchParamKind::GetExceptionPtr))
      return emitOpError(
          "needs 'begin' or 'get_exception_ptr' to work with exception "
          "pointer");
    return success();
  }
  if (!getKind() && !(*this)->getParentOfType<cir::TryOp>())
    return emitOpError("without 'kind' requires 'cir.try' surrounding scope");
  return success();
}

//===----------------------------------------------------------------------===//
// Forward declarations for address space parsing/printing
//===----------------------------------------------------------------------===//

mlir::ParseResult
parseGlobalAddressSpaceValue(mlir::AsmParser &p,
                             mlir::ptr::MemorySpaceAttrInterface &attr);

void printGlobalAddressSpaceValue(mlir::AsmPrinter &printer, cir::GlobalOp op,
                                  mlir::ptr::MemorySpaceAttrInterface attr);

//===----------------------------------------------------------------------===//
// AssumeAlignedOp
//===----------------------------------------------------------------------===//

mlir::LogicalResult cir::AssumeAlignedOp::verify() {
  bool hasConstant = getAlignmentAttr() != nullptr;
  bool hasDynamic = getAlignmentValue() != nullptr;
  if (hasConstant == hasDynamic)
    return emitOpError("requires exactly one of 'alignment' attribute or "
                       "'alignment_value' operand");
  return mlir::success();
}

mlir::LogicalResult cir::StoreOp::verify() {
  auto addrPtrTy = mlir::cast<cir::PointerType>(getAddr().getType());
  mlir::Type pointeeTy = addrPtrTy.getPointee();
  mlir::Type valueTy = getValue().getType();

  if (pointeeTy == valueTy)
    return mlir::success();

  // Allow pointer subtype mismatches — CIR's typed pointers can have
  // different pointee types for the same logical pointer due to
  // self-referential template types and placeholder function types.
  if (mlir::isa<cir::PointerType>(pointeeTy) &&
      mlir::isa<cir::PointerType>(valueTy))
    return mlir::success();

  return emitOpError("failed to verify that type of 'value' matches "
                     "pointee type of 'addr'");
}

mlir::ParseResult cir::AssumeAlignedOp::parse(mlir::OpAsmParser &parser,
                                               mlir::OperationState &result) {
  mlir::OpAsmParser::UnresolvedOperand pointer;
  mlir::Type ptrType;

  if (parser.parseOperand(pointer))
    return mlir::failure();

  // Parse alignment: either "alignment <int>" or "[align_value %val : type]"
  mlir::IntegerAttr alignAttr;
  mlir::OpAsmParser::UnresolvedOperand alignValue;
  mlir::Type alignValueType;
  bool hasDynamicAlign = false;

  if (parser.parseOptionalKeyword("alignment").succeeded()) {
    int64_t alignVal;
    if (parser.parseInteger(alignVal))
      return mlir::failure();
    alignAttr = parser.getBuilder().getI64IntegerAttr(alignVal);
    result.addAttribute("alignment", alignAttr);
  } else if (parser.parseOptionalLSquare().succeeded()) {
    if (parser.parseKeyword("align_value") ||
        parser.parseOperand(alignValue) ||
        parser.parseColonType(alignValueType) ||
        parser.parseRSquare())
      return mlir::failure();
    hasDynamicAlign = true;
  } else {
    return parser.emitError(parser.getCurrentLocation(),
                            "expected 'alignment' or '[align_value ...]'");
  }

  // Parse optional offset
  mlir::OpAsmParser::UnresolvedOperand offsetValue;
  mlir::Type offsetType;
  bool hasOffset = false;
  if (parser.parseOptionalLSquare().succeeded()) {
    if (parser.parseKeyword("offset") ||
        parser.parseOperand(offsetValue) ||
        parser.parseColonType(offsetType) ||
        parser.parseRSquare())
      return mlir::failure();
    hasOffset = true;
  }

  if (parser.parseColon() || parser.parseCustomTypeWithFallback(ptrType))
    return mlir::failure();

  if (parser.parseOptionalAttrDict(result.attributes))
    return mlir::failure();

  if (parser.resolveOperand(pointer, ptrType, result.operands))
    return mlir::failure();

  if (hasDynamicAlign) {
    if (parser.resolveOperand(alignValue, alignValueType, result.operands))
      return mlir::failure();
  }

  if (hasOffset) {
    if (parser.resolveOperand(offsetValue, offsetType, result.operands))
      return mlir::failure();
  }

  // operand_segment_sizes: [pointer=1, alignment_value=0or1, offset=0or1]
  result.addAttribute("operandSegmentSizes",
                       parser.getBuilder().getDenseI32ArrayAttr(
                           {1, hasDynamicAlign ? 1 : 0, hasOffset ? 1 : 0}));

  result.addTypes(ptrType);
  return mlir::success();
}

void cir::AssumeAlignedOp::print(mlir::OpAsmPrinter &p) {
  p << " " << getPointer();

  if (auto alignAttr = getAlignmentAttr()) {
    p << " alignment " << alignAttr.getInt();
  } else {
    p << " [align_value " << getAlignmentValue() << " : "
      << getAlignmentValue().getType() << "]";
  }

  if (auto offset = getOffset()) {
    p << " [offset " << offset << " : " << offset.getType() << "]";
  }

  p << " : " << getPointer().getType();
  p.printOptionalAttrDict((*this)->getAttrs(),
                          {"alignment", "operandSegmentSizes"});
}

//===----------------------------------------------------------------------===//
// TableGen'd op method definitions
//===----------------------------------------------------------------------===//

#define GET_OP_CLASSES
#include "clang/CIR/Dialect/IR/CIROps.cpp.inc"

//===----------------------------------------------------------------------===//
// Pipeline stage
//===----------------------------------------------------------------------===//

static constexpr llvm::StringLiteral cirStageNames[] = {"cirgen", "cir-prepare",
                                                        "cir-abi", "cir-flat"};

llvm::StringRef cir::stringifyCIRStage(CIRStage stage) {
  return cirStageNames[static_cast<unsigned>(stage)];
}

std::optional<cir::CIRStage> cir::symbolizeCIRStage(llvm::StringRef name) {
  for (auto [idx, stageName] : llvm::enumerate(cirStageNames))
    if (name == stageName)
      return static_cast<CIRStage>(idx);
  return std::nullopt;
}

std::optional<cir::CIRStage> cir::getStage(mlir::Operation *op) {
  auto stage =
      op->getAttrOfType<mlir::StringAttr>(CIRDialect::getStageAttrName());
  if (!stage)
    return std::nullopt;
  return symbolizeCIRStage(stage.getValue());
}

void cir::advanceStage(mlir::Operation *op, CIRStage stage) {
  if (!mlir::isa<mlir::ModuleOp>(op))
    return;
  std::optional<CIRStage> current = getStage(op);
  if (current && *current >= stage)
    return;
  op->setAttr(
      CIRDialect::getStageAttrName(),
      mlir::StringAttr::get(op->getContext(), stringifyCIRStage(stage)));
}

void cir::clearStage(mlir::Operation *op) {
  op->removeAttr(CIRDialect::getStageAttrName());
}

llvm::LogicalResult
cir::CIRDialect::verifyOperationAttribute(mlir::Operation *op,
                                          mlir::NamedAttribute attr) {
  if (attr.getName() != getStageAttrName())
    return mlir::success();
  if (!mlir::isa<mlir::ModuleOp>(op))
    return op->emitOpError()
           << "'" << getStageAttrName() << "' belongs on a module";
  auto stage = mlir::dyn_cast<mlir::StringAttr>(attr.getValue());
  if (!stage || !symbolizeCIRStage(stage.getValue()))
    return op->emitOpError()
           << "'" << getStageAttrName()
           << "' must be one of cirgen, cir-prepare, cir-abi, cir-flat";
  return mlir::success();
}
