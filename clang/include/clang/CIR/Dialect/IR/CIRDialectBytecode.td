//===-- CIRDialectBytecode.td - CIR bytecode defs ----------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is the CIR bytecode reader/writer definition file.
//
// Without a BytecodeDialectInterface, MLIR falls back to encoding an unknown
// dialect's attributes and types through their *assembly format* -- it embeds
// the printed text inside the bytecode. That fallback is correct but it is the
// reason a bytecode file can be larger than the equivalent text, which matters
// here: CIR carried to link time is serialized bytecode, and a real app has
// hundreds of such objects.
//
// Modelled on mlir/include/mlir/Dialect/LLVMIR/LLVMDialectBytecode.td. Every
// entry's argument names must match CIRAttrs.td / CIRTypes.td, because the
// generated reader feeds them positionally to the attribute's get().
//
// Coverage is deliberately partial and grows by measurement. An attribute or
// type absent from the lists at the bottom keeps using the assembly-format
// fallback, exactly as every CIR attribute did before this file existed.
//
// That fallback is NOT unconditionally safe, which an earlier version of this
// comment claimed: it re-parses what it printed, so it is only correct for an
// attribute whose *pretty* form round-trips. An attribute that prints its self
// type after its closing `>` does not, and its .cir sections are unreadable.
// GlobalViewDiffAttr, BlockAddrAttr and BlockAddrDiffAttr all did; all three
// now print the type inside their brackets. See
// test/CIR/IR/bytecode-selftyped-attrs.cir.
//
// cir.record is absent for a reason outside this file's control: MLIR
// **refuses custom encodings for mutable types**. IRNumbering.cpp:499 guards
// the whole dialect-interface path with
//
//   // TODO: We don't allow custom encodings for mutable types right now.
//   if (!type.hasTrait<TypeTrait::IsMutable>()) { ... }
//
// and cir.record is MutableType (CIRTypes.td:624), so writeType is never even
// called for it -- adding a table entry here has no effect whatsoever. The
// same guard exists for mutable attributes (IRNumbering.cpp:332).
//
// This is the expensive gap, not a cosmetic one. Records are the only types
// that nest, and the fallback writes a type's *fully expanded* printed form
// while the text printer emits one alias per record -- so bytecode is
// O(width^depth) in nesting where text is linear. Measured: 257x at depth 7,
// 1,859x at depth 9. Closing it needs an upstream MLIR change.
//
//===----------------------------------------------------------------------===//

#ifndef CIR_DIALECT_BYTECODE
#define CIR_DIALECT_BYTECODE

include "mlir/IR/BytecodeBase.td"

//===----------------------------------------------------------------------===//
// Bytecode classes for attributes and types.
//===----------------------------------------------------------------------===//

def String :
  WithParser <"succeeded($_reader.readString($_var))",
  WithBuilder<"$_args",
  WithPrinter<"$_writer.writeOwnedString($_getter)",
  WithType   <"StringRef">>>>;

class Attr<string type> : WithType<type, Attribute>;

// Same idea for types: read a generic type into a narrowed C++ handle. The
// reader's templated readType does the dyn_cast and fails cleanly on a
// mismatch, so this stays safe against a malformed or cross-version file.
class Ty<string type> : WithType<type, Type>;

// CIR's FuncType has an optional single return type rather than builtin's
// result list, so presence needs its own flag. Mirrors readOptionalAttribute.
def OptionalType :
  WithParser <"succeeded(readOptionalType($_reader, $_var))",
  WithBuilder<"$_args",
  WithPrinter<"writeOptionalType($_writer, $_getter)",
  WithType   <"mlir::Type">>>>;

class OptionalAttribute<string type> :
  WithParser <"succeeded($_reader.readOptionalAttribute($_var))",
  WithPrinter<"$_writer.writeOptionalAttribute($_getter)",
  WithType<type, Attribute>>>;

// For a parameter declared OptionalParameter<"std::optional<...>">, where the
// C++ type is an optional rather than a nullable attribute, so "absent" needs
// a flag of its own. The helpers live in CIRDialectBytecode.cpp.
class OptionalInt<string type> :
  WithParser <"succeeded(readOptionalInt($_reader, $_var))",
  WithBuilder<"$_args",
  WithPrinter<"writeOptionalInt($_writer, $_getter)",
  WithType   <"std::optional<" # type # ">">>>>;

// Named for the C++ spelling, not the concept, because the difference from
// OptionalAttribute above is exactly the C++ spelling and nothing else:
// OptionalAttribute is for a parameter that is a nullable attribute, this is
// for one declared std::optional<...>. They are not interchangeable -- the two
// produce different on-disk layouts -- and at a glance nothing but the name
// says which a given parameter needs.
class StdOptionalAttribute<string type> :
  WithParser <"succeeded(readStdOptionalAttribute($_reader, $_var))",
  WithBuilder<"$_args",
  WithPrinter<"writeStdOptionalAttribute($_writer, $_getter)",
  WithType   <"std::optional<" # type # ">">>>>;

//===----------------------------------------------------------------------===//
// TypeSizeInfoAttr
//===----------------------------------------------------------------------===//

def TypeSizeInfoAttr : DialectAttribute<(attr
  VarInt:$char_,
  VarInt:$int_,
  VarInt:$size_t
)>;

//===----------------------------------------------------------------------===//
// OptInfoAttr
//===----------------------------------------------------------------------===//

def OptInfoAttr : DialectAttribute<(attr
  VarInt:$level,
  VarInt:$size
)>;

//===----------------------------------------------------------------------===//
// AddressPointAttr
//
// index and offset are int32_t and an address point legitimately carries a
// negative offset, so these are signed.
//===----------------------------------------------------------------------===//

def AddressPointAttr : DialectAttribute<(attr
  SignedVarInt:$index,
  SignedVarInt:$offset
)>;

//===----------------------------------------------------------------------===//
// OpenCLVersionAttr
//===----------------------------------------------------------------------===//

def OpenCLVersionAttr : DialectAttribute<(attr
  SignedVarInt:$major,
  SignedVarInt:$minor
)>;

//===----------------------------------------------------------------------===//
// BlockAddrInfoAttr
//===----------------------------------------------------------------------===//

def BlockAddrInfoAttr : DialectAttribute<(attr
  Attr<"mlir::FlatSymbolRefAttr">:$func,
  Attr<"mlir::StringAttr">:$label
)>;

//===----------------------------------------------------------------------===//
// StructorDedupAttr
//===----------------------------------------------------------------------===//

def StructorDedupAttr : DialectAttribute<(attr
  Attr<"mlir::FlatSymbolRefAttr">:$base
)>;

//===----------------------------------------------------------------------===//
// DontCallAttr
//===----------------------------------------------------------------------===//

def DontCallAttr : DialectAttribute<(attr
  Attr<"mlir::StringAttr">:$message,
  Bool:$is_error
)>;

//===----------------------------------------------------------------------===//
// BitfieldInfoAttr
//===----------------------------------------------------------------------===//

def BitfieldInfoAttr : DialectAttribute<(attr
  Attr<"mlir::StringAttr">:$name,
  Type:$storage_type,
  VarInt:$size,
  VarInt:$offset,
  Bool:$is_signed
)>;

//===----------------------------------------------------------------------===//
// Dialect registration
//===----------------------------------------------------------------------===//


//===----------------------------------------------------------------------===//
// Constants
//===----------------------------------------------------------------------===//
//
// These are the entries that pay for themselves. Every literal in a function
// body is an IntAttr, an FPAttr or a BoolAttr, so under the assembly fallback
// each one costs its full printed form ("#cir.int<42> : !s32i") while the
// native encoding costs a type reference plus the payload's own width.

// The value's width is implied by the type, so it is not stored twice; the
// helpers below recover it. This mirrors builtin's IntegerAttr / FloatAttr.
def IntAttr : DialectAttribute<(attr
  Type:$type,
  KnownWidthAPInt<"type">:$value
)> {
  let cBuilder = "get<$_resultType>(context, mlir::cast<cir::IntTypeInterface>(type), *value)";
}

def FPAttr : DialectAttribute<(attr
  Type:$type,
  KnownSemanticsAPFloat<"type">:$value
)> {
  let cBuilder = "get<$_resultType>(context, mlir::cast<cir::FPTypeInterface>(type), *value)";
}

// cir::BoolType is a singleton, so the self type carries no information and is
// rebuilt on read rather than stored.
def BoolAttr : DialectAttribute<(attr
  Bool:$value
)> {
  let cBuilder = "get<$_resultType>(context, cir::BoolType::get(context), value)";
}

def ConstPtrAttr : DialectAttribute<(attr
  Ty<"cir::PointerType">:$type,
  Attr<"mlir::IntegerAttr">:$value
)>;

//===----------------------------------------------------------------------===//
// Parameterless marker attributes
//===----------------------------------------------------------------------===//
//
// No payload at all: the entry exists so the attribute costs a kind index
// instead of its printed mnemonic.

def CatchAllAttr : DialectAttribute<(attr)>;
def UnwindAttr : DialectAttribute<(attr)>;
def NoThrowAttr : DialectAttribute<(attr)>;
def NoReturnAttr : DialectAttribute<(attr)>;
def ConvergentAttr : DialectAttribute<(attr)>;
def HotAttr : DialectAttribute<(attr)>;
def ReturnsTwiceAttr : DialectAttribute<(attr)>;
def OpenCLKernelAttr : DialectAttribute<(attr)>;


//===----------------------------------------------------------------------===//
// Constant initializers
//===----------------------------------------------------------------------===//
//
// These carry a self type, and under the fallback each *occurrence* embeds
// that type's whole printed form -- so a zero-initialized record costs the
// full record text at every use. Encoding them natively writes a reference
// into the bytecode type table instead, which is one copy even while the
// record type itself is still on the fallback.

def ZeroAttr : DialectAttribute<(attr
  Type:$type
)>;

def UndefAttr : DialectAttribute<(attr
  Type:$type
)>;

def PoisonAttr : DialectAttribute<(attr
  Type:$type
)>;

def ConstArrayAttr : DialectAttribute<(attr
  Type:$type,
  Attribute:$elts,
  SignedVarInt:$trailingZerosNum
)>;

def ConstRecordAttr : DialectAttribute<(attr
  Type:$type,
  Attr<"mlir::ArrayAttr">:$members
)>;

// The C++ special-member attributes on cir.func. Each is small -- a type and a
// flag -- but its type is the *record* the member belongs to, and while these
// fell back the printed form dragged that record in fully expanded. One
// cxx_ctor on a shared_ptr control block in an arfx scene TU printed to 131 MB
// on its own, and the TU's .cir section came to 958 MB, which then overflowed
// the bitcode writer's 32-bit buffer under -ffat-lto-objects.
def CXXCtorAttr : DialectAttribute<(attr
  Type:$type,
  // The enum is scoped, so the write side needs the cast the generated
  // writeVarInt call will not do for it.
  WithGetter<"static_cast<uint64_t>($_attrType.getCtorKind())", VarInt>:$ctorKind,
  Bool:$isTrivial
)> {
  let cBuilder = "cir::CXXCtorAttr::get(context, type, static_cast<cir::CtorKind>(ctorKind), isTrivial)";
}

def CXXDtorAttr : DialectAttribute<(attr
  Type:$type,
  Bool:$isTrivial
)> {
  let cBuilder = "cir::CXXDtorAttr::get(context, type, isTrivial)";
}

def CXXAssignAttr : DialectAttribute<(attr
  Type:$type,
  WithGetter<"static_cast<uint64_t>($_attrType.getAssignKind())", VarInt>:$assignKind,
  Bool:$isTrivial
)> {
  let cBuilder = "cir::CXXAssignAttr::get(context, type, static_cast<cir::AssignKind>(assignKind), isTrivial)";
}

def VTableAttr : DialectAttribute<(attr
  Type:$type,
  Attr<"mlir::ArrayAttr">:$data
)>;

def GlobalViewDiffAttr : DialectAttribute<(attr
  Type:$type,
  Attr<"cir::GlobalViewAttr">:$lhs,
  Attr<"cir::GlobalViewAttr">:$rhs
)>;

// The rest of the type-carrying attributes. Encoding GlobalViewDiffAttr but
// not GlobalViewAttr bought nothing: the diff wrote its two operands as nested
// attributes, and each of those went straight back to the fallback and printed
// its own self type in full.
//
// A relative-vtable build puts a global_view in every vtable slot and a
// typeinfo beside it, so this is the shape that recurs. In one generated
// translation unit a single global_view printed to 9.89 MB -- the symbol is a
// std::variant dispatcher whose mangled name and function type are both
// enormous -- and there were enough of them to take the .cir section to
// 280 MB, over the bitcode writer's 4 GB buffer limit once fat LTO serialized
// it.
def GlobalViewAttr : DialectAttribute<(attr
  Type:$type,
  Attr<"mlir::FlatSymbolRefAttr">:$symbol,
  OptionalAttribute<"mlir::ArrayAttr">:$indices,
  OptionalAttribute<"mlir::IntegerAttr">:$rawByteOffset,
  OptionalAttribute<"mlir::IntegerAttr">:$addrAdjustment,
  OptionalAttribute<"mlir::DenseI32ArrayAttr">:$inRange,
  Bool:$noCFI,
  Bool:$inbounds
)>;

def TypeInfoAttr : DialectAttribute<(attr
  Type:$type,
  Attr<"mlir::ArrayAttr">:$data
)>;

def DynamicCastInfoAttr : DialectAttribute<(attr
  Attr<"cir::GlobalViewAttr">:$src_rtti,
  Attr<"cir::GlobalViewAttr">:$dest_rtti,
  Attr<"mlir::FlatSymbolRefAttr">:$runtime_func,
  Attr<"mlir::FlatSymbolRefAttr">:$bad_cast_func,
  Attr<"cir::IntAttr">:$offset_hint
)>;

def ConstVectorAttr : DialectAttribute<(attr
  Type:$type,
  Attr<"mlir::ArrayAttr">:$elts
)>;

def ConstComplexAttr : DialectAttribute<(attr
  Ty<"cir::ComplexType">:$type,
  Attr<"mlir::TypedAttr">:$real,
  Attr<"mlir::TypedAttr">:$imag
)>;

// A pointer to data member is a record type plus an index, so under the
// fallback it cost the whole record per occurrence.
def DataMemberAttr : DialectAttribute<(attr
  Ty<"cir::DataMemberType">:$type,
  OptionalInt<"unsigned">:$member_index,
  OptionalInt<"unsigned">:$offset_bytes
)>;

// this_adjustment is signed: a pointer to a member of a virtual base adjusts
// downwards.
def MethodAttr : DialectAttribute<(attr
  Ty<"cir::MethodType">:$type,
  StdOptionalAttribute<"mlir::FlatSymbolRefAttr">:$symbol,
  OptionalInt<"uint64_t">:$vtable_offset,
  SignedVarInt:$this_adjustment,
  OptionalInt<"uint64_t">:$ptrauth_discriminator,
  Bool:$noCFI
)>;

def BlockAddrAttr : DialectAttribute<(attr
  Type:$type,
  Attr<"cir::BlockAddrInfoAttr">:$info
)>;

def BlockAddrDiffAttr : DialectAttribute<(attr
  Type:$type,
  Attr<"cir::BlockAddrInfoAttr">:$lhs,
  Attr<"cir::BlockAddrInfoAttr">:$rhs
)>;

// Present on every cir.func, so it is worth the entry even though the payload
// is usually the empty dictionary.
def ExtraFuncAttributesAttr : DialectAttribute<(attr
  Attr<"mlir::DictionaryAttr">:$elements
)>;

//===----------------------------------------------------------------------===//
// TBAA attributes
//===----------------------------------------------------------------------===//
//
// Every memory operation in C++ carries a tbaa_tag, and each tag nests a
// struct and scalars whose `type` is the accessed record -- so while these
// fell back, each occurrence printed its record in fully expanded form. One
// tag on a deep LLVM class hierarchy printed to 886 MB, two scalars to
// 443 MB each, and the translation unit's .cir section came to 5.2 GB, past
// what the embed path can hold in a single section.
//
// All six are encoded together for the GlobalViewDiffAttr/GlobalViewAttr
// reason: scalar nests its parent, tag nests base and access, member nests
// its type_desc, struct nests its members, and any one left on the fallback
// reprints the whole chain it points at.

def TBAAOmnipotentCharAttr : DialectAttribute<(attr)>;

def TBAAVTablePointerAttr : DialectAttribute<(attr
  Type:$type,
  SignedVarInt:$size
)>;

def TBAAScalarAttr : DialectAttribute<(attr
  String:$id,
  Type:$type,
  OptionalAttribute<"cir::TBAAScalarAttr">:$parent,
  SignedVarInt:$size
)>;

def TBAATagAttr : DialectAttribute<(attr
  Attr<"cir::TBAAAttr">:$base,
  Attr<"cir::TBAAAttr">:$access,
  SignedVarInt:$offset,
  SignedVarInt:$size
)>;

def TBAAMemberAttr : DialectAttribute<(attr
  Attr<"cir::TBAAAttr">:$type_desc,
  SignedVarInt:$offset,
  SignedVarInt:$size
)>;

// Array elements go through List rather than directly: the write side emits a
// writeList lambda whose parameter is the element, and a bare Attr element
// would ask that parameter for a member it does not have. List's "$_member"
// getter passes the element itself through instead.
def TBAAStructAttr : DialectAttribute<(attr
  String:$id,
  Array<List<Attr<"cir::TBAAMemberAttr">>>:$members,
  SignedVarInt:$size
)>;


//===----------------------------------------------------------------------===//
// Types
//===----------------------------------------------------------------------===//
//
// Types are where the size problem actually lives. The fallback writes a
// type's *fully expanded* printed form, while the text printer emits one alias
// per record and refers to it by name -- so bytecode is O(width^depth) in
// record nesting where text is linear. Measured on synthetic nested records:
// 257x at depth 7, and 1,328x for a 40-line width-6/depth-6 file.

def IntType : DialectType<(type
  VarInt:$width,
  Bool:$isSigned
)>;

def SingleType : DialectType<(type)>;
def DoubleType : DialectType<(type)>;
def FP16Type : DialectType<(type)>;
def BF16Type : DialectType<(type)>;
def FP80Type : DialectType<(type)>;
def FP128Type : DialectType<(type)>;
def LongDoubleType : DialectType<(type
  Type:$underlying
)>;

def BoolType : DialectType<(type)>;
def VoidType : DialectType<(type)>;
def VPtrType : DialectType<(type)>;

def PointerType : DialectType<(type
  Type:$pointee,
  OptionalAttribute<"mlir::ptr::MemorySpaceAttrInterface">:$addrSpace
)>;

def ArrayType : DialectType<(type
  Type:$elementType,
  VarInt:$size
)>;

def ComplexType : DialectType<(type
  Type:$elementType
)>;

// Worth covering even though a function type is not itself large: it is the
// only place most parameter types appear, so while it falls back its printed
// signature drags every one of them into the blob as literal text.
// The mutable, possibly self-referential one. `kind` and `name` come first so
// the reader can build the incomplete record and publish it via
// tryStartCyclicRead before it reads `members` -- a member that refers back to
// this record then resolves to the incomplete type instead of re-entering this
// entry. Same shape as the text parser's tryStartCyclicParse dance.
def RecordType : DialectType<(type
  VarInt:$kind,
  OptionalAttribute<"mlir::StringAttr">:$name,
  Bool:$incomplete,
  Bool:$packed,
  Bool:$padded,
  LocalVar<"cir::RecordType", "buildIncompleteRecord($_reader, kind, name)">:$partial,
  Array<Type>:$members
)> {
  let cBuilder = "completeRecord(context, partial, kind, name, incomplete, packed, padded, members)";
}

def FuncType : DialectType<(type
  Array<Type>:$inputs,
  OptionalType:$optionalReturnType,
  Bool:$varArg
)> {
  // The generated reader would otherwise call mlir::get<FuncType>(context,
  // ...), which prefers a no-context `get` when one exists -- and CIR's is
  // TypeBuilderWithInferredContext, which takes the context off the *return
  // type*. A void function has no return type, so that overload dereferences
  // null. Name the context-taking builder explicitly.
  let cBuilder = "cir::FuncType::get(context, inputs, optionalReturnType, varArg)";
}


def CIRDialectAttributes : DialectAttributes<"CIR"> {
  let elems = [
    TypeSizeInfoAttr,
    OptInfoAttr,
    AddressPointAttr,
    OpenCLVersionAttr,
    BlockAddrInfoAttr,
    StructorDedupAttr,
    DontCallAttr,
    BitfieldInfoAttr,
    IntAttr,
    FPAttr,
    BoolAttr,
    ConstPtrAttr,
    CatchAllAttr,
    UnwindAttr,
    NoThrowAttr,
    NoReturnAttr,
    ConvergentAttr,
    HotAttr,
    ReturnsTwiceAttr,
    OpenCLKernelAttr,
    ZeroAttr,
    UndefAttr,
    PoisonAttr,
    ConstArrayAttr,
    ConstRecordAttr,
    ExtraFuncAttributesAttr,

    // APPEND ONLY below this line. An entry's position in this list *is* its
    // kind discriminator on disk, so inserting one renumbers every entry after
    // it and a file written by an older build decodes as the wrong attribute --
    // silently, not as a version error.
    VTableAttr,
    GlobalViewDiffAttr,
    BlockAddrAttr,
    BlockAddrDiffAttr,
    CXXCtorAttr,
    CXXDtorAttr,
    CXXAssignAttr,
    GlobalViewAttr,
    TypeInfoAttr,
    DynamicCastInfoAttr,
    ConstVectorAttr,
    ConstComplexAttr,
    DataMemberAttr,
    MethodAttr,
    TBAAOmnipotentCharAttr,
    TBAAVTablePointerAttr,
    TBAAScalarAttr,
    TBAATagAttr,
    TBAAMemberAttr,
    TBAAStructAttr
  ];
}

// Types are not encoded yet. The LLVM dialect does the same (its DialectTypes
// list is empty), but for CIR this is a deliberate deferral rather than a
// non-issue: !cir.record is a *mutable* type and may be recursive. The textual
// parser handles that with AsmParser::tryStartCyclicParse; the bytecode
// reader/writer interface in mlir/include/mlir/Bytecode/BytecodeImplementation.h
// has no cyclic equivalent, and no in-tree dialect encodes a mutable type. That
// needs a forward-reference-then-complete scheme designed for it, so it is
// tracked separately rather than bolted on here.
def CIRDialectTypes : DialectTypes<"CIR"> {
  let elems = [
    IntType,
    SingleType,
    DoubleType,
    FP16Type,
    BF16Type,
    FP80Type,
    FP128Type,
    LongDoubleType,
    BoolType,
    VoidType,
    VPtrType,
    PointerType,
    ArrayType,
    ComplexType,
    FuncType,
    RecordType
  ];
}

#endif // CIR_DIALECT_BYTECODE
