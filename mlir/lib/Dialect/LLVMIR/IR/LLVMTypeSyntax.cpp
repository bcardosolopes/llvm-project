//===- LLVMTypeSyntax.cpp - Parsing/printing for MLIR LLVM Dialect types --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/LLVMIR/LLVMTypes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/DialectImplementation.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/TypeSwitch.h"

using namespace mlir;
using namespace mlir::LLVM;

//===----------------------------------------------------------------------===//
// Printing.
//===----------------------------------------------------------------------===//

/// If the given type is compatible with the LLVM dialect, prints it using
/// internal functions to avoid getting a verbose `!llvm` prefix. Otherwise
/// prints it as usual.
static void dispatchPrint(AsmPrinter &printer, Type type) {
  if (isCompatibleType(type) &&
      !(llvm::isa<IntegerType, FloatType, VectorType>(type) ||
        (llvm::isa<PtrLikeTypeInterface>(type) &&
         !llvm::isa<LLVMPointerType>(type))))
    return mlir::LLVM::detail::printType(type, printer);
  printer.printType(type);
}

/// Returns the keyword to use for the given type.
static StringRef getTypeKeyword(Type type) {
  return TypeSwitch<Type, StringRef>(type)
      .Case<LLVMVoidType>([&](Type) { return "void"; })
      .Case<LLVMPPCFP128Type>([&](Type) { return "ppc_fp128"; })
      .Case<LLVMTokenType>([&](Type) { return "token"; })
      .Case<LLVMLabelType>([&](Type) { return "label"; })
      .Case<LLVMMetadataType>([&](Type) { return "metadata"; })
      .Case<LLVMFunctionType>([&](Type) { return "func"; })
      .Case<LLVMPointerType>([&](Type) { return "ptr"; })
      .Case<LLVMArrayType>([&](Type) { return "array"; })
      .Case<LLVMStructType>([&](Type) { return "struct"; })
      .Case<LLVMTargetExtType>([&](Type) { return "target"; })
      .Case<LLVMX86AMXType>([&](Type) { return "x86_amx"; })
      .DefaultUnreachable("unexpected 'llvm' type kind");
}

/// Prints a structure type. Keeps track of known struct names to handle self-
/// or mutually-referring structs without falling into infinite recursion.
void LLVMStructType::print(AsmPrinter &printer) const {
  FailureOr<AsmPrinter::CyclicPrintReset> cyclicPrint;

  printer << "<";
  if (isIdentified()) {
    cyclicPrint = printer.tryStartCyclicPrint(*this);

    printer << '"';
    llvm::printEscapedString(getName(), printer.getStream());
    printer << '"';
    // If we are printing a reference to one of the enclosing structs, just
    // print the name and stop to avoid infinitely long output.
    if (failed(cyclicPrint)) {
      printer << '>';
      return;
    }
    printer << ", ";
  }

  if (isIdentified() && isOpaque()) {
    printer << "opaque>";
    return;
  }

  if (isPacked())
    printer << "packed ";

  // Put the current type on stack to avoid infinite recursion.
  printer << '(';
  llvm::interleaveComma(getBody(), printer.getStream(),
                        [&](Type subtype) { dispatchPrint(printer, subtype); });
  printer << ')';
  printer << '>';
}

/// Prints the given LLVM dialect type recursively. This leverages closedness of
/// the LLVM dialect type system to avoid printing the dialect prefix
/// repeatedly. For recursive structures, only prints the name of the structure
/// when printing a self-reference. Note that this does not apply to sibling
/// references. For example,
///   struct<"a", (ptr<struct<"a">>)>
///   struct<"c", (ptr<struct<"b", (ptr<struct<"c">>)>>,
///                ptr<struct<"b", (ptr<struct<"c">>)>>)>
/// note that "b" is printed twice.
void mlir::LLVM::detail::printType(Type type, AsmPrinter &printer) {
  if (!type) {
    printer << "<<NULL-TYPE>>";
    return;
  }

  printer << getTypeKeyword(type);

  llvm::TypeSwitch<Type>(type)
      .Case<LLVMPointerType, LLVMArrayType, LLVMFunctionType, LLVMTargetExtType,
            LLVMStructType>([&](auto type) { type.print(printer); });
}

//===----------------------------------------------------------------------===//
// Parsing.
//===----------------------------------------------------------------------===//

static ParseResult dispatchParse(AsmParser &parser, Type &type);

/// Attempts to set the body of an identified structure type. Reports a parsing
/// error at `subtypesLoc` in case of failure.
static LLVMStructType trySetStructBody(LLVMStructType type,
                                       ArrayRef<Type> subtypes, bool isPacked,
                                       AsmParser &parser, SMLoc subtypesLoc) {
  for (Type t : subtypes) {
    if (!LLVMStructType::isValidElementType(t)) {
      parser.emitError(subtypesLoc)
          << "invalid LLVM structure element type: " << t;
      return LLVMStructType();
    }
  }

  if (succeeded(type.setBody(subtypes, isPacked)))
    return type;

  parser.emitError(subtypesLoc)
      << "identified type already used with a different body";
  return LLVMStructType();
}

namespace {
/// One frame per identified struct body being parsed on this thread.
struct StructParseFrame {
  LLVMStructType placeholder;
  bool inCycle;
};

/// A struct parse runs start to end on one thread, so a thread-local stack
/// mirrors the parser's cyclic parse stack.
SmallVector<StructParseFrame> &getStructParseFrames() {
  thread_local SmallVector<StructParseFrame> structParseFrames;
  return structParseFrames;
}

class StructParseFrameGuard {
public:
  explicit StructParseFrameGuard(LLVMStructType placeholder) {
    getStructParseFrames().push_back({placeholder, false});
  }
  ~StructParseFrameGuard() { getStructParseFrames().pop_back(); }
  bool inCycle() const { return getStructParseFrames().back().inCycle; }
};

/// Marks every struct from the innermost one out to `target` as part of a
/// cycle. All of them must use the mutable placeholder so that the result does
/// not depend on which struct of the cycle the printer started from.
void markStructCycle(LLVMStructType target) {
  for (StructParseFrame &frame : llvm::reverse(getStructParseFrames())) {
    frame.inCycle = true;
    if (frame.placeholder == target)
      return;
  }
}
} // namespace

/// Parses an LLVM dialect structure type.
///   llvm-type ::= `struct<` (string-literal `,`)? `packed`?
///                 `(` llvm-type-list `)` `>`
///               | `struct<` string-literal `>`
///               | `struct<` string-literal `, opaque>`
Type LLVMStructType::parse(AsmParser &parser) {
  Location loc = parser.getEncodedSourceLoc(parser.getCurrentLocation());

  if (failed(parser.parseLess()))
    return LLVMStructType();

  // If we are parsing a self-reference to a recursive struct, i.e. the parsing
  // stack already contains a struct with the same identifier, bail out after
  // the name.
  std::string name;
  bool isIdentified = succeeded(parser.parseOptionalString(&name));
  if (isIdentified) {
    SMLoc greaterLoc = parser.getCurrentLocation();
    if (succeeded(parser.parseOptionalGreater())) {
      auto type = LLVMStructType::getIdentifiedChecked(
          [loc] { return emitError(loc); }, loc.getContext(), name);
      if (succeeded(parser.tryStartCyclicParse(type))) {
        parser.emitError(
            greaterLoc,
            "struct without a body only allowed in a recursive struct");
        return nullptr;
      }

      markStructCycle(type);
      return type;
    }
    if (failed(parser.parseComma()))
      return LLVMStructType();
  }

  // Handle intentionally opaque structs.
  SMLoc kwLoc = parser.getCurrentLocation();
  if (succeeded(parser.parseOptionalKeyword("opaque"))) {
    if (!isIdentified)
      return parser.emitError(kwLoc, "only identified structs can be opaque"),
             LLVMStructType();
    if (failed(parser.parseGreater()))
      return LLVMStructType();
    return LLVMStructType::getFrozenOpaque(loc.getContext(), name);
  }

  FailureOr<AsmParser::CyclicParseReset> cyclicParse;
  LLVMStructType placeholder;
  std::optional<StructParseFrameGuard> frame;
  // A full definition inside an enclosing struct of the same name is a
  // different struct. The printer emits this for a frozen struct whose body
  // holds another frozen struct with the same name. It is only an error if the
  // inner struct is recursive, because both would need the same placeholder.
  bool redefinesEnclosingStruct = false;
  if (isIdentified) {
    placeholder = LLVMStructType::getIdentifiedChecked(
        [loc] { return emitError(loc); }, loc.getContext(), name);
    cyclicParse = parser.tryStartCyclicParse(placeholder);
    redefinesEnclosingStruct = failed(cyclicParse);
    frame.emplace(placeholder);
  }

  // Check for packedness.
  bool isPacked = succeeded(parser.parseOptionalKeyword("packed"));
  if (failed(parser.parseLParen()))
    return LLVMStructType();

  // A struct that is part of a recursive cycle needs the mutable placeholder.
  // Any other identified struct is frozen, so it does not depend on what other
  // threads or earlier inputs put in the context.
  auto finishIdentified = [&](ArrayRef<Type> body,
                              SMLoc bodyLoc) -> LLVMStructType {
    if (frame->inCycle() && redefinesEnclosingStruct) {
      parser.emitError(kwLoc,
                       "identifier already used for an enclosing struct");
      return LLVMStructType();
    }
    if (frame->inCycle())
      return trySetStructBody(placeholder, body, isPacked, parser, bodyLoc);
    return LLVMStructType::getFrozenIdentified(loc.getContext(), name, body,
                                               isPacked);
  };

  // Fast pass for structs with zero subtypes.
  SMLoc subtypesLoc = parser.getCurrentLocation();
  if (succeeded(parser.parseOptionalRParen())) {
    if (failed(parser.parseGreater()))
      return LLVMStructType();
    if (!isIdentified)
      return LLVMStructType::getLiteralChecked([loc] { return emitError(loc); },
                                               loc.getContext(), {}, isPacked);
    return finishIdentified({}, subtypesLoc);
  }

  // Parse subtypes. For identified structs, put the identifier of the struct on
  // the stack to support self-references in the recursive calls.
  SmallVector<Type, 4> subtypes;
  do {
    Type type;
    if (dispatchParse(parser, type))
      return LLVMStructType();
    subtypes.push_back(type);
  } while (succeeded(parser.parseOptionalComma()));

  if (parser.parseRParen() || parser.parseGreater())
    return LLVMStructType();

  // Construct the struct with body.
  if (!isIdentified)
    return LLVMStructType::getLiteralChecked(
        [loc] { return emitError(loc); }, loc.getContext(), subtypes, isPacked);
  for (Type t : subtypes) {
    if (!LLVMStructType::isValidElementType(t)) {
      parser.emitError(subtypesLoc)
          << "invalid LLVM structure element type: " << t;
      return LLVMStructType();
    }
  }
  return finishIdentified(subtypes, subtypesLoc);
}

/// Parses a type appearing inside another LLVM dialect-compatible type. This
/// will try to parse any type in full form (including types with the `!llvm`
/// prefix), and on failure fall back to parsing the short-hand version of the
/// LLVM dialect types without the `!llvm` prefix.
static Type dispatchParse(AsmParser &parser, bool allowAny = true) {
  SMLoc keyLoc = parser.getCurrentLocation();

  // Try parsing any MLIR type.
  Type type;
  OptionalParseResult result = parser.parseOptionalType(type);
  if (result.has_value()) {
    if (failed(result.value()))
      return nullptr;
    if (!allowAny) {
      parser.emitError(keyLoc) << "unexpected type, expected keyword";
      return nullptr;
    }
    return type;
  }

  // If no type found, fallback to the shorthand form.
  StringRef key;
  if (failed(parser.parseKeyword(&key)))
    return Type();

  MLIRContext *ctx = parser.getContext();
  return StringSwitch<function_ref<Type()>>(key)
      .Case("void", [&] { return LLVMVoidType::get(ctx); })
      .Case("ppc_fp128", [&] { return LLVMPPCFP128Type::get(ctx); })
      .Case("token", [&] { return LLVMTokenType::get(ctx); })
      .Case("label", [&] { return LLVMLabelType::get(ctx); })
      .Case("metadata", [&] { return LLVMMetadataType::get(ctx); })
      .Case("func", [&] { return LLVMFunctionType::parse(parser); })
      .Case("ptr", [&] { return LLVMPointerType::parse(parser); })
      .Case("array", [&] { return LLVMArrayType::parse(parser); })
      .Case("struct", [&] { return LLVMStructType::parse(parser); })
      .Case("target", [&] { return LLVMTargetExtType::parse(parser); })
      .Case("x86_amx", [&] { return LLVMX86AMXType::get(ctx); })
      .Default([&] {
        parser.emitError(keyLoc) << "unknown LLVM type: " << key;
        return Type();
      })();
}

/// Helper to use in parse lists.
static ParseResult dispatchParse(AsmParser &parser, Type &type) {
  type = dispatchParse(parser);
  return success(type != nullptr);
}

/// Parses one of the LLVM dialect types.
Type mlir::LLVM::detail::parseType(DialectAsmParser &parser) {
  SMLoc loc = parser.getCurrentLocation();
  Type type = dispatchParse(parser, /*allowAny=*/false);
  if (!type)
    return type;
  if (!isCompatibleOuterType(type)) {
    parser.emitError(loc) << "unexpected type, expected keyword";
    return nullptr;
  }
  return type;
}

ParseResult LLVM::parsePrettyLLVMType(AsmParser &p, Type &type) {
  return dispatchParse(p, type);
}

void LLVM::printPrettyLLVMType(AsmPrinter &p, Type type) {
  return dispatchPrint(p, type);
}
