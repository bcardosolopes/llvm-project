//===- CIRAttrs.cpp - MLIR CIR Attributes ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the attributes in the CIR dialect.
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"

#include "mlir/Dialect/LLVMIR/LLVMTypes.h"
#include "mlir/IR/DialectImplementation.h"
#include "clang/AST/DeclCXX.h"
#include "llvm/ADT/TypeSwitch.h"

//===-----------------------------------------------------------------===//
// RecordMembers
//===-----------------------------------------------------------------===//

static void printRecordMembers(mlir::AsmPrinter &p, mlir::ArrayAttr members);
static mlir::ParseResult parseRecordMembers(mlir::AsmParser &parser,
                                            mlir::ArrayAttr &members);

//===-----------------------------------------------------------------===//
// IntLiteral
//===-----------------------------------------------------------------===//

static void printIntLiteral(mlir::AsmPrinter &p, llvm::APInt value,
                            cir::IntTypeInterface ty);
static mlir::ParseResult parseIntLiteral(mlir::AsmParser &parser,
                                         llvm::APInt &value,
                                         cir::IntTypeInterface ty);
//===-----------------------------------------------------------------===//
// FloatLiteral
//===-----------------------------------------------------------------===//

static void printFloatLiteral(mlir::AsmPrinter &p, llvm::APFloat value,
                              mlir::Type ty);
static mlir::ParseResult
parseFloatLiteral(mlir::AsmParser &parser,
                  mlir::FailureOr<llvm::APFloat> &value,
                  cir::FPTypeInterface fpType);

//===----------------------------------------------------------------------===//
// AddressSpaceAttr
//===----------------------------------------------------------------------===//

mlir::ParseResult parseTargetAddressSpace(mlir::AsmParser &p,
                                          cir::TargetAddressSpaceAttr &attr);

void printTargetAddressSpace(mlir::AsmPrinter &p,
                             cir::TargetAddressSpaceAttr attr);

static mlir::ParseResult parseConstPtr(mlir::AsmParser &parser,
                                       mlir::IntegerAttr &value);

static void printConstPtr(mlir::AsmPrinter &p, mlir::IntegerAttr value);

#define GET_ATTRDEF_CLASSES
#include "clang/CIR/Dialect/IR/CIROpsAttributes.cpp.inc"

using namespace mlir;
using namespace cir;

//===----------------------------------------------------------------------===//
// MemorySpaceAttrInterface implementations for Lang and Target address space
// attributes
//===----------------------------------------------------------------------===//
namespace cir {

bool LangAddressSpaceAttr::isValidLoad(
    mlir::Type type, mlir::ptr::AtomicOrdering ordering,
    std::optional<int64_t> alignment, const mlir::DataLayout *dataLayout,
    llvm::function_ref<mlir::InFlightDiagnostic()> emitError) const {
  assert(false && "NYI");
  return false;
}

bool LangAddressSpaceAttr::isValidStore(
    mlir::Type type, mlir::ptr::AtomicOrdering ordering,
    std::optional<int64_t> alignment, const mlir::DataLayout *dataLayout,
    llvm::function_ref<mlir::InFlightDiagnostic()> emitError) const {
  assert(false && "NYI");
  return false;
}

bool LangAddressSpaceAttr::isValidAtomicOp(
    mlir::ptr::AtomicBinOp op, mlir::Type type,
    mlir::ptr::AtomicOrdering ordering, std::optional<int64_t> alignment,
    const mlir::DataLayout *dataLayout,
    llvm::function_ref<mlir::InFlightDiagnostic()> emitError) const {
  assert(false && "NYI");
  return false;
}

bool LangAddressSpaceAttr::isValidAtomicXchg(
    mlir::Type type, mlir::ptr::AtomicOrdering successOrdering,
    mlir::ptr::AtomicOrdering failureOrdering, std::optional<int64_t> alignment,
    const mlir::DataLayout *dataLayout,
    llvm::function_ref<mlir::InFlightDiagnostic()> emitError) const {
  assert(false && "NYI");
  return false;
}

bool LangAddressSpaceAttr::isValidAddrSpaceCast(
    mlir::Type tgt, mlir::Type src,
    llvm::function_ref<mlir::InFlightDiagnostic()> emitError) const {
  assert(false && "NYI");
  return false;
}

bool LangAddressSpaceAttr::isValidPtrIntCast(
    mlir::Type intLikeTy, mlir::Type ptrLikeTy,
    llvm::function_ref<mlir::InFlightDiagnostic()> emitError) const {
  assert(false && "NYI");
  return false;
}

bool TargetAddressSpaceAttr::isValidLoad(
    mlir::Type type, mlir::ptr::AtomicOrdering ordering,
    std::optional<int64_t> alignment, const mlir::DataLayout *dataLayout,
    llvm::function_ref<mlir::InFlightDiagnostic()> emitError) const {
  assert(false && "NYI");
  return false;
}

bool TargetAddressSpaceAttr::isValidStore(
    mlir::Type type, mlir::ptr::AtomicOrdering ordering,
    std::optional<int64_t> alignment, const mlir::DataLayout *dataLayout,
    llvm::function_ref<mlir::InFlightDiagnostic()> emitError) const {
  assert(false && "NYI");
  return false;
}

bool TargetAddressSpaceAttr::isValidAtomicOp(
    mlir::ptr::AtomicBinOp op, mlir::Type type,
    mlir::ptr::AtomicOrdering ordering, std::optional<int64_t> alignment,
    const mlir::DataLayout *dataLayout,
    llvm::function_ref<mlir::InFlightDiagnostic()> emitError) const {
  assert(false && "NYI");
  return false;
}

bool TargetAddressSpaceAttr::isValidAtomicXchg(
    mlir::Type type, mlir::ptr::AtomicOrdering successOrdering,
    mlir::ptr::AtomicOrdering failureOrdering, std::optional<int64_t> alignment,
    const mlir::DataLayout *dataLayout,
    llvm::function_ref<mlir::InFlightDiagnostic()> emitError) const {
  assert(false && "NYI");
  return false;
}

bool TargetAddressSpaceAttr::isValidAddrSpaceCast(
    mlir::Type tgt, mlir::Type src,
    llvm::function_ref<mlir::InFlightDiagnostic()> emitError) const {
  assert(false && "NYI");
  return false;
}

bool TargetAddressSpaceAttr::isValidPtrIntCast(
    mlir::Type intLikeTy, mlir::Type ptrLikeTy,
    llvm::function_ref<mlir::InFlightDiagnostic()> emitError) const {
  assert(false && "NYI");
  return false;
}

} // namespace cir

//===----------------------------------------------------------------------===//
// General CIR parsing / printing
//===----------------------------------------------------------------------===//

Attribute CIRDialect::parseAttribute(DialectAsmParser &parser,
                                     Type type) const {
  llvm::SMLoc typeLoc = parser.getCurrentLocation();
  llvm::StringRef mnemonic;
  Attribute genAttr;

  // First, try to parse the #cir<mnemonic ...> format where the mnemonic
  // is inside angle brackets.
  if (succeeded(parser.parseOptionalLess())) {
    // We're in the #cir<...> format. Parse the mnemonic keyword.
    if (failed(parser.parseKeyword(&mnemonic)))
      return Attribute();

    // Use the mnemonic to dispatch to the appropriate attribute parser.
    OptionalParseResult parseResult =
        generatedAttributeParser(parser, &mnemonic, type, genAttr);
    if (parseResult.has_value()) {
      // Parse the closing '>'
      if (failed(parser.parseGreater()))
        return Attribute();
      return genAttr;
    }
    parser.emitError(typeLoc, "unknown attribute `")
        << mnemonic << "` in dialect `cir`";
    return Attribute();
  }

  // Standard #cir.mnemonic<...> format
  OptionalParseResult parseResult =
      generatedAttributeParser(parser, &mnemonic, type, genAttr);
  if (parseResult.has_value())
    return genAttr;
  parser.emitError(typeLoc, "unknown attribute in CIR dialect");
  return Attribute();
}

void CIRDialect::printAttribute(Attribute attr, DialectAsmPrinter &os) const {
  if (failed(generatedAttributePrinter(attr, os)))
    llvm_unreachable("unexpected CIR attribute kind");
}

static void printRecordMembers(mlir::AsmPrinter &printer,
                               mlir::ArrayAttr members) {
  printer << '{';
  llvm::interleaveComma(members, printer);
  printer << '}';
}

static ParseResult parseRecordMembers(mlir::AsmParser &parser,
                                      mlir::ArrayAttr &members) {
  llvm::SmallVector<mlir::Attribute, 4> elts;

  auto delimiter = AsmParser::Delimiter::Braces;
  auto result = parser.parseCommaSeparatedList(delimiter, [&]() {
    mlir::TypedAttr attr;
    if (parser.parseAttribute(attr).failed())
      return mlir::failure();
    elts.push_back(attr);
    return mlir::success();
  });

  if (result.failed())
    return mlir::failure();

  members = mlir::ArrayAttr::get(parser.getContext(), elts);
  return mlir::success();
}

//===----------------------------------------------------------------------===//
// ConstRecordAttr definitions
//===----------------------------------------------------------------------===//

LogicalResult
ConstRecordAttr::verify(function_ref<InFlightDiagnostic()> emitError,
                        mlir::Type type, ArrayAttr members) {
  auto sTy = mlir::dyn_cast_if_present<cir::RecordType>(type);
  if (!sTy)
    return emitError() << "expected !cir.record type";

  if (sTy.getMembers().size() != members.size())
    return emitError() << "number of elements must match";

  unsigned attrIdx = 0;
  for (auto &member : sTy.getMembers()) {
    auto m = mlir::cast<mlir::TypedAttr>(members[attrIdx]);
    if (member != m.getType())
      return emitError() << "element at index " << attrIdx << " has type "
                         << m.getType()
                         << " but the expected type for this element is "
                         << member;
    attrIdx++;
  }

  return success();
}

//===----------------------------------------------------------------------===//
// OptInfoAttr definitions
//===----------------------------------------------------------------------===//

LogicalResult OptInfoAttr::verify(function_ref<InFlightDiagnostic()> emitError,
                                  unsigned level, unsigned size) {
  if (level > 3)
    return emitError()
           << "optimization level must be between 0 and 3 inclusive";
  if (size > 2)
    return emitError()
           << "size optimization level must be between 0 and 2 inclusive";
  return success();
}

//===----------------------------------------------------------------------===//
// ConstPtrAttr definitions
//===----------------------------------------------------------------------===//

// TODO(CIR): Consider encoding the null value differently and use conditional
// assembly format instead of custom parsing/printing.
static ParseResult parseConstPtr(AsmParser &parser, mlir::IntegerAttr &value) {

  if (parser.parseOptionalKeyword("null").succeeded()) {
    value = parser.getBuilder().getI64IntegerAttr(0);
    return success();
  }

  return parser.parseAttribute(value);
}

static void printConstPtr(AsmPrinter &p, mlir::IntegerAttr value) {
  if (!value.getInt())
    p << "null";
  else
    p << value;
}

//===----------------------------------------------------------------------===//
// IntAttr definitions
//===----------------------------------------------------------------------===//

template <typename IntT>
static bool isTooLargeForType(const mlir::APInt &value, IntT expectedValue) {
  if constexpr (std::is_signed_v<IntT>) {
    return value.getSExtValue() != expectedValue;
  } else {
    return value.getZExtValue() != expectedValue;
  }
}

template <typename IntT>
static mlir::ParseResult parseIntLiteralImpl(mlir::AsmParser &p,
                                             llvm::APInt &value,
                                             cir::IntTypeInterface ty) {
  IntT ivalue;
  const bool isSigned = ty.isSigned();
  if (p.parseInteger(ivalue))
    return p.emitError(p.getCurrentLocation(), "expected integer value");

  value = mlir::APInt(ty.getWidth(), ivalue, isSigned, /*implicitTrunc=*/true);
  if (isTooLargeForType(value, ivalue))
    return p.emitError(p.getCurrentLocation(),
                       "integer value too large for the given type");

  return success();
}

mlir::ParseResult parseIntLiteral(mlir::AsmParser &parser, llvm::APInt &value,
                                  cir::IntTypeInterface ty) {
  if (ty.isSigned())
    return parseIntLiteralImpl<int64_t>(parser, value, ty);
  return parseIntLiteralImpl<uint64_t>(parser, value, ty);
}

void printIntLiteral(mlir::AsmPrinter &p, llvm::APInt value,
                     cir::IntTypeInterface ty) {
  if (ty.isSigned())
    p << value.getSExtValue();
  else
    p << value.getZExtValue();
}

LogicalResult IntAttr::verify(function_ref<InFlightDiagnostic()> emitError,
                              cir::IntTypeInterface type, llvm::APInt value) {
  if (value.getBitWidth() != type.getWidth())
    return emitError() << "type and value bitwidth mismatch: "
                       << type.getWidth() << " != " << value.getBitWidth();
  return success();
}

//===----------------------------------------------------------------------===//
// FPAttr definitions
//===----------------------------------------------------------------------===//

static void printFloatLiteral(AsmPrinter &p, APFloat value, Type ty) {
  p << value;
}

static ParseResult parseFloatLiteral(AsmParser &parser,
                                     FailureOr<APFloat> &value,
                                     cir::FPTypeInterface fpType) {

  APFloat parsedValue(0.0);
  if (parser.parseFloat(fpType.getFloatSemantics(), parsedValue))
    return failure();

  value.emplace(parsedValue);
  return success();
}

FPAttr FPAttr::getZero(Type type) {
  return get(type,
             APFloat::getZero(
                 mlir::cast<cir::FPTypeInterface>(type).getFloatSemantics()));
}

LogicalResult FPAttr::verify(function_ref<InFlightDiagnostic()> emitError,
                             cir::FPTypeInterface fpType, APFloat value) {
  if (APFloat::SemanticsToEnum(fpType.getFloatSemantics()) !=
      APFloat::SemanticsToEnum(value.getSemantics()))
    return emitError() << "floating-point semantics mismatch";

  return success();
}

//===----------------------------------------------------------------------===//
// ConstComplexAttr definitions
//===----------------------------------------------------------------------===//

LogicalResult
ConstComplexAttr::verify(function_ref<InFlightDiagnostic()> emitError,
                         cir::ComplexType type, mlir::TypedAttr real,
                         mlir::TypedAttr imag) {
  mlir::Type elemType = type.getElementType();
  if (real.getType() != elemType)
    return emitError()
           << "type of the real part does not match the complex type";

  if (imag.getType() != elemType)
    return emitError()
           << "type of the imaginary part does not match the complex type";

  return success();
}

//===----------------------------------------------------------------------===//
// CUDAVarRegistrationInfoAttr definitions
//===----------------------------------------------------------------------===//

void CUDAVarRegistrationInfoAttr::print(AsmPrinter &p) const {
  p << "<" << stringifyEnum(getKind());
  if (getIsExtern())
    p << ", extern";
  if (getIsConstant())
    p << ", constant";
  if (getIsManaged())
    p << ", managed";
  p << ">";
}

Attribute CUDAVarRegistrationInfoAttr::parse(AsmParser &parser, Type odsType) {
  if (parser.parseLess())
    return {};

  // Parse the device variable kind (Variable, Surface, Texture)
  StringRef kindStr;
  if (parser.parseKeyword(&kindStr))
    return {};

  auto kind = symbolizeCUDADeviceVarKind(kindStr);
  if (!kind) {
    parser.emitError(parser.getCurrentLocation(),
                     "unknown device variable kind: ")
        << kindStr;
    return {};
  }

  // Parse optional flags: extern, constant, managed
  bool isExtern = false;
  bool isConstant = false;
  bool isManaged = false;

  while (parser.parseOptionalGreater().failed()) {
    if (parser.parseComma())
      return {};

    StringRef flag;
    if (parser.parseKeyword(&flag))
      return {};

    if (flag == "extern")
      isExtern = true;
    else if (flag == "constant")
      isConstant = true;
    else if (flag == "managed")
      isManaged = true;
    else {
      parser.emitError(parser.getCurrentLocation(), "unknown flag: ") << flag;
      return {};
    }
  }

  return get(parser.getContext(), *kind, isExtern, isConstant, isManaged);
}

//===----------------------------------------------------------------------===//
// DataMemberAttr definitions
//===----------------------------------------------------------------------===//

LogicalResult
DataMemberAttr::verify(function_ref<InFlightDiagnostic()> emitError,
                       cir::DataMemberType ty,
                       std::optional<unsigned> memberIndex) {
  // DataMemberAttr without a given index represents a null value.
  if (!memberIndex.has_value())
    return success();

  cir::RecordType recTy = ty.getClassTy();
  if (recTy.isIncomplete())
    return emitError()
           << "incomplete 'cir.record' cannot be used to build a non-null "
              "data member pointer";

  unsigned memberIndexValue = memberIndex.value();
  if (memberIndexValue >= recTy.getNumElements())
    return emitError()
           << "member index of a #cir.data_member attribute is out of range";

  mlir::Type memberTy = recTy.getMembers()[memberIndexValue];
  if (memberTy != ty.getMemberTy())
    return emitError()
           << "member type of a #cir.data_member attribute must match the "
              "attribute type";

  return success();
}

//===----------------------------------------------------------------------===//
// MethodAttr definitions
//===----------------------------------------------------------------------===//

LogicalResult MethodAttr::verify(function_ref<InFlightDiagnostic()> emitError,
                                 cir::MethodType type,
                                 std::optional<FlatSymbolRefAttr> symbol,
                                 std::optional<uint64_t> vtable_offset) {
  if (symbol.has_value() && vtable_offset.has_value())
    return emitError()
           << "at most one of symbol and vtable_offset can be present "
              "in #cir.method";

  return success();
}

Attribute MethodAttr::parse(AsmParser &parser, Type odsType) {
  auto ty = mlir::cast<cir::MethodType>(odsType);

  if (parser.parseLess().failed())
    return {};

  // Try to parse the null pointer constant.
  if (parser.parseOptionalKeyword("null").succeeded()) {
    if (parser.parseGreater().failed())
      return {};
    return get(ty);
  }

  // Try to parse a flat symbol ref for a pointer to non-virtual member
  // function.
  FlatSymbolRefAttr symbol;
  mlir::OptionalParseResult parseSymbolRefResult =
      parser.parseOptionalAttribute(symbol);
  if (parseSymbolRefResult.has_value()) {
    if (parseSymbolRefResult.value().failed())
      return {};
    if (parser.parseGreater().failed())
      return {};
    return get(ty, symbol);
  }

  // Parse a uint64 that represents the vtable offset.
  std::uint64_t vtableOffset = 0;
  if (parser.parseKeyword("vtable_offset"))
    return {};
  if (parser.parseEqual())
    return {};
  if (parser.parseInteger(vtableOffset))
    return {};

  if (parser.parseGreater().failed())
    return {};

  return get(ty, vtableOffset);
}

void MethodAttr::print(AsmPrinter &printer) const {
  auto symbol = getSymbol();
  auto vtableOffset = getVtableOffset();

  printer << '<';
  if (symbol.has_value()) {
    printer << *symbol;
  } else if (vtableOffset.has_value()) {
    printer << "vtable_offset = " << *vtableOffset;
  } else {
    printer << "null";
  }
  printer << '>';
}

//===----------------------------------------------------------------------===//
// CIR ConstArrayAttr
//===----------------------------------------------------------------------===//

LogicalResult
ConstArrayAttr::verify(function_ref<InFlightDiagnostic()> emitError, Type type,
                       Attribute elts, int trailingZerosNum) {

  if (!(mlir::isa<ArrayAttr, StringAttr>(elts)))
    return emitError() << "constant array expects ArrayAttr or StringAttr";

  if (auto strAttr = mlir::dyn_cast<StringAttr>(elts)) {
    const auto arrayTy = mlir::cast<ArrayType>(type);
    const auto intTy = mlir::dyn_cast<IntType>(arrayTy.getElementType());

    // TODO: add CIR type for char.
    if (!intTy || intTy.getWidth() != 8)
      return emitError()
             << "constant array element for string literals expects "
                "!cir.int<u, 8> element type";
    return success();
  }

  assert(mlir::isa<ArrayAttr>(elts));
  const auto arrayAttr = mlir::cast<mlir::ArrayAttr>(elts);
  const auto arrayTy = mlir::cast<ArrayType>(type);

  // Make sure both number of elements and subelement types match type.
  if (arrayTy.getSize() != arrayAttr.size() + trailingZerosNum)
    return emitError() << "constant array size should match type size";
  return success();
}

Attribute ConstArrayAttr::parse(AsmParser &parser, Type type) {
  mlir::FailureOr<Type> resultTy;
  mlir::FailureOr<Attribute> resultVal;

  // Parse literal '<'
  if (parser.parseLess())
    return {};

  // Parse variable 'value'
  resultVal = FieldParser<Attribute>::parse(parser);
  if (failed(resultVal)) {
    parser.emitError(
        parser.getCurrentLocation(),
        "failed to parse ConstArrayAttr parameter 'value' which is "
        "to be a `Attribute`");
    return {};
  }

  // ArrayAttrrs have per-element type, not the type of the array...
  if (mlir::isa<ArrayAttr>(*resultVal)) {
    // Array has implicit type: infer from const array type.
    if (parser.parseOptionalColon().failed()) {
      resultTy = type;
    } else { // Array has explicit type: parse it.
      resultTy = FieldParser<Type>::parse(parser);
      if (failed(resultTy)) {
        parser.emitError(
            parser.getCurrentLocation(),
            "failed to parse ConstArrayAttr parameter 'type' which is "
            "to be a `::mlir::Type`");
        return {};
      }
    }
  } else {
    auto ta = mlir::cast<TypedAttr>(*resultVal);
    resultTy = ta.getType();
    if (mlir::isa<mlir::NoneType>(*resultTy)) {
      parser.emitError(parser.getCurrentLocation(),
                       "expected type declaration for string literal");
      return {};
    }
  }

  unsigned zeros = 0;
  if (parser.parseOptionalComma().succeeded()) {
    if (parser.parseOptionalKeyword("trailing_zeros").succeeded()) {
      unsigned typeSize =
          mlir::cast<cir::ArrayType>(resultTy.value()).getSize();
      mlir::Attribute elts = resultVal.value();
      if (auto str = mlir::dyn_cast<mlir::StringAttr>(elts))
        zeros = typeSize - str.size();
      else
        zeros = typeSize - mlir::cast<mlir::ArrayAttr>(elts).size();
    } else {
      return {};
    }
  }

  // Parse literal '>'
  if (parser.parseGreater())
    return {};

  return parser.getChecked<ConstArrayAttr>(
      parser.getCurrentLocation(), parser.getContext(), resultTy.value(),
      resultVal.value(), zeros);
}

void ConstArrayAttr::print(AsmPrinter &printer) const {
  printer << "<";
  printer.printStrippedAttrOrType(getElts());
  if (getTrailingZerosNum())
    printer << ", trailing_zeros";
  printer << ">";
}

//===----------------------------------------------------------------------===//
// CIR ConstVectorAttr
//===----------------------------------------------------------------------===//

LogicalResult
cir::ConstVectorAttr::verify(function_ref<InFlightDiagnostic()> emitError,
                             Type type, ArrayAttr elts) {

  if (!mlir::isa<cir::VectorType>(type))
    return emitError() << "type of cir::ConstVectorAttr is not a "
                          "cir::VectorType: "
                       << type;

  const auto vecType = mlir::cast<cir::VectorType>(type);

  if (vecType.getSize() != elts.size())
    return emitError()
           << "number of constant elements should match vector size";

  // Check if the types of the elements match
  LogicalResult elementTypeCheck = success();
  elts.walkImmediateSubElements(
      [&](Attribute element) {
        if (elementTypeCheck.failed()) {
          // An earlier element didn't match
          return;
        }
        auto typedElement = mlir::dyn_cast<TypedAttr>(element);
        if (!typedElement ||
            typedElement.getType() != vecType.getElementType()) {
          elementTypeCheck = failure();
          emitError() << "constant type should match vector element type";
        }
      },
      [&](Type) {});

  return elementTypeCheck;
}

//===----------------------------------------------------------------------===//
// CIR VTableAttr
//===----------------------------------------------------------------------===//

LogicalResult cir::VTableAttr::verify(
    llvm::function_ref<mlir::InFlightDiagnostic()> emitError, mlir::Type type,
    mlir::ArrayAttr data) {
  auto sTy = mlir::dyn_cast_if_present<cir::RecordType>(type);
  if (!sTy)
    return emitError() << "expected !cir.record type result";
  if (sTy.getMembers().empty() || data.empty())
    return emitError() << "expected record type with one or more subtype";

  if (cir::ConstRecordAttr::verify(emitError, type, data).failed())
    return failure();

  for (const auto &element : data.getAsRange<mlir::Attribute>()) {
    const auto &constArrayAttr = mlir::dyn_cast<cir::ConstArrayAttr>(element);
    if (!constArrayAttr)
      return emitError() << "expected constant array subtype";

    LogicalResult eltTypeCheck = success();
    auto arrayElts = mlir::cast<ArrayAttr>(constArrayAttr.getElts());
    arrayElts.walkImmediateSubElements(
        [&](mlir::Attribute attr) {
          if (mlir::isa<ConstPtrAttr, GlobalViewAttr>(attr))
            return;

          eltTypeCheck = emitError()
                         << "expected GlobalViewAttr or ConstPtrAttr";
        },
        [&](mlir::Type type) {});
    if (eltTypeCheck.failed())
      return eltTypeCheck;
  }
  return success();
}

//===----------------------------------------------------------------------===//
// CmpThreeWayInfoAttr definitions
//===----------------------------------------------------------------------===//

std::string CmpThreeWayInfoAttr::getAlias() const {
  std::string alias = "cmp3way_info";

  if (getOrdering() == CmpOrdering::Strong)
    alias.append("_strong_");
  else
    alias.append("_partial_");

  auto appendInt = [&](int64_t value) {
    if (value < 0) {
      alias.push_back('n');
      value = -value;
    }
    alias.append(std::to_string(value));
  };

  alias.append("lt");
  appendInt(getLt());
  alias.append("eq");
  appendInt(getEq());
  alias.append("gt");
  appendInt(getGt());

  if (auto unordered = getUnordered()) {
    alias.append("un");
    appendInt(unordered.value());
  }

  return alias;
}

LogicalResult
CmpThreeWayInfoAttr::verify(function_ref<InFlightDiagnostic()> emitError,
                            CmpOrdering ordering, int64_t lt, int64_t eq,
                            int64_t gt, std::optional<int64_t> unordered) {
  // The presense of unordered must match the value of ordering.
  if (ordering == CmpOrdering::Strong && unordered)
    return emitError() << "strong ordering does not include unordered ordering";

  if (ordering == CmpOrdering::Partial && !unordered)
    return emitError() << "partial ordering lacks unordered ordering";

  return success();
}

//===----------------------------------------------------------------------===//
// DynamicCastInfoAtttr definitions
//===----------------------------------------------------------------------===//

std::string DynamicCastInfoAttr::getAlias() const {
  // The alias looks like: `dyn_cast_info_<src>_<dest>`

  std::string alias = "dyn_cast_info_";

  alias.append(getSrcRtti().getSymbol().getValue());
  alias.push_back('_');
  alias.append(getDestRtti().getSymbol().getValue());

  return alias;
}

LogicalResult DynamicCastInfoAttr::verify(
    function_ref<InFlightDiagnostic()> emitError, cir::GlobalViewAttr srcRtti,
    cir::GlobalViewAttr destRtti, mlir::FlatSymbolRefAttr runtimeFunc,
    mlir::FlatSymbolRefAttr badCastFunc, cir::IntAttr offsetHint) {
  auto isRttiPtr = [](mlir::Type ty) {
    // RTTI pointers are !cir.ptr<!u8i>.

    auto ptrTy = mlir::dyn_cast<cir::PointerType>(ty);
    if (!ptrTy)
      return false;

    auto pointeeIntTy = mlir::dyn_cast<cir::IntType>(ptrTy.getPointee());
    if (!pointeeIntTy)
      return false;

    return pointeeIntTy.isUnsigned() && pointeeIntTy.getWidth() == 8;
  };

  if (!isRttiPtr(srcRtti.getType()))
    return emitError() << "srcRtti must be an RTTI pointer";

  if (!isRttiPtr(destRtti.getType()))
    return emitError() << "destRtti must be an RTTI pointer";

  return success();
}

//===----------------------------------------------------------------------===//
// GlobalAnnotationValuesAttr
//===----------------------------------------------------------------------===//

LogicalResult
GlobalAnnotationValuesAttr::verify(function_ref<InFlightDiagnostic()> emitError,
                                   mlir::ArrayAttr annotations) {
  if (annotations.empty())
    return emitError() << "GlobalAnnotationValuesAttr should at least have "
                          "one annotation";

  for (auto &entry : annotations) {
    auto annoEntry = mlir::dyn_cast<mlir::ArrayAttr>(entry);
    if (!annoEntry)
      return emitError()
             << "Element of GlobalAnnotationValuesAttr annotations array"
                " must be an array";

    if (annoEntry.size() != 2)
      return emitError()
             << "Element of GlobalAnnotationValuesAttr annotations array"
             << " must be a 2-element array and you have " << annoEntry.size();

    if (!mlir::isa<mlir::StringAttr>(annoEntry[0]))
      return emitError()
             << "Element of GlobalAnnotationValuesAttr annotations"
                "array must start with a string, which is the name of "
                "global op or func it annotates";

    if (!mlir::isa<cir::AnnotationAttr>(annoEntry[1]))
      return emitError() << "The second element of GlobalAnnotationValuesAttr"
                            "annotations array element must be of "
                            "type AnnotationAttr";
  }
  return success();
}

//===----------------------------------------------------------------------===//
// OpenCLKernelMetadataAttr
//===----------------------------------------------------------------------===//

LogicalResult OpenCLKernelMetadataAttr::verify(
    function_ref<InFlightDiagnostic()> emitError, ArrayAttr workGroupSizeHint,
    ArrayAttr reqdWorkGroupSize, TypeAttr vecTypeHint,
    std::optional<bool> vecTypeHintSignedness,
    IntegerAttr intelReqdSubGroupSize) {
  // If no field is present, the attribute is considered invalid.
  if (!workGroupSizeHint && !reqdWorkGroupSize && !vecTypeHint &&
      !vecTypeHintSignedness && !intelReqdSubGroupSize) {
    return emitError()
           << "metadata attribute without any field present is invalid";
  }

  // Check for 3-dim integer tuples
  auto is3dimIntTuple = [](ArrayAttr arr) {
    auto isInt = [](Attribute dim) { return mlir::isa<IntegerAttr>(dim); };
    return arr.size() == 3 && llvm::all_of(arr, isInt);
  };
  if (workGroupSizeHint && !is3dimIntTuple(workGroupSizeHint)) {
    return emitError()
           << "work_group_size_hint must have exactly 3 integer elements";
  }
  if (reqdWorkGroupSize && !is3dimIntTuple(reqdWorkGroupSize)) {
    return emitError()
           << "reqd_work_group_size must have exactly 3 integer elements";
  }

  // Check that vec_type_hint is from the CIR or LLVM dialect.
  if (vecTypeHint) {
    mlir::Type vecTypeHintValue = vecTypeHint.getValue();
    if (mlir::isa<cir::CIRDialect>(vecTypeHintValue.getDialect())) {
      // Check for signedness alignment in CIR
      if (isSignedHint(vecTypeHintValue) != vecTypeHintSignedness) {
        return emitError() << "vec_type_hint_signedness must match the "
                              "signedness of the vec_type_hint type";
      }
    } else if (!mlir::LLVM::isCompatibleType(vecTypeHintValue)) {
      return emitError()
             << "vec_type_hint must be a type from the CIR or LLVM dialect";
    }
  }

  // Check for co-presence of vecTypeHintSignedness
  if (!!vecTypeHint != vecTypeHintSignedness.has_value()) {
    return emitError() << "vec_type_hint_signedness should be present if and "
                          "only if vec_type_hint is set";
  }

  return success();
}

//===----------------------------------------------------------------------===//
// OpenCLKernelArgMetadataAttr
//===----------------------------------------------------------------------===//

LogicalResult OpenCLKernelArgMetadataAttr::verify(
    function_ref<InFlightDiagnostic()> emitError, ArrayAttr addrSpaces,
    ArrayAttr accessQuals, ArrayAttr types, ArrayAttr baseTypes,
    ArrayAttr typeQuals, ArrayAttr argNames) {
  auto isIntArray = [](ArrayAttr elt) {
    return llvm::all_of(
        elt, [](Attribute elt) { return mlir::isa<IntegerAttr>(elt); });
  };
  auto isStrArray = [](ArrayAttr elt) {
    return llvm::all_of(
        elt, [](Attribute elt) { return mlir::isa<StringAttr>(elt); });
  };

  if (!isIntArray(addrSpaces))
    return emitError() << "addr_space must be integer arrays";
  if (!llvm::all_of<ArrayRef<ArrayAttr>>(
          {accessQuals, types, baseTypes, typeQuals}, isStrArray))
    return emitError()
           << "access_qual, type, base_type, type_qual must be string arrays";
  if (argNames && !isStrArray(argNames)) {
    return emitError() << "name must be a string array";
  }

  if (!llvm::all_of<ArrayRef<ArrayAttr>>(
          {addrSpaces, accessQuals, types, baseTypes, typeQuals, argNames},
          [&](ArrayAttr arr) {
            return !arr || arr.size() == addrSpaces.size();
          })) {
    return emitError() << "all arrays must have the same number of elements";
  }
  return success();
}

//===----------------------------------------------------------------------===//
// TBAAAttr
//===----------------------------------------------------------------------===//

bool cir::TBAAAttr::classof(mlir::Attribute attr) {
  return llvm::isa<cir::TBAAOmnipotentCharAttr, cir::TBAAVTablePointerAttr,
                   cir::TBAAScalarAttr, cir::TBAAStructAttr, cir::TBAATagAttr>(
      attr);
}

//===----------------------------------------------------------------------===//
// CIR Dialect
//===----------------------------------------------------------------------===//

void CIRDialect::registerAttributes() {
  addAttributes<
#define GET_ATTRDEF_LIST
#include "clang/CIR/Dialect/IR/CIROpsAttributes.cpp.inc"
      >();
}

//===----------------------------------------------------------------------===//
// makeFuncDeclAttr
//===----------------------------------------------------------------------===//

namespace cir {
mlir::Attribute makeFuncDeclAttr(const clang::Decl *decl,
                                 mlir::MLIRContext *ctx) {
  return llvm::TypeSwitch<const clang::Decl *, mlir::Attribute>(decl)
      .Case([ctx](const clang::CXXConstructorDecl *ast) {
        return ASTCXXConstructorDeclAttr::get(ctx, ast);
      })
      .Case([ctx](const clang::CXXConversionDecl *ast) {
        return ASTCXXConversionDeclAttr::get(ctx, ast);
      })
      .Case([ctx](const clang::CXXDestructorDecl *ast) {
        return ASTCXXDestructorDeclAttr::get(ctx, ast);
      })
      .Case([ctx](const clang::CXXMethodDecl *ast) {
        return ASTCXXMethodDeclAttr::get(ctx, ast);
      })
      .Case([ctx](const clang::FunctionDecl *ast) {
        return ASTFunctionDeclAttr::get(ctx, ast);
      })
      .Case([ctx](const clang::RecordDecl *ast) {
        return ASTRecordDeclAttr::get(ctx, ast);
      })
      .Default([](auto) {
        llvm_unreachable("unexpected Decl kind");
        return mlir::Attribute();
      });
}
} // namespace cir
