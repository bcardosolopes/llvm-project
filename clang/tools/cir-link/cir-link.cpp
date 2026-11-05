//===- cir-link.cpp - Merge several CIR modules into one ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Reads several CIR modules and writes one. This is the first piece of the
// CIR link-time path: the whole point of holding CIR to link time is that some
// component has to put the translation units together, and that is this.
//
// Symbol resolution is deliberately *permissive*, the same way LLVM's IR
// linker and LTO are, because real programs violate the ODR and a
// linker that refuses to link them is not useful. Where two translation units
// disagree the first definition wins and the divergence is recorded rather
// than diagnosed. Reporting on those records is what eventually gives us an
// ODR checker; see --warn-odr-divergence, and --odr-events, which records
// every choice between two definitions -- the silent weak-vs-weak and
// strong-vs-weak ones included, with whether the copies agree -- without
// changing which one survives.
//
// Resolution follows linkage, because "two definitions" means three different
// things in C++ and only one of them is an ODR problem:
//
//   strong + weak    the strong definition wins, whichever order they arrive
//   weak   + weak    keep either -- this is what every header-defined inline
//                    function and template instantiation looks like, so it is
//                    the common case and must be *silent*
//   strong + strong  a genuine divergence, and the only one worth reporting
//
// Getting the middle case wrong is what makes an ODR report useless: a
// two-file toy that shares one header already produces three duplicate
// definitions, and a real library would bury any true finding under thousands
// of them.
//
// Local-linkage symbols are renamed, not resolved. Two translation units may
// each define `static void helper()`, and both mangle to the same name; they
// are *different functions* and merging them is a miscompile, not an ODR
// question. Anything with internal or private linkage therefore gets a unique
// suffix when its name is already taken, with its references updated, before
// it joins the output.
//
// Record types get the same treatment as local symbols, for the same reason.
// Two TUs may each define a different `struct S` inside an anonymous
// namespace; both mangle to `(anonymous namespace)::S` and they are not the
// same type. Across 132 real CIR CodeGen tests, 13 of 105 named record types
// already have more than one distinct body -- `struct "S"` alone has nine --
// so this is reachable from legal C++, not only from ODR violations.
//
// Records that agree still unify to one type. Records that disagree are
// renamed, and every reference to them is rewritten. That needs each input
// parsed in its own MLIRContext, because MLIR uniques identified records by
// name at parse time: sharing one context makes the collision unrepresentable
// and the parser dies inside a file that is perfectly valid on its own. The
// modules are then handed to the shared context as bytecode, which is the
// only way ops cross a context boundary.
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Interfaces/CIROpInterfaces.h"
#include "clang/CIR/LowerToLLVM.h"

#include "mlir/Dialect/DLTI/DLTI.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"

#include "mlir/Bytecode/BytecodeReader.h"
#include "mlir/Bytecode/BytecodeWriter.h"
#include "mlir/IR/AsmState.h"
#include "mlir/IR/AttrTypeSubElements.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OwningOpRef.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/IR/Threading.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Parser/Parser.h"

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ThreadPool.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <functional>
#include <future>
#include <set>
#include <variant>
#include <vector>

using namespace mlir;
using namespace llvm;

static cl::list<std::string> inputFilenames(cl::Positional, cl::OneOrMore,
                                            cl::desc("<input cir files>"));

static cl::opt<std::string> outputFilename("o", cl::desc("Output filename"),
                                           cl::value_desc("filename"),
                                           cl::init("-"));

static cl::opt<bool>
    emitBytecode("emit-bytecode",
                 cl::desc("Write the merged module as MLIR bytecode (default: "
                          "whatever the inputs were)"),
                 cl::init(false));

// clang's -fclangir-inline, for the lowering of the merged module; the
// relink's driver passes it on through cir-link-wrapper.
static cl::opt<bool>
    runInliner("inline", cl::desc("Run the CIR inliner when lowering the "
                                  "merged module (-fclangir-inline)"));
static cl::opt<unsigned> inlineThreshold(
    "cir-inline-threshold",
    cl::desc("Threshold for --inline (-fclangir-inline-threshold=); the "
             "inliner's own when not given"));

static cl::opt<unsigned> partitions(
    "partitions",
    cl::desc("Lower the merged module in this many pieces, in parallel, after "
             "the inliner; piece 0 goes to -o and piece N to <-o>.N, for a "
             "regular LTO link to take together"),
    cl::init(1));

static cir::InlinerOptions getInlinerOptions() {
  cir::InlinerOptions options;
  options.enabled = runInliner;
  if (inlineThreshold.getNumOccurrences())
    options.threshold = inlineThreshold;
  return options;
}

static cl::opt<bool> internalize(
    "internalize",
    cl::desc("After merging, give internal linkage to every definition that is "
             "not named by --keep, so the optimizer may delete or specialize "
             "it"),
    cl::init(false));

static cl::list<std::string>
    keepSymbols("keep",
                cl::desc("Symbol to keep externally visible under "
                         "--internalize (repeatable)"),
                cl::value_desc("symbol"));

static cl::opt<std::string> keepListFile(
    "keep-list",
    cl::desc("File of symbol names, one per line, to keep externally visible"),
    cl::value_desc("filename"));

static cl::opt<std::string> sharedLibrarySymbols(
    "shared-library-symbols",
    cl::desc("File naming, one per line, the symbols the link's shared "
             "libraries define. A linkonce global another input declares is "
             "deferred to its owner only if the owner is one of them"),
    cl::value_desc("filename"), cl::init(""));

static cl::opt<bool> warnOdrDivergence(
    "warn-odr-divergence",
    cl::desc("Report symbols defined in more than one input, and print a "
             "summary of how symbols were resolved"),
    cl::init(false));

static cl::opt<std::string> odrEventsFile(
    "odr-events",
    cl::desc("Write, as JSON, every choice the merge made between two "
             "definitions of one symbol -- weak copies deduplicated, a weak "
             "definition displaced by a strong one, two strong definitions -- "
             "with each copy's linkage and whether the copies agree. Recording "
             "never changes the link: what a choice means is left to the "
             "reader"),
    cl::value_desc("filename"), cl::init(""));

// Where the time goes is not guessable from the outside, and a symbol profile
// answers a different question: it says which *function* is hot, not which
// phase of the link that function was serving. Parsing, record unification and
// the bytecode transfer all bottom out in the same MLIR uniquing code, so a
// profile attributes all three to one place.
static cl::opt<cir::LTOBitcodeOptions::Kind> lto(
    "lto",
    cl::desc("The kind of LTO link the merged module is joining. It is written "
             "the way clang writes bitcode for that kind of link, so it agrees "
             "with the objects it was merged from. The build knows this; "
             "nothing here can work it out"),
    cl::init(cir::LTOBitcodeOptions::Kind::None),
    cl::values(
        clEnumValN(cir::LTOBitcodeOptions::Kind::Full, "full", "regular LTO"),
        clEnumValN(cir::LTOBitcodeOptions::Kind::Thin, "thin", "ThinLTO")));

static cl::opt<bool> splitLTOUnit(
    "split-lto-unit",
    cl::desc("clang's -fsplit-lto-unit, including when it is defaulted on by "
             "-fwhole-program-vtables or CFI. Only affects -lto=thin: for "
             "regular LTO clang writes EnableSplitLTOUnit=1 regardless"),
    cl::init(false));

static cl::opt<std::string> targetTriple(
    "target",
    cl::desc("Target triple to give the merged module when it has none, or to "
             "override the one it has"),
    cl::value_desc("triple"), cl::init(""));

static cl::opt<bool> emitLLVM(
    "emit-llvm",
    cl::desc("Lower the merged module to LLVM IR and write that instead of "
             "CIR. Writes bitcode unless -emit-llvm-text is given"),
    cl::init(false));

static cl::opt<bool>
    emitLLVMText("emit-llvm-text",
                 cl::desc("With -emit-llvm, write textual LLVM IR instead of "
                          "bitcode. For reading, not for feeding to anything"),
                 cl::init(false));

static cl::opt<std::string> emitCIRPath(
    "emit-cir",
    cl::desc("Also write the merged CIR module here. A side output for looking "
             "at, off the path the pipeline takes"),
    cl::value_desc("filename"), cl::init(""));

static cl::opt<unsigned> jobs(
    "j",
    cl::desc("How many inputs to prepare and transfer at once (0: pick from "
             "the hardware, capped). Each one in flight costs the memory of a "
             "parsed module, so this trades peak RSS for wall time"),
    cl::value_desc("n"), cl::init(0));

static cl::opt<bool>
    timeTrace("time-trace", cl::desc("Write a Chrome trace of the link phases"),
              cl::init(false));

static cl::opt<std::string> timeTraceFile(
    "time-trace-file",
    cl::desc("Where to write the -time-trace output (default: the output file "
             "with .time-trace.json appended)"),
    cl::value_desc("filename"), cl::init(""));

static cl::opt<unsigned> timeTraceGranularity(
    "time-trace-granularity",
    cl::desc("Minimum time in microseconds for an event to be recorded"),
    cl::init(500));

static cl::opt<unsigned> toleranceDrillBudget(
    "tolerance-drill-budget",
    cl::desc("Cap on record-pair visits per tolerance drill; a drill past "
             "the cap fails closed (splits) instead of hanging. Debug hook "
             "for the budget path; the default is far above any healthy drill"),
    cl::value_desc("n"), cl::init(5000000), cl::Hidden);

static cl::opt<bool> toleranceDrillTrace(
    "tolerance-drill-trace",
    cl::desc("Log every tolerance-drill verdict with its rejection site. "
             "Debug hook for misattributed splits."),
    cl::init(false), cl::Hidden);

namespace {

/// What we know about a symbol we have already placed in the output.
struct Resolution {
  Operation *op = nullptr;
  /// Index of the input that supplied the winning definition.
  unsigned inputIdx = 0;
  bool isDefinition = false;
  /// A weak definition is a placeholder: a strong one displaces it silently.
  bool isWeak = false;
  /// A local symbol only holds its name provisionally: an externally visible
  /// symbol of the same name owns it, and the local one steps aside.
  bool isLocal = false;
  /// Some input declared this as an external global, expecting its owner
  /// elsewhere. See deferDiscardableDataToOwners.
  bool declaredExternal = false;
  /// Some input built with RTTI declared this as an external global. Only
  /// together with a -fno-rtti kept copy does that mean an outside owner: see
  /// deferDiscardableDataToOwners.
  bool declaredExternalWithRTTI = false;
  /// Weak copies dropped as identical to the kept one, when resolutions are
  /// recorded. Counted rather than listed: every inline function in more than
  /// one input has them, and listing them would bury everything else.
  unsigned equalWeakCopies = 0;
  /// This symbol's entry among the recorded resolutions, or -1 if it has none.
  int recordedAt = -1;
};

/// A symbol that more than one input defined. Kept whole rather than counted,
/// because the interesting part of an ODR report is *which* inputs disagreed.
struct Divergence {
  std::string symbol;
  unsigned firstInput;
  unsigned laterInput;
};

/// The choice the merge made between two definitions of one symbol. None of
/// these changes what the merge does, and none is an error: real programs
/// break the ODR, and a linker that refuses them is not useful. They are
/// recorded so that what each one means can be decided by whoever reads the
/// record, rather than settled here once and for all.
enum class ResolutionKind {
  /// Two weak copies; the first is kept. Every inline function and template
  /// instantiation used by more than one input looks like this.
  WeakWeak,
  /// A strong definition and a weak one, in either order; the strong one is
  /// kept. Legal for a `weak` default that an out-of-line definition
  /// overrides, and an ODR question for a linkonce_odr or weak_odr copy: an
  /// explicit specialization next to an implicit instantiation looks like this.
  StrongOverWeak,
  /// Two weak copies of a global, and the one from a -fno-rtti input gives way
  /// to the other; see the weak-vs-weak case in the merge.
  RTTIOverNoRTTI,
  /// Two strong definitions; the first is kept.
  StrongStrong,
};

static StringRef resolutionKindName(ResolutionKind kind) {
  switch (kind) {
  case ResolutionKind::WeakWeak:
    return "weak-weak";
  case ResolutionKind::StrongOverWeak:
    return "strong-over-weak";
  case ResolutionKind::RTTIOverNoRTTI:
    return "rtti-over-nortti";
  case ResolutionKind::StrongStrong:
    return "strong-strong";
  }
  llvm_unreachable("unknown resolution kind");
}

/// One copy of a symbol taking part in a resolution.
struct ResolvedCopy {
  unsigned input = 0;
  std::string linkage;
  std::string digest;
  /// Where the definition came from in the source, when CIRGen recorded it.
  /// Kept as the attribute: it outlives the op, which is buried.
  mlir::LocationAttr loc;
};

/// How a disagreement between two definitions is reported. Recording never
/// depends on it: it only decides what reaches the user, and only `Error`
/// changes the outcome of the link.
enum class OdrSeverity { Ignore, Remark, Warning, Error };

static StringRef odrSeverityName(OdrSeverity severity) {
  switch (severity) {
  case OdrSeverity::Ignore:
    return "ignore";
  case OdrSeverity::Remark:
    return "remark";
  case OdrSeverity::Warning:
    return "warning";
  case OdrSeverity::Error:
    return "error";
  }
  llvm_unreachable("unknown ODR severity");
}

static std::optional<OdrSeverity> parseOdrSeverity(StringRef name) {
  for (OdrSeverity severity : {OdrSeverity::Ignore, OdrSeverity::Remark,
                               OdrSeverity::Warning, OdrSeverity::Error})
    if (name == odrSeverityName(severity))
      return severity;
  return std::nullopt;
}

/// One side of a difference between two copies: where it is in that copy,
/// and what is there -- as CIR, and in source terms where there are some.
struct DifferenceSite {
  mlir::LocationAttr loc;
  std::string text;
  std::optional<std::string> source = std::nullopt;
};

/// The first place two copies disagree on one thing -- their type, or their
/// body -- and what each has there.
struct Difference {
  /// What disagrees, as a message says it: "parameter 2", "the body".
  std::string what;
  /// The same in source terms, as a sentence: "the copies first differ".
  std::string sourceWhat;
  DifferenceSite kept, dropped;
};

/// An attribute two copies disagree on, and its value in each, printed; none
/// where a copy lacks it.
struct AttributeDifference {
  std::string name;
  std::optional<std::string> kept, dropped;
};

/// One recorded resolution. Two copies are compared on three things: their
/// type; their other attributes, apart from linkage, visibility and source
/// locations -- what CIRGen put on them, inferred attributes included; and
/// their body, which is their regions and initializer.
struct ResolutionEvent {
  ResolutionKind kind;
  ResolvedCopy kept, dropped;
  bool sameType = true;
  bool sameAttributes = true;
  bool sameBody = true;
  /// What the policy made of it; Ignore for copies that agree.
  OdrSeverity severity = OdrSeverity::Ignore;
  /// What the copies disagree on, for whoever has to fix it; see
  /// DefinitionComparer::Result.
  std::optional<Difference> typeDifference, bodyDifference;
  SmallVector<AttributeDifference, 1> attributeDifferences;
};

/// Every recorded resolution of one symbol, in input order.
struct SymbolResolutions {
  std::string symbol;
  std::string opName;
  SmallVector<ResolutionEvent, 2> events;
  /// The kept op whose digest `keptDigest` holds: a strong definition can
  /// displace the kept copy, and its digest is then computed again.
  Operation *digestedKept = nullptr;
  std::string keptDigest;
};

/// A record an input defines with a body that differs from an earlier
/// input's, renamed in that input so both bodies survive.
struct RecordRenameEvent {
  unsigned input;
  std::string from;
  std::string to;
  OdrSeverity severity = OdrSeverity::Ignore;
  /// The input whose definition keeps the name, and the record's kind, to
  /// find both bodies once they are in the shared context.
  unsigned keptInput = 0;
  cir::RecordType::RecordKind kind = cir::RecordType::RecordKind::Struct;
  /// Where each definition is, as CIRGen recorded it in cir.record_locations,
  /// and the first member they disagree on; see describeRecordRenames.
  mlir::LocationAttr loc, keptLoc;
  std::optional<Difference> memberDifference;
};

/// The kinds a severity is set for: the four resolution kinds, in their
/// order, then renamed records.
constexpr size_t numOdrKinds = 5;
constexpr size_t recordRenamedKind = 4;

static StringRef odrKindName(size_t kind) {
  return kind == recordRenamedKind
             ? "record-renamed"
             : resolutionKindName(static_cast<ResolutionKind>(kind));
}

/// The severity each kind of choice is reported at.
struct OdrPolicy {
  std::array<OdrSeverity, numOdrKinds> byKind;

  OdrSeverity of(ResolutionKind kind) const {
    return byKind[static_cast<size_t>(kind)];
  }
  bool reportsAnything() const {
    return llvm::any_of(byKind,
                        [](OdrSeverity s) { return s != OdrSeverity::Ignore; });
  }
};

static cl::opt<OdrSeverity> odrSeverityDefault(
    "odr",
    cl::desc("How to report a choice between two definitions that disagree, "
             "for every kind --odr-severity does not name"),
    cl::values(clEnumValN(OdrSeverity::Ignore, "ignore",
                          "Record it only (the default)"),
               clEnumValN(OdrSeverity::Remark, "remark", "Report a remark"),
               clEnumValN(OdrSeverity::Warning, "warning", "Report a warning"),
               clEnumValN(OdrSeverity::Error, "error",
                          "Report an error: the link fails, after "
                          "--odr-events is written")),
    cl::init(OdrSeverity::Ignore));

/// One --odr-severity entry: a kind of choice and the severity it gets.
struct OdrKindSeverity {
  size_t kind = 0;
  OdrSeverity severity = OdrSeverity::Ignore;
};

/// Parses `<kind>=<level>`, so that a bad entry is rejected the way a bad
/// value of any other option is.
class OdrKindSeverityParser : public cl::basic_parser<OdrKindSeverity> {
public:
  using basic_parser::basic_parser;

  bool parse(cl::Option &option, StringRef, StringRef arg,
             OdrKindSeverity &value) {
    auto [kindName, levelName] = arg.split('=');
    std::optional<size_t> kind;
    for (size_t k = 0; k < numOdrKinds; ++k)
      if (kindName == odrKindName(k))
        kind = k;
    if (!kind)
      return option.error("unknown kind '" + kindName +
                          "'; expected weak-weak, strong-over-weak, "
                          "rtti-over-nortti, strong-strong or record-renamed");
    std::optional<OdrSeverity> severity = parseOdrSeverity(levelName);
    if (!severity)
      return option.error("unknown level '" + levelName +
                          "'; expected ignore, remark, warning or error");
    value = {*kind, *severity};
    return false;
  }

  StringRef getValueName() const override { return "kind=level"; }
};

static cl::list<OdrKindSeverity, bool, OdrKindSeverityParser> odrSeverityByKind(
    "odr-severity",
    cl::desc("Override --odr for one kind of choice, as <kind>=<level>: kind "
             "is weak-weak, strong-over-weak, rtti-over-nortti, strong-strong "
             "or record-renamed. Comma-separated, or repeated"),
    cl::CommaSeparated);

/// Where a diagnostic about a definition points, and in whose terms it
/// says what differs.
enum class DiagnosticStyle { CIR, Mixed, Source };

static cl::opt<DiagnosticStyle> diagnosticStyle(
    "diagnostic-style",
    cl::desc("Where diagnostics about a definition point, and in whose terms "
             "they say what differs"),
    cl::values(
        clEnumValN(DiagnosticStyle::CIR, "cir",
                   "At the CIR, in CIR terms: an op's position in a text .cir "
                   "input, else the input itself (the default)"),
        clEnumValN(DiagnosticStyle::Mixed, "mixed",
                   "At the source location CIRGen recorded, where there is "
                   "one, showing the CIR that differs"),
        clEnumValN(DiagnosticStyle::Source, "source",
                   "At the source location CIRGen recorded, where there is "
                   "one, in source terms only")),
    cl::init(DiagnosticStyle::CIR));

/// One --diagnostic-path-prefix-map entry.
struct PathPrefixMapping {
  std::string from;
  std::string to;
};

/// Parses `<old>=<new>`.
class PathPrefixMappingParser : public cl::basic_parser<PathPrefixMapping> {
public:
  using basic_parser::basic_parser;

  bool parse(cl::Option &option, StringRef, StringRef arg,
             PathPrefixMapping &value) {
    auto [from, to] = arg.split('=');
    if (!arg.contains('=') || from.empty())
      return option.error("expected <old>=<new>, got '" + arg + "'");
    value = {from.str(), to.str()};
    return false;
  }

  StringRef getValueName() const override { return "old=new"; }
};

static cl::list<PathPrefixMapping, bool, PathPrefixMappingParser>
    diagnosticPathPrefixMap(
        "diagnostic-path-prefix-map",
        cl::desc("Rewrite a path prefix in the files diagnostics about a "
                 "definition and --odr-events name, as <old>=<new>, like "
                 "-fdebug-prefix-map: the last one that matches applies. "
                 "Repeatable"));

/// Does this op define the symbol, as opposed to merely declaring it?
///
/// Both functions and globals can be declarations, and getting globals wrong
/// is not a cosmetic error. A module that throws references
/// `_ZTVN10__cxxabiv117__class_type_infoE`, the vtable that libc++abi defines,
/// as an external `cir.global` with no initializer. Treating that as a
/// definition let --internalize turn it into a local empty array, so the
/// typeinfo it backs pointed at nothing and any program that threw segfaulted.
///
/// Anything else carrying a symbol is treated as a definition, which is the
/// conservative direction: mistaking a definition for a declaration would drop
/// code, while the reverse costs at most a redundant entry the duplicate check
/// already handles.
static bool isSymbolDefinition(Operation *op) {
  // isDeclarationForLinker, not isDeclaration: it also covers
  // available_externally, which is a body supplied purely so the inliner can
  // see it. The real definition lives elsewhere and the backend drops this
  // one, so for linking purposes it is a declaration.
  //
  // Treating it as a definition is not merely noisy. It made a 31-TU program
  // report 842 "ODR divergences" -- 28 available_externally functions per TU,
  // mostly libstdc++ header inlines -- which would bury any genuine finding.
  // Worse, first-wins could let an available_externally body beat the real
  // definition, and that body is then dropped at lowering, leaving an
  // undefined symbol.
  if (auto gv = dyn_cast<cir::CIRGlobalValueInterface>(op))
    return !gv.isDeclarationForLinker();
  return true;
}

/// The linkage the linker sees for `alias`: a weak alias is weak whatever its
/// linkage says, as `__attribute__((weak, alias(...)))` is emitted.
static cir::GlobalLinkageKind aliasLinkage(cir::AliasOp alias) {
  return alias.getWeak() ? cir::GlobalLinkageKind::WeakAnyLinkage
                         : alias.getLinkage();
}

/// Make `declOp`, a function declaration, the alias `aliasOp` defines, and
/// return it; null if it cannot be.
///
/// A function one input declares and another defines as a `cir.alias`: the
/// alias cannot stand in for the declaration, since a `cir.call` names a
/// `cir.func`, and the inputs that call the declaration would no longer
/// verify. So the declaration becomes the alias, as CIRGen does when a TU
/// declares a function and then aliases it: a `cir.func` with an aliasee, which
/// the lowering emits as the same LLVM alias. An ifunc stays as it is.
static Operation *aliasAsFunction(Operation *declOp, Operation *aliasOp) {
  auto decl = dyn_cast<cir::FuncOp>(declOp);
  auto alias = dyn_cast<cir::AliasOp>(aliasOp);
  if (!decl || !alias || !decl.isDeclaration() || alias.getIsIfunc() ||
      !isa<cir::FuncType>(alias.getSymType()))
    return nullptr;
  decl.setAliasee(alias.getAliasee());
  decl.setLinkage(aliasLinkage(alias));
  decl.setGlobalVisibility(alias.getGlobalVisibility());
  decl.setDsoLocal(alias.getDsoLocal());
  return decl;
}

/// Can this definition be replaced by a strong one, and silently discarded in
/// favour of an equivalent copy? True for linkonce/weak in all their flavours,
/// which is how CIRGen emits inline functions and template instantiations.
///
/// A `cir.alias` is not a CIRGlobalValueInterface and answers from its own
/// linkage: asked through the interface, every alias was strong, so a
/// linkonce_odr vtable alias in two inputs was a strong-strong divergence, and
/// a weak alias first in the link beat a strong definition of the same name.
static bool isWeakForLinker(Operation *op) {
  if (auto gv = dyn_cast<cir::CIRGlobalValueInterface>(op))
    return gv.isWeakForLinker();
  if (auto alias = dyn_cast<cir::AliasOp>(op))
    return cir::isWeakForLinker(aliasLinkage(alias));
  return false;
}

/// Is this a declaration of a global variable owned outside the TU -- the form
/// a TU emits when some other TU is responsible for the definition?
static bool isExternalGlobalDeclaration(Operation *op) {
  auto global = dyn_cast<cir::GlobalOp>(op);
  return global && global.isDeclaration() &&
         global.getLinkage() == cir::GlobalLinkageKind::ExternalLinkage;
}

/// Is this symbol private to the translation unit it came from?
///
/// Ops that do not carry linkage at all are treated as externally visible.
/// That is the safe default: mistaking an external symbol for a local one
/// would rename something the rest of the program refers to by name.
static bool hasLocalLinkage(Operation *op) {
  if (auto gv = dyn_cast<cir::CIRGlobalValueInterface>(op))
    return gv.hasLocalLinkage();
  if (auto alias = dyn_cast<cir::AliasOp>(op))
    return cir::isLocalLinkage(aliasLinkage(alias));
  return false;
}

/// Give `op` a name nothing has claimed, updating every reference to it inside
/// `scope`. Must run while the op still sits in its own module, so that the
/// uses being rewritten are exactly that translation unit's.
static void renameToUnique(Operation *op, Operation *scope, StringRef base,
                           llvm::StringMap<Resolution> &claimed) {
  SmallString<64> candidate;
  unsigned n = 1;
  do {
    candidate.assign(base);
    candidate += ".cirlink.";
    candidate += std::to_string(n++);
  } while (claimed.count(candidate));

  auto newName = StringAttr::get(op->getContext(), candidate);
  if (failed(SymbolTable::replaceAllSymbolUses(op, newName, scope)))
    llvm::reportFatalInternalError("could not rename a local symbol");
  SymbolTable::setSymbolName(op, newName);
}

/// The location of a whole file, as the MLIR parser gives one: what is said
/// about an input or an output as a whole is said here.
static mlir::Location fileLoc(MLIRContext *context, StringRef filename) {
  return mlir::FileLineColLoc::get(context, filename, 0, 0);
}

/// The first file position inside `loc`, if any: CIRGen locations are ranges
/// and fusions of them, and a diagnostic wants one place.
static std::optional<mlir::FileLineColRange> filePosition(mlir::Location loc) {
  std::optional<mlir::FileLineColRange> found;
  loc->walk([&](mlir::Location inner) {
    if (auto range = dyn_cast<mlir::FileLineColRange>(inner)) {
      found = range;
      return mlir::WalkResult::interrupt();
    }
    return mlir::WalkResult::advance();
  });
  return found;
}

/// Where a diagnostic about a definition points, under --diagnostic-locations
/// and --diagnostic-path-prefix-map, and how it names an input.
class DiagnosticLocator {
public:
  DiagnosticLocator(MLIRContext &context, ArrayRef<std::string> inputs)
      : context(context), inputs(inputs), sources(inputs.size()) {
    for (const std::string &input : inputs)
      inputSet.insert(input);
  }

  /// The translation unit `input` was built from, as CIRGen recorded it.
  void setSource(unsigned input, StringRef source) {
    sources[input] = source.str();
  }
  StringRef source(unsigned input) const { return sources[input]; }

  /// Whether diagnostics point at the source CIRGen recorded.
  static bool usesSource() { return diagnosticStyle != DiagnosticStyle::CIR; }
  /// Whether they show CIR; not in source style, for a reader who never
  /// wrote any.
  static bool showsCIR() { return diagnosticStyle != DiagnosticStyle::Source; }

  /// Where to report something at `loc` that came from `input`. In cir style
  /// `loc` is used only where it is a position in a text .cir input -- what
  /// the parser gives an op with no location of its own -- and the input as a
  /// whole otherwise; in the other styles any file position is used. One
  /// position: CIRGen's locations are ranges, often fused, and a diagnostic
  /// shows one place.
  mlir::FileLineColLoc at(mlir::LocationAttr loc, unsigned input) const {
    std::optional<mlir::FileLineColRange> pos =
        loc ? filePosition(mlir::Location(loc)) : std::nullopt;
    if (pos &&
        (usesSource() || inputSet.contains(pos->getFilename().getValue())))
      return mlir::FileLineColLoc::get(
          &context, mapPath(pos->getFilename().getValue()), pos->getStartLine(),
          pos->getStartColumn());
    return mlir::FileLineColLoc::get(&context, mapPath(inputs[input]), 0, 0);
  }

  /// An input as a message names it: quoted, followed, where diagnostics
  /// point at the source, by its translation unit, which is what there is to
  /// fix.
  std::string name(unsigned input) const {
    std::string out = "'" + mapPath(inputs[input]) + "'";
    if (usesSource() && !sources[input].empty())
      out += " (" + mapPath(sources[input]) + ")";
    return out;
  }

  /// `path` with the last --diagnostic-path-prefix-map that matches applied,
  /// as -fdebug-prefix-map does.
  std::string mapPath(StringRef path) const {
    llvm::SmallString<256> mapped(path);
    for (const PathPrefixMapping &mapping :
         llvm::reverse(diagnosticPathPrefixMap))
      if (llvm::sys::path::replace_path_prefix(mapped, mapping.from,
                                               mapping.to))
        break;
    return std::string(mapped);
  }

private:
  MLIRContext &context;
  ArrayRef<std::string> inputs;
  llvm::StringSet<> inputSet;
  std::vector<std::string> sources;
};

/// MLIR diagnostics rendered into a string rather than to stderr.
struct BufferedDiagnostics {
  std::string text;
  llvm::raw_string_ostream stream{text};
  llvm::SourceMgr sourceMgr;
  std::optional<mlir::SourceMgrDiagnosticHandler> handler;
};

/// Everything about one input that can be done without looking at any other
/// input: open it, find its CIR, and parse it into its own context.
///
/// This is the part of the link that is embarrassingly parallel, and it has to
/// be lifted out of the main loop to become so, because that loop threads the
/// record-unification state from one input to the next and cannot be reordered.
struct PreparedInput {
  /// Owns everything below it, so it is declared first and destroyed last.
  std::unique_ptr<MLIRContext> context;
  /// What the input's own context reports, rendered. Inputs are prepared on
  /// worker threads, and a diagnostic printed from one interleaves with every
  /// other worker's, so the merge prints these when it reaches the input, in
  /// input order. The handler is dropped with the context; the text stays.
  std::unique_ptr<BufferedDiagnostics> diagnostics;
  std::unique_ptr<llvm::MemoryBuffer> embedded;
  OwningOpRef<ModuleOp> parsed;
  /// The module re-read into the shared context, and the bytecode it came
  /// from. Filled by the transfer stage, which runs in parallel over a window.
  std::string transfer;
  OwningOpRef<ModuleOp> inShared;
  /// The inline functions localizeTUAttributes renamed in this input, for the
  /// merge to collect.
  llvm::StringSet<> noExceptionsRenamed;
  /// Whether the input was built with -fno-exceptions, read before
  /// localizeTUAttributes strips the marker.
  bool noCXXExceptions = false;
  bool sawBytecode = false;
  /// Whether preparing or transferring the input failed; what went wrong has
  /// been reported.
  bool failed = false;
};

static void prepareInput(StringRef filename, const DialectRegistry &registry,
                         PreparedInput &out) {
  llvm::TimeTraceScope prepareScope("prepareInput", filename);

  // A private context per input is what makes a record-name collision
  // detectable instead of fatal; see the comment in the merge loop. That it
  // also makes this stage independent is a bonus, not the reason.
  //
  // Single-threaded: inputs are already prepared a window at a time in
  // parallel, and a multithreaded context starts a thread pool of its own --
  // one per input in flight, each as wide as the machine, contending on the
  // locks of parallel verification.
  out.context =
      std::make_unique<MLIRContext>(registry, MLIRContext::Threading::DISABLED);
  out.context->loadAllAvailableDialects();
  out.context->allowUnregisteredDialects(false);
  out.diagnostics = std::make_unique<BufferedDiagnostics>();
  out.diagnostics->handler.emplace(out.diagnostics->sourceMgr, &*out.context,
                                   out.diagnostics->stream);

  // An input may be an object file rather than CIR. A fat object carries its
  // CIR in a .cir section alongside the native code, which is the form a
  // link-time pipeline actually receives -- the build system hands over the
  // same .o list it would hand to the linker.
  bool isObjectFile = false;
  {
    llvm::TimeTraceScope readScope("readInput");
    if (auto binary = llvm::object::createBinary(filename)) {
      if (auto *obj =
              llvm::dyn_cast<llvm::object::ObjectFile>(binary->getBinary())) {
        isObjectFile = true;
        for (const llvm::object::SectionRef &sec : obj->sections()) {
          llvm::Expected<StringRef> name = sec.getName();
          if (!name) {
            // An Expected must be consumed even when it is only being tested;
            // dropping it aborts an assertions build.
            llvm::consumeError(name.takeError());
            continue;
          }
          if (*name != ".cir")
            continue;
          llvm::Expected<StringRef> data = sec.getContents();
          if (!data) {
            // Distinct from the diagnostic below: the section is there and
            // unreadable, so telling the user to rebuild with
            // -fclangir-fat-objects would send them after a flag they already
            // used.
            mlir::emitError(fileLoc(&*out.context, filename))
                << "cannot read the .cir section: "
                << llvm::toString(data.takeError());
            out.failed = true;
            return;
          }
          out.embedded = llvm::MemoryBuffer::getMemBufferCopy(*data, filename);
          break;
        }
      }
    } else {
      // Not an object file at all: the ordinary case of a .cir input.
      llvm::consumeError(binary.takeError());
    }
  }
  if (!out.embedded && isObjectFile) {
    mlir::emitError(fileLoc(&*out.context, filename))
        << "object file has no .cir section; was it built with "
           "-fclangir-fat-objects?";
    out.failed = true;
    return;
  }

  if (out.embedded) {
    out.sawBytecode = mlir::isBytecode(out.embedded->getMemBufferRef());
  } else if (auto head = llvm::MemoryBuffer::getFileSlice(filename, 16, 0)) {
    // Only the magic is needed, so the file is not read twice.
    out.sawBytecode = mlir::isBytecode((*head)->getMemBufferRef());
  }

  {
    llvm::TimeTraceScope parseScope("parsePrivate");
    out.parsed =
        out.embedded
            ? parseSourceString<ModuleOp>(out.embedded->getBuffer(),
                                          ParserConfig(&*out.context), filename)
            : parseSourceFile<ModuleOp>(filename, &*out.context);
  }
  // The parser has said what is wrong, and where.
  out.failed = !out.parsed;
}

//===----------------------------------------------------------------------===//
// Module attributes
//===----------------------------------------------------------------------===//
//
// A module attribute is a statement about one TU, and the merged module speaks
// for all of them. Taking the first input's copy of each is right only where
// every TU says the same thing, and several do not: across one large
// library's 6043 inputs, 447 define pointer overflow (-fwrapv-pointer), 194 are
// C, three optimization settings occur, 105 carry a frame-pointer policy, and
// each marks the op kinds it happens to contain. So every attribute has a
// stated policy, and one with none must agree -- an input that disagrees about
// an attribute no policy covers stops the link, rather than silently taking the
// first input's answer for everyone.

static constexpr llvm::StringLiteral pointerOverflowDefinedAttr =
    "cir.pointer_overflow_defined";

/// Rewrite the attributes of \p op through \p replacer. A registered op's
/// inherent attributes are read one by one rather than through
/// getAttrDictionary, which builds and uniques a dictionary for every op it is
/// asked about -- every op of every input, here.
static void replaceElementsInAttrs(Operation *op,
                                   mlir::AttrTypeReplacer &replacer) {
  std::optional<mlir::RegisteredOperationName> info = op->getRegisteredInfo();
  if (!info || !op->getPropertiesStorageSize()) {
    DictionaryAttr attrs = op->getRawDictionaryAttrs();
    auto replaced = cast<DictionaryAttr>(replacer.replace(attrs));
    if (replaced != attrs)
      op->setAttrs(replaced);
    return;
  }
  for (StringAttr name : info->getAttributeNames()) {
    std::optional<mlir::Attribute> attr = op->getInherentAttr(name);
    if (!attr || !*attr)
      continue;
    mlir::Attribute replaced = replacer.replace(*attr);
    if (replaced != *attr)
      op->setInherentAttr(name, replaced);
  }
  for (NamedAttribute attr : llvm::to_vector(op->getDiscardableAttrs())) {
    mlir::Attribute replaced = replacer.replace(attr.getValue());
    if (replaced != attr.getValue())
      op->setDiscardableAttr(attr.getName(), replaced);
  }
}

/// Point every flat symbol reference \p refMap names at its new name, in the
/// ops of \p module and in its own attributes. The ops are walked a top-level
/// op at a time in parallel: functions are isolated from above, so no two
/// walks touch one op.
static void
replaceSymbolRefs(ModuleOp module,
                  const llvm::StringMap<mlir::FlatSymbolRefAttr> &refMap) {
  auto replaceIn = [&](Operation *root, bool nested) {
    mlir::AttrTypeReplacer replacer;
    replacer.addReplacement(
        [&](mlir::FlatSymbolRefAttr ref) -> std::optional<mlir::Attribute> {
          auto it = refMap.find(ref.getValue());
          if (it == refMap.end())
            return std::nullopt;
          return it->second;
        });
    if (!nested) {
      replaceElementsInAttrs(root, replacer);
      return;
    }
    root->walk([&](Operation *op) { replaceElementsInAttrs(op, replacer); });
  };
  SmallVector<Operation *> topLevel;
  for (Operation &op : module.getBody()->getOperations())
    topLevel.push_back(&op);
  mlir::parallelForEach(module.getContext(), topLevel,
                        [&](Operation *op) { replaceIn(op, /*nested=*/true); });
  replaceIn(module.getOperation(), /*nested=*/false);
}

/// Keep the inline functions of a TU built without C++ exceptions apart from
/// the same functions built with them.
///
/// An inline function's body can depend on the exception mode while its name
/// does not. libc++ throws out_of_range from std::__throw_out_of_range, or
/// aborts under -fno-exceptions; upstream libc++ puts the mode in an ABI tag on
/// the name, and the NDK's does not. Both are the same linkonce_odr symbol, and
/// the merge keeps the first copy: libarmerged_arservicesforwolf.so ended up
/// with the aborting one, so every vector::at out of range in the library
/// aborted where it used to throw something a caller could catch. In a TU's
/// own compile each copy is mostly inlined before any linker sees it.
///
/// So the hidden linkonce functions such a TU defines get a name of their own,
/// shared by every TU in that mode: those copies merge with each other and not
/// with the exception-enabled ones, as an ABI tag would have it. Hidden only --
/// a default-visibility function may be exported under its name, and libc++'s
/// helpers are all _LIBCPP_HIDE_FROM_ABI. Uses within the TU follow the name.
///
/// Each original name goes into `renamed`, for the fix-up after the merge: a
/// TU that only declares the function still refers to it by that name.
static void separateNoExceptionsInlines(ModuleOp input,
                                        llvm::StringSet<> &renamed) {
  MLIRContext *ctx = input.getContext();
  llvm::StringMap<mlir::FlatSymbolRefAttr> refMap;
  SmallVector<std::pair<Operation *, StringAttr>> toRename;
  for (auto fn : input.getOps<cir::FuncOp>()) {
    cir::GlobalLinkageKind linkage = fn.getLinkage();
    if (fn.isDeclaration() ||
        (linkage != cir::GlobalLinkageKind::LinkOnceODRLinkage &&
         linkage != cir::GlobalLinkageKind::LinkOnceAnyLinkage) ||
        fn.getGlobalVisibility() != cir::VisibilityKind::Hidden)
      continue;
    auto newName =
        StringAttr::get(ctx, (fn.getSymName() + ".cirlink.noexcept").str());
    refMap[fn.getSymName()] = mlir::FlatSymbolRefAttr::get(newName);
    renamed.insert(fn.getSymName());
    toRename.emplace_back(fn, newName);
  }
  if (toRename.empty())
    return;
  replaceSymbolRefs(input, refMap);
  for (auto &[op, newName] : toRename)
    SymbolTable::setSymbolName(op, newName);

  // The per-TU lists on the module name their symbols with plain strings, not
  // symbol references, so replaceSymbolRefs does not see them. An
  // __attribute__((annotate)) on one of these inline functions kept the
  // original name, the merged module had nothing by that name, and lowering the
  // annotation asserted on an address-of an undefined function
  // (libwebrtcLatest.so: webrtc::PortInterface::SubscribeReadPacket).
  auto follow = [&](StringAttr name) -> StringAttr {
    if (!name)
      return name;
    auto it = refMap.find(name.getValue());
    return it == refMap.end() ? name : it->second.getAttr();
  };
  Operation *mod = input.getOperation();
  if (auto annotations = mod->getAttrOfType<cir::GlobalAnnotationValuesAttr>(
          cir::CIRDialect::getGlobalAnnotationsAttrName())) {
    SmallVector<mlir::Attribute> entries;
    for (mlir::Attribute entry : annotations.getAnnotations()) {
      auto pair = dyn_cast<ArrayAttr>(entry);
      if (pair && pair.size() == 2)
        if (auto symbol = dyn_cast<StringAttr>(pair[0]))
          entry = ArrayAttr::get(ctx, {follow(symbol), pair[1]});
      entries.push_back(entry);
    }
    mod->setAttr(
        cir::CIRDialect::getGlobalAnnotationsAttrName(),
        cir::GlobalAnnotationValuesAttr::get(ArrayAttr::get(ctx, entries)));
  }
  auto followList = [&](StringRef attrName, auto make) {
    auto list = mod->getAttrOfType<ArrayAttr>(attrName);
    if (!list)
      return;
    SmallVector<mlir::Attribute> entries;
    for (mlir::Attribute entry : list)
      entries.push_back(make(entry));
    mod->setAttr(attrName, ArrayAttr::get(ctx, entries));
  };
  followList(cir::CIRDialect::getGlobalCtorsAttrName(),
             [&](mlir::Attribute entry) -> mlir::Attribute {
               if (auto ctor = dyn_cast<cir::GlobalCtorAttr>(entry))
                 return cir::GlobalCtorAttr::get(
                     follow(ctor.getName()), ctor.getPriority(),
                     follow(ctor.getAssociatedData()));
               return entry;
             });
  followList(cir::CIRDialect::getGlobalDtorsAttrName(),
             [&](mlir::Attribute entry) -> mlir::Attribute {
               if (auto dtor = dyn_cast<cir::GlobalDtorAttr>(entry))
                 return cir::GlobalDtorAttr::get(
                     follow(dtor.getName()), dtor.getPriority(),
                     follow(dtor.getAssociatedData()));
               return entry;
             });
}

/// Point references at a function's -fno-exceptions copy where that copy is
/// the only definition of it.
///
/// separateNoExceptionsInlines renames the definitions a -fno-exceptions TU
/// makes, and the uses in that TU. Another TU may only declare the function,
/// though, and refer to it under its own name: Hermes defines every native
/// function inline in its JSLib TU (Array.cpp's arrayPrototypeForEach, hidden
/// linkonce_odr) and takes their addresses in another (JSNativeFunctions.cpp's
/// table), which only declares them. With the definition renamed and nothing
/// else defining the name, the library's link failed on an undefined symbol.
/// When no definition of the name survives but its -fno-exceptions copy does,
/// every reference goes to that copy -- the only definition there is, as it
/// is in a classic link. When an exception-enabled definition survives too,
/// references to the name stay on it.
static void redirectToNoExceptionsCopies(ModuleOp merged,
                                         const llvm::StringSet<> &renamed) {
  if (renamed.empty())
    return;
  MLIRContext *ctx = merged.getContext();
  SymbolTable symbols(merged.getOperation());
  llvm::StringMap<mlir::FlatSymbolRefAttr> refMap;
  SmallVector<Operation *> strandedDeclarations;
  for (const auto &entry : renamed) {
    StringRef name = entry.getKey();
    Operation *copy = symbols.lookup((name + ".cirlink.noexcept").str());
    if (!copy || !isSymbolDefinition(copy))
      continue;
    Operation *original = symbols.lookup(name);
    if (original && isSymbolDefinition(original))
      continue;
    refMap[name] = mlir::FlatSymbolRefAttr::get(
        ctx, SymbolTable::getSymbolName(copy).getValue());
    if (original)
      strandedDeclarations.push_back(original);
  }
  if (refMap.empty())
    return;
  replaceSymbolRefs(merged, refMap);
  for (Operation *declaration : strandedDeclarations)
    declaration->erase();
}

/// Move what is per-TU off the input module and onto its ops, before any of
/// them are merged. Runs on every input, the first included.
///
///  - cir.pointer_overflow_defined forbids inbounds/nuw on the GEPs that member
///    access and array decay lower to. It is marked on those ops instead, so
///    each keeps its own TU's rule.
///  - A function from a TU that is not C++ records that TU's language as
///    cir.lang: the language decides its calling convention, and the merged
///    module is C++ if any input is (see finalizeMergedLanguage).
static void localizeTUAttributes(ModuleOp input,
                                 llvm::StringSet<> &noExceptionsRenamed) {
  Operation *mod = input.getOperation();
  MLIRContext *ctx = input.getContext();
  if (mod->hasAttr(pointerOverflowDefinedAttr)) {
    auto unit = UnitAttr::get(ctx);
    input.walk([&](Operation *op) {
      if (isa<cir::GetMemberOp>(op))
        op->setAttr(pointerOverflowDefinedAttr, unit);
      else if (auto cast = dyn_cast<cir::CastOp>(op);
               cast && cast.getKind() == cir::CastKind::array_to_ptrdecay)
        op->setAttr(pointerOverflowDefinedAttr, unit);
    });
    mod->removeAttr(pointerOverflowDefinedAttr);
  }
  auto lang = mod->getAttrOfType<cir::SourceLanguageAttr>(
      cir::CIRDialect::getSourceLanguageAttrName());
  if (lang && lang.getValue() != cir::SourceLanguage::CXX)
    for (auto fn : input.getOps<cir::FuncOp>())
      fn->setAttr(cir::CIRDialect::getSourceLanguageAttrName(), lang);
  if (mod->hasAttr(cir::CIRDialect::getNoCXXExceptionsAttrName())) {
    separateNoExceptionsInlines(input, noExceptionsRenamed);
    mod->removeAttr(cir::CIRDialect::getNoCXXExceptionsAttrName());
  }
}

/// clang's FramePointerKind (what cir.frame_pointer holds) to LLVM's, whose
/// numeric order the "frame-pointer" module flag's Max behaviour ranks by. An
/// input without the attribute has no flag, and LLVM merges a missing flag as
/// no vote at all: it ranks below every policy, None included.
static int framePointerRank(mlir::Attribute attr) {
  if (!attr)
    return -1;
  static const int clangToLLVM[] = {
      static_cast<int>(llvm::FramePointerKind::NonLeafNoReserve),
      static_cast<int>(llvm::FramePointerKind::None),
      static_cast<int>(llvm::FramePointerKind::Reserved),
      static_cast<int>(llvm::FramePointerKind::NonLeaf),
      static_cast<int>(llvm::FramePointerKind::All),
  };
  auto intAttr = dyn_cast<mlir::IntegerAttr>(attr);
  if (!intAttr || intAttr.getInt() < 0 ||
      intAttr.getInt() >= static_cast<int64_t>(std::size(clangToLLVM)))
    return -1;
  return clangToLLVM[intAttr.getInt()];
}

/// Fold a later input's module attributes into the merged module's.
/// The record ABI tables of the inputs merged so far. Kept apart from the
/// merged module and written to it once the merge is done: rebuilding the
/// module's table as a DictionaryAttr for every input cost time in proportion
/// to inputs times records.
struct RecordABIFacts {
  bool seen = false;
  llvm::MapVector<StringAttr, uint64_t> bits;

  void add(mlir::Attribute table) {
    seen = true;
    if (auto dict = dyn_cast_or_null<DictionaryAttr>(table))
      for (NamedAttribute entry : dict)
        if (auto value = dyn_cast<mlir::IntegerAttr>(entry.getValue()))
          bits[entry.getName()] |= value.getValue().getZExtValue();
  }

  void writeTo(ModuleOp merged) const {
    if (!seen)
      return;
    MLIRContext *ctx = merged.getContext();
    SmallVector<NamedAttribute> entries;
    for (auto &[entryName, value] : bits)
      entries.emplace_back(
          entryName,
          mlir::IntegerAttr::get(mlir::IntegerType::get(ctx, 32), value));
    merged->setAttr(cir::CIRDialect::getRecordABIFactsAttrName(),
                    DictionaryAttr::get(ctx, entries));
  }
};

static LogicalResult mergeModuleAttributes(ModuleOp merged, ModuleOp input,
                                           StringRef filename,
                                           RecordABIFacts &abiFacts) {
  Operation *dst = merged.getOperation();
  Operation *src = input.getOperation();
  MLIRContext *ctx = merged.getContext();

  llvm::SetVector<StringAttr> names;
  for (NamedAttribute attr : dst->getAttrs())
    names.insert(attr.getName());
  for (NamedAttribute attr : src->getAttrs())
    names.insert(attr.getName());

  // Every attribute without a merge rule is reported before failing, not just
  // the first: each one found costs a rebuild of the library to find the next.
  bool unmerged = false;
  for (StringAttr nameAttr : names) {
    StringRef name = nameAttr.getValue();
    mlir::Attribute mine = dst->getAttr(nameAttr);
    mlir::Attribute theirs = src->getAttr(nameAttr);
    if (mine == theirs)
      continue;

    // The module's own name and visibility, not a statement about the TU; the
    // merged module is named for the first input.
    if (name == SymbolTable::getSymbolAttrName() ||
        name == SymbolTable::getVisibilityAttrName())
      continue;

    // Lists every input contributes to, merged where their names are final
    // (constructors, destructors, llvm.used).
    if (name == cir::CIRDialect::getGlobalCtorsAttrName() ||
        name == cir::CIRDialect::getGlobalDtorsAttrName() ||
        name == cir::CIRDialect::getGlobalAnnotationsAttrName() ||
        name == cir::CIRDialect::getLLVMUsedAttrName() ||
        name == cir::CIRDialect::getLLVMCompilerUsedAttrName())
      continue;

    // How far each input is through the pipeline decides what lowering is
    // still to run, and the merged module is lowered as one: inputs at
    // different stages would leave some functions lowered twice, or not at
    // all.
    if (name == cir::CIRDialect::getStageAttrName()) {
      auto stageOf = [](mlir::Attribute attr) {
        auto stage = mlir::dyn_cast_if_present<StringAttr>(attr);
        return stage ? stage.getValue().str() : std::string("(none)");
      };
      mlir::emitError(fileLoc(ctx, filename))
          << "input is at CIR stage " << stageOf(theirs)
          << " and earlier inputs are at " << stageOf(mine)
          << "; a merged module is lowered as one";
      return failure();
    }

    // "The module contains X" markers an older CIRGen set; nothing reads them
    // any more, and the merged module does not carry them.
    if (name.starts_with("cir.has_")) {
      dst->removeAttr(nameAttr);
      continue;
    }

    // Flags that switch something off -- TBAA on ABI-created accesses, pointer
    // TBAA, strict returns. Any input's wins: weaker is merely less optimized
    // for the TUs that did not ask, where stronger would be wrong for the one
    // that did.
    if (name == "cir.relaxed_aliasing" || name == "cir.no_pointer_tbaa" ||
        name == "cir.no_strict_return") {
      if (theirs)
        dst->setAttr(nameAttr, theirs);
      continue;
    }

    // What C++ decides about passing each record, keyed by name: every input's
    // records are in the merged module. A name two inputs disagree about keeps
    // both answers -- by invisible reference wins, as the safe one.
    if (name == cir::CIRDialect::getRecordABIFactsAttrName()) {
      if (theirs)
        abiFacts.add(theirs);
      continue;
    }

    // Top-level asm is per TU and all of it is kept, as the linker would.
    if (name == cir::CIRDialect::getModuleLevelAsmAttrName()) {
      SmallVector<mlir::Attribute> combined;
      if (auto list = dyn_cast_or_null<ArrayAttr>(mine))
        combined.append(list.begin(), list.end());
      if (auto list = dyn_cast_or_null<ArrayAttr>(theirs))
        combined.append(list.begin(), list.end());
      dst->setAttr(nameAttr, ArrayAttr::get(ctx, combined));
      continue;
    }

    // Lowered to the "frame-pointer" module flag, which LLVM merges by Max.
    // An input without the attribute asked for none.
    if (name == "cir.frame_pointer") {
      if (framePointerRank(theirs) > framePointerRank(mine))
        dst->setAttr(nameAttr, theirs);
      continue;
    }

    // Lowered to the !llvm.errno.tbaa named metadata, which the IR linker
    // appends from every module that has it: set where the TU had TBAA (it
    // records that the "int" access tag exists), absent where it did not --
    // real libraries mix C TUs with and without. Any input's counts.
    if (name == "cir.emit_errno_tbaa") {
      if (theirs)
        dst->setAttr(nameAttr, theirs);
      continue;
    }

    // Lowered to the "uwtable" module flag, which LLVM merges by Max, and
    // given to the functions the lowering synthesizes (each input's own
    // functions carry their own). An input built without unwind tables has no
    // attribute, and no say, as a module without the flag has none in LLVM.
    // Real libraries link TUs with and without.
    if (name == cir::CIRDialect::getUWTableAttrName()) {
      auto kind = [](mlir::Attribute attr) {
        auto uwtable = mlir::dyn_cast_if_present<cir::UWTableAttr>(attr);
        return uwtable ? static_cast<int>(uwtable.getValue()) : -1;
      };
      if (kind(theirs) > kind(mine))
        dst->setAttr(nameAttr, theirs);
      continue;
    }

    // Lowered to LLVM's "Virtual Function Elim" flag, merged with Error
    // behaviour: a module without the flag has no say, two that disagree are an
    // error. clang sets it only under -fwhole-program-vtables, which a C TU is
    // not built with.
    if (name == "cir.virtual_function_elim") {
      if (!mine) {
        dst->setAttr(nameAttr, theirs);
        continue;
      }
      if (!theirs)
        continue;
    }

    // Read only for optimization hints (constant allocas, branch weights,
    // invariant.start); the link's own optimization level is the linker's.
    // The highest asked for, so no input loses hints it was built with.
    if (name == cir::CIRDialect::getOptInfoAttrName()) {
      auto a = dyn_cast_or_null<cir::OptInfoAttr>(mine);
      auto b = dyn_cast_or_null<cir::OptInfoAttr>(theirs);
      if (!a || !b) {
        if (b)
          dst->setAttr(nameAttr, b);
        continue;
      }
      dst->setAttr(nameAttr,
                   cir::OptInfoAttr::get(ctx, std::max(a.getLevel(), b.getLevel()),
                                         std::max(a.getSize(), b.getSize())));
      continue;
    }

    // Settled once all inputs are in; see finalizeMergedLanguage.
    if (name == cir::CIRDialect::getSourceLanguageAttrName())
      continue;

    // Identify the debug compile unit, which the merged module has one of: it
    // is named for the first input. Per-TU compile units are 00375.
    if (name == "cir.main_file_name" || name == "cir.ident" ||
        name.starts_with("cir.debug_"))
      continue;

    // Signed overflow is decided per op at CIRGen (nsw); nothing reads the
    // module's copy after that.
    if (name == cir::CIRDialect::getSOBAttrName())
      continue;

    auto show = [](mlir::Attribute attr) {
      if (!attr)
        return std::string("(absent)");
      std::string text;
      llvm::raw_string_ostream os(text);
      os << attr;
      return text.size() > 120 ? text.substr(0, 120) + "..." : text;
    };
    mlir::emitError(fileLoc(ctx, filename))
        << "module attribute '" << name << "' is " << show(theirs)
        << " here but " << show(mine)
        << " in earlier inputs, and there is no rule for merging it";
    unmerged = true;
  }
  return failure(unmerged);
}

/// The merged module is C++ if any input is: C++ is the language whose
/// module-wide fast paths (CXXABILowering's) must not be skipped. Functions of
/// other languages carry their own (localizeTUAttributes); where that is the
/// merged module's language anyway, the mark says nothing and comes off.
static void finalizeMergedLanguage(ModuleOp merged, bool anyCXX) {
  Operation *mod = merged.getOperation();
  StringAttr name =
      StringAttr::get(merged.getContext(),
                      cir::CIRDialect::getSourceLanguageAttrName());
  if (anyCXX)
    mod->setAttr(name, cir::SourceLanguageAttr::get(merged.getContext(),
                                                    cir::SourceLanguage::CXX));
  auto lang = mod->getAttrOfType<cir::SourceLanguageAttr>(name);
  for (auto fn : merged.getOps<cir::FuncOp>())
    if (fn->getAttr(name) == lang)
      fn->removeAttr(name);
}

/// Turn a discardable global definition back into the declaration other
/// inputs made of it, where one did.
///
/// A TU emits a linkonce copy of a global it needs but is not sure anyone else
/// provides: typeinfo under -fno-rtti, whose owner (the TU holding the class's
/// key function) might have been built without RTTI too. Every other TU
/// declares that typeinfo external and relies on the owner -- libc++ for
/// std::bad_function_call. Merged, the copy is a definition among declarations
/// and prevails, internalization makes it local, and the library ends up with
/// its own std::bad_function_call typeinfo: an exception it throws is no
/// longer the type catch clauses in other libraries compare against, and a
/// typeinfo is compared by address. One real library had exactly this, from one
/// -fno-rtti TU (JNI OnLoad glue) among 1093 that use the type.
///
/// In a TU's own compile the copy mostly disappears, deleted with the code
/// that needed it before the linker sees it; that is how the classic build
/// kept importing libc++'s. The link cannot rely on that, so it keeps the
/// declaration outright: when some input declares a global external and the
/// only definition is linkonce, the owner is somewhere else. Should there turn
/// out to be none, the symbol is undefined at the final link, loudly, rather
/// than silently split in two.
///
/// That inference needs an RTTI split to be sound: a -fno-rtti kept copy
/// beside an external declaration from a TU built with RTTI. When every input
/// agrees on RTTI, a linkonce copy with external declarations is a class whose
/// key function is inline in one header and merely declared in another: each
/// view emits what it believes in, a weak vtable here and an external
/// declaration there, and no strong definition exists anywhere. Deferring the
/// only copy then manufactures the undefined symbol, and the archive member
/// the final link pulls for it brings its other definitions with it.
///
/// Globals only. A function has no identity to split, and its linkonce body
/// stays available to the inliner.
///
/// But the owner is not always somewhere the link can reach: a library's
/// TUs can use a vendored libc++ in its own namespace, whose
/// typeinfo for length_error no library it links against defines, and the
/// classic build linked only because the linkonce copy survived. Deferring it
/// left the symbol undefined. So when the link says which symbols its shared
/// libraries define (`sharedSymbols`, from the wrapper), a global is deferred
/// only if one of them does; without that list (cir-link run on its own), as
/// before. An empty list -- a link with no shared library -- defers nothing,
/// on purpose: deferring is for an owner in another shared object, so that the
/// program has one copy; an owner among the link's own objects resolves the
/// kept linkonce copy to itself, and none at all is the case above.
static unsigned deferDiscardableDataToOwners(
    llvm::StringMap<Resolution> &resolved, const llvm::BitVector &noRTTIInputs,
    const DiagnosticLocator *report, const llvm::StringSet<> *sharedSymbols) {
  unsigned count = 0;
  for (auto &entry : resolved) {
    Resolution &res = entry.second;
    if (!res.declaredExternal || !res.isDefinition)
      continue;
    if (!res.declaredExternalWithRTTI || !noRTTIInputs.test(res.inputIdx))
      continue;
    if (sharedSymbols && !sharedSymbols->contains(entry.getKey()))
      continue;
    auto global = dyn_cast<cir::GlobalOp>(res.op);
    if (!global ||
        (global.getLinkage() != cir::GlobalLinkageKind::LinkOnceODRLinkage &&
         global.getLinkage() != cir::GlobalLinkageKind::LinkOnceAnyLinkage) ||
        !global.getCtorRegion().empty() || !global.getDtorRegion().empty())
      continue;
    global.setLinkage(cir::GlobalLinkageKind::ExternalLinkage);
    global.removeInitialValueAttr();
    global.removeComdatAttr();
    global.removeDsoLocalAttr();
    // As the declarations it now matches are: MLIR does not let a symbol
    // declaration be public.
    SymbolTable::setSymbolVisibility(global, SymbolTable::Visibility::Private);
    res.isDefinition = false;
    res.isWeak = false;
    ++count;
    if (report)
      mlir::emitWarning(report->at(global.getLoc(), res.inputIdx))
          << "linkonce definition of '" << entry.getKey()
          << "' dropped in favour of the external definition other inputs "
             "declare";
  }
  return count;
}

/// Rename every colliding local in `module`, in one pass over it.
///
/// All the new names are decided first, then every use is rewritten in a
/// single walk. The obvious implementation -- call renameToUnique per symbol --
/// costs one whole-module walk *per rename*, because replaceAllSymbolUses
/// cannot know that another rename is coming. That is quadratic in
/// (renames x module size), and on 380 real inputs it was 82% of the entire
/// link: 123 seconds of 151, for 14,982 renames.
///
/// Returns how many symbols were renamed, and fills `renamed` with old -> new
/// so callers can follow the rename into anything held by name.
static unsigned renameLocalsInBatch(ModuleOp module,
                                    llvm::StringMap<Resolution> &claimed,
                                    llvm::StringMap<unsigned> &nextSuffix,
                                    llvm::StringMap<std::string> &renamed) {
  MLIRContext *ctx = module.getContext();

  SmallVector<std::pair<Operation *, StringAttr>> toRename;

  // `claimed` holds only what *earlier inputs* put in the output, so on its own
  // it does not stop a generated name from landing on a symbol this very module
  // already has. A translation unit with both `helper` and `helper.cirlink.1`
  // is enough: `helper` collides, the first name tried for it is the one its
  // neighbour is already using, and the merged module then receives two
  // definitions under that name and keeps one. A function disappears, and the
  // build is green.
  //
  // So the claim set starts as every symbol name in this module, and each name
  // handed out during the batch joins it -- the second part matters because a
  // batch picks all its names before applying any of them, so a name is not
  // observable in the module until afterwards.
  //
  // The set is only built for an input that has a local to rename.
  std::optional<llvm::TimeTraceScope> decideScope;
  decideScope.emplace("decideLocalRenames");
  SmallVector<Operation *> colliding;
  for (Operation &op : module.getBody()->getOperations()) {
    auto symbol = dyn_cast<SymbolOpInterface>(&op);
    if (symbol && symbol.getNameAttr() && hasLocalLinkage(&op) &&
        claimed.count(symbol.getNameAttr().getValue()))
      colliding.push_back(&op);
  }
  if (colliding.empty())
    return 0;

  // Every name tried has the form `<base>.cirlink.<n>`, so only the module's
  // names of that form can be in its way.
  llvm::StringSet<> batchClaimed;
  for (Operation &op : module.getBody()->getOperations())
    if (auto symbol = dyn_cast<SymbolOpInterface>(&op))
      if (StringAttr name = symbol.getNameAttr();
          name && name.getValue().contains(".cirlink."))
        batchClaimed.insert(name.getValue());

  for (Operation *opPtr : colliding) {
    Operation &op = *opPtr;
    StringRef base = SymbolTable::getSymbolName(&op).getValue();

    // The search starts where the last one for this base ended rather than at
    // 1: every TU has its __cxx_global_var_init, and trying all the names
    // earlier TUs took made the rename quadratic in the inputs. The names
    // skipped are all claimed: one handed out here or found in this module is
    // claimed once the module is merged, which claims every name it has, and
    // nothing is ever unclaimed.
    unsigned &n = nextSuffix[base];
    n = std::max(n, 1u);
    SmallString<64> candidate;
    do {
      candidate.assign(base);
      candidate += ".cirlink.";
      candidate += std::to_string(n++);
    } while (claimed.count(candidate) || batchClaimed.count(candidate));
    batchClaimed.insert(candidate);
    renamed[base] = std::string(candidate);
    toRename.emplace_back(&op, StringAttr::get(ctx, candidate));
  }
  llvm::StringMap<mlir::FlatSymbolRefAttr> refMap;
  for (auto &[op, newName] : toRename)
    refMap[SymbolTable::getSymbolName(op).getValue()] =
        mlir::FlatSymbolRefAttr::get(newName);

  decideScope.reset();

  // One walk, every rename. This also covers the module's own attributes,
  // which is where the retention lists live.
  llvm::TimeTraceScope walkScope("replaceSymbolRefs");
  replaceSymbolRefs(module, refMap);

  // The definitions are renamed after the uses, so that the walk above sees
  // the old names it is keyed on.
  for (auto &[op, newName] : toRename)
    SymbolTable::setSymbolName(op, newName);

  return toRename.size();
}

} // namespace

//===----------------------------------------------------------------------===//
// Record-type unification
//===----------------------------------------------------------------------===//

/// Every named record reachable from `root`, in a deterministic order.
///
/// Deterministic matters more than it looks: the order decides which of two
/// colliding records keeps the original name and which gets a suffix, and a
/// linker whose output depends on hash-map iteration order is not one anybody
/// can bisect against.
///
/// A record's *members* have to be descended into explicitly. Records are
/// mutable types, so the generic sub-element walk does not go through them,
/// and a record that only ever appears as a field of another record --
/// `struct Outer { Inner i; }` where nothing mentions `Inner` on its own --
/// is otherwise never seen. Missing it is not a missing diagnostic: `Outer`
/// gets renamed for holding a different `Inner`, `Inner` does not, and the
/// renamed `Outer` is rebuilt around the wrong one. It verifies and it is
/// wrong.
static void collectNamedRecords(Operation *root,
                                llvm::SetVector<cir::RecordType> &out) {
  llvm::DenseSet<mlir::Type> seenRecords;
  AttrTypeWalker walker;
  walker.addWalk([&](cir::RecordType rec) {
    // The visited set is what breaks recursive records; the walker's own
    // cycle handling never gets the chance, because the recursion below is
    // ours rather than the walker's.
    if (!seenRecords.insert(rec).second)
      return WalkResult::skip();
    if (rec.getName())
      out.insert(rec);
    if (!rec.isIncomplete())
      for (mlir::Type member : rec.getMembers())
        walker.walk(member);
    return WalkResult::advance();
  });

  auto note = [&](mlir::Type t) {
    if (t)
      walker.walk(t);
  };
  root->walk([&](Operation *op) {
    for (NamedAttribute attr : op->getAttrs())
      walker.walk(attr.getValue());
    for (mlir::Type t : op->getResultTypes())
      note(t);
    for (Region &region : op->getRegions())
      for (Block &block : region)
        for (BlockArgument arg : block.getArguments())
          note(arg.getType());
  });
}

/// Is this the name CIRGen invents for an anonymous record?
///
/// `anon`, then `anon.0`, `anon.1`, ... from a counter that restarts every
/// translation unit. The number says nothing about the type -- the same union
/// is `anon.1` in one TU and `anon.0` in the next -- so it must not be read as
/// identity, in either direction: not as two types being the same, and not as
/// one type differing from itself.
static bool isAnonRecordName(StringRef name) {
  if (!name.consume_front("anon"))
    return false;
  if (name.empty())
    return true;
  return name.consume_front(".") && !name.empty() &&
         llvm::all_of(name, llvm::isDigit);
}

/// A signature with those per-TU numbers taken out, so that two records
/// compare equal exactly when they are the same type.
///
/// Quotes are located by scanning, with no unescaping: a record name comes
/// from a C++ type, and the printer emits it between quotes without any of its
/// own, so a `"` inside one is not representable here.
///
/// Only a quoted name that is *entirely* an anon name is rewritten. Substring
/// matching would also hit `(anonymous namespace)::S`, whose name is the one
/// thing making it distinct.
static std::string normalizeAnonNames(StringRef sig) {
  std::string out;
  out.reserve(sig.size());
  while (!sig.empty()) {
    size_t quote = sig.find('"');
    if (quote == StringRef::npos) {
      out += sig;
      break;
    }
    out += sig.take_front(quote + 1);
    sig = sig.drop_front(quote + 1);
    size_t close = sig.find('"');
    if (close == StringRef::npos) {
      out += sig;
      break;
    }
    StringRef name = sig.take_front(close);
    out += isAnonRecordName(name) ? "anon" : name;
    out += '"';
    sig = sig.drop_front(close + 1);
  }
  return out;
}

/// MD5 of a canonical record form, as 32 hex characters, for comparing bodies
/// without retaining them.
///
/// The unification state and the transfer check each hold one entry per named
/// record of every input seen so far; the digest answers every comparison
/// those maps are asked. Hex, not raw bytes: digests are embedded in
/// enclosing canonical forms, and raw bytes can contain quotes that would
/// corrupt the anonymous-name normalization scan. MD5, not a 64-bit hash: the
/// digest decides whether two records are the same type, and a collision
/// would silently merge two different layouts.
static std::string bodyDigest(StringRef body) {
  auto bytes = ArrayRef<uint8_t>(reinterpret_cast<const uint8_t *>(body.data()),
                                 body.size());
  llvm::MD5::MD5Result digest = llvm::MD5::hash(bytes);
  llvm::SmallString<32> hex;
  llvm::MD5::stringifyResult(digest, hex);
  return std::string(hex.str());
}

/// The attributes that say how a definition links rather than what it is.
/// Each copy's linkage is recorded on its own, and the two copies of a
/// strong-over-weak pair differ in it by definition, so comparisons and
/// digests leave these out.
static bool isLinkageAttribute(StringRef name) {
  return name == "linkage" || name == "global_visibility" ||
         name == "sym_visibility" || name == "comdat" || name == "dso_local";
}

/// A global's initializer is its body, not part of its signature.
static bool isBodyAttribute(StringRef name) { return name == "initial_value"; }

/// The definition's type, kept apart from its other attributes: two copies
/// that disagree on a type are a different finding from two that disagree on,
/// say, an inferred nothrow.
static bool isTypeAttribute(StringRef name) {
  return name == "function_type" || name == "sym_type";
}

/// Source locations CIRGen carries as attributes, for the backend's inline asm
/// and call diagnostics. They are locations, so they are not compared.
static bool isLocationAttribute(StringRef name) {
  return name == "cir.asm_srcloc" || name == "cir.call_srcloc" ||
         name == "cir.srcloc";
}

/// A local variable's name, which CIRGen keeps on its alloca for the
/// debugger: two copies that differ only in what a parameter or a local is
/// called -- an explicit specialization leaving a parameter unnamed, a macro
/// counting names per TU -- are the same definition.
static bool isLocalName(Operation *op, StringRef name) {
  return name == "name" && isa<cir::AllocaOp>(op);
}

/// What a copy carries because of how its TU was compiled rather than what
/// its source says. Two copies of one definition from TUs built with
/// different options, or that inferred different things, are the same
/// definition:
///
///  - nothrow, on a function or a call: inferred per TU from the callees it
///    has already emitted, as classic codegen's TryMarkNoThrow does, and
///    implied by -fno-exceptions. A declared noexcept lands in the same place,
///    so two declarations that disagree on noexcept are not told apart here.
///  - frame-pointer, ssp, sspstrong and sspreq: -f[no-]omit-frame-pointer
///    and -fstack-protector*.
///  - null-pointer-is-valid, and what it changes: under
///    -fno-delete-null-pointer-checks a reference is dereferenceable_or_null
///    rather than nonnull and dereferenceable, and a load is not nonnull.
///    Whether a pointer may be null is compared as neither.
static bool isCompileOptionAttribute(StringRef name) {
  return name == "nothrow" || name == "frame-pointer" || name == "ssp" ||
         name == "sspstrong" || name == "sspreq" ||
         name == "null-pointer-is-valid";
}

/// `attr` without what isCompileOptionAttribute describes, wherever it
/// appears: a function's extra attributes, and the nullness attributes of an
/// op, a parameter, a result or a call's arguments.
static mlir::Attribute withoutCompileOptions(mlir::Attribute attr) {
  if (auto extra = dyn_cast<cir::ExtraFuncAttributesAttr>(attr)) {
    SmallVector<NamedAttribute> kept;
    for (NamedAttribute entry : extra.getElements())
      if (!isCompileOptionAttribute(entry.getName().getValue()))
        kept.push_back(entry);
    return cir::ExtraFuncAttributesAttr::get(
        DictionaryAttr::get(attr.getContext(), kept));
  }
  if (auto array = dyn_cast<ArrayAttr>(attr)) {
    SmallVector<mlir::Attribute> elements;
    for (mlir::Attribute element : array)
      elements.push_back(withoutCompileOptions(element));
    return ArrayAttr::get(attr.getContext(), elements);
  }
  auto dict = dyn_cast<DictionaryAttr>(attr);
  if (!dict)
    return attr;
  NamedAttrList kept;
  for (NamedAttribute entry : dict) {
    StringRef name = entry.getName().getValue();
    if (name == "cir.nonnull" || name == "cir.callsite_nonnull")
      continue;
    if (name == "cir.dereferenceable")
      name = "cir.dereferenceable_or_null";
    kept.set(name, withoutCompileOptions(entry.getValue()));
  }
  return kept.getDictionary(attr.getContext());
}

/// Whether `path`, where diffElements found two attributes to differ, is
/// one of the places withoutCompileOptions removes: what is reported is the
/// attributes as they are, less what was not compared.
static bool isCompileOptionPath(StringRef path) {
  if (path.consume_front("extra_attrs."))
    return isCompileOptionAttribute(path);
  return path == "nothrow" || path == "personality" ||
         path.ends_with("cir.nonnull") ||
         path.ends_with("cir.callsite_nonnull");
}

/// `op`'s attributes as a definition compares them; see
/// isCompileOptionAttribute.
///
/// A function's personality is set aside too: it is there because the
/// function has calls that may throw, which is the same per-TU inference; see
/// withoutExceptionShape.
static DictionaryAttr withoutCompileOptions(Operation *op) {
  DictionaryAttr attrs = op->getAttrDictionary();
  StringRef dropped = isa<cir::CIRCallOpInterface>(op) ? "nothrow"
                      : isa<cir::FuncOp>(op)           ? "personality"
                                                       : "";
  if (!dropped.empty() && attrs.contains(dropped))
    attrs = DictionaryAttr::get(
        op->getContext(), llvm::to_vector(llvm::make_filter_range(
                              attrs.getValue(), [&](NamedAttribute entry) {
                                return entry.getName() != dropped;
                              })));
  return cast<DictionaryAttr>(withoutCompileOptions(mlir::Attribute(attrs)));
}

/// Whether `op` has anything withoutExceptionShape removes.
static bool hasExceptionShape(Operation *op) {
  return op
      ->walk([](Operation *nested) {
        auto call = dyn_cast<cir::CallOp>(nested);
        auto tryOp = dyn_cast<cir::TryOp>(nested);
        if ((call && call.getException()) || (tryOp && tryOp.getSynthetic()))
          return mlir::WalkResult::interrupt();
        return mlir::WalkResult::advance();
      })
      .wasInterrupted();
}

/// Rewrites `op`, a copy made to be compared, without the exception
/// handling CIRGen adds because a call may throw. Whether a call may throw is
/// decided per TU, from the callees whose definitions it has already emitted:
/// one TU calls a function the other wraps in a synthetic try that
/// terminates, with the result carried out through a "tmp.try.call.res"
/// slot. So:
///
///  - a synthetic try whose body falls through is replaced by its body; a try
///    the source wrote stays, and so does one whose body returns, breaks or
///    continues, which the code after it would otherwise appear to follow;
///  - a call with an unwind edge becomes a plain call, without its cleanups;
///  - a "tmp.try.call.res" slot written once, before it is read in the same
///    block, is replaced by the value written to it.
static void withoutExceptionShape(Operation *op) {
  // Innermost first, so a body moved out never holds a try still to visit.
  // A try has no results, so once its body is moved out nothing refers to it.
  SmallVector<cir::TryOp> tries;
  op->walk([&](cir::TryOp tryOp) {
    Region &region = tryOp.getTryRegion();
    if (tryOp.getSynthetic() && region.hasOneBlock() &&
        region.front().mightHaveTerminator() &&
        isa<cir::YieldOp>(region.front().getTerminator()))
      tries.push_back(tryOp);
  });
  for (cir::TryOp tryOp : tries) {
    Block &body = tryOp.getTryRegion().front();
    Block *parent = tryOp->getBlock();
    parent->getOperations().splice(Block::iterator(tryOp), body.getOperations(),
                                   body.begin(), std::prev(body.end()));
    tryOp->erase();
  }
  op->walk([](cir::CallOp call) {
    if (!call.getException())
      return;
    call.setException(false);
    call.getCleanup().getBlocks().clear();
  });
  SmallVector<cir::AllocaOp> slots;
  op->walk([&](cir::AllocaOp alloca) {
    if (alloca.getName() == "tmp.try.call.res")
      slots.push_back(alloca);
  });
  for (cir::AllocaOp slot : slots) {
    cir::StoreOp store;
    SmallVector<cir::LoadOp> loads;
    bool forwardable = true;
    for (Operation *user : slot->getUsers()) {
      auto asStore = dyn_cast<cir::StoreOp>(user);
      auto asLoad = dyn_cast<cir::LoadOp>(user);
      if (asStore && asStore.getAddr() == slot.getResult() && !store)
        store = asStore;
      else if (asLoad)
        loads.push_back(asLoad);
      else
        forwardable = false;
    }
    if (!forwardable || !store)
      continue;
    if (!llvm::all_of(loads, [&](cir::LoadOp load) {
          return load->getBlock() == store->getBlock() &&
                 store->isBeforeInBlock(load);
        }))
      continue;
    for (cir::LoadOp load : loads) {
      load.getResult().replaceAllUsesWith(store.getValue());
      load->erase();
    }
    store->erase();
    slot->erase();
  }
}

/// The attributes compared as attributes: not linkage, body, type or
/// location.
static bool isOtherDefinitionAttribute(StringRef name) {
  return !isLinkageAttribute(name) && !isBodyAttribute(name) &&
         !isTypeAttribute(name) && !isLocationAttribute(name);
}

/// `text` on one line and at most `limit` characters, for a message.
static std::string oneLine(std::string text, size_t limit) {
  for (char &c : text)
    if (c == '\n')
      c = ' ';
  if (text.size() > limit)
    text = text.substr(0, limit) + "...";
  return text;
}

template <typename T> static std::string printed(T value, size_t limit) {
  std::string text;
  llvm::raw_string_ostream os(text);
  os << value;
  return oneLine(std::move(text), limit);
}

/// An op as a difference shows it: its own line, without its regions, with
/// the value names of its function.
///
/// Numbered from the function, not the module: the names are the same, as
/// each function numbers its own, and the module is every input's code -- on
/// a large library, numbering it for each difference made recording the
/// differences take several times as long as the link.
static std::string describeOperation(Operation *op) {
  std::string text;
  llvm::raw_string_ostream os(text);
  op->print(os, mlir::OpPrintingFlags()
                    .skipRegions()
                    .assumeVerified()
                    .useLocalScope()
                    .elideLargeElementsAttrs(/*largeElementLimit=*/16));
  return oneLine(std::move(text), 200);
}

namespace {
using AttrOrType = std::variant<mlir::Attribute, mlir::Type>;

/// One innermost difference between two attributes or types: where it is
/// below the top -- ".key" for a dictionary entry, "[i]" for an array element,
/// nothing for the elements of anything else -- and what each side has there,
/// none where it has nothing.
struct ElementDifference {
  std::string path;
  std::optional<std::string> a, b;
  AttrOrType aElement, bElement;
};
} // namespace

/// `word` with its indefinite article, as English spells it.
static std::string withArticle(StringRef word) {
  bool vowelSound =
      !word.empty() &&
      (StringRef("aeiou").contains(word[0]) || word.starts_with("8") ||
       word.starts_with("11-") || word.starts_with("18-"));
  return (vowelSound ? "an " : "a ") + word.str();
}

static std::string counted(uint64_t n, StringRef noun) {
  return std::to_string(n) + " " + noun.str() + (n == 1 ? "" : "s");
}

/// A record's name as the source spells it: without the suffix a rename gave
/// it.
static StringRef sourceRecordName(cir::RecordType rec) {
  StringRef name = rec.getName() ? rec.getName().getValue() : StringRef();
  size_t dot = name.rfind(".m");
  if (dot != StringRef::npos && dot + 2 < name.size() &&
      llvm::all_of(name.drop_front(dot + 2), llvm::isDigit))
    return name.take_front(dot);
  return name;
}

/// An attribute or type in source terms, where it has an obvious one: a
/// symbol by its demangled name, a constant by its value, an integer type by
/// its width, an array by its length.
static std::optional<std::string> sourceDescription(const AttrOrType &element) {
  if (const auto *attr = std::get_if<mlir::Attribute>(&element)) {
    if (!*attr)
      return std::nullopt;
    if (auto ref = dyn_cast<mlir::FlatSymbolRefAttr>(*attr))
      return "'" + llvm::demangle(ref.getValue()) + "'";
    if (auto value = dyn_cast<cir::IntAttr>(*attr))
      return value.isSigned() ? std::to_string(value.getSInt())
                              : std::to_string(value.getUInt());
    if (auto value = dyn_cast<cir::BoolAttr>(*attr))
      return value.getValue() ? "true" : "false";
    if (auto value = dyn_cast<cir::FPAttr>(*attr)) {
      llvm::SmallString<16> text;
      value.getValue().toString(text);
      return std::string(text);
    }
    if (auto value = dyn_cast<cir::ConstPtrAttr>(*attr))
      if (value.isNullValue())
        return std::string("a null pointer");
    return std::nullopt;
  }
  mlir::Type type = std::get<mlir::Type>(element);
  if (!type)
    return std::nullopt;
  if (auto array = dyn_cast<cir::ArrayType>(type))
    return counted(array.getSize(), "element");
  if (auto integer = dyn_cast<cir::IntType>(type))
    return withArticle(std::to_string(integer.getWidth()) + "-bit " +
                       (integer.isSigned() ? "signed" : "unsigned") +
                       " integer");
  if (isa<cir::BoolType>(type))
    return std::string("a bool");
  if (isa<cir::SingleType>(type))
    return std::string("a float");
  if (isa<cir::DoubleType>(type))
    return std::string("a double");
  // A pointer, however deep, to a record a rename split: the record is the
  // difference, and the reader knows it by its source name.
  mlir::Type pointee = type;
  while (auto pointer = dyn_cast<cir::PointerType>(pointee))
    pointee = pointer.getPointee();
  if (auto rec = dyn_cast<cir::RecordType>(pointee))
    if (!sourceRecordName(rec).empty())
      return "'" + sourceRecordName(rec).str() + "'";
  if (isa<cir::PointerType>(type))
    return std::string("a pointer");
  return std::nullopt;
}

/// An attribute or type as a difference shows it; a symbol reference with its
/// demangled name after it, which is what the reader looks for in the source.
static std::optional<std::string> printedElement(const AttrOrType &element) {
  return std::visit(
      [](auto value) -> std::optional<std::string> {
        if (!value)
          return std::nullopt;
        std::string text = printed(value, 120);
        if constexpr (std::is_same_v<decltype(value), mlir::Attribute>)
          if (auto ref = dyn_cast<mlir::FlatSymbolRefAttr>(value)) {
            std::string demangled = llvm::demangle(ref.getValue());
            if (demangled != ref.getValue())
              text += " (" + demangled + ")";
          }
        return text;
      },
      element);
}

/// Collects the innermost places `a` and `b` differ into `out`, at most
/// eight: a dictionary by key, an array by index, anything else through its
/// immediate sub-elements -- attributes before types, since an initializer's
/// type differs whenever its contents do -- while both sides have the same
/// kind and number of them. Where that stops -- different kinds, a leaf, a
/// symbol reference, a difference in a parameter that is not an element --
/// the two sides are the difference. Bounded in depth: an identified record
/// reaches itself through a pointer.
static void diffElements(const AttrOrType &a, const AttrOrType &b,
                         const std::string &path,
                         SmallVectorImpl<ElementDifference> &out,
                         unsigned depth = 0) {
  if (a == b || out.size() >= 8)
    return;
  auto leaf = [&] {
    out.push_back({path, printedElement(a), printedElement(b), a, b});
  };
  auto typeID = [](const AttrOrType &element) {
    return std::visit([](auto value) { return value.getTypeID(); }, element);
  };
  bool aNull = std::visit([](auto value) { return !value; }, a);
  bool bNull = std::visit([](auto value) { return !value; }, b);
  if (aNull || bNull || a.index() != b.index() || typeID(a) != typeID(b) ||
      depth > 8)
    return leaf();

  if (const auto *aAttr = std::get_if<mlir::Attribute>(&a)) {
    mlir::Attribute bAttr = std::get<mlir::Attribute>(b);
    if (isa<mlir::SymbolRefAttr>(*aAttr))
      return leaf();
    if (auto aDict = dyn_cast<DictionaryAttr>(*aAttr)) {
      auto bDict = cast<DictionaryAttr>(bAttr);
      SmallVector<StringRef> names;
      for (DictionaryAttr dict : {aDict, bDict})
        for (NamedAttribute entry : dict)
          names.push_back(entry.getName().getValue());
      llvm::sort(names);
      names.erase(llvm::unique(names), names.end());
      for (StringRef name : names)
        diffElements(aDict.get(name), bDict.get(name), path + "." + name.str(),
                     out, depth + 1);
      return;
    }
    if (auto aArray = dyn_cast<ArrayAttr>(*aAttr)) {
      auto bArray = cast<ArrayAttr>(bAttr);
      size_t common = std::min(aArray.size(), bArray.size());
      for (size_t i = 0; i < common; ++i)
        diffElements(aArray[i], bArray[i], path + "[" + std::to_string(i) + "]",
                     out, depth + 1);
      if (aArray.size() != bArray.size() && out.size() < 8) {
        AttrOrType aExtra =
            common < aArray.size() ? aArray[common] : mlir::Attribute();
        AttrOrType bExtra =
            common < bArray.size() ? bArray[common] : mlir::Attribute();
        out.push_back({path + "[" + std::to_string(common) + "]",
                       printedElement(aExtra), printedElement(bExtra), aExtra,
                       bExtra});
      }
      return;
    }
  }

  // Records are not walked as sub-elements; two of one name, or two unnamed
  // ones, are compared member by member.
  if (const auto *aType = std::get_if<mlir::Type>(&a)) {
    auto aRec = dyn_cast<cir::RecordType>(*aType);
    auto bRec = dyn_cast<cir::RecordType>(std::get<mlir::Type>(b));
    if (aRec && bRec && aRec.getName() == bRec.getName() &&
        aRec.getMembers().size() == bRec.getMembers().size() &&
        !aRec.getMembers().empty()) {
      size_t before = out.size();
      for (auto [x, y] : llvm::zip_equal(aRec.getMembers(), bRec.getMembers()))
        diffElements(x, y, path, out, depth + 1);
      if (out.size() == before)
        leaf();
      return;
    }
  }

  auto subElements = [](const AttrOrType &element) {
    SmallVector<AttrOrType> attrs, types;
    std::visit(
        [&](auto value) {
          value.walkImmediateSubElements(
              [&](mlir::Attribute attr) { attrs.push_back(attr); },
              [&](mlir::Type type) { types.push_back(type); });
        },
        element);
    llvm::append_range(attrs, types);
    return attrs;
  };
  SmallVector<AttrOrType> aSubs = subElements(a), bSubs = subElements(b);
  if (aSubs.empty() || aSubs.size() != bSubs.size())
    return leaf();
  size_t before = out.size();
  for (auto [x, y] : llvm::zip_equal(aSubs, bSubs))
    diffElements(x, y, path, out, depth + 1);
  if (out.size() == before)
    leaf();
}

/// The first innermost difference of two attributes or types, as one side of
/// a Difference each: `what` gains where below the top it is; `sourceWhat`
/// is said instead in source terms.
static Difference firstElementDifference(StringRef what, StringRef sourceWhat,
                                         const AttrOrType &a,
                                         mlir::LocationAttr aLoc,
                                         const AttrOrType &b,
                                         mlir::LocationAttr bLoc) {
  SmallVector<ElementDifference> differences;
  diffElements(a, b, "", differences);
  if (differences.empty())
    return {what.str(),
            sourceWhat.str(),
            {aLoc, *printedElement(a)},
            {bLoc, *printedElement(b)}};
  ElementDifference &first = differences.front();
  std::string where = what.str();
  if (!first.path.empty())
    where += " at " + first.path;
  return {
      where,
      sourceWhat.str(),
      {aLoc, first.a.value_or("nothing"), sourceDescription(first.aElement)},
      {bLoc, first.b.value_or("nothing"), sourceDescription(first.bElement)}};
}

/// Whether `op` is a C++ vtable: its own words in source terms.
static bool isVTable(Operation *op) {
  auto name = op->getAttrOfType<StringAttr>(SymbolTable::getSymbolAttrName());
  return name && name.getValue().starts_with("_ZTV");
}

/// The linkage a definition carries, as CIR spells it; empty for an op that
/// carries none.
static std::string linkageName(Operation *op) {
  if (auto alias = dyn_cast<cir::AliasOp>(op))
    return cir::stringifyGlobalLinkageKind(aliasLinkage(alias)).str();
  if (auto linkage = op->getAttrOfType<cir::GlobalLinkageKindAttr>("linkage"))
    return cir::stringifyGlobalLinkageKind(linkage.getValue()).str();
  return std::string();
}

namespace {

/// Decides whether two definitions of one symbol are the same definition, and
/// gives a definition a digest that names it across runs.
///
/// The comparison runs on the two ops as they sit in the shared context, where
/// every type and attribute is uniqued, so the common case -- two identical
/// copies of a header-defined inline function -- is a walk of pointer
/// comparisons with nothing printed. Two things count as equal without being
/// identical:
///
///  - References to local symbols. Each input's `static` helpers are renamed
///    on the way in (`helper`, `helper.1`, ...), so two copies of one inline
///    function calling their own TU's helper name different symbols. Any
///    reference to a local symbol compares equal to any other.
///  - The linkage attributes; see isLinkageAttribute.
///
/// Locations are never compared. A record two inputs define differently has
/// been renamed in one of them by the time its uses are compared, so a copy
/// that uses the renamed record differs from one that uses the original: a
/// layout divergence shows up in every definition that touches the record.
class DefinitionComparer {
public:
  struct Result {
    bool sameType = true;
    bool sameAttributes = true;
    bool sameBody = true;
    /// What the copies disagree on, where they do: the first parameter or
    /// result of a function type that differs, the first op of the bodies
    /// that does not match, and every other attribute that differs. The first
    /// copy compared is `kept`, the second `dropped`.
    std::optional<Difference> type, body;
    SmallVector<AttributeDifference, 1> attributes;
  };

  DefinitionComparer(MLIRContext &context, const llvm::StringSet<> &localNames)
      : context(context), localNames(localNames),
        localPlaceholder(
            mlir::FlatSymbolRefAttr::get(&context, "cir-link.local")) {}

  /// Compares `a` and `b` as they are and, where their bodies differ and
  /// either has exception handling a TU may add or leave out, compares the
  /// bodies again without it; see withoutExceptionShape.
  Result compare(Operation *a, Operation *b) {
    Result result = compareAsIs(a, b);
    if (result.sameBody || (!hasExceptionShape(a) && !hasExceptionShape(b)))
      return result;
    mlir::IRMapping aMapping, bMapping;
    Operation *aCopy = a->clone(aMapping);
    Operation *bCopy = b->clone(bMapping);
    // A difference the copies still have is shown as the inputs have it.
    for (const mlir::IRMapping *mapping : {&aMapping, &bMapping})
      for (auto [original, copy] : mapping->getOperationMap())
        originals[copy] = original;
    auto eraseCopies = llvm::make_scope_exit([&] {
      originals.clear();
      aCopy->erase();
      bCopy->erase();
    });
    withoutExceptionShape(aCopy);
    withoutExceptionShape(bCopy);
    Result again = compareAsIs(aCopy, bCopy);
    result.sameBody = again.sameBody;
    result.body = std::move(again.body);
    return result;
  }

  /// Digest of `op` as a definition: what compare() looks at, normalized the
  /// same way, so two copies compare() calls equal get the same digest.
  /// Printing makes it slow; it is only asked for copies worth a full record.
  std::string digest(Operation *op) {
    normalizer.reset();
    Operation *copy = op->clone();
    auto eraseCopy = llvm::make_scope_exit([&] { copy->erase(); });
    withoutExceptionShape(copy);
    if (copy->getAttr("linkage"))
      copy->setAttr("linkage",
                    cir::GlobalLinkageKindAttr::get(
                        &context, cir::GlobalLinkageKind::ExternalLinkage));
    if (copy->getAttr("global_visibility"))
      copy->setAttr("global_visibility", cir::VisibilityAttr::get(&context));
    for (StringRef name : {"sym_visibility", "comdat", "dso_local"})
      copy->removeAttr(name);
    copy->walk([](Operation *nested) {
      nested->setAttrs(withoutCompileOptions(nested));
      for (StringRef name : {"cir.asm_srcloc", "cir.call_srcloc", "cir.srcloc"})
        nested->removeAttr(name);
      if (auto alloca = dyn_cast<cir::AllocaOp>(nested))
        alloca.setName("");
    });
    getNormalizer().recursivelyReplaceElementsIn(copy);
    std::string text;
    llvm::raw_string_ostream os(text);
    copy->print(os,
                mlir::OpPrintingFlags().printGenericOpForm().useLocalScope());
    return bodyDigest(text);
  }

private:
  /// compare() without a second look: the two ops exactly as they are, less
  /// what is never compared.
  Result compareAsIs(Operation *a, Operation *b) {
    values.clear();
    blocks.clear();
    pending.clear();
    normalizer.reset();
    firstBodyDifference.reset();
    definition = a;
    if (a->getName() != b->getName() ||
        a->getNumRegions() != b->getNumRegions()) {
      Result result{false, false, false};
      result.body = Difference{"the operation", "the copies first differ",
                               site(a), site(b)};
      return result;
    }
    Result result;
    bool sameBodyAttributes = true;
    DictionaryAttr aAttrs = a->getAttrDictionary();
    DictionaryAttr bAttrs = b->getAttrDictionary();
    if (aAttrs != bAttrs) {
      DictionaryAttr aCompared = withoutCompileOptions(a);
      DictionaryAttr bCompared = withoutCompileOptions(b);
      result.sameType = attributesEqual(aCompared, bCompared, isTypeAttribute);
      result.sameAttributes =
          attributesEqual(aCompared, bCompared, isOtherDefinitionAttribute);
      sameBodyAttributes =
          attributesEqual(aCompared, bCompared, isBodyAttribute);
      if (!result.sameType)
        result.type = typeDifference(a, b);
      if (!result.sameAttributes)
        result.attributes = attributeDifferences(aAttrs, bAttrs);
      if (!sameBodyAttributes)
        result.body = firstElementDifference(
            "the initializer",
            isVTable(a) ? "the vtable differs" : "the initializer differs",
            aAttrs.get("initial_value"), a->getLoc(),
            bAttrs.get("initial_value"), b->getLoc());
    }
    result.sameBody = sameBodyAttributes &&
                      regionsEqual(a->getRegions(), b->getRegions()) &&
                      pendingResolved();
    if (!result.sameBody && !result.body)
      result.body = std::move(firstBodyDifference);
    return result;
  }

  DifferenceSite site(Operation *op) const {
    return {op->getLoc(), describeOperation(originals.lookup_or(op, op))};
  }

  /// Notes `a` and `b` as where the bodies first differ, unless an earlier
  /// pair is noted; returns false, for the caller to return.
  bool differ(DifferenceSite a, DifferenceSite b) {
    if (!firstBodyDifference)
      firstBodyDifference = Difference{"the body", "the copies first differ",
                                       std::move(a), std::move(b)};
    return false;
  }
  bool differ(Operation *a, Operation *b) {
    if (firstBodyDifference)
      return false;
    return differ(site(a), site(b));
  }

  /// What is at `it` in `block`, or, past its end, that the block ends.
  DifferenceSite leftover(Block &block, Block::iterator it) const {
    if (it != block.end())
      return site(&*it);
    Operation *last = block.empty() ? block.getParentOp() : &block.back();
    return {last->getLoc(), "the end of the block"};
  }

  /// The first parameter or result two function types disagree on, with the
  /// parameter's own location where the function has a body; or the type
  /// attribute that differs.
  std::optional<Difference> typeDifference(Operation *a, Operation *b) {
    auto fa = dyn_cast<cir::FuncOp>(a);
    auto fb = dyn_cast<cir::FuncOp>(b);
    if (fa && fb) {
      cir::FuncType ta = fa.getFunctionType();
      cir::FuncType tb = fb.getFunctionType();
      auto parameter = [](cir::FuncOp f, mlir::Type type, unsigned i) {
        mlir::Location loc = f.getLoc();
        if (!f.getBody().empty() && i < f.getBody().front().getNumArguments())
          loc = f.getBody().front().getArgument(i).getLoc();
        return DifferenceSite{loc, "'" + printed(type, 120) + "'",
                              sourceDescription(type)};
      };
      // A method's first parameter is its object, which the source does not
      // count: CIRGen gives it a local named "this".
      bool hasThis = llvm::any_of(
          fa.getBody().getOps<cir::AllocaOp>(),
          [](cir::AllocaOp alloca) { return alloca.getName() == "this"; });
      auto sourceParameter = [&](unsigned i) -> std::string {
        if (hasThis && i == 0)
          return "'this'";
        return "parameter " + std::to_string(hasThis ? i : i + 1);
      };
      unsigned common = std::min(ta.getNumInputs(), tb.getNumInputs());
      for (unsigned i = 0; i < common; ++i)
        if (ta.getInput(i) != tb.getInput(i))
          return Difference{"parameter " + std::to_string(i + 1),
                            "the type of " + sourceParameter(i) + " differs",
                            parameter(fa, ta.getInput(i), i),
                            parameter(fb, tb.getInput(i), i)};
      auto parameters = [&](cir::FuncType type) {
        unsigned n = type.getNumInputs() - (hasThis ? 1 : 0);
        return DifferenceSite{fa.getLoc(), std::to_string(type.getNumInputs()),
                              counted(n, "parameter")};
      };
      if (ta.getNumInputs() != tb.getNumInputs()) {
        DifferenceSite kept = parameters(ta), dropped = parameters(tb);
        dropped.loc = fb.getLoc();
        return Difference{"the number of parameters",
                          "the number of parameters differs", kept, dropped};
      }
      if (ta.getReturnType() != tb.getReturnType())
        return Difference{
            "the return type",
            "the return type differs",
            {fa.getLoc(), "'" + printed(ta.getReturnType(), 120) + "'",
             sourceDescription(ta.getReturnType())},
            {fb.getLoc(), "'" + printed(tb.getReturnType(), 120) + "'",
             sourceDescription(tb.getReturnType())}};
      if (ta.isVarArg() != tb.isVarArg()) {
        auto variadic = [](cir::FuncType type) -> std::string {
          return type.isVarArg() ? "variadic" : "not variadic";
        };
        return Difference{"whether the function is variadic",
                          "whether the function is variadic differs",
                          {fa.getLoc(), variadic(ta), variadic(ta)},
                          {fb.getLoc(), variadic(tb), variadic(tb)}};
      }
    }
    for (StringRef name : {"function_type", "sym_type"})
      if (!attributeEqual(a->getAttr(name), b->getAttr(name)))
        return firstElementDifference(
            "the type",
            isVTable(a) ? "the size of the vtable differs" : "the type differs",
            a->getAttr(name), a->getLoc(), b->getAttr(name), b->getLoc());
    return std::nullopt;
  }

  /// Every attribute isOtherDefinitionAttribute accepts that the copies
  /// disagree on, by name.
  SmallVector<AttributeDifference, 1> attributeDifferences(DictionaryAttr a,
                                                           DictionaryAttr b) {
    SmallVector<StringRef> names;
    for (DictionaryAttr dict : {a, b})
      for (NamedAttribute attr : dict)
        if (isOtherDefinitionAttribute(attr.getName().getValue()))
          names.push_back(attr.getName().getValue());
    llvm::sort(names);
    names.erase(llvm::unique(names), names.end());
    SmallVector<AttributeDifference, 1> out;
    for (StringRef name : names) {
      mlir::Attribute x = a.get(name), y = b.get(name);
      if (attributeEqual(x, y))
        continue;
      SmallVector<ElementDifference> differences;
      diffElements(x, y, name.str(), differences);
      // A dereferenceable on one side and a dereferenceable_or_null of the
      // same size on the other is a null-pointer option, not a difference.
      auto isNullnessSwap = [&](const ElementDifference &d) {
        StringRef path = d.path;
        StringRef other;
        if (path.consume_back("cir.dereferenceable_or_null"))
          other = "cir.dereferenceable";
        else if (path.consume_back("cir.dereferenceable"))
          other = "cir.dereferenceable_or_null";
        else
          return false;
        std::string otherPath = (path + other).str();
        return llvm::any_of(differences, [&](const ElementDifference &e) {
          return e.path == otherPath && e.a == d.b && e.b == d.a;
        });
      };
      for (ElementDifference &d : differences)
        if (!isCompileOptionPath(d.path) && !isNullnessSwap(d))
          out.push_back({d.path, d.a, d.b});
    }
    return out;
  }

  /// Rewrites every reference to a local symbol to one placeholder. Rebuilt
  /// for each comparison: the set of local names grows as inputs arrive, and
  /// the replacer caches what it has already rewritten.
  mlir::AttrTypeReplacer &getNormalizer() {
    if (!normalizer) {
      normalizer.emplace();
      normalizer->addReplacement(
          [this](mlir::FlatSymbolRefAttr ref) -> std::optional<mlir::Attribute> {
            if (localNames.contains(ref.getValue()))
              return localPlaceholder;
            return std::nullopt;
          });
    }
    return *normalizer;
  }

  bool attributeEqual(mlir::Attribute a, mlir::Attribute b) {
    if (a == b)
      return true;
    if (!a || !b)
      return false;
    return getNormalizer().replace(a) == getNormalizer().replace(b);
  }

  /// Compare the named attributes `include` accepts. Dictionaries are sorted
  /// by name, so the two filtered lists line up.
  template <typename IncludeFn>
  bool attributesEqual(DictionaryAttr a, DictionaryAttr b, IncludeFn include) {
    SmallVector<NamedAttribute> aKept, bKept;
    for (NamedAttribute attr : a)
      if (include(attr.getName().getValue()))
        aKept.push_back(attr);
    for (NamedAttribute attr : b)
      if (include(attr.getName().getValue()))
        bKept.push_back(attr);
    if (aKept.size() != bKept.size())
      return false;
    for (auto [x, y] : llvm::zip_equal(aKept, bKept))
      if (x.getName() != y.getName() ||
          !attributeEqual(x.getValue(), y.getValue()))
        return false;
    return true;
  }

  bool opsEqual(Operation *a, Operation *b) {
    if (a->getName() != b->getName() ||
        a->getNumOperands() != b->getNumOperands() ||
        a->getNumResults() != b->getNumResults() ||
        a->getNumSuccessors() != b->getNumSuccessors() ||
        a->getNumRegions() != b->getNumRegions() ||
        !llvm::equal(a->getResultTypes(), b->getResultTypes()))
      return differ(a, b);
    // Identical discardable attributes and properties are what identical
    // copies look like; only a difference is worth the normalized comparison.
    if (a->getRawDictionaryAttrs() != b->getRawDictionaryAttrs() ||
        !a->getName().compareOpProperties(a->getPropertiesStorage(),
                                          b->getPropertiesStorage()))
      if (!attributesEqual(withoutCompileOptions(a), withoutCompileOptions(b),
                           [&](StringRef name) {
                             return !isLocationAttribute(name) &&
                                    !isLocalName(a, name);
                           }))
        return differ(a, b);
    for (auto [x, y] : llvm::zip_equal(a->getOperands(), b->getOperands())) {
      // A use can come before its definition in block order; check those once
      // every definition has been paired.
      auto it = values.find(x);
      if (it == values.end())
        pending.push_back({x, y, a, b});
      else if (it->second != y)
        return differ(a, b);
    }
    for (auto [x, y] : llvm::zip_equal(a->getSuccessors(), b->getSuccessors()))
      if (blocks.lookup(x) != y)
        return differ(a, b);
    for (auto [x, y] : llvm::zip_equal(a->getResults(), b->getResults()))
      values[x] = y;
    return regionsEqual(a->getRegions(), b->getRegions());
  }

  bool regionsEqual(MutableArrayRef<Region> a, MutableArrayRef<Region> b) {
    for (auto [x, y] : llvm::zip_equal(a, b)) {
      auto blockCount = [](Region &region) -> DifferenceSite {
        return {region.getParentOp()->getLoc(),
                counted(std::distance(region.begin(), region.end()), "block")};
      };
      if (std::distance(x.begin(), x.end()) !=
          std::distance(y.begin(), y.end()))
        return differ(blockCount(x), blockCount(y));
      // Blocks are paired first, so that a branch to a later block finds it.
      for (auto [bx, by] : llvm::zip_equal(x, y))
        blocks[&bx] = &by;
      for (auto [bx, by] : llvm::zip_equal(x, y)) {
        // The definition's own parameters are its type, reported as such.
        bool parameters = bx.isEntryBlock() && bx.getParentOp() == definition;
        if (bx.getNumArguments() != by.getNumArguments()) {
          if (parameters)
            return false;
          return differ({bx.getParentOp()->getLoc(),
                         counted(bx.getNumArguments(), "block argument")},
                        {by.getParentOp()->getLoc(),
                         counted(by.getNumArguments(), "block argument")});
        }
        for (auto [ax, ay] :
             llvm::zip_equal(bx.getArguments(), by.getArguments())) {
          if (ax.getType() != ay.getType() && parameters)
            return false;
          if (ax.getType() != ay.getType())
            return differ({ax.getLoc(), "a block argument of type '" +
                                            printed(ax.getType(), 120) + "'"},
                          {ay.getLoc(), "a block argument of type '" +
                                            printed(ay.getType(), 120) + "'"});
          values[ax] = ay;
        }
        auto ox = bx.begin(), oy = by.begin();
        for (; ox != bx.end() && oy != by.end(); ++ox, ++oy)
          if (!opsEqual(&*ox, &*oy))
            return false;
        if (ox != bx.end() || oy != by.end())
          return firstBodyDifference
                     ? false
                     : differ(leftover(bx, ox), leftover(by, oy));
      }
    }
    return true;
  }

  bool pendingResolved() {
    for (const PendingUse &use : pending) {
      auto it = values.find(use.a);
      if (it == values.end() || it->second != use.b)
        return differ(use.userA, use.userB);
    }
    return true;
  }

  MLIRContext &context;
  const llvm::StringSet<> &localNames;
  mlir::FlatSymbolRefAttr localPlaceholder;
  std::optional<mlir::AttrTypeReplacer> normalizer;
  llvm::DenseMap<mlir::Value, mlir::Value> values;
  llvm::DenseMap<Block *, Block *> blocks;
  /// A use met before its definition, and the ops that use it.
  struct PendingUse {
    mlir::Value a, b;
    Operation *userA, *userB;
  };
  SmallVector<PendingUse> pending;
  std::optional<Difference> firstBodyDifference;
  /// The first copy compared: its entry block's arguments are its parameters.
  Operation *definition = nullptr;
  /// While compare() looks at copies without their exception shape: the op
  /// in the input each copied op came from.
  llvm::DenseMap<Operation *, Operation *> originals;
};

} // namespace

/// Canonical digest of record bodies, without materializing their expansions.
///
/// Comparing bodies by print expands nested records inline with no aliasing:
/// one `clang::Sema` is tens of megabytes in text and tens of *gigabytes*
/// bare, so a single small translation unit costs 70 GB to merge. Hashing
/// each record once -- nested named records contributing their (name, digest)
/// rather than their bodies -- compares the same thing for a linear cost: two
/// records share a digest exactly when their bare prints would be equal.
///
/// The print comparison this replaces is still the rule for what must compare
/// unequal: it expands nested records inline, so `struct Outer { S s; }` has a
/// different signature in two inputs exactly when their `S` differs -- which
/// is what has to happen, since an Outer built from a different S is a
/// different Outer and renaming S without renaming Outer would leave two
/// incompatible layouts sharing a name. Digests preserve that: Outer's digest
/// covers S's digest.
///
/// Recursion is handled per strongly connected component of the
/// record-reference graph: each member's digest covers the whole component --
/// all members' bodies, with within-component references as bare names --
/// together with the member's own body, which tells the members apart.
/// Cutting on the traversal path instead (tokens for whichever ancestor the
/// walk happens to be inside) hides nested bodies from enclosing digests --
/// the container would unify while its member renames -- and makes digests
/// depend on discovery order. Components are path-independent, so digests
/// are too, and every member shares its component's fate: renamed together
/// or unified together, exactly as the prints they replace did.
///
/// One cache per input, in one mode: unify mode normalizes anonymous per-TU
/// numbers before hashing, the transfer check hashes the form as written.
/// The two must not share a cache, and neither outlives its input.
struct RecordDigestCache {
  explicit RecordDigestCache(bool normalize, ArrayRef<cir::RecordType> seeds)
      : normalize(normalize) {
    buildGraph(seeds);
    findSCCs();
    findCyclic();
    precompute();
  }

  /// Hex digest of a record: what the maps compare. Precomputed for every
  /// record in the input; this is a lookup, and the memo is frozen after
  /// precompute, so the reference is valid for the cache's lifetime.
  const std::string &digestRecord(cir::RecordType rec) {
    auto it = memo.find(rec);
    if (LLVM_UNLIKELY(it == memo.end()))
      llvm::reportFatalInternalError("digest of undiscovered record");
    return it->second;
  }

  /// The canonical form itself, for the divergence diagnostic.
  std::string diagnosticString(cir::RecordType rec) {
    currentRoot = rec;
    return canonicalRecordString(rec);
  }

  /// Whether `rec` can lie on a reference cycle: a member of a
  /// non-trivial component, or a self-edge. The tolerance drill scopes its
  /// visiting set to these: any other record appears at most once on a drill
  /// path, so every visiting test on it misses and skipping the test changes
  /// nothing. Sound while the input's records are unchanged since this cache
  /// was built -- no completions, no rewrites -- which all drill sites
  /// satisfy: unification drills run pre-rewrite, and the transfer check
  /// reads the input's own post-rewrite context, which nothing mutates after
  /// the rewrite. Conservative (true) for undiscovered records.
  bool isCycleRelevant(mlir::Type rec) const {
    if (!nodesSet.count(rec))
      return true;
    return cyclic.count(rec);
  }

  /// Hex digest of a record's layout: member structure with nested named
  /// records as bare names, never bodies. Two records share a layout exactly
  /// when their member lists agree on everything except nested named bodies
  /// -- scalars, wrappers, kind, packing, arity, and the nested names
  /// themselves. The completeness-tolerant comparison gates on it: a layout
  /// difference is directly visible at this level, never hidden behind a
  /// cycle edge, so it splits absolutely, while a layout match confines the
  /// digest difference to nested named bodies, which the drill then
  /// attributes to completeness or splits on. Memoized per record; the walk
  /// only descends through unnamed records, which cannot cycle, so unlike
  /// the body digest this needs no component machinery and no precompute.
  const std::string &layoutRecord(cir::RecordType rec) {
    auto it = layoutMemo.find(rec);
    if (it != layoutMemo.end())
      return it->second;
    it = layoutMemo.try_emplace(rec, bodyDigest(layoutRecordString(rec))).first;
    return it->second;
  }

  // Unmemoized layout form, for --tolerance-drill-trace only.
  std::string layoutFormForTrace(cir::RecordType rec) {
    return layoutRecordString(rec);
  }

private:
  bool normalize;
  /// Type -> hex digest. Every named record hashed once per input, in
  /// component-topological order so nested digests are final before use;
  /// this memo is what makes the cost linear.
  llvm::DenseMap<mlir::Type, std::string> memo;
  /// Type -> hex layout digest. Filled lazily, on first shape build or
  /// first drill probe; most records of most inputs are never laid out.
  llvm::DenseMap<mlir::Type, std::string> layoutMemo;
  /// Records in the input, in discovery order, and their reference graph.
  std::vector<mlir::Type> nodes;
  llvm::DenseSet<mlir::Type> nodesSet;
  llvm::DenseMap<mlir::Type, std::vector<mlir::Type>> graph;
  /// Record -> component id, and records in reverse-topological component
  /// order (leaves first): the precompute order.
  llvm::DenseMap<mlir::Type, unsigned> sccId;
  std::vector<mlir::Type> topoOrder;
  /// Records that can lie on a reference cycle (see isCycleRelevant).
  llvm::DenseSet<mlir::Type> cyclic;
  /// The record whose canonical form is being built; references to its own
  /// component cut to bare names. Set once per top-level build, never
  /// recursed through.
  cir::RecordType currentRoot;
  /// Set only while refining a component, so that nothing else -- the
  /// divergence diagnostic included -- sees the refinement's extra tokens.
  bool keyAnonymousRefs = false;

  /// The canonical form of one record, with nested types referenced through
  /// emitTypeRef. Reads currentRoot for component-relative bare names.
  std::string canonicalRecordString(cir::RecordType rec) {
    std::string out;
    llvm::raw_string_ostream os(out);
    StringRef name = rec.getName() ? rec.getName().getValue() : StringRef();
    os << "record(" << static_cast<int>(rec.getKind()) << " \"" << name << "\"";
    if (rec.isIncomplete()) {
      os << " incomplete)";
      return out;
    }
    os << " packed=" << rec.getPacked() << " padded=" << rec.getPadded();
    for (mlir::Type member : rec.getMembers()) {
      os << " ";
      emitTypeRef(os, member);
    }
    os << ")";
    return out;
  }

  /// A nested type reference: a named record contributes its name as well as
  /// its digest, exactly as the print it replaces does. The name is
  /// load-bearing -- two inputs can nest same-bodied records under different
  /// names, and unifying those would merge different layouts. Within the
  /// current record's component the name stands alone: those bodies are
  /// covered by the component digest every member's key includes.
  ///
  /// An anonymous name does not stand alone -- normalization spells every one
  /// of them "anon" -- so while a component is being refined its members'
  /// references to each other carry the referent's key instead.
  void emitTypeRef(llvm::raw_string_ostream &os, mlir::Type t) {
    if (auto rec = mlir::dyn_cast<cir::RecordType>(t)) {
      if (rec.getName()) {
        StringRef name = rec.getName().getValue();
        os << "\"" << name << "\" ";
        auto a = sccId.find(currentRoot), b = sccId.find(rec);
        if (a != sccId.end() && b != sccId.end() && a->second == b->second) {
          if (keyAnonymousRefs && isAnonRecordName(name))
            os << "anon-key(" << digestRecord(rec) << ") ";
          return;
        }
      }
    }
    emitNestedDigest(os, t);
  }

  /// A nested type's hex digest, streamed. Within-component references never
  /// reach here, the emitter above cuts them to bare names.
  void emitNestedDigest(llvm::raw_string_ostream &os, mlir::Type t) {
    if (auto rec = mlir::dyn_cast<cir::RecordType>(t)) {
      if (!rec.getName()) {
        // Unnamed records cannot recurse; hash structurally. Not memoized:
        // nested references are relative to the enclosing root.
        os << digestOf(canonicalRecordString(rec));
        return;
      }
      os << digestRecord(rec);
      return;
    }
    // Scalars are recomputed per use: their nested references are relative to
    // the enclosing root. Each recompute is one shallow walk over memoized
    // pieces.
    os << digestOf(canonicalScalarString(t));
  }

  /// Digest of one nested canonical form, with this translation unit's
  /// anonymous record numbers taken out first in unify mode.
  ///
  /// Every nested digest is taken here. A form hashed without the
  /// normalization leaves an `anon.<n>` in the hex, and hex is opaque to every
  /// normalizer upstream of it, so the number becomes type identity for
  /// whatever encloses it: `struct Boxed { union { ... } slots[2]; }` unified
  /// or not depending on which number this translation unit's counter
  /// happened to reach.
  std::string digestOf(StringRef canonical) {
    return bodyDigest(normalize ? normalizeAnonNames(canonical) : canonical);
  }

  /// Canonical form of a non-record type. Records never reach here; every
  /// composite kind recurses through emitTypeRef so nested records contribute
  /// references, never expansions.
  std::string canonicalScalarString(mlir::Type t) {
    std::string out;
    llvm::raw_string_ostream os(out);
    if (auto ptr = mlir::dyn_cast<cir::PointerType>(t)) {
      os << "ptr(";
      emitTypeRef(os, ptr.getPointee());
      os << " ";
      if (auto space = ptr.getAddrSpace())
        os << space;
      else
        os << "default-space";
      os << ")";
    } else if (auto arr = mlir::dyn_cast<cir::ArrayType>(t)) {
      os << "array(" << arr.getSize() << " ";
      emitTypeRef(os, arr.getElementType());
      os << ")";
    } else if (auto vec = mlir::dyn_cast<cir::VectorType>(t)) {
      os << "vector(" << vec.getSize() << " ";
      emitTypeRef(os, vec.getElementType());
      os << " scalable=" << vec.getIsScalable()
         << " sve-fixed=" << vec.getIsSveFixedLength() << ")";
    } else if (auto cplx = mlir::dyn_cast<cir::ComplexType>(t)) {
      os << "complex(";
      emitTypeRef(os, cplx.getElementType());
      os << ")";
    } else if (auto dm = mlir::dyn_cast<cir::DataMemberType>(t)) {
      os << "data-member(";
      emitTypeRef(os, dm.getMemberTy());
      os << " ";
      emitTypeRef(os, dm.getClassTy());
      os << ")";
    } else if (auto m = mlir::dyn_cast<cir::MethodType>(t)) {
      os << "method(";
      emitTypeRef(os, m.getMemberFuncTy());
      os << " ";
      emitTypeRef(os, m.getClassTy());
      os << ")";
    } else if (auto fn = mlir::dyn_cast<cir::FuncType>(t)) {
      os << "func(";
      for (mlir::Type in : fn.getInputs()) {
        emitTypeRef(os, in);
        os << " ";
      }
      os << "-> ";
      if (mlir::Type ret = fn.getOptionalReturnType())
        emitTypeRef(os, ret);
      else
        os << "void";
      os << " vararg=" << fn.getVarArg() << ")";
    } else {
      // A leaf: ints, floats, bool, void, vptr. Small by nature; the guard
      // below is for a future composite kind reaching here, which would
      // reintroduce the expansion this cache exists to avoid. Loud, not
      // silent: falling back to a huge print would be the old bug again.
      std::string leaf;
      llvm::raw_string_ostream los(leaf);
      los << t;
      los.flush();
      if (leaf.size() > 65536)
        llvm::reportFatalInternalError(
            "record digest hit an unhandled large type: " +
            StringRef(leaf).substr(0, 200) + "...");
      os << leaf;
    }
    return out;
  }

  /// The layout form of one record, for the layout digest above: kind,
  /// packing, member count, and per member the scalar structure with nested
  /// named records as bare names. Anonymous names spell "anon" -- the
  /// per-TU number is not type identity -- and anonymous bodies are
  /// excluded, left to the member loop's structural drill. Unnamed records
  /// recurse inline; they cannot cycle, so this always terminates.
  std::string layoutRecordString(cir::RecordType rec) {
    std::string out;
    llvm::raw_string_ostream os(out);
    StringRef name = rec.getName() ? rec.getName().getValue() : StringRef();
    // The record's own name identifies it only when genuinely named: a
    // per-TU anonymous number is not identity, and the drill compares
    // anonymous bodies structurally (see collectMemberRecords), so the
    // layout gate must not see the numbering -- or every anonymous pair
    // splits here before the member loop can excuse incompleteness.
    if (isAnonRecordName(name))
      name = "anon";
    os << "record(" << static_cast<int>(rec.getKind()) << " \"" << name << "\"";
    if (rec.isIncomplete()) {
      os << " incomplete)";
      return out;
    }
    os << " packed=" << rec.getPacked() << " padded=" << rec.getPadded();
    for (mlir::Type member : rec.getMembers()) {
      os << " ";
      emitLayoutRef(os, member);
    }
    os << ")";
    return out;
  }

  /// A nested type reference in a layout form: a named record contributes
  /// its name alone, never its body, which is what makes the layout blind
  /// to nested completeness and sensitive to everything else.
  void emitLayoutRef(llvm::raw_string_ostream &os, mlir::Type t) {
    if (auto rec = mlir::dyn_cast<cir::RecordType>(t)) {
      // The same test collectMemberRecords makes, so the layout and the
      // member shapes agree on which records are referenced by name.
      if (rec.getName() && !rec.getName().getValue().empty()) {
        StringRef name = rec.getName().getValue();
        os << "\"" << (isAnonRecordName(name) ? "anon" : name) << "\" ";
        return;
      }
      os << layoutRecord(rec);
      return;
    }
    os << bodyDigest(layoutScalarString(t));
  }

  /// Layout form of a non-record type. Mirrors canonicalScalarString
  /// structurally -- same composite kinds, same order -- with references
  /// through emitLayoutRef so nested records contribute names, never bodies.
  std::string layoutScalarString(mlir::Type t) {
    std::string out;
    llvm::raw_string_ostream os(out);
    if (auto ptr = mlir::dyn_cast<cir::PointerType>(t)) {
      os << "ptr(";
      emitLayoutRef(os, ptr.getPointee());
      os << " ";
      if (auto space = ptr.getAddrSpace())
        os << space;
      else
        os << "default-space";
      os << ")";
    } else if (auto arr = mlir::dyn_cast<cir::ArrayType>(t)) {
      os << "array(" << arr.getSize() << " ";
      emitLayoutRef(os, arr.getElementType());
      os << ")";
    } else if (auto vec = mlir::dyn_cast<cir::VectorType>(t)) {
      os << "vector(" << vec.getSize() << " ";
      emitLayoutRef(os, vec.getElementType());
      os << " scalable=" << vec.getIsScalable()
         << " sve-fixed=" << vec.getIsSveFixedLength() << ")";
    } else if (auto cplx = mlir::dyn_cast<cir::ComplexType>(t)) {
      os << "complex(";
      emitLayoutRef(os, cplx.getElementType());
      os << ")";
    } else if (auto dm = mlir::dyn_cast<cir::DataMemberType>(t)) {
      os << "data-member(";
      emitLayoutRef(os, dm.getMemberTy());
      os << " ";
      emitLayoutRef(os, dm.getClassTy());
      os << ")";
    } else if (auto m = mlir::dyn_cast<cir::MethodType>(t)) {
      os << "method(";
      emitLayoutRef(os, m.getMemberFuncTy());
      os << " ";
      emitLayoutRef(os, m.getClassTy());
      os << ")";
    } else if (auto fn = mlir::dyn_cast<cir::FuncType>(t)) {
      os << "func(";
      for (mlir::Type in : fn.getInputs()) {
        emitLayoutRef(os, in);
        os << " ";
      }
      os << "-> ";
      if (mlir::Type ret = fn.getOptionalReturnType())
        emitLayoutRef(os, ret);
      else
        os << "void";
      os << " vararg=" << fn.getVarArg() << ")";
    } else {
      // A leaf: same guard as the canonical walk -- an unhandled composite
      // kind reaching here would hash an expansion instead of a form.
      std::string leaf;
      llvm::raw_string_ostream los(leaf);
      los << t;
      los.flush();
      if (leaf.size() > 65536)
        llvm::reportFatalInternalError(
            "record layout hit an unhandled large type: " +
            StringRef(leaf).substr(0, 200) + "...");
      os << leaf;
    }
    return out;
  }

  /// Reference graph over the input's named records: an edge R -> S for each
  /// named S reachable from R's members through composites and unnamed
  /// records, which are transparent. Named records are leaves of the walk.
  void buildGraph(ArrayRef<cir::RecordType> seeds) {
    std::vector<mlir::Type> worklist(seeds.begin(), seeds.end());
    while (!worklist.empty()) {
      mlir::Type node = worklist.back();
      worklist.pop_back();
      if (!nodesSet.insert(node).second)
        continue;
      nodes.push_back(node);
      auto root = mlir::cast<cir::RecordType>(node);
      std::vector<mlir::Type> neighbors;
      if (!root.isIncomplete())
        for (mlir::Type member : root.getMembers())
          collectRefs(member, neighbors, 0);
      llvm::sort(neighbors, [](mlir::Type a, mlir::Type b) {
        return a.getAsOpaquePointer() < b.getAsOpaquePointer();
      });
      neighbors.erase(std::unique(neighbors.begin(), neighbors.end()),
                      neighbors.end());
      graph[node] = neighbors;
      for (mlir::Type target : neighbors)
        if (!nodesSet.count(target))
          worklist.push_back(target);
    }
  }

  /// Named records visible through one type without passing through another
  /// named record. The transparent fragment (composites, unnamed records) is
  /// acyclic, so this terminates; the depth cap turns a violated invariant
  /// into a loud abort instead of a stack overflow.
  void collectRefs(mlir::Type t, std::vector<mlir::Type> &out, int depth) {
    if (depth > 10000)
      llvm::reportFatalInternalError("record digest hit pathological nesting");
    if (auto rec = mlir::dyn_cast<cir::RecordType>(t)) {
      if (rec.getName()) {
        out.push_back(rec);
        return;
      }
      if (!rec.isIncomplete())
        for (mlir::Type member : rec.getMembers())
          collectRefs(member, out, depth + 1);
      return;
    }
    if (auto ptr = mlir::dyn_cast<cir::PointerType>(t)) {
      collectRefs(ptr.getPointee(), out, depth + 1);
    } else if (auto arr = mlir::dyn_cast<cir::ArrayType>(t)) {
      collectRefs(arr.getElementType(), out, depth + 1);
    } else if (auto vec = mlir::dyn_cast<cir::VectorType>(t)) {
      collectRefs(vec.getElementType(), out, depth + 1);
    } else if (auto cplx = mlir::dyn_cast<cir::ComplexType>(t)) {
      collectRefs(cplx.getElementType(), out, depth + 1);
    } else if (auto dm = mlir::dyn_cast<cir::DataMemberType>(t)) {
      collectRefs(dm.getMemberTy(), out, depth + 1);
      collectRefs(dm.getClassTy(), out, depth + 1);
    } else if (auto m = mlir::dyn_cast<cir::MethodType>(t)) {
      collectRefs(m.getMemberFuncTy(), out, depth + 1);
      collectRefs(m.getClassTy(), out, depth + 1);
    } else if (auto fn = mlir::dyn_cast<cir::FuncType>(t)) {
      for (mlir::Type in : fn.getInputs())
        collectRefs(in, out, depth + 1);
      if (mlir::Type ret = fn.getOptionalReturnType())
        collectRefs(ret, out, depth + 1);
    }
    // Anything else is a leaf: no record references.
  }

  /// Iterative Tarjan; components complete in reverse-topological order,
  /// which is the precompute order (nested digests final before their users).
  void findSCCs() {
    llvm::DenseMap<mlir::Type, unsigned> index, lowlink;
    llvm::DenseSet<mlir::Type> onStack;
    std::vector<mlir::Type> stack;
    unsigned nextIndex = 0, nextScc = 0;
    struct Frame {
      mlir::Type node;
      size_t nextChild = 0;
    };
    for (mlir::Type root : nodes) {
      if (index.count(root))
        continue;
      std::vector<Frame> call;
      call.reserve(nodes.size());
      call.push_back({root, 0});
      index[root] = lowlink[root] = nextIndex++;
      stack.push_back(root);
      onStack.insert(root);
      while (!call.empty()) {
        // Fresh lookup every iteration: index[] below may rehash.
        auto neighbors = graph.find(call.back().node);
        size_t count = neighbors == graph.end() ? 0 : neighbors->second.size();
        if (call.back().nextChild < count) {
          mlir::Type child = neighbors->second[call.back().nextChild++];
          if (!index.count(child)) {
            index[child] = lowlink[child] = nextIndex++;
            stack.push_back(child);
            onStack.insert(child);
            call.push_back({child, 0});
          } else if (onStack.count(child)) {
            mlir::Type node = call.back().node;
            lowlink[node] = std::min(lowlink[node], index[child]);
          }
        } else {
          mlir::Type done = call.back().node;
          call.pop_back();
          if (!call.empty()) {
            mlir::Type parent = call.back().node;
            lowlink[parent] = std::min(lowlink[parent], lowlink[done]);
          }
          if (lowlink[done] == index[done]) {
            unsigned id = nextScc++;
            while (true) {
              mlir::Type member = stack.back();
              stack.pop_back();
              onStack.erase(member);
              sccId[member] = id;
              topoOrder.push_back(member);
              if (member == done)
                break;
            }
          }
        }
      }
    }
  }

  /// Mark the records that can lie on a reference cycle: members of
  /// non-trivial components, plus self-edges. Runs once per input, linear in
  /// the graph.
  void findCyclic() {
    llvm::DenseMap<unsigned, unsigned> sccSize;
    for (auto &[node, id] : sccId)
      ++sccSize[id];
    for (auto &[node, id] : sccId)
      if (sccSize[id] > 1)
        cyclic.insert(node);
    for (auto &[node, neighbors] : graph)
      for (mlir::Type target : neighbors)
        if (target == node)
          cyclic.insert(node);
  }

  /// Hash every component, leaves first, so every cross-component reference
  /// resolves to a final digest. Every member's key includes one digest over
  /// all the component's bodies: bodies that recursion could not order are
  /// compared together, or not at all.
  void precompute() {
    // topoOrder lists components consecutively, leaves first; group them.
    std::vector<mlir::Type> group;
    auto flush = [&]() {
      if (group.empty())
        return;
      // Each member's form: a header naming it, then its canonical record
      // string. The header names the member, and an anonymous per-TU number
      // is not identity: without normalizing it the two spellings of one
      // union hash differently and every record containing one looks
      // divergent.
      std::vector<std::pair<std::string, mlir::Type>> forms;
      forms.reserve(group.size());
      for (mlir::Type node : group) {
        auto rec = mlir::cast<cir::RecordType>(node);
        currentRoot = rec;
        StringRef memberName = rec.getName().getValue();
        std::string form =
            isAnonRecordName(memberName) ? "anon" : memberName.str();
        form += "\n";
        form += canonicalRecordString(rec);
        forms.emplace_back(normalize ? normalizeAnonNames(form) : form, node);
      }
      // Ordered by form, not by name. An anonymous member's name is a per-TU
      // counter, so sorting on it put the same component's members in a
      // different order in each TU, and the component hashed differently even
      // though its text, member by member, was the same.
      llvm::stable_sort(forms, [](const auto &a, const auto &b) {
        return a.first < b.first;
      });
      std::string cluster;
      for (const auto &[form, node] : forms) {
        cluster += form;
        cluster += "\n";
      }
      std::string componentDigest = bodyDigest(cluster);
      // One key per member: the component's digest, which covers every body
      // the recursion could not order, plus the member's own form, which says
      // which member this is.
      //
      // Handing every member the bare component digest made the members
      // indistinguishable by it. For named records that was harmless -- the
      // name tells them apart wherever the digest is used -- but an anonymous
      // record is known by its digest alone. Two different anonymous unions in
      // one component, such as the storage of two std::optionals whose
      // payloads point back at the record holding them, were given one
      // canonical name, and the second silently took the first one's layout.
      // References from outside the component read the same digest, so a
      // record holding one of those unions hashed the same as a record holding
      // the other.
      for (const auto &[form, node] : forms)
        memo[node] = bodyDigest(componentDigest + "\n" + form);
      refineAnonymousSiblings(group, componentDigest);
      group.clear();
    };
    unsigned current = 0;
    bool first = true;
    for (mlir::Type node : topoOrder) {
      unsigned id = sccId[node];
      if (!first && id != current)
        flush();
      first = false;
      current = id;
      group.push_back(node);
    }
    flush();
  }

  /// Tell apart members that differ only in which anonymous sibling they hold.
  ///
  /// Two rules meet here, each right on its own. Within a component a
  /// reference cuts to a bare name, because the bodies behind it are already
  /// covered by the shared component digest. And an anonymous name is a per-TU
  /// counter, so normalization spells every one of them "anon". Together they
  /// leave a reference to an anonymous sibling carrying nothing at all:
  ///
  ///     struct H1 { union { char c; struct { Node *n; int x; } s; } u; };
  ///     struct H2 { union { char c; struct { int y; Node *m; } s; } u; };
  ///     struct Node { H1 h1; H2 h2; };
  ///
  /// is one component in which both unions read as `union "anon" <char>
  /// "anon"`. They were given one key, and `unifyRecordNames` -- which knows an
  /// anonymous record by its key alone -- gave them one name, so H1 came out
  /// holding H2's payload with nothing said.
  ///
  /// So re-key the component in rounds, each round's references to anonymous
  /// siblings carrying the key those siblings had in the round before, until a
  /// round stops splitting classes. Two properties hold it up:
  ///
  ///  - A key is a function of the component's shape and never of the
  ///    counters, so two inputs holding the same types still agree on it.
  ///  - `anon-key(` appears in no unrefined form, so deleting those tokens
  ///    recovers the form they were added to: distinct keys cannot become
  ///    equal, and a round that adds no class has reached the coarsest stable
  ///    partition. (Barring an MD5 collision, which would already have merged
  ///    two layouts before reaching this.)
  ///
  /// Rounds cost a re-hash of the whole component, and a component is strongly
  /// connected so there is no part of it to skip. Each committed round does
  /// separate records that would otherwise share a name, so the work is real
  /// where it happens; the gates below are what keep it from happening at all
  /// on components that were never ambiguous, which is nearly all of them.
  void refineAnonymousSiblings(ArrayRef<mlir::Type> group,
                               StringRef componentDigest) {
    auto isAnon = [](mlir::Type node) {
      return isAnonRecordName(
          mlir::cast<cir::RecordType>(node).getName().getValue());
    };
    auto holdsAnonSibling = [&](mlir::Type node) {
      auto neighbors = graph.find(node);
      if (neighbors == graph.end())
        return false;
      unsigned id = sccId.lookup(node);
      return llvm::any_of(neighbors->second, [&](mlir::Type target) {
        return isAnon(target) && sccId.lookup(target) == id;
      });
    };
    // With one anonymous member at most, or with none of them referenced from
    // inside the component, no round can emit a token that tells two members
    // apart, so every round would reproduce the keys already seeded.
    if (llvm::count_if(group, isAnon) < 2 ||
        llvm::none_of(group, holdsAnonSibling))
      return;

    keyAnonymousRefs = true;
    llvm::StringSet<> seeded;
    for (mlir::Type node : group)
      seeded.insert(memo.lookup(node));
    size_t distinct = seeded.size();
    // At most one round per member: every round but the last adds a class.
    for (size_t round = 0, rounds = group.size(); round != rounds; ++round) {
      llvm::DenseMap<mlir::Type, std::string> next;
      llvm::StringSet<> keys;
      for (mlir::Type node : group) {
        auto rec = mlir::cast<cir::RecordType>(node);
        currentRoot = rec;
        std::string form =
            isAnon(node) ? "anon" : rec.getName().getValue().str();
        form += "\n";
        form += canonicalRecordString(rec);
        std::string key = bodyDigest(
            componentDigest.str() + "\n" +
            (normalize ? normalizeAnonNames(form) : form));
        keys.insert(key);
        next[node] = std::move(key);
      }
      // Committed only once the whole round is built, so a round reads nothing
      // but the round before it and the order members are visited in cannot
      // change what comes out. Committed even when nothing split: the round
      // was computed either way, and a key that carries one more level of the
      // component is one more level another input has to match.
      for (mlir::Type node : group)
        memo[node] = next[node];
      if (keys.size() == distinct)
        break;
      distinct = keys.size();
    }
    keyAnonymousRefs = false;
  }
};

namespace {

/// What the type-unification pass did to one input, for reporting.
struct RenamePlan {
  /// old name -> new name, in the order names were assigned.
  SmallVector<std::pair<std::string, std::string>> renames;
  /// Anonymous records given their cross-input name, old -> new. Kept apart
  /// from renames: those count as disagreements between inputs, these are
  /// bookkeeping. Both feed the record ABI table rewrite below.
  SmallVector<std::pair<std::string, std::string>> anonRenames;
  /// Anonymous records given their cross-input name. Counted, not reported:
  /// this is bookkeeping, not a disagreement between inputs.
  unsigned anonCanonicalized = 0;
};

struct RecordShape;

/// One named record nested in a member, as the shaping input holds it: the
/// name as written -- post-rename canonicals and variants for transferred
/// ones -- the normalized digest of the nested body, and whether that
/// nested record was complete. Only genuinely named records land here;
/// anonymous ones drill by body through the unnamed subshapes below (see
/// collectMemberRecords). The completeness bit is what makes the
/// tolerance stable: excusing a nested difference because the shaping input
/// holds the nested record incomplete is a fact about that input's body,
/// fixed when the shape is built, while excusing it because no shape is
/// known yet flips the moment a later input -- or a later record of the same
/// input -- brings a complete one.
struct NestedRecordShape {
  std::string name;
  std::string hex;
  bool complete = true;
};

/// One member's nested records: named ones in order, unnamed ones as
/// subshapes in order. Unnamed records cannot recurse, so the subshapes
/// bottom out; pristine-numbered anonymous records ride along here too (see
/// collectMemberRecords) and likewise cannot cycle back, since spelling a
/// cycle needs a name. Scalar parts leave no trace here -- they are covered
/// once, for the whole record, by the layout digest below -- so the member
/// loop only ever attributes a difference to nested records.
struct RecordMemberShape {
  llvm::SmallVector<NestedRecordShape, 2> nested;
  std::vector<std::unique_ptr<RecordShape>> unnamed;
};

struct RecordShape {
  int kind = 0;
  bool packed = false;
  bool padded = false;
  // Whether the record this shape was built from was complete. Named shapes
  // always are -- incomplete records never reach the map -- but an unnamed
  // subshape may not be.
  bool complete = true;
  // Hex layout digest: member structure with nested named records as bare
  // names, never bodies. The drill gates on it before examining any nested
  // record, so a scalar or wrapper difference splits absolutely instead of
  // being misattributed to an unrelated completeness excuse elsewhere in
  // the body. Blind to nested completeness by construction -- names carry
  // no bodies -- so it never fires on the differences the tolerance exists
  // to excuse.
  std::string layoutHex;
  llvm::SmallVector<RecordMemberShape, 8> members;
};

/// What the pass has learned from the inputs seen so far.
struct TypeUnificationState {
  /// record name -> digest of the normalized signature of the body under that
  /// name. Digests, not text: the map outlives every input, and the text is
  /// gigabytes on a real link.
  llvm::StringMap<std::string> knownBodyDigests;
  /// Every name in use, so a generated one never lands on a real record.
  llvm::StringSet<> takenNames;
  /// digest of the normalized signature -> the name every input calls that
  /// anonymous record by, once merged.
  llvm::StringMap<std::string> anonCanonical;
  /// digest -> body shape of the first holder, for completeness-tolerant
  /// canonical reuse: a digest that misses above drills against these shapes
  /// and reuses a canonical whose body matches modulo member completeness,
  /// instead of minting a second canonical for the same type. Keyed by
  /// digest, parallel to anonCanonical, so each entry self-describes.
  llvm::StringMap<RecordShape> anonCanonShapes;
  /// Digests of anonCanonical in first-insertion order. The reuse check
  /// visits canonicals through this, not the map: StringMap iteration order
  /// is unspecified, and which of several matching canonicals is reused
  /// must not depend on it. Parallel to anonCanonical's keys.
  std::vector<std::string> anonCanonOrder;
  unsigned anonCounter = 0;
  /// canonical anonymous name -> the named records its first holder's
  /// transferred body references, post-rename, in collect order. Two inputs
  /// can hold the same pristine anonymous body -- same digest, same canonical
  /// name -- yet rename a nested record differently, and then the canonical
  /// name carries two bodies. Same digest means same pristine structure, so
  /// the flat lists line up positionally. The fixpoint compares post-rename
  /// nested names through this map, re-canonicalizes a divergent anonymous
  /// record onto a fresh name, and splits its parents: they reference
  /// different canonicals.
  llvm::StringMap<llvm::SmallVector<std::string, 2>> anonPostNested;
  /// (digest, post-rename nested names) -> canonical name, for anonymous
  /// records re-canonicalized by the fixpoint. Keyed by post-rename body so
  /// two inputs that rename alike share the fresh name and two that rename
  /// apart do not.
  llvm::StringMap<std::string> anonRecanonical;
  /// record name -> compact body shape under that name. Kept alongside the
  /// digests above for the completeness-tolerant comparison: when two digests
  /// disagree, the shapes say whether the difference is a genuine body change
  /// or only nested completeness. A shape holds kind, packing, member count,
  /// a layout digest (member structure with nested named records as bare
  /// names), and per member the nested named records (name + digest + the
  /// shaping input's completeness bit) and unnamed subshapes -- structure
  /// and hex, not text, so the map stays megabytes where the text would be
  /// gigabytes.
  llvm::StringMap<RecordShape> knownBodyShapes;
  /// record name -> index, into the input filenames, of the input that first
  /// held a complete body under that name. The split-propagation fixpoint
  /// reads that input's rename plan through it: a record that unifies must
  /// reference, post-rename, the same names its winner's transferred body
  /// references, and the winner's plan says what those are.
  llvm::StringMap<unsigned> knownBodyOwner;
};

/// Collect the records nested in one member type, descending through the same
/// wrappers canonicalScalarString does, in the same order. Named records land
/// in `named` (not descended into: the matcher recurses by name); unnamed
/// ones land in `unnamed`, as do pristine-numbered anonymous records
/// ("anon.N"): a per-TU number is not identity, so anons drill by body
/// through the unnamed path, where member incompleteness excuses exactly as
/// for named records. (Routing them by name forced anonymous bodies to
/// compare exactly, splitting parents whose anon members differed only in
/// completeness -- see the interpreter Block/Pointer cascade.)
static void
collectMemberRecords(mlir::Type t,
                     llvm::SmallVectorImpl<cir::RecordType> &named,
                     llvm::SmallVectorImpl<cir::RecordType> &unnamed) {
  if (auto rec = mlir::dyn_cast<cir::RecordType>(t)) {
    if (rec.getName() && !rec.getName().getValue().empty() &&
        !isAnonRecordName(rec.getName().getValue()))
      named.push_back(rec);
    else
      unnamed.push_back(rec);
    return;
  }
  if (auto ptr = mlir::dyn_cast<cir::PointerType>(t)) {
    collectMemberRecords(ptr.getPointee(), named, unnamed);
    return;
  }
  if (auto arr = mlir::dyn_cast<cir::ArrayType>(t)) {
    collectMemberRecords(arr.getElementType(), named, unnamed);
    return;
  }
  if (auto vec = mlir::dyn_cast<cir::VectorType>(t)) {
    collectMemberRecords(vec.getElementType(), named, unnamed);
    return;
  }
  if (auto cplx = mlir::dyn_cast<cir::ComplexType>(t)) {
    collectMemberRecords(cplx.getElementType(), named, unnamed);
    return;
  }
  if (auto dm = mlir::dyn_cast<cir::DataMemberType>(t)) {
    collectMemberRecords(dm.getMemberTy(), named, unnamed);
    collectMemberRecords(dm.getClassTy(), named, unnamed);
    return;
  }
  if (auto m = mlir::dyn_cast<cir::MethodType>(t)) {
    collectMemberRecords(m.getMemberFuncTy(), named, unnamed);
    collectMemberRecords(m.getClassTy(), named, unnamed);
    return;
  }
  if (auto fn = mlir::dyn_cast<cir::FuncType>(t)) {
    for (mlir::Type in : fn.getInputs())
      collectMemberRecords(in, named, unnamed);
    if (mlir::Type ret = fn.getOptionalReturnType())
      collectMemberRecords(ret, named, unnamed);
    return;
  }
  // Scalars: nothing to collect.
}

static RecordShape buildRecordShape(cir::RecordType rec,
                                    RecordDigestCache &cache);

static RecordMemberShape buildMemberShape(mlir::Type t,
                                          RecordDigestCache &cache) {
  RecordMemberShape out;
  llvm::SmallVector<cir::RecordType, 2> named;
  llvm::SmallVector<cir::RecordType, 1> unnamed;
  collectMemberRecords(t, named, unnamed);
  for (cir::RecordType ref : named) {
    NestedRecordShape entry;
    entry.name = ref.getName().getValue().str();
    entry.hex = cache.digestRecord(ref);
    entry.complete = !ref.isIncomplete();
    out.nested.push_back(std::move(entry));
  }
  for (cir::RecordType ref : unnamed)
    out.unnamed.push_back(
        std::make_unique<RecordShape>(buildRecordShape(ref, cache)));
  return out;
}

static RecordShape buildRecordShape(cir::RecordType rec,
                                    RecordDigestCache &cache) {
  RecordShape out;
  out.kind = static_cast<int>(rec.getKind());
  out.packed = rec.getPacked();
  out.padded = rec.getPadded();
  out.complete = !rec.isIncomplete();
  out.layoutHex = cache.layoutRecord(rec);
  if (!rec.isIncomplete())
    for (mlir::Type member : rec.getMembers())
      out.members.push_back(buildMemberShape(member, cache));
  return out;
}

/// Outcome of the completeness-tolerant comparison: whether the bodies agree
/// modulo completeness, whether anything had to be excused to get there, and
/// whether any cycle edge was cut along the way. A scalar-only difference
/// never reaches the excuse logic: the layout gate above splits it first,
/// absolutely, so the excuse bit only ever covers nested named bodies.
/// The cut bit marks a verdict the drill could not fully see: a cycle edge
/// cut hides the subtree behind it, so "match, nothing excused" on a drill
/// that cut an edge is not evidence of a genuine difference, only of a
/// difference the drill could not reach. The nested rule abstains on those --
/// keeps looking at the other members -- instead of splitting early, or two
/// mutually recursive records disagree about each other depending on member
/// order: the one drilled first splits on the cut edge before reaching the
/// member that excuses, while the other unifies. Every record still gets a
/// fresh top-level drill where its whole body is examined, so nothing a cut
/// hides stays hidden from all drills; and a drill that abstains everywhere
/// ends unexcused, which the top-level rule still splits on.
struct ToleranceVerdict {
  bool match = false;
  bool excused = false;
  bool cut = false;
};

static void traceDrillVerdict(const char *site, cir::RecordType recB,
                              const ToleranceVerdict &v) {
  if (!toleranceDrillTrace)
    return;
  StringRef name = recB.getName() ? recB.getName().getValue() : StringRef();
  llvm::dbgs() << "drill trace: '" << name << "' @" << site
               << " match=" << v.match << " excused=" << v.excused
               << " cut=" << v.cut << "\n";
}

/// Memo for one drill: (shape, type) -> verdict, cut-free verdicts only.
/// Without it the drill re-explores every shared nested subgraph once per
/// path that reaches it, which is exponential on DAG-shaped record graphs
/// and hangs large links. Storing only cut-free verdicts changes nothing
/// observable: a pair whose verdict cut -- itself or anywhere below it,
/// since cuts propagate up -- is never stored, so every stored pair's
/// reachable subgraph is acyclic, and re-drilling it under any visiting set
/// explores that same subgraph with zero cuts and the identical verdict. The
/// visiting check still runs first, so cycle edges cut exactly as before.
/// Per-drill (like `visiting`): shapes and digests are fixed within a drill
/// but grow between drills, and drills run on worker threads. Keyed by shape
/// address, which is stable: the maps only ever try_emplace, and StringMap
/// entries never move.
using ToleranceMemo = llvm::DenseMap<std::pair<const RecordShape *, mlir::Type>,
                                     ToleranceVerdict>;

/// Visit budget for one drill. A verdict computed under one visiting set
/// need not hold under another -- a cycle edge cut in one comparison is
/// drilled in the next -- so cyclic pairs cannot memoize, and a drill that
/// revisits them once per path is exponential in the path count. Dense
/// real-world graphs (the ASTContext/FoldingSet cluster) hit this: one
/// CheckerManager drill burned 45+ minutes without finishing. Past the
/// budget the drill fails closed (splits the record) instead of hanging;
/// the trip is logged at the drill site. The limit is generous -- the
/// largest drill observed completing used a fraction of it -- so only a
/// runaway drill trips it. The limit comes from --tolerance-drill-budget
/// (a hidden debug flag) so tests can trip the path deterministically.
struct DrillBudget {
  unsigned long long limit = toleranceDrillBudget;
  unsigned long long visits = 0;
  bool exceeded = false;
};

static ToleranceVerdict
matchRecordShape(const RecordShape &shapeA, cir::RecordType recB,
                 RecordDigestCache &cacheB,
                 const llvm::StringMap<RecordShape> &shapes,
                 const llvm::StringMap<std::string> &envDigests,
                 llvm::DenseSet<mlir::Type> &visiting, ToleranceMemo &memo,
                 DrillBudget &budget);

static ToleranceVerdict
matchUnnamedShape(const RecordShape &shapeA, cir::RecordType recB,
                  RecordDigestCache &cacheB,
                  const llvm::StringMap<RecordShape> &shapes,
                  const llvm::StringMap<std::string> &envDigests,
                  llvm::DenseSet<mlir::Type> &visiting, ToleranceMemo &memo,
                  DrillBudget &budget) {
  // Either side incomplete excuses the pair, as for named records: an
  // incomplete record has no body to disagree with.
  if (!shapeA.complete || recB.isIncomplete()) {
    ToleranceVerdict v{true, true};
    traceDrillVerdict("incomplete", recB, v);
    return v;
  }
  if (static_cast<int>(recB.getKind()) != shapeA.kind ||
      recB.getPacked() != shapeA.packed || recB.getPadded() != shapeA.padded ||
      recB.getMembers().size() != shapeA.members.size()) {
    ToleranceVerdict v{false, false};
    traceDrillVerdict("shape", recB, v);
    return v;
  }
  // Layout first, nested records after: a scalar or wrapper difference is
  // directly visible at this level -- it never hides behind a cycle edge,
  // so no cut can abstain it away -- and it is never completeness, so no
  // excuse can cover it. Without this gate an unrelated nested excuse
  // elsewhere in the body misattributes the digest difference and unifies
  // two different layouts. A layout match confines the difference to
  // nested named bodies, which is what the member loop below attributes.
  if (cacheB.layoutRecord(recB) != shapeA.layoutHex) {
    ToleranceVerdict v{false, false};
    traceDrillVerdict("layout", recB, v);
    if (toleranceDrillTrace)
      llvm::dbgs() << "drill trace: layoutB=["
                   << cacheB.layoutFormForTrace(recB) << "]\n";
    return v;
  }
  ToleranceVerdict out{true, false};
  auto membersB = recB.getMembers();
  for (size_t i = 0; i < membersB.size(); ++i) {
    llvm::SmallVector<cir::RecordType, 2> namedB;
    llvm::SmallVector<cir::RecordType, 1> unnamedB;
    collectMemberRecords(membersB[i], namedB, unnamedB);
    const RecordMemberShape &memberA = shapeA.members[i];
    if (namedB.size() != memberA.nested.size() ||
        unnamedB.size() != memberA.unnamed.size()) {
      ToleranceVerdict v{false, false};
      traceDrillVerdict("member-count", recB, v);
      return v;
    }
    for (size_t j = 0; j < namedB.size(); ++j) {
      // Only genuinely named records reach this loop: anonymous ones route
      // through the unnamed subshape drill below, which excuses member
      // incompleteness instead of demanding exact bodies (see
      // collectMemberRecords).
      StringRef nameB = namedB[j].getName().getValue();
      const NestedRecordShape &nestedA = memberA.nested[j];
      if (nameB != nestedA.name) {
        ToleranceVerdict v{false, false};
        traceDrillVerdict("name", recB, v);
        return v;
      }
      const std::string &hexB = cacheB.digestRecord(namedB[j]);
      if (hexB == nestedA.hex)
        continue;
      // Either side incomplete excuses the pair: a declaration has no body
      // to disagree with, whichever input holds it. The shaping side's bit
      // comes from its shape, frozen when that shape was built -- not from
      // whether some shape happens to be known yet, which flips as later
      // records arrive.
      if (namedB[j].isIncomplete() || !nestedA.complete) {
        out.excused = true;
        continue;
      }
      // A record already on the comparison path is a same-component
      // reference, which the digests spell as a bare name: the names matched
      // to get here, so this edge contributes nothing further either way. It
      // must not count as an excuse -- the top-level rule demands an excuse
      // for the difference it was called on, and a cut cycle edge is not
      // one -- but neither may it force a split, or every recursive record
      // would. Read-only: matchRecordShape owns the insert. It does mark the
      // verdict cut: whatever hides behind this edge, this drill did not see
      // it, and a caller that treats "match, nothing excused" as genuine
      // must know the drill abstained on this subtree.
      // Cycle-relevant records only (see isCycleRelevant): any other
      // record appears at most once per drill path, so this test would miss.
      if (cacheB.isCycleRelevant(namedB[j])) {
        if (visiting.count(namedB[j])) {
          out.cut = true;
          continue;
        }
      }
      // B is the environment's body for this name, digest-identical, so
      // drilling B against the environment's shape below can only find
      // them identical with nothing excused -- and then the backstop
      // misreads that as a genuine difference and splits the parent.
      // Excuse outright instead: A holds this name too, and it got there
      // by matching or excusing; the digests differed to get here, so it
      // excused, and B inherits the excuse. This is the variant-reuse
      // shape: the environment holds the minter's body, A holds a
      // tolerance-divergent reuse, and B holds the minter's body again.
      // After the visiting check, so a self-edge still cuts instead of
      // excusing the record against itself.
      if (auto envIt = envDigests.find(nameB);
          envIt != envDigests.end() && hexB == envIt->second) {
        out.excused = true;
        continue;
      }
      auto shapeIt = shapes.find(nameB);
      // Both sides complete, so a body is known: a complete record is
      // inserted the first input that holds it, and first wins. A miss here
      // is a broken invariant, not a new record -- fail closed.
      if (shapeIt == shapes.end()) {
        ToleranceVerdict v{false, false};
        traceDrillVerdict("shape-miss", recB, v);
        return v;
      }
      ToleranceVerdict sub =
          matchRecordShape(shapeIt->second, namedB[j], cacheB, shapes,
                           envDigests, visiting, memo, budget);
      // A nested mismatch is absolute: the drill found structural evidence
      // of a difference, and a cut edge elsewhere in the drill does not
      // un-find it. Abstaining here would defer genuine evidence to a drill
      // that cuts the same edge and abstains in turn, silencing it forever.
      if (!sub.match) {
        ToleranceVerdict v{false, false};
        traceDrillVerdict("nested", recB, v);
        return v;
      }
      // A nested verdict of "match, nothing excused" on differing digests
      // means a genuine difference the drill saw but attributed to nothing,
      // and splits -- but only when the drill saw the whole subtree. The
      // layout gate catches scalar differences before the member loop, so
      // in practice a surviving difference always excuses or cuts deeper
      // and this fires only as a backstop; fail closed anyway. When the
      // drill cut a cycle edge it abstained on whatever hides behind it,
      // and the parent must abstain too: keep looking at the other members
      // instead of splitting early. A mismatch hiding behind the cut edge
      // still splits the record whose body holds it -- every body difference
      // is cut-free visible to its owner's own drill, where the rule above
      // fires absolutely -- so abstaining here can only leave the transfer
      // check a loud inconsistency, never a silent one.
      if (!sub.excused && !sub.cut) {
        ToleranceVerdict v{false, false};
        traceDrillVerdict("nested-backstop", recB, v);
        return v;
      }
      if (sub.excused)
        out.excused = true;
      out.cut |= sub.cut;
    }
    for (size_t j = 0; j < unnamedB.size(); ++j) {
      ToleranceVerdict sub =
          matchUnnamedShape(*memberA.unnamed[j], unnamedB[j], cacheB, shapes,
                            envDigests, visiting, memo, budget);
      if (!sub.match) {
        ToleranceVerdict v{false, false};
        traceDrillVerdict("unnamed", recB, v);
        return v;
      }
      if (!sub.excused && !sub.cut) {
        ToleranceVerdict v{false, false};
        traceDrillVerdict("unnamed-backstop", recB, v);
        return v;
      }
      if (sub.excused)
        out.excused = true;
      out.cut |= sub.cut;
    }
  }
  traceDrillVerdict("final", recB, out);
  return out;
}

static ToleranceVerdict
matchRecordShape(const RecordShape &shapeA, cir::RecordType recB,
                 RecordDigestCache &cacheB,
                 const llvm::StringMap<RecordShape> &shapes,
                 const llvm::StringMap<std::string> &envDigests,
                 llvm::DenseSet<mlir::Type> &visiting, ToleranceMemo &memo,
                 DrillBudget &budget) {
  // Termination comes from the visiting set, and the drill runs only on
  // digest mismatch. The nested rule pre-checks membership, so the insert
  // below is the backstop, not the cut. It runs before the memo: a verdict
  // computed under one visiting set need not hold under another -- a cycle
  // edge cut in one comparison is drilled in the next -- so only cut-free
  // verdicts are stored, and a stored verdict is what re-drilling the pair
  // would compute anyway (see ToleranceMemo).
  // The visiting set tracks cycle-relevant records only (see
  // isCycleRelevant): any other record is visited at most once per drill
  // path, so it can neither trip the backstop nor be cut below.
  ++budget.visits;
  const bool track = cacheB.isCycleRelevant(recB);
  if (track && !visiting.insert(recB).second)
    return ToleranceVerdict{true, false, true};
  if (auto memoIt = memo.find({&shapeA, recB}); memoIt != memo.end()) {
    if (track)
      visiting.erase(recB);
    return memoIt->second;
  }
  // Past the visit budget the drill fails closed (splits) without
  // exploring, so a runaway drill unwinds instead of hanging (see
  // DrillBudget). Stored verdicts above stay honored: only pairs never
  // computed get the split, which is memoized so the tripped drill is
  // stable within itself.
  if (budget.exceeded || budget.visits > budget.limit) {
    budget.exceeded = true;
    ToleranceVerdict split{false, false};
    memo[{&shapeA, recB}] = split;
    if (track)
      visiting.erase(recB);
    return split;
  }
  ToleranceVerdict out = matchUnnamedShape(shapeA, recB, cacheB, shapes,
                                           envDigests, visiting, memo, budget);
  if (track)
    visiting.erase(recB);
  if (!out.cut)
    memo[{&shapeA, recB}] = out;
  return out;
}
} // namespace

/// Give the colliding records in `input` fresh names, and rewrite every
/// reference to them.
///
/// This is the type-level analogue of the local-symbol renaming below, and it
/// exists for the same reason: two inputs can legally use one name for two
/// different things. `static void helper()` is the symbol case; a `struct S`
/// in an anonymous namespace is the type case. Both mangle identically across
/// TUs and merging either one is a miscompile.
///
/// The mechanics are harder than for symbols because MLIR types are uniqued
/// and cannot be renamed in place. So the renamed record is *built* -- created
/// incomplete first, registered in the mapping, and only then completed with
/// its members mapped through that same mapping. Registering before completing
/// is what makes a self-referential record work: by the time its own member
/// list is mapped, the replacement for itself is already there to be found,
/// and nothing recurses forever.
///
/// Records that are not renamed but *contain* one that is get their members
/// swapped in place. They are mutable and this is a scratch context that
/// belongs to this input alone, so mutating them is safe here in a way it
/// would not be in the shared destination context.
///
/// It runs in three parts, so that only the one that reads and writes the
/// state carried from input to input is serial: analyzeRecords collects and
/// hashes the input's records, decideRecordNames picks the names, and
/// applyRecordNames rewrites the input. The first and last touch nothing but
/// the input's own context, and run over a window of inputs in parallel.
namespace {
/// One input's named records, and their digests before any renaming.
struct InputRecords {
  /// The input, for what is said about its records: they have no position of
  /// their own.
  mlir::LocationAttr loc;
  llvm::SetVector<cir::RecordType> records;
  /// `records` made random-access; SetVector iterates in insertion order, so
  /// this is that same order.
  SmallVector<cir::RecordType> ordered;
  std::unique_ptr<RecordDigestCache> digests;
};

/// A record decideRecordNames gives a new name: a collision's or an anonymous
/// record's cross-input one.
struct RecordRename {
  cir::RecordType rec;
  std::string newName;
};
} // namespace

static void analyzeRecords(ModuleOp input, StringRef filename,
                           InputRecords &out) {
  out.loc = fileLoc(input.getContext(), filename);
  collectNamedRecords(input.getOperation(), out.records);
  out.ordered.assign(out.records.begin(), out.records.end());
  // Digests are memoized per input, so hashing every record costs one walk
  // over the input's types; the decisions run in the same order as the prints
  // they replace did.
  out.digests = std::make_unique<RecordDigestCache>(true, out.ordered);
}

/// The variant name for a record that splits: reuse an earlier variant
/// holding this same body, else mint a fresh `.m<n>` no input uses. Fresh
/// variants are registered in the unification state like any new record, with
/// this input as their owner; reused ones keep their original owner. A
/// variant being re-split passes every variant it has already tried, so the
/// scan never hands one back and the fixpoint cannot cycle between two
/// reused variants whose owners renamed a nested record differently.
static std::string pickVariantName(StringRef name, const std::string &digest,
                                   cir::RecordType rec, mlir::Location inputLoc,
                                   RecordDigestCache &digestCache,
                                   TypeUnificationState &state,
                                   const llvm::StringSet<> &inputNames,
                                   unsigned globalInputIdx,
                                   const llvm::StringSet<> *skip = nullptr) {
  auto &knownBodyDigests = state.knownBodyDigests;
  auto &knownBodyShapes = state.knownBodyShapes;
  auto &knownBodyOwner = state.knownBodyOwner;
  auto &takenNames = state.takenNames;
  std::string newName;
  for (unsigned n = 1;; ++n) {
    // ".m<n>", not ".<n>". CIRGen suffixes a repeated record name with a
    // decimal counter, and a C++ name cannot contain a dot otherwise, so
    // `Foo.1` is a name real records already have. Generating into that same
    // namespace collided with them, and the damage was not local: an input
    // that had to skip a taken `Foo.1` chose a different suffix than its
    // neighbour did for the very same type, so two inputs ended up
    // disagreeing about a record they had agreed on.
    newName = (name + ".m" + Twine(n)).str();
    if (skip && skip->count(newName))
      continue;
    // Records are identified, so asking for a name this input already uses
    // hands back *that* record; completing it with another type's members is
    // the three-input form of the same bug.
    if (inputNames.count(newName))
      continue;
    auto known = knownBodyDigests.find(newName);
    if (known != knownBodyDigests.end()) {
      if (known->second == digest)
        break; // Already have this exact variant under this name.
      // Same tolerance as unification: a variant that differs only by nested
      // completeness is the same variant.
      if (auto shapeIt = knownBodyShapes.find(newName);
          shapeIt != knownBodyShapes.end()) {
        llvm::DenseSet<mlir::Type> visiting;
        ToleranceMemo memo;
        DrillBudget budget;
        ToleranceVerdict verdict =
            matchRecordShape(shapeIt->second, rec, digestCache, knownBodyShapes,
                             knownBodyDigests, visiting, memo, budget);
        if (budget.exceeded)
          mlir::emitWarning(inputLoc)
              << "tolerance drill for record '" << newName
              << "' exceeded its visit budget; splitting";
        traceDrillVerdict("decide", rec, verdict);
        if (verdict.match && verdict.excused)
          break; // Already have this variant modulo completeness.
      }
      continue; // Held by a different body.
    }
    if (takenNames.count(newName))
      continue; // Held by a record that has its own claim to the name.
    takenNames.insert(newName);
    knownBodyDigests.try_emplace(newName, digest);
    knownBodyShapes.try_emplace(newName, buildRecordShape(rec, digestCache));
    knownBodyOwner.try_emplace(newName, globalInputIdx);
    break;
  }
  return newName;
}

/// Post-rename nested names of one record of this input, in collect order,
/// through the per-object split table. Named and anonymous alike: anonymous
/// records map to their current canonical, which the fixpoint below moves
/// onto post-canonicals as nested renames diverge. Unnamed records have no
/// name to map and CIRGen never emits them nameless in practice, so they
/// contribute nothing here; the transfer check still examines them
/// structurally.
///
/// collectMemberRecords routes pristine anonymous records to its unnamed
/// list for the drill, so both lists are collected into one here: dropping
/// the anonymous ones leaves a parent's list empty where its winner's
/// transferred shape names the canonical. Across windows that length
/// mismatch splits a parent that differs only in nested completeness;
/// within a window two empty lists compare equal although the bodies
/// reference different canonicals, and the transfer check fails the link.
static llvm::SmallVector<std::string, 2>
postNestedNames(cir::RecordType rec,
                const llvm::DenseMap<mlir::Type, std::string> &post) {
  llvm::SmallVector<std::string, 2> out;
  for (mlir::Type member : rec.getMembers()) {
    llvm::SmallVector<cir::RecordType, 2> refs;
    collectMemberRecords(member, refs, refs);
    for (cir::RecordType ref : refs) {
      if (auto it = post.find(ref); it != post.end())
        out.push_back(it->second);
      else if (ref.getName() && !ref.getName().getValue().empty())
        out.push_back(ref.getName().getValue().str());
    }
  }
  return out;
}

/// One same-window input's records and renames, as views into the in-flight
/// window. The fixpoint reads winners that have not transferred yet through
/// these: their contexts are alive, so per-object post-rename names resolve
/// exactly, the way the transfer will write them.
struct FixpointWindowInput {
  ArrayRef<cir::RecordType> ordered;
  ArrayRef<RecordRename> renames;
};

/// Split every record whose transferred references would not match its
/// winner's, and repeat until none remains.
///
/// decideRecordNames drills each record against the winner's body in turn,
/// and each drill is pairwise: nothing stops it from unifying a parent whose
/// nested record split in this same input, unifying against a winner that
/// itself split that nested record, or reusing a variant whose owner renamed
/// a nested record differently. Any of those leaves the transfer holding two
/// bodies under one name and checkTransferredBodies fails the link. The fix
/// is to split the parent too -- or to re-split a reused variant onto a
/// variant of its own: a record must reference, post-rename, exactly the
/// names its winner's transferred body references.
///
/// The winner's post-rename names come from two precise sources, never from
/// a name-keyed plan that collapses the several objects one input can hold
/// under one name. Winners from earlier windows have transferred already,
/// and their transferred shapes hold the post-rename nested names
/// positionally. Winners from this window have not, but their contexts are
/// alive, so per-object post-rename names resolve through their rename
/// lists exactly the way the transfer will write them. Either way the
/// comparison is positional: same pristine body means same structure, so the
/// nested lists line up.
///
/// Unnamed records have no name to map and incomplete ones hold no body to
/// split, so both are skipped. Anonymous records cannot split -- their name
/// is their digest -- so a divergent one is re-canonicalized onto a fresh
/// name keyed by (digest, post-rename nested names) instead, and its parents
/// split on the canonical mismatch like any other. Splits cascade upward, so
/// this runs to a fixpoint; it terminates because splits only accumulate and
/// records are finite, a re-split either reuses another existing variant or
/// mints a fresh one this input owns outright, and re-canonicalization
/// follows the nested renames it keys on. A propagated split is always
/// transfer-clean: the check only compares bodies transferred under one
/// name, and a fresh variant name has no earlier body.
static void
propagateRecordSplits(const InputRecords &in, RenamePlan &plan,
                      SmallVectorImpl<RecordRename> &renames,
                      TypeUnificationState &state,
                      const llvm::StringMap<RecordShape> &transferredShapes,
                      ArrayRef<FixpointWindowInput> window, size_t windowBase,
                      unsigned globalInputIdx) {
  // No early return on empty renames: the mismatch can come from the
  // winner's side. The decide drill compares pristine nested names, so an
  // input that renames nothing still unifies a parent whose winner renamed
  // a nested record -- the names match textually, the completeness excuse
  // fires, and the parent unifies against a body that references a
  // different type. Only this fixpoint compares post-rename references,
  // and it is the only thing standing between that stale unify and the
  // transfer check. (RegisterBankInfo in member-998: unified at decide,
  // failed at check, renamed nothing itself.)
  auto &knownBodyOwner = state.knownBodyOwner;
  auto &takenNames = state.takenNames;
  RecordDigestCache &digestCache = *in.digests;

  llvm::StringSet<> inputNames;
  for (cir::RecordType rec : in.ordered)
    inputNames.insert(rec.getName().getValue());

  // This input's post-rename name per renamed object, anonymous records
  // included: renames are per object, plans per name for reporting. The
  // anonymous entries start at their digest-canonicals and the fixpoint
  // moves divergent ones onto post-canonicals.
  llvm::DenseMap<mlir::Type, std::string> splitName;
  for (const RecordRename &r : renames)
    splitName[r.rec] = r.newName;
  // Same-window winners' per-object split tables, rebuilt once per winner
  // from their rename lists: earlier decisions are frozen.
  llvm::DenseMap<unsigned, llvm::DenseMap<mlir::Type, std::string>>
      winnerSplitNames;
  // Variants already tried per re-split object, so re-splitting never cycles.
  llvm::DenseMap<mlir::Type, llvm::StringSet<>> triedVariants;
  // Post that keyed each fresh post-canonical, so the end-of-input record
  // writes what the key means rather than whatever the last pass computed.
  llvm::StringMap<llvm::SmallVector<std::string, 2>> recanonPost;

  bool changed = true;
  while (changed) {
    changed = false;
    // Anonymous records first: a re-canonicalization changes what the
    // parents below compare against. Nested anonymous references resolve
    // through the split table as it stands; a stale entry this pass is
    // recomputed the next, and nesting is finite.
    // The first holder of a canonical in this input stands for the input
    // until the end-of-input record below: two records sharing a pristine
    // body here are compared with it like a later input's, rather than both
    // keeping the canonical whatever they reference.
    llvm::DenseMap<StringRef, llvm::SmallVector<std::string, 2>> inputCreator;
    for (cir::RecordType rec : in.ordered) {
      if (rec.isIncomplete())
        continue;
      StringRef pristine = rec.getName().getValue();
      if (!isAnonRecordName(pristine))
        continue;
      const std::string &digest = digestCache.digestRecord(rec);
      auto canonIt = state.anonCanonical.find(digest);
      if (canonIt == state.anonCanonical.end())
        continue; // Never canonicalized: incomplete at decide, skipped here.
      const std::string &canon = canonIt->second;
      llvm::SmallVector<std::string, 2> post = postNestedNames(rec, splitName);
      const llvm::SmallVector<std::string, 2> *creator = nullptr;
      if (auto creatorIt = state.anonPostNested.find(canon);
          creatorIt != state.anonPostNested.end()) {
        creator = &creatorIt->second;
      } else {
        // This input defines the canonical: its first holder here is the
        // creator, recorded for good at the end.
        auto [local, first] = inputCreator.try_emplace(canon, post);
        if (first)
          continue;
        creator = &local->second;
      }
      if (post == *creator)
        continue;
      // Same pristine body, different post-rename references: genuinely two
      // bodies under one canonical. Re-canonicalize onto a name keyed by
      // what this input actually references, shared with any input -- past
      // or future -- that references the same.
      std::string key = digest;
      for (const std::string &nested : post) {
        key.push_back('\0');
        key += nested;
      }
      auto [known, fresh] = state.anonRecanonical.try_emplace(key);
      if (fresh) {
        std::string generated;
        do {
          generated = ("anon.m" + Twine(state.anonCounter++)).str();
        } while (takenNames.count(generated));
        known->second = generated;
        takenNames.insert(generated);
      }
      const std::string &recanon = known->second;
      // Its name so far: the rename decided or moved to earlier, or the
      // pristine one if it kept it (canonical == pristine), which leaves no
      // entry in the split table, the plan or the renames.
      auto current = splitName.find(rec);
      std::string currentName =
          current == splitName.end() ? pristine.str() : current->second;
      if (recanon == currentName)
        continue; // Already there from an earlier pass: same key, same
                  // name. Not a change, or the fixpoint never settles.
      recanonPost[recanon] = post;
      bool planMoved = false;
      for (auto &[from, to] : plan.anonRenames)
        if (from == pristine && to == currentName) {
          to = recanon;
          planMoved = true;
          break;
        }
      if (!planMoved)
        plan.anonRenames.emplace_back(pristine.str(), recanon);
      bool renameMoved = false;
      for (RecordRename &r : renames)
        if (r.rec == rec) {
          r.newName = recanon;
          renameMoved = true;
          break;
        }
      if (!renameMoved)
        renames.push_back({rec, recanon});
      splitName[rec] = recanon;
      ++plan.anonCanonicalized;
      changed = true;
    }
    for (cir::RecordType rec : in.ordered) {
      if (rec.isIncomplete())
        continue;
      StringRef name = rec.getName().getValue();
      if (isAnonRecordName(name))
        continue;
      // Unified records match against the record's winner; variants against
      // the variant's owner, which is whoever first transferred under it.
      StringRef postName = name;
      if (auto splitIt = splitName.find(rec); splitIt != splitName.end())
        postName = splitIt->second;
      auto ownerIt = knownBodyOwner.find(postName);
      if (ownerIt == knownBodyOwner.end() || ownerIt->second == globalInputIdx)
        continue; // New here, or owned by this input: nothing to match.
      unsigned ownerIdx = ownerIt->second;
      // This input's post-rename nested names, positionally.
      llvm::SmallVector<std::string, 2> postB = postNestedNames(rec, splitName);
      // The winner's, from whichever precise source is available: the
      // transferred shape for an earlier window, the live rename list for
      // this one. Same pristine body means same structure, so a length
      // mismatch is itself a structural difference: split, fail closed.
      llvm::SmallVector<std::string, 2> postWinner;
      if (ownerIdx < windowBase) {
        auto shapeIt = transferredShapes.find(postName);
        if (shapeIt == transferredShapes.end())
          continue; // Not transferred under this name: nothing to match.
        for (const RecordMemberShape &member : shapeIt->second.members)
          for (const NestedRecordShape &nested : member.nested)
            postWinner.push_back(nested.name);
      } else {
        const FixpointWindowInput &winner = window[ownerIdx - windowBase];
        auto tableIt = winnerSplitNames.find(ownerIdx);
        if (tableIt == winnerSplitNames.end()) {
          llvm::DenseMap<mlir::Type, std::string> built;
          for (const RecordRename &r : winner.renames)
            built[r.rec] = r.newName;
          tableIt =
              winnerSplitNames.try_emplace(ownerIdx, std::move(built)).first;
        }
        const llvm::DenseMap<mlir::Type, std::string> &winnerPost =
            tableIt->second;
        // The winner's first object under this name: the transfer reads
        // inputs in order and records the first body it sees.
        cir::RecordType winnerRec;
        for (cir::RecordType cand : winner.ordered) {
          if (cand.isIncomplete())
            continue;
          StringRef candPost = cand.getName().getValue();
          if (auto it = winnerPost.find(cand); it != winnerPost.end())
            candPost = it->second;
          if (candPost == postName) {
            winnerRec = cand;
            break;
          }
        }
        if (!winnerRec)
          continue; // Winner holds no body under this name.
        postWinner = postNestedNames(winnerRec, winnerPost);
      }
      bool split = postB.size() != postWinner.size();
      for (size_t k = 0; !split && k < postB.size(); ++k)
        split = postB[k] != postWinner[k];
      if (!split)
        continue;
      const llvm::StringSet<> *skip = nullptr;
      if (postName != name) {
        triedVariants[rec].insert(postName);
        skip = &triedVariants[rec];
      }
      std::string newName =
          pickVariantName(name, digestCache.digestRecord(rec), rec, in.loc,
                          digestCache, state, inputNames, globalInputIdx, skip);
      if (postName == name) {
        plan.renames.emplace_back(name.str(), newName);
        renames.push_back({rec, newName});
      } else {
        // A reused variant re-split onto a variant of its own: move this
        // object's rename, matching the plan entry by both names.
        for (auto &[from, to] : plan.renames)
          if (from == name && to == postName) {
            to = newName;
            break;
          }
        for (RecordRename &r : renames)
          if (r.rec == rec) {
            r.newName = newName;
            break;
          }
      }
      splitName[rec] = std::move(newName);
      changed = true;
    }
  }
  // First holders record their final post-rename nested names; later inputs
  // compared against them above. Records on the way out, not during: the
  // posts evolve with the plans until the loop settles.
  for (cir::RecordType rec : in.ordered) {
    if (rec.isIncomplete())
      continue;
    if (!isAnonRecordName(rec.getName().getValue()))
      continue;
    const std::string &digest = digestCache.digestRecord(rec);
    auto canonIt = state.anonCanonical.find(digest);
    if (canonIt == state.anonCanonical.end())
      continue;
    llvm::SmallVector<std::string, 2> post = postNestedNames(rec, splitName);
    state.anonPostNested.try_emplace(canonIt->second, post);
  }
  for (auto &[recanon, post] : recanonPost)
    state.anonPostNested.try_emplace(recanon, post);
}

static void decideRecordNames(const InputRecords &in,
                              TypeUnificationState &state, RenamePlan &plan,
                              SmallVectorImpl<RecordRename> &renames,
                              unsigned globalInputIdx) {
  auto &knownBodyDigests = state.knownBodyDigests;
  auto &knownBodyShapes = state.knownBodyShapes;
  auto &knownBodyOwner = state.knownBodyOwner;
  auto &takenNames = state.takenNames;
  ArrayRef<cir::RecordType> ordered = in.ordered;
  RecordDigestCache &digestCache = *in.digests;

  // Every name this input uses, before any of them is renamed.
  //
  // CIRGen suffixes a repeated record name from a per-TU counter, so `.1` is
  // already a spelling real records have -- one pair of executorch TUs shares
  // 1053 such names. A generated `Foo.1` must therefore avoid the records
  // this input has not been walked to yet: creating one that collides returns
  // the *existing* identified record, and completing it with the other type's
  // members is exactly the "member type mismatch" the merge then dies on.
  llvm::StringSet<> inputNames;
  for (cir::RecordType rec : ordered) {
    inputNames.insert(rec.getName().getValue());
    takenNames.insert(rec.getName().getValue());
  }

  for (size_t i = 0, n = ordered.size(); i != n; ++i) {
    cir::RecordType rec = ordered[i];
    StringRef name = rec.getName().getValue();
    // An incomplete record is a declaration. It carries no body to disagree
    // with, and unifying it with whichever input has the definition is the
    // whole point of sharing a context.
    if (rec.isIncomplete()) {
      takenNames.insert(name);
      continue;
    }

    const std::string &digest = digestCache.digestRecord(rec);

    // An anonymous record has no name to preserve, so it is given one that
    // every input agrees on. Adopting the first input's spelling instead
    // would not work: the numbers overlap, and a later input's unrelated
    // `anon.1` would be pulled onto the same type.
    if (isAnonRecordName(name)) {
      auto [known, fresh] = state.anonCanonical.try_emplace(digest);
      if (fresh) {
        // A new digest still needs a reuse check before minting: the digest
        // hashes nested records by body, so an incomplete view of a member
        // (a bare declaration reference) digests differently from the
        // complete view, and two inputs holding the same anonymous type
        // under different completeness mint two canonicals for it. Parents
        // then spell different canonicals and fail the transfer check even
        // though the drill unifies them.
        // Drill against same-structure canonicals and reuse on a
        // completeness-only match; the transfer keeps the first body and
        // drops later views as tolerated, exactly as for named records.
        // Prefiltered by layout (member structure blind to nested bodies),
        // so genuinely different anons never drill.
        std::string layoutHex = digestCache.layoutRecord(rec);
        bool reused = false;
        // Insertion order, not map order: StringMap iteration is
        // unspecified, and which of several matching canonicals is reused
        // must not depend on it. The current digest joins the order below,
        // after this loop over prior holders.
        for (const std::string &canonDigest : state.anonCanonOrder) {
          auto shapeIt = state.anonCanonShapes.find(canonDigest);
          if (shapeIt == state.anonCanonShapes.end() ||
              shapeIt->second.layoutHex != layoutHex)
            continue;
          llvm::DenseSet<mlir::Type> visiting;
          ToleranceMemo memo;
          DrillBudget budget;
          ToleranceVerdict verdict = matchRecordShape(
              shapeIt->second, rec, digestCache, knownBodyShapes,
              knownBodyDigests, visiting, memo, budget);
          traceDrillVerdict("canon", rec, verdict);
          // A tripped budget is not evidence either way: mint fresh, which
          // is exactly what no reuse check would do. Silent -- unlike the
          // named sites, a fresh canonical here is the status quo.
          if (verdict.match && verdict.excused && !budget.exceeded) {
            // Present: a stored shape implies its digest was emplaced in
            // anonCanonical by the same block, so this lookup never inserts.
            known->second = state.anonCanonical[canonDigest];
            reused = true;
            break;
          }
        }
        if (!reused) {
          std::string generated;
          do {
            generated = ("anon.m" + Twine(state.anonCounter++)).str();
          } while (takenNames.count(generated));
          known->second = generated;
          takenNames.insert(generated);
        }
        state.anonCanonOrder.push_back(digest);
        // Only the minter stores a shape: a reused digest's body is a later
        // completeness view of the same type, and later digests must drill
        // against the minter's first body rather than this variant.
        if (!reused)
          state.anonCanonShapes.try_emplace(digest,
                                            buildRecordShape(rec, digestCache));
      }
      if (known->second != name) {
        renames.push_back({rec, known->second});
        plan.anonRenames.emplace_back(name.str(), known->second);
        ++plan.anonCanonicalized;
      }
      continue;
    }

    auto [it, inserted] = knownBodyDigests.try_emplace(name, digest);
    if (inserted) {
      takenNames.insert(name);
      knownBodyShapes.try_emplace(name, buildRecordShape(rec, digestCache));
      knownBodyOwner.try_emplace(name, globalInputIdx);
      continue;
    }
    if (it->second == digest)
      continue; // Same name, same body: one type, no work.

    // Same name, different digest. That is not yet a disagreement: the digest
    // hashes nested records by body, so a nested record one input leaves
    // incomplete and another completes splits the parent even though the
    // nested record itself unifies and the layouts are identical. Splitting
    // the parent is not just bloat -- constructor and destructor can then
    // disagree about which variant an object has, and the destructor runs on
    // unconstructed memory. Unify when the bodies agree modulo completeness.
    if (auto shapeIt = knownBodyShapes.find(name);
        shapeIt != knownBodyShapes.end()) {
      llvm::DenseSet<mlir::Type> visiting;
      ToleranceMemo memo;
      DrillBudget budget;
      ToleranceVerdict verdict =
          matchRecordShape(shapeIt->second, rec, digestCache, knownBodyShapes,
                           knownBodyDigests, visiting, memo, budget);
      if (budget.exceeded)
        mlir::emitWarning(in.loc) << "tolerance drill for record '" << name
                                  << "' exceeded its visit budget; splitting";
      traceDrillVerdict("reconcile", rec, verdict);
      if (verdict.match && verdict.excused)
        continue; // Same type; only nested completeness differs. No rename.
    }

    // Same name, different body. Reuse an earlier variant if this is the
    // same disagreement again -- three inputs sharing a header that a fourth
    // contradicts should produce two types, not four. Compared by digest of
    // the pre-rename signature on both sides, so they line up.
    std::string newName =
        pickVariantName(name, digest, rec, in.loc, digestCache, state,
                        inputNames, globalInputIdx);
    plan.renames.emplace_back(name.str(), newName);
    renames.push_back({rec, std::move(newName)});
  }
}

/// Rewrite `input` for the names decideRecordNames picked.
static LogicalResult applyRecordNames(ModuleOp input, const InputRecords &in,
                                      ArrayRef<RecordRename> renames) {
  if (renames.empty())
    return success();

  MLIRContext *ctx = input.getContext();
  llvm::DenseMap<mlir::Type, mlir::Type> mapping;
  SmallVector<std::pair<cir::RecordType, cir::RecordType>> toComplete;
  for (const RecordRename &rename : renames) {
    auto renamed = cir::RecordType::get(
        ctx, StringAttr::get(ctx, rename.newName), rename.rec.getKind());
    mapping.try_emplace(rename.rec, renamed);
    toComplete.emplace_back(rename.rec, renamed);
  }

  // Records are handled explicitly rather than by recursion into their
  // sub-elements. A mutable recursive record would send the default walk
  // around its own cycle forever; returning a concrete answer for every
  // record stops the walk at each one.
  AttrTypeReplacer replacer;

  // Both handlers below replace their own sub-elements and hand back a
  // finished type, so both end with WalkResult::skip(). Returning a bare type
  // means advance(), which runs the generic sub-element rebuild *afterwards*
  // -- the very walk these handlers exist to replace.
  using Replaced = std::optional<std::pair<mlir::Type, mlir::WalkResult>>;
  auto done = [](mlir::Type t) -> Replaced {
    return std::make_pair(t, mlir::WalkResult::skip());
  };

  // cir.func types are handled explicitly, and only rebuilt when something
  // actually changed.
  //
  // FuncType's return is an OptionalParameter: a null return type *is* how CIR
  // spells void. The generic rebuild reconstructs through FuncType::get with
  // that null, so letting it run at all is a crash on any void function
  // reachable from a renamed record -- which is every real library.
  replacer.addReplacement([&](cir::FuncType fn) -> Replaced {
    SmallVector<mlir::Type> inputs;
    bool changed = false;
    for (mlir::Type in : fn.getInputs()) {
      inputs.push_back(replacer.replace(in));
      changed |= inputs.back() != in;
    }
    // Left null if it was null; replace() is never handed one.
    mlir::Type ret = fn.getOptionalReturnType();
    mlir::Type newRet = ret ? replacer.replace(ret) : ret;
    changed |= newRet != ret;
    if (!changed)
      return done(fn);
    return done(cir::FuncType::get(ctx, inputs, newRet, fn.getVarArg()));
  });

  replacer.addReplacement([&](cir::RecordType rec) -> Replaced {
    if (mlir::Type mapped = mapping.lookup(rec))
      return done(mapped);
    if (rec.getName())
      return done(rec); // Named, not renamed: mutated in place below.
    // Anonymous records are uniqued structurally and cannot be recursive, so
    // rebuilding one from mapped members terminates.
    SmallVector<mlir::Type> members;
    bool changed = false;
    for (mlir::Type member : rec.getMembers()) {
      members.push_back(replacer.replace(member));
      changed |= members.back() != member;
    }
    if (!changed)
      return done(rec);
    return done(cir::RecordType::get(ctx, members, rec.getPacked(),
                                     rec.getPadded(), rec.getKind()));
  });

  for (auto &[oldRec, newRec] : toComplete) {
    SmallVector<mlir::Type> members;
    for (mlir::Type member : oldRec.getMembers())
      members.push_back(replacer.replace(member));
    newRec.complete(members, oldRec.getPacked(), oldRec.getPadded());
  }

  for (cir::RecordType rec : in.records) {
    if (rec.isIncomplete() || mapping.count(rec))
      continue;
    SmallVector<mlir::Type> members;
    bool changed = false;
    for (mlir::Type member : rec.getMembers()) {
      members.push_back(replacer.replace(member));
      changed |= members.back() != member;
    }
    if (changed)
      rec.replaceMemberTypes(members);
  }

  // A renamed record must end up shaped exactly like the one it replaces.
  // Attributes carry their record type and their field list separately --
  // #cir.const_record is the common one -- and the generic rebuild pairs the
  // replaced type with the untouched fields. If the two disagree on how many
  // members there are, that rebuild asserts deep inside the uniquer with
  // nothing naming the record, so it is caught here instead.
  for (auto &[oldRec, newRec] : toComplete) {
    if (newRec.isIncomplete() ||
        oldRec.getMembers().size() != newRec.getMembers().size()) {
      mlir::emitError(in.loc)
          << "cannot rename record '" << oldRec.getName().getValue() << "' to '"
          << newRec.getName().getValue() << "': the replacement has "
          << (newRec.isIncomplete()
                  ? std::string("no body")
                  : std::to_string(newRec.getMembers().size()) + " members")
          << ", but the original has " << oldRec.getMembers().size();
      // Returning is the point: the rewrite below is the rebuild that would
      // assert on this, so reporting and continuing would just reach the
      // crash with an explanation printed above it.
      return failure();
    }
  }

  // What replaceElementsIn(op, attributes, no locations, types) replaces --
  // the attributes, the result types, the block argument types -- with the
  // attributes read as replaceElementsInAttrs does, not as a dictionary built
  // for every op.
  auto replaceType = [&](mlir::Type type) {
    mlir::Type replaced = replacer.replace(type);
    return replaced ? replaced : type;
  };
  input->walk([&](Operation *op) {
    replaceElementsInAttrs(op, replacer);
    for (OpResult result : op->getResults())
      result.setType(replaceType(result.getType()));
    for (Region &region : op->getRegions())
      for (Block &block : region)
        for (BlockArgument arg : block.getArguments())
          arg.setType(replaceType(arg.getType()));
  });
  return success();
}

/// A symbol as a message names it: demangled, with the mangled name after it
/// when the two differ, since the mangled name is what a symbol table shows.
static std::string displayName(StringRef symbol) {
  std::string demangled = llvm::demangle(symbol);
  if (demangled == symbol)
    return "'" + symbol.str() + "'";
  return "'" + demangled + "' (" + symbol.str() + ")";
}

/// What two copies disagree on, for a message: "type, body" and so on.
static std::string describeDifference(const ResolutionEvent &event) {
  SmallVector<StringRef, 3> parts;
  if (!event.sameType)
    parts.push_back("type");
  if (!event.sameAttributes)
    parts.push_back("attributes");
  if (!event.sameBody)
    parts.push_back("body");
  return llvm::join(parts, ", ");
}

/// Where each renamed record is defined, in its input and in the input that
/// keeps the name, and the first member the two definitions disagree on --
/// what a reader needs, since a record has no position of its own. Both
/// bodies are in the shared context by now: the kept one under the name, the
/// other under its new one.
static void describeRecordRenames(MLIRContext &context,
                                  ArrayRef<DictionaryAttr> recordLocations,
                                  MutableArrayRef<RecordRenameEvent> records) {
  for (RecordRenameEvent &rename : records) {
    auto locationsOf = [&](unsigned input) -> ArrayAttr {
      if (input >= recordLocations.size() || !recordLocations[input])
        return {};
      return recordLocations[input].getAs<ArrayAttr>(rename.from);
    };
    ArrayAttr locs = locationsOf(rename.input);
    ArrayAttr keptLocs = locationsOf(rename.keptInput);
    auto at = [](ArrayAttr list, size_t i) -> mlir::LocationAttr {
      if (!list || i >= list.size())
        return {};
      return dyn_cast<mlir::LocationAttr>(list[i]);
    };
    rename.loc = at(locs, 0);
    rename.keptLoc = at(keptLocs, 0);

    auto kept = cir::RecordType::get(
        &context, StringAttr::get(&context, rename.from), rename.kind);
    auto dropped = cir::RecordType::get(
        &context, StringAttr::get(&context, rename.to), rename.kind);
    if (kept.isIncomplete() || dropped.isIncomplete())
      continue;
    ArrayRef<mlir::Type> keptMembers = kept.getMembers();
    ArrayRef<mlir::Type> members = dropped.getMembers();
    size_t common = std::min(keptMembers.size(), members.size());
    size_t i = 0;
    while (i < common && keptMembers[i] == members[i])
      ++i;
    if (i == common && keptMembers.size() == members.size())
      continue;
    auto site = [&](ArrayRef<mlir::Type> list, ArrayAttr where,
                    mlir::LocationAttr record) -> DifferenceSite {
      if (i >= list.size())
        return {record, "nothing", std::string("none")};
      mlir::LocationAttr loc = at(where, i + 1);
      return {loc ? loc : record, "'" + printed(list[i], 120) + "'",
              sourceDescription(list[i])};
    };
    // The source has no member numbers -- bases and a vtable pointer are
    // members here -- so it is told what kind of difference this is.
    StringRef sourceWhat = i >= members.size()       ? "a member is missing"
                           : i >= keptMembers.size() ? "an extra member"
                                                     : "a member differs";
    DifferenceSite droppedSite = site(members, locs, rename.loc);
    if (i >= members.size())
      droppedSite.source.reset();
    rename.memberDifference =
        Difference{"member " + std::to_string(i + 1), sourceWhat.str(),
                   site(keptMembers, keptLocs, rename.keptLoc), droppedSite};
  }
}

/// A linkage as the source would say it.
static StringRef sourceLinkage(StringRef linkage) {
  if (linkage == "linkonce_odr" || linkage == "linkonce")
    return "inline";
  if (linkage == "weak_odr" || linkage == "weak")
    return "weak";
  return linkage;
}

/// The pair of notes that shows `difference`: where it is in the copy the
/// diagnostic is about, from `input`, and in the other one, from
/// `otherInput`, which `other` names ("the kept copy").
static void noteDifference(mlir::InFlightDiagnostic &diag,
                           const DiagnosticLocator &locator,
                           const Difference &difference, unsigned input,
                           unsigned otherInput, StringRef other) {
  mlir::Location here = locator.at(difference.dropped.loc, input);
  mlir::Location there = locator.at(difference.kept.loc, otherInput);
  if (DiagnosticLocator::showsCIR()) {
    diag.attachNote(here) << difference.what
                          << " differs here: " << difference.dropped.text;
    diag.attachNote(there) << other << " has: " << difference.kept.text;
    return;
  }
  // The same name on both sides is a type the two define differently.
  if (difference.dropped.source &&
      difference.dropped.source == difference.kept.source) {
    diag.attachNote(here) << difference.sourceWhat
                          << " here: " << *difference.dropped.source
                          << " is defined differently in each copy";
    diag.attachNote(there) << "and here in " << other;
    return;
  }
  mlir::Diagnostic &note = diag.attachNote(here)
                           << difference.sourceWhat << " here";
  if (difference.dropped.source)
    note << ": " << *difference.dropped.source;
  if (difference.kept.source)
    diag.attachNote(there) << other << " has: " << *difference.kept.source;
  else
    diag.attachNote(there) << other << " has this instead";
}

/// The attributes two copies disagree on, as a note lists them.
static std::string
describeAttributeDifferences(ArrayRef<AttributeDifference> differences) {
  SmallVector<std::string> parts;
  for (const AttributeDifference &d : differences) {
    if (!d.kept)
      parts.push_back("'" + d.name + "' only in this copy");
    else if (!d.dropped)
      parts.push_back("'" + d.name + "' only in the kept copy");
    else
      parts.push_back("'" + d.name + "' is " + *d.dropped + " here and " +
                      *d.kept + " in the kept copy");
  }
  return llvm::join(parts, "; ");
}

/// Decide each recorded choice's severity under `policy` and report the ones
/// that are not ignored, through MLIR diagnostics, in the order the choices
/// were made: records first -- they are settled before any symbol is merged --
/// then symbols. A choice is a finding when the copies disagree; two strong
/// definitions always are, and so is a renamed record. A finding is reported
/// at the dropped definition, with notes at the kept one and at what the
/// copies disagree on, in each; `locator` decides where those point. Returns
/// how many were reported as errors.
static unsigned reportResolutions(const DiagnosticLocator &locator,
                                  MutableArrayRef<SymbolResolutions> symbols,
                                  MutableArrayRef<RecordRenameEvent> records,
                                  const OdrPolicy &policy) {
  unsigned errors = 0;
  auto emit = [&](OdrSeverity severity, mlir::Location loc) {
    if (severity == OdrSeverity::Error)
      ++errors;
    switch (severity) {
    case OdrSeverity::Remark:
      return mlir::emitRemark(loc);
    case OdrSeverity::Warning:
      return mlir::emitWarning(loc);
    default:
      return mlir::emitError(loc);
    }
  };

  for (RecordRenameEvent &rename : records) {
    rename.severity = policy.byKind[recordRenamedKind];
    if (rename.severity == OdrSeverity::Ignore)
      continue;
    mlir::Location here = locator.at(rename.loc, rename.input);
    mlir::InFlightDiagnostic diag = emit(rename.severity, here);
    if (DiagnosticLocator::showsCIR())
      diag << "record '" << rename.from
           << "' has a different body than in an earlier input; renamed to '"
           << rename.to << "'";
    else
      diag << "'" << rename.from
           << "' is defined differently than in an earlier input";
    mlir::Location earlier = locator.at(rename.keptLoc, rename.keptInput);
    if (earlier != here)
      diag.attachNote(earlier) << "the earlier definition is here";
    if (rename.memberDifference)
      noteDifference(diag, locator, *rename.memberDifference, rename.input,
                     rename.keptInput, "the earlier definition");
  }

  for (SymbolResolutions &entry : symbols) {
    for (ResolutionEvent &event : entry.events) {
      bool agree = event.sameType && event.sameAttributes && event.sameBody;
      if (agree && event.kind != ResolutionKind::StrongStrong)
        continue;
      event.severity = policy.of(event.kind);
      if (event.severity == OdrSeverity::Ignore)
        continue;
      std::string kept = locator.name(event.kept.input);
      std::string dropped = locator.name(event.dropped.input);
      std::string differ = describeDifference(event);
      std::string name = displayName(entry.symbol);
      mlir::Location droppedLoc =
          locator.at(event.dropped.loc, event.dropped.input);
      mlir::InFlightDiagnostic diag = emit(event.severity, droppedLoc);
      switch (event.kind) {
      case ResolutionKind::WeakWeak:
        diag << name << " has different definitions in " << kept << " and "
             << dropped << " (they differ in " << differ
             << "); keeping the one from " << kept;
        break;
      case ResolutionKind::StrongOverWeak:
        diag << name << ": the strong definition from " << kept << " replaces "
             << withArticle(DiagnosticLocator::showsCIR()
                                ? StringRef(event.dropped.linkage)
                                : sourceLinkage(event.dropped.linkage))
             << " definition from " << dropped << " that differs in " << differ;
        break;
      case ResolutionKind::RTTIOverNoRTTI:
        diag << name << ": the copy from " << kept << " replaces the one from "
             << dropped << ", built with -fno-rtti, which differs in "
             << differ;
        break;
      case ResolutionKind::StrongStrong:
        diag << name << " defined in more than one input; keeping the one from "
             << kept << ", ignoring " << dropped;
        if (agree)
          diag << "; the definitions are identical";
        else
          diag << "; they differ in " << differ;
        break;
      }
      // Only where it adds something: the copies of a header-defined inline
      // function all point at the same line of the header.
      mlir::Location keptLoc = locator.at(event.kept.loc, event.kept.input);
      if (keptLoc != droppedLoc)
        diag.attachNote(keptLoc) << "the definition kept is here";
      for (const std::optional<Difference> &difference :
           {event.typeDifference, event.bodyDifference})
        if (difference)
          noteDifference(diag, locator, *difference, event.dropped.input,
                         event.kept.input, "the kept copy");
      // The attributes still compared once compile options are set aside
      // have no source spelling, so the source style leaves them out.
      if (DiagnosticLocator::showsCIR() && !event.attributeDifferences.empty())
        diag.attachNote() << "attributes that differ: "
                          << describeAttributeDifferences(
                                 event.attributeDifferences);
    }
  }
  return errors;
}

/// Opens `path` for writing, reporting a failure at it.
static std::unique_ptr<llvm::ToolOutputFile> openOutput(MLIRContext &context,
                                                        StringRef path) {
  std::error_code ec;
  auto file =
      std::make_unique<llvm::ToolOutputFile>(path, ec, llvm::sys::fs::OF_None);
  if (!ec)
    return file;
  mlir::emitError(fileLoc(&context, path))
      << "cannot open for writing: " << ec.message();
  return nullptr;
}

int main(int argc, char **argv) {
  InitLLVM y(argc, argv);
  // Writing a module's bitcode for LTO builds its symbol table, which parses
  // file-scope asm (ModuleSymbolTable) and so needs the target's asm parser;
  // without one a module with any -- libearlystartup.so has some -- asserts.
  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmParsers();
  cl::ParseCommandLineOptions(argc, argv, "CIR module linker\n");

  // --odr sets every kind, --odr-severity overrides one. --warn-odr-divergence
  // keeps meaning what it always has: at least a warning for two strong
  // definitions and for a renamed record, and nothing about the weak cases.
  OdrPolicy odrPolicy;
  odrPolicy.byKind.fill(odrSeverityDefault);
  if (warnOdrDivergence)
    for (size_t kind :
         {static_cast<size_t>(ResolutionKind::StrongStrong), recordRenamedKind})
      odrPolicy.byKind[kind] =
          std::max(odrPolicy.byKind[kind], OdrSeverity::Warning);
  for (const OdrKindSeverity &entry : odrSeverityByKind)
    odrPolicy.byKind[entry.kind] = entry.severity;

  if (timeTrace)
    llvm::timeTraceProfilerInitialize(timeTraceGranularity, "cir-link");
  // The trace has to be written even on the error paths, which return from a
  // dozen places, and it is the failing run whose timings are most worth
  // having.
  auto writeTimeTrace = llvm::make_scope_exit([&] {
    if (!llvm::timeTraceProfilerEnabled())
      return;
    std::string path = timeTraceFile;
    if (path.empty())
      path = (outputFilename == "-" ? std::string("cir-link")
                                    : std::string(outputFilename)) +
             ".time-trace.json";
    // Not through the shared context's diagnostics: the context is gone by
    // now when the link lowers to LLVM IR.
    if (llvm::Error err = llvm::timeTraceProfilerWrite(path, outputFilename))
      llvm::WithColor::warning() << "could not write the time trace: "
                                 << llvm::toString(std::move(err)) << "\n";
    llvm::timeTraceProfilerCleanup();
  });

  DialectRegistry registry;
  // CIRGen output is not pure CIR: the module carries a #dlti.dl_spec data
  // layout, and lowering-adjacent attributes can reference the LLVM dialect.
  // Registering them is required to parse a real -emit-cir module, not a
  // hand-written one.
  registry.insert<cir::CIRDialect, mlir::DLTIDialect, mlir::LLVM::LLVMDialect,
                  mlir::func::FuncDialect>();

  // One context for every input. This is what makes identified records unify
  // across translation units instead of being duplicated per input.
  // Held in an optional so it can be destroyed while main is still running:
  // once the module has been lowered, every byte the MLIR context is holding
  // is dead, and it is holding a lot of them.
  //
  // Its thread pool is as wide as the input window (-j, else 16), not the
  // machine: on a large library a 96-wide pool spent over a thousand
  // CPU-seconds contending on the context's locks in the parallel passes, for
  // no wall time a 16-wide one does not also get. Declared first, as the
  // context uses it to the end.
  std::optional<llvm::DefaultThreadPool> threadPool;
  std::optional<MLIRContext> contextStorage;
  contextStorage.emplace(registry, MLIRContext::Threading::DISABLED);
  if (jobs != 1) {
    threadPool.emplace(llvm::hardware_concurrency(jobs ? jobs : 16));
    contextStorage->setThreadPool(*threadPool);
  }
  MLIRContext &context = *contextStorage;
  // What the link reports, it reports as MLIR diagnostics on this context,
  // with the source line shown where the file can be read. Dropped with the
  // context.
  llvm::SourceMgr sourceMgr;
  std::optional<mlir::SourceMgrDiagnosticHandler> diagnostics;
  diagnostics.emplace(sourceMgr, &context);
  context.loadAllAvailableDialects();
  context.allowUnregisteredDialects(false);

  // The symbols the link's shared libraries define; see
  // deferDiscardableDataToOwners.
  llvm::StringSet<> sharedSymbolSet;
  if (!sharedLibrarySymbols.empty()) {
    auto buf = llvm::MemoryBuffer::getFile(sharedLibrarySymbols);
    if (!buf) {
      mlir::emitError(fileLoc(&context, sharedLibrarySymbols))
          << "cannot read the shared-library symbols: "
          << buf.getError().message();
      return 1;
    }
    SmallVector<StringRef> lines;
    (*buf)->getBuffer().split(lines, '\n', /*MaxSplit=*/-1,
                              /*KeepEmpty=*/false);
    for (StringRef line : lines)
      sharedSymbolSet.insert(line.trim());
  }
  DiagnosticLocator locator(context, inputFilenames);

  // Only the shared context needs the LLVM IR translation interfaces, and only
  // when we are going to use them. Putting them in the registry the per-input
  // contexts are built from would make all of those load every dialect that
  // has a translation, hundreds of times over, for nothing.
  if (emitLLVM) {
    DialectRegistry translations;
    cir::registerCIRToLLVMIRTranslations(translations);
    context.appendDialectRegistry(translations);
    context.loadAllAvailableDialects();
  }

  // The merged module borrows its location from the first input so that
  // diagnostics point somewhere real.
  OwningOpRef<ModuleOp> merged;
  // Whether any input is C++, which makes the merged module C++.
  bool anyCXX = false;
  // Constructor, destructor and annotation entries, each with the input it came
  // from, under the names the merged module gives their symbols. Written to the
  // merged module once all inputs are in, when it is known whose definition of
  // each comdat key survived; see installInitializerLists.
  struct AccumulatedEntry {
    size_t input;
    mlir::Attribute entry;
  };
  SmallVector<AccumulatedEntry> ctorEntries, dtorEntries, annotationEntries;
  // For each comdat key, the first input with an entry keyed on it: the owner
  // when no definition of the key is left in the merged module.
  llvm::StringMap<size_t> keyFirstInput;
  // Hidden linkonce functions given their -fno-exceptions name in some input;
  // see redirectToNoExceptionsCopies.
  llvm::StringSet<> noExceptionsRenamed;
  // Inputs built without RTTI data; see the weak-vs-weak case below.
  llvm::BitVector noRTTIInputs(inputFilenames.size());
  unsigned duplicateInitsDropped = 0;

  // Whether the inputs arrived as bytecode, which is what the output format
  // defaults to. Any text input is enough to make the output text: a mixed
  // link is a hand-run one, and the readable answer is the useful one there.
  bool inputWasBytecode = !inputFilenames.empty();

  llvm::StringMap<Resolution> resolved;
  SmallVector<Divergence> divergences;
  // Every choice between two definitions, when asked for; see ResolutionKind.
  // Recording compares the two copies before one is dropped, and never
  // changes which one survives.
  const bool recordResolutions = warnOdrDivergence || !odrEventsFile.empty() ||
                                 odrPolicy.reportsAnything();
  std::vector<SymbolResolutions> symbolResolutions;
  std::vector<RecordRenameEvent> recordRenames;
  /// Each input's cir.record_locations, for the renamed records.
  std::vector<DictionaryAttr> recordLocations(inputFilenames.size());
  // The local symbols merged so far, under the names the merged module gives
  // them: references to them are what DefinitionComparer excuses.
  llvm::StringSet<> localSymbolNames;
  DefinitionComparer comparer(context, localSymbolNames);
  unsigned weakCopiesDiffer = 0;
  llvm::BitVector noExceptionsInputs(inputFilenames.size());
  unsigned symbolsMerged = 0, definitionsKept = 0, declarationsDropped = 0;
  // Where the next rename of a local of each name starts its search; see
  // renameLocalsInBatch.
  llvm::StringMap<unsigned> nextRenameSuffix;
  unsigned localsRenamed = 0, weakDeduped = 0, weakReplaced = 0,
           noRTTIReplaced = 0;
  unsigned deferredToOwners = 0;

  // The two retention lists, accumulated across every input and written back
  // once at the end.
  //
  // They are per-TU lists like cir.global_ctors, but they cannot be handled
  // by simply concatenating the module attributes the way that one is, for two
  // reasons. Their entries are *symbol references*, so a local renamed out of
  // a collision has to be followed, and an entry copied verbatim would end up
  // naming whichever TU won the name -- retaining the wrong object while
  // reading as if it worked. And the same symbol legitimately appears in the
  // lists of every TU that saw it, so they have to be deduplicated rather than
  // appended; a 69-input link produced a 134-entry list with a handful of
  // distinct names in it.
  //
  // A SetVector, not a StringSet: the output has to be byte-identical across
  // runs, and iteration order over a hash set is not. It is backed by
  // std::set rather than the default DenseSet because std::string has no
  // DenseMapInfo, and the entries have to own their storage -- the input
  // module a name was read from is destroyed long before the list is written.
  using RetentionList = llvm::SetVector<std::string, std::vector<std::string>,
                                        std::set<std::string>>;
  RetentionList usedSyms;
  RetentionList compilerUsedSyms;

  // Follow a rename into the retention lists. Safe to apply to both without
  // checking which one held the name, because a symbol may be in either or
  // both and the new name means the same thing in each.
  auto renameRetained = [&](StringRef from, StringRef to) {
    for (RetentionList *list : {&usedSyms, &compilerUsedSyms})
      if (list->remove(from.str()))
        list->insert(to.str());
  };

  // Follow a local's step-aside rename into the entries of the input it came
  // from. Other inputs' entries under the old name -- above all the arriving
  // external symbol's own, accumulated before its input's ops move -- keep it.
  auto renameAccumulated = [&](size_t input, StringRef from, StringRef to) {
    auto follow = [&](mlir::StringAttr name) {
      return name && name.getValue() == from ? StringAttr::get(&context, to)
                                             : name;
    };
    for (AccumulatedEntry &acc : ctorEntries)
      if (acc.input == input)
        if (auto ctor = dyn_cast<cir::GlobalCtorAttr>(acc.entry))
          acc.entry = cir::GlobalCtorAttr::get(follow(ctor.getName()),
                                               ctor.getPriority(),
                                               follow(ctor.getAssociatedData()));
    for (AccumulatedEntry &acc : dtorEntries)
      if (acc.input == input)
        if (auto dtor = dyn_cast<cir::GlobalDtorAttr>(acc.entry))
          acc.entry = cir::GlobalDtorAttr::get(follow(dtor.getName()),
                                               dtor.getPriority(),
                                               follow(dtor.getAssociatedData()));
    for (AccumulatedEntry &acc : annotationEntries) {
      auto pair = dyn_cast<ArrayAttr>(acc.entry);
      if (acc.input != input || !pair || pair.size() != 2)
        continue;
      if (auto symbol = dyn_cast<StringAttr>(pair[0]))
        acc.entry = ArrayAttr::get(&context, {follow(symbol), pair[1]});
    }
  };

  TypeUnificationState typeState;
  RecordABIFacts recordABIFacts;
  // What the merge drops -- a declaration of something already present, a
  // duplicate definition -- is detached from its module at once and destroyed
  // a window later on a thread of its own: erasing a function body is most of
  // the cost of dropping it, and none of it is needed by the merge. A
  // top-level op is isolated from above, so destroying a detached one touches
  // nothing outside it: no attribute, type or location is reference-counted,
  // and MLIR's own parallel passes erase ops in one context the same way.
  std::vector<Operation *> graveyard;
  std::future<void> burying;
  auto bury = [&](Operation *op) {
    op->remove();
    graveyard.push_back(op);
  };
  auto buryWindow = [&] {
    if (burying.valid())
      burying.get();
    if (!context.isMultithreadingEnabled()) {
      for (Operation *op : graveyard)
        op->erase();
      graveyard.clear();
      return;
    }
    burying = std::async(std::launch::async, [ops = std::move(graveyard)] {
      for (Operation *op : ops)
        op->erase();
    });
    graveyard.clear();
  };
  // Record one choice between two definitions of `name`. Runs before either
  // copy is buried or edited, and on this thread: the bury thread runs while
  // the next window completes records in the shared context, and comparing
  // types there would race with it. Identical weak copies are only counted.
  auto recordResolution = [&](ResolutionKind kind, StringRef name,
                              Resolution &res, Operation *kept,
                              unsigned keptInput, Operation *dropped,
                              unsigned droppedInput) {
    if (!recordResolutions)
      return;
    llvm::TimeTraceScope recordScope("recordResolution");
    DefinitionComparer::Result same = comparer.compare(kept, dropped);
    bool identical = same.sameType && same.sameAttributes && same.sameBody;
    if (kind == ResolutionKind::WeakWeak) {
      if (identical) {
        ++res.equalWeakCopies;
        return;
      }
      ++weakCopiesDiffer;
    }
    if (res.recordedAt < 0) {
      res.recordedAt = static_cast<int>(symbolResolutions.size());
      SymbolResolutions &entry = symbolResolutions.emplace_back();
      entry.symbol = name.str();
      entry.opName = kept->getName().getStringRef().str();
    }
    SymbolResolutions &entry = symbolResolutions[res.recordedAt];
    if (entry.digestedKept != kept) {
      entry.keptDigest = comparer.digest(kept);
      entry.digestedKept = kept;
    }
    ResolutionEvent &event = entry.events.emplace_back();
    event.kind = kind;
    event.kept = {keptInput, linkageName(kept), entry.keptDigest,
                  kept->getLoc()};
    event.dropped = {droppedInput, linkageName(dropped),
                     identical ? entry.keptDigest : comparer.digest(dropped),
                     dropped->getLoc()};
    event.sameType = same.sameType;
    event.sameAttributes = same.sameAttributes;
    event.sameBody = same.sameBody;
    event.typeDifference = std::move(same.type);
    event.bodyDifference = std::move(same.body);
    event.attributeDifferences = std::move(same.attributes);
  };
  /// record name -> digest of the body actually handed to the shared context.
  /// Digests, not text, for the same reason as the unification state above.
  llvm::StringMap<std::string> transferredBodyDigests;
  /// record name -> compact body shape, for the same completeness-tolerant
  /// comparison the unification stage does: decideRecordNames may unify two
  /// bodies that differ only by nested completeness, and this check must
  /// accept what unification promised.
  llvm::StringMap<RecordShape> transferredBodyShapes;
  unsigned recordsRenamed = 0;
  unsigned anonCanonicalized = 0;

  // Inputs are prepared a window at a time, in parallel, and then merged in
  // order. The merge itself cannot be parallel -- it threads the record
  // unification state and the symbol table from one input to the next, and the
  // output has to be byte-identical run to run -- but opening, extracting and
  // parsing an input needs nothing from any other input.
  //
  // A window rather than the whole list because each prepared input holds a
  // parsed module and its context: all 380 of a real library at once does not
  // fit, and the peak is already 1.8 GB.
  size_t window = 1;
  if (jobs)
    window = jobs;
  else if (context.isMultithreadingEnabled())
    window = std::min<size_t>(context.getThreadPool().getMaxConcurrency(), 16);
  if (window == 1)
    context.disableMultithreading();

  const size_t inputCount = inputFilenames.size();

  // Parsing an input and rewriting its records touch only its own context, so
  // those stages run on a pool of their own: on the shared context's pool they
  // took threads from the transfer and the merge, and the overlap between the
  // stages bought little. Twice the window, as two stages share it. Work on
  // the shared context stays on its pool, whose width bounds the contention
  // on its locks. Only when the shared context is multithreaded: otherwise
  // every stage runs in place.
  std::optional<llvm::DefaultThreadPool> privatePool;
  if (threadPool && context.isMultithreadingEnabled())
    privatePool.emplace(llvm::hardware_concurrency(2 * window));
  auto privateParallelFor = [&](size_t count,
                                llvm::function_ref<void(size_t)> fn) {
    if (!privatePool) {
      for (size_t i = 0; i < count; ++i)
        fn(i);
      return;
    }
    llvm::ThreadPoolTaskGroup tasks(*privatePool);
    for (size_t i = 0; i < count; ++i)
      tasks.async([fn, i] { fn(i); });
    tasks.wait();
  };

  // The record unification of each input; see unifyRecordNames.
  struct RecordWork {
    InputRecords in;
    RenamePlan plan;
    SmallVector<RecordRename> renames;
    /// After the renames: the records as they will be transferred, and their
    /// digests as written, for checkTransferredBodies.
    SmallVector<cir::RecordType> transferred;
    std::unique_ptr<RecordDigestCache> transferDigests;
    bool failed = false;
  };
  // One window's front half: everything before the merge. It depends on the
  // earlier inputs only through the record unification state, which nothing
  // in the merge touches, so the next window's front half runs while this one
  // is merged. What it reports is kept in its inputs' diagnostics and
  // printed when its window is merged, in input order.
  struct FrontWindow {
    std::vector<PreparedInput> slots;
    std::vector<RecordWork> work;
    /// Which wave of the transfer each input goes in; see transferWindow.
    std::vector<unsigned> wave;
    /// How many of the inputs' diagnostics to print: all of them, unless the
    /// window failed, and then up to the input that failed it, as if the
    /// inputs had been handled one by one.
    size_t reported = 0;
    /// Records renamed because an input's body differed, for the merge to
    /// record; filled on the unification thread, read on the merge thread.
    std::vector<RecordRenameEvent> recordRenames;
    bool failed = false;
  };
  // Opening, parsing and hashing a window needs nothing from any other input,
  // so it runs a window ahead of the rest of the front half: the next window
  // is parsed while this one is unified and transferred.
  auto prepareWindow = [&](size_t windowBase) -> std::unique_ptr<FrontWindow> {
    auto front = std::make_unique<FrontWindow>();
    const size_t windowSize = std::min<size_t>(window, inputCount - windowBase);
    front->slots.resize(windowSize);
    front->work.resize(windowSize);
    llvm::TimeTraceScope prepareScope("prepareWindow");
    privateParallelFor(windowSize, [&](size_t k) {
      PreparedInput &prep = front->slots[k];
      prepareInput(inputFilenames[windowBase + k], registry, prep);
      if (!prep.failed)
        analyzeRecords(*prep.parsed, inputFilenames[windowBase + k],
                       front->work[k].in);
    });
    return front;
  };
  const bool overlap = context.isMultithreadingEnabled();
  const bool tracing = llvm::timeTraceProfilerEnabled();
  // On a thread of its own when the shared context is multithreaded, traced
  // like the main thread; otherwise when its result is asked for.
  auto launch = [&](auto fn) {
    using Result = decltype(fn());
    if (!overlap)
      return std::async(std::launch::deferred, fn);
    return std::async(std::launch::async, [&tracing, fn]() -> Result {
      if (tracing)
        llvm::timeTraceProfilerInitialize(timeTraceGranularity, "cir-link");
      Result result = fn();
      if (tracing)
        llvm::timeTraceProfilerFinishThread();
      return result;
    });
  };
  // Only the unification stage touches this: each window's unification takes
  // its window's preparation and starts the next one's.
  std::future<std::unique_ptr<FrontWindow>> nextPrepared;
  auto unifyWindow = [&](size_t windowBase) -> std::unique_ptr<FrontWindow> {
    std::unique_ptr<FrontWindow> front = nextPrepared.get();
    if (windowBase + window < inputCount)
      nextPrepared =
          launch([&, next = windowBase + window] { return prepareWindow(next); });
    const size_t windowSize = front->slots.size();
    front->reported = windowSize;
    auto fail = [&](size_t k) {
      front->failed = true;
      front->reported = k + 1;
      return std::move(front);
    };
    std::vector<PreparedInput> &slots = front->slots;
    std::vector<RecordWork> &work = front->work;

    // The names are decided in input order, one input at a time: each input's
    // decisions depend on every earlier input's. Up to the first input that
    // failed to prepare, whose error is reported once the inputs before it
    // have been checked, as if they had been handled one by one.
    size_t decided = 0;
    {
      llvm::TimeTraceScope unifyScope("unifyRecordNames");
      // Views into the decided inputs of this window for the fixpoint's
      // same-window winner reads. Rebuilt per input: the rename lists they
      // view grow as their inputs' fixpoints append.
      SmallVector<FixpointWindowInput, 16> windowViews;
      for (; decided < windowSize && !slots[decided].failed; ++decided) {
        RecordWork &w = work[decided];
        // Any text input makes the output text; see where it is written.
        inputWasBytecode &= slots[decided].sawBytecode;
        unsigned globalIdx = windowBase + decided;
        decideRecordNames(w.in, typeState, w.plan, w.renames, globalIdx);
        windowViews.clear();
        for (size_t j = 0; j < decided; ++j)
          windowViews.push_back({work[j].in.ordered, work[j].renames});
        propagateRecordSplits(w.in, w.plan, w.renames, typeState,
                              transferredBodyShapes, windowViews, windowBase,
                              globalIdx);
        recordsRenamed += w.plan.renames.size();
        anonCanonicalized += w.plan.anonCanonicalized;
      }
    }

    {
      llvm::TimeTraceScope applyScope("applyRecordNames");
      privateParallelFor(decided, [&](size_t k) {
        PreparedInput &prep = slots[k];
        RecordWork &w = work[k];
        if (failed(applyRecordNames(*prep.parsed, w.in, w.renames))) {
          w.failed = true;
          return;
        }
        // The record ABI table is keyed by record name, so it follows the names
        // this input's records were given, collisions and anonymous
        // canonicalizations alike.
        if (!work[k].plan.renames.empty() || !work[k].plan.anonRenames.empty())
          if (auto table =
                  prep.parsed->getOperation()->getAttrOfType<DictionaryAttr>(
                      cir::CIRDialect::getRecordABIFactsAttrName())) {
            llvm::StringMap<StringRef> renamedTo;
            for (auto &[from, to] : work[k].plan.renames)
              renamedTo[from] = to;
            for (auto &[from, to] : work[k].plan.anonRenames)
              renamedTo[from] = to;
            // Keyed by the new name, with bits OR-ed as the merge does: two
            // entries never land on one name today, since a rename never takes
            // a name the input uses, but a dictionary must not get duplicate
            // keys if one ever did.
            llvm::MapVector<StringAttr, uint64_t> renamedFacts;
            for (NamedAttribute entry : table) {
              auto it = renamedTo.find(entry.getName().getValue());
              StringAttr newName =
                  it == renamedTo.end()
                      ? entry.getName()
                      : StringAttr::get(prep.parsed->getContext(), it->second);
              if (auto bits = dyn_cast<mlir::IntegerAttr>(entry.getValue()))
                renamedFacts[newName] |= bits.getValue().getZExtValue();
            }
            SmallVector<NamedAttribute> entries;
            for (auto &[newName, bits] : renamedFacts)
              entries.emplace_back(
                  newName, mlir::IntegerAttr::get(
                               mlir::IntegerType::get(prep.parsed->getContext(), 32),
                               bits));
            prep.parsed->getOperation()->setAttr(
                cir::CIRDialect::getRecordABIFactsAttrName(),
                DictionaryAttr::get(prep.parsed->getContext(), entries));
          }
        llvm::SetVector<cir::RecordType> transformed;
        collectNamedRecords(prep.parsed->getOperation(), transformed);
        w.transferred.assign(transformed.begin(), transformed.end());
        w.transferDigests =
            std::make_unique<RecordDigestCache>(false, w.transferred);

        // The bytecode the transfer stage reads into the shared context,
        // written here because it needs only the input's own context and this
        // stage has the lighter load. The context itself stays until the
        // transfer: the check below still reads it.
        llvm::TimeTraceScope writeScope("writeBytecode");
        llvm::raw_string_ostream os(prep.transfer);
        if (failed(mlir::writeBytecodeToFile(prep.parsed->getOperation(), os))) {
          // The check stops the link on the error before any transfer; the
          // partial bytes are not kept either way.
          prep.transfer.clear();
          mlir::emitError(w.in.loc)
              << "cannot write the input as bytecode for the shared context";
          w.failed = true;
        }
      });
    }

    // The inputs of a window are read into the shared context in parallel, and
    // a record is completed there by the first input to bring its body: until
    // that input is read, the record is incomplete, and another input of the
    // window reading it at the same time would read it while it is completed.
    // So each new record is owned by the input that brings its body, and an
    // input that names a record another input of the window owns is
    // transferred in a later wave than its owner (see transferWindow).
    llvm::StringMap<unsigned> recordOwner;
    front->wave.assign(windowSize, 0);
    for (size_t k = 0; k < decided; ++k) {
      StringRef filename = inputFilenames[windowBase + k];
      llvm::TimeTraceScope inputScope("input", filename);
      RecordWork &w = work[k];
      if (w.failed)
        return fail(k);
      if (recordResolutions)
        for (auto &[from, to] : w.plan.renames) {
          RecordRenameEvent &rename =
              front->recordRenames.emplace_back(RecordRenameEvent{
                  static_cast<unsigned>(windowBase + k), from, to});
          rename.keptInput = typeState.knownBodyOwner.lookup(from);
          for (const RecordRename &r : w.renames)
            if (r.newName == to)
              rename.kind = r.rec.getKind();
        }

      // What decideRecordNames decided is checked against what earlier inputs
      // actually put in the shared context, rather than against the bodies they
      // arrived with. Those are not the same thing -- renaming and anonymous
      // canonicalization both rewrite a body after it has been compared -- and
      // when they disagree the destination context rejects the record with a
      // bare "member type mismatch" naming a header line and nothing else.
      //
      // Compared literally, not through normalizeAnonNames: that normalization
      // exists to decide whether two records are the *same type*, whereas what
      // matters here is whether the destination context can hold both bodies
      // under one name, and it uniques on the body as written. Normalizing here
      // would hide exactly the disagreement this check is for.
      {
        llvm::TimeTraceScope bodyScope("checkTransferredBodies");
        RecordDigestCache &transferCache = *w.transferDigests;
        bool diverged = false;
        for (cir::RecordType rec : w.transferred) {
          StringRef recName = rec.getName().getValue();
          if (auto owner = recordOwner.find(recName);
              owner != recordOwner.end() && owner->second != k)
            front->wave[k] =
                std::max(front->wave[k], front->wave[owner->second] + 1);
          if (rec.isIncomplete())
            continue;
          const std::string &digest = transferCache.digestRecord(rec);
          auto [it, inserted] =
              transferredBodyDigests.try_emplace(recName, digest);
          if (inserted) {
            recordOwner.try_emplace(recName, k);
            transferredBodyShapes.try_emplace(
                recName, buildRecordShape(rec, transferCache));
          }
          if (!inserted && it->second != digest) {
            // Same tolerance as unification: an incomplete nested record on
            // either side excuses the difference, and the transfer itself
            // completes it in the shared context.
            bool tolerated = [&] {
              auto shapeIt = transferredBodyShapes.find(recName);
              if (shapeIt == transferredBodyShapes.end())
                return false;
              llvm::DenseSet<mlir::Type> visiting;
              ToleranceMemo memo;
              DrillBudget budget;
              ToleranceVerdict verdict = matchRecordShape(
                  shapeIt->second, rec, transferCache, transferredBodyShapes,
                  transferredBodyDigests, visiting, memo, budget);
              if (budget.exceeded)
                mlir::emitWarning(w.in.loc)
                    << "tolerance drill for record '" << recName
                    << "' exceeded its visit budget; splitting";
              traceDrillVerdict("transfer", rec, verdict);
              return verdict.match && verdict.excused;
            }();
            if (tolerated)
              continue;
            // The body is not retained -- only its digest -- so the message
            // shows the canonical form of this input's side instead of the
            // body itself. Nested records appear as (name, digest) pairs,
            // which is what the comparison actually saw.
            mlir::InFlightDiagnostic diag =
                mlir::emitError(w.in.loc)
                << "record '" << recName
                << "' does not match the body an earlier input put under "
                   "that name";
            diag.attachNote()
                << "its body here, in canonical form: "
                << StringRef(transferCache.diagnosticString(rec)).substr(0, 200)
                << "...";
            diverged = true;
          }
        }
        // Stopping here is the difference between a diagnostic and a fix: the
        // transfer below is where the shared context rejects the record, and
        // letting it run turns the message above into a preamble to the
        // unhelpful error it was written to replace.
        if (diverged)
          return fail(k);
      }
      w = RecordWork();
    }
    if (decided < windowSize)
      return fail(decided);

    return front;
  };
  // Likewise only the transfer stage touches this.
  std::future<std::unique_ptr<FrontWindow>> nextUnified;
  auto transferWindow = [&](size_t windowBase) -> std::unique_ptr<FrontWindow> {
    std::unique_ptr<FrontWindow> front = nextUnified.get();
    if (front->failed)
      return front;
    if (windowBase + window < inputCount)
      nextUnified =
          launch([&, next = windowBase + window] { return unifyWindow(next); });
    const size_t windowSize = front->slots.size();
    std::vector<PreparedInput> &slots = front->slots;
    // Ops cannot move between contexts, so each module is handed over as
    // bytecode. Text would work too and be far slower; the point of the
    // bytecode support is exactly this kind of transfer.
    //
    // The whole window is transferred at once. Both halves are per-input work
    // with no shared state -- MLIR's uniquer is sharded and takes the parallel
    // traffic, which is what lets many modules be read into one context at the
    // same time -- and between them they were a third of the link.
    //
    // The private context is dropped as soon as its bytes are out, so the
    // window does not hold two representations of every module at its peak.
    //
    // In waves: an input waits for the inputs that own the records it names;
    // see where the waves are assigned.
    llvm::TimeTraceScope transferScope("transferWindow");
    const unsigned waves =
        front->wave.empty()
            ? 0
            : *std::max_element(front->wave.begin(), front->wave.end()) + 1;
    for (unsigned wave = 0; wave != waves; ++wave) {
      (void)mlir::parallelFor(&context, 0, windowSize, [&](size_t k) {
        PreparedInput &prep = slots[k];
        if (front->wave[k] != wave || prep.failed)
          return;
        // Written in the unification stage; see there.
        prep.parsed = nullptr;
        prep.diagnostics->handler.reset();
        prep.context.reset();
        prep.embedded.reset();
        {
          llvm::TimeTraceScope reparseScope("reparseShared");
          // Not verified again: the module was verified when it was parsed
          // into its own context, which is where a broken input is reported
          // with its name, and the merged module is verified as a whole.
          prep.inShared = parseSourceString<ModuleOp>(
              StringRef(prep.transfer),
              ParserConfig(&context, /*verifyAfterParse=*/false));
        }
        prep.transfer.clear();
        prep.transfer.shrink_to_fit();
        // The parser reports why, on the shared context; the merge says
        // which input when it reaches it.
        prep.failed = !prep.inShared;
        if (prep.failed)
          return;
        // Per-input, so here rather than in the merge.
        llvm::TimeTraceScope localizeScope("localizeTUAttributes");
        prep.noCXXExceptions = prep.inShared->getOperation()->hasAttr(
            cir::CIRDialect::getNoCXXExceptionsAttrName());
        localizeTUAttributes(*prep.inShared, prep.noExceptionsRenamed);
      });
    }
    return front;
  };
  // The front half is three stages, each a window ahead of the next and each
  // on a thread of its own: parsing, record unification, and the transfer
  // into the shared context, which runs while the main thread merges the
  // window before. Overlapping the merge needs the shared context
  // multithreaded: the transfer reads modules into it while the merge edits
  // the merged module.
  auto startFront = [&](size_t windowBase) {
    return launch([&, windowBase] { return transferWindow(windowBase); });
  };
  std::future<std::unique_ptr<FrontWindow>> nextFront;
  if (inputCount) {
    nextPrepared = launch([&] { return prepareWindow(0); });
    nextUnified = launch([&] { return unifyWindow(0); });
    nextFront = startFront(0);
  }
  for (size_t windowBase = 0; windowBase < inputCount; windowBase += window) {
    std::unique_ptr<FrontWindow> front;
    {
      llvm::TimeTraceScope waitScope("waitFront");
      front = nextFront.get();
    }
    if (!front->failed && windowBase + window < inputCount)
      nextFront = startFront(windowBase + window);
    for (size_t k = 0; k < front->reported; ++k)
      llvm::errs() << front->slots[k].diagnostics->text;
    llvm::append_range(recordRenames, front->recordRenames);
    if (front->failed)
      return 1;
    const size_t windowSize = front->slots.size();
    std::vector<PreparedInput> &slots = front->slots;

    for (size_t k = 0; k < windowSize; ++k) {
      const size_t idx = windowBase + k;
      StringRef filename = inputFilenames[idx];
      PreparedInput &prep = slots[k];
      llvm::TimeTraceScope mergeInputScope("mergeInput", filename);
      if (prep.failed) {
        mlir::emitError(fileLoc(&context, filename))
            << "cannot read the input back into the shared context after "
               "record unification";
        return 1;
      }

      OwningOpRef<ModuleOp> &input = prep.inShared;

      if (auto lang = input->getOperation()->getAttrOfType<cir::SourceLanguageAttr>(
              cir::CIRDialect::getSourceLanguageAttrName()))
        anyCXX |= lang.getValue() == cir::SourceLanguage::CXX;
      if (std::optional<StringRef> source = input->getSymName())
        locator.setSource(idx, *source);
      // Read here and nowhere else: the merged module has no use for it, and
      // a module attribute left on an input must merge.
      if (auto locations = input->getOperation()->removeAttr(
              cir::CIRDialect::getRecordLocationsAttrName());
          locations && recordResolutions)
        recordLocations[idx] = dyn_cast<DictionaryAttr>(locations);
      // localizeTUAttributes ran in the transfer.
      for (const auto &name : prep.noExceptionsRenamed)
        noExceptionsRenamed.insert(name.getKey());
      if (input->getOperation()->removeAttr(
              cir::CIRDialect::getNoRTTIAttrName()))
        noRTTIInputs.set(idx);
      if (prep.noCXXExceptions)
        noExceptionsInputs.set(idx);

      if (!merged) {
        merged = OwningOpRef<ModuleOp>(
            ModuleOp::create(input->getLoc(), input->getName()));
        // Module-level attributes (triple, data layout, opt info) come from the
        // first input. Checking that later inputs agree is a separate concern:
        // a mismatched triple is a build error, not an ODR question, and it
        // belongs wherever the build system assembles the input list.
        merged->getOperation()->setAttrs(input->getOperation()->getAttrs());
        if (mlir::Attribute table = merged->getOperation()->getAttr(
                cir::CIRDialect::getRecordABIFactsAttrName()))
          recordABIFacts.add(table);
        // Except the constructor and destructor lists, which every input
        // contributes to, the first included, once its names are final; see
        // below.
        merged->getOperation()->removeAttr(
            cir::CIRDialect::getGlobalCtorsAttrName());
        merged->getOperation()->removeAttr(
            cir::CIRDialect::getGlobalDtorsAttrName());
        merged->getOperation()->removeAttr(
            cir::CIRDialect::getGlobalAnnotationsAttrName());
        // And the markers an older CIRGen set; see mergeModuleAttributes.
        for (NamedAttribute attr : input->getOperation()->getAttrs())
          if (attr.getName().getValue().starts_with("cir.has_"))
            merged->getOperation()->removeAttr(attr.getName());
      } else {
        llvm::TimeTraceScope attrsScope("mergeModuleAttributes");
        if (failed(mergeModuleAttributes(*merged, *input, filename,
                                         recordABIFacts)))
          return 1;
      }

      Block *dst = merged->getBody();

      // Rename every colliding local *before* moving anything.
      //
      // This has to be a separate pass over the whole input. Renaming rewrites
      // uses within the input module, so if a caller has already been moved
      // into the output by the time its callee is renamed, that call site is
      // never updated and silently keeps pointing at the symbol the earlier
      // input supplied. It depends purely on declaration order, so it is
      // invisible until it is not: two TUs with anonymous-namespace helpers of
      // the same name produced a program that linked, built, ran, and called
      // the wrong constructor.
      std::optional<llvm::TimeTraceScope> renameScope;
      renameScope.emplace("renameLocals");
      llvm::StringMap<std::string> renamedInInput;
      localsRenamed += renameLocalsInBatch(*input, resolved, nextRenameSuffix,
                                           renamedInInput);
      std::optional<llvm::TimeTraceScope> listsScope;
      listsScope.emplace("collectInputLists");

      // Collect this input's retention lists, after the rename pass above so
      // the names are the ones the merged module will use.
      //
      // The rename map is applied unconditionally rather than relying on
      // replaceAllSymbolUses to have rewritten the module's own attribute
      // dictionary: whether a module-level symbol reference counts as a "use"
      // is an MLIR implementation detail, and this does the right thing either
      // way -- a name already rewritten is not a key in the map.
      auto collectRetained = [&](StringRef listName, RetentionList &dest) {
        auto incoming =
            input->getOperation()->getAttrOfType<ArrayAttr>(listName);
        if (!incoming)
          return;
        for (mlir::Attribute entry : incoming) {
          auto ref = dyn_cast<FlatSymbolRefAttr>(entry);
          if (!ref)
            continue;
          auto renamed = renamedInInput.find(ref.getValue());
          dest.insert(renamed == renamedInInput.end() ? ref.getValue().str()
                                                      : renamed->second);
        }
      };
      collectRetained(cir::CIRDialect::getLLVMUsedAttrName(), usedSyms);
      collectRetained(cir::CIRDialect::getLLVMCompilerUsedAttrName(),
                      compilerUsedSyms);

      // Append this input's constructors and destructors, under the names the
      // merged module will use.
      //
      // Not every module attribute is TU-invariant configuration:
      // cir.global_ctors and cir.global_dtors are per-TU lists, and taking only
      // the first input's copy drops every other TU's static initializers.
      //
      // And an entry names its function with a plain string, not a symbol
      // reference, so the rename above does not reach it. The functions are
      // internal and CIRGen numbers them per TU -- every TU has its
      // __cxx_global_var_init -- so a later input's are renamed, and an entry
      // appended as written names the earlier input's function instead. Its
      // own initializer is then never called: generated per-class registration,
      // a template static member initialized in whichever TUs use the class,
      // was lost for every class that a later TU used first, and 45 of 53 JNI
      // classes in one library were never registered.
      auto renamedName = [&](mlir::StringAttr name) -> mlir::StringAttr {
        if (!name)
          return name;
        auto renamed = renamedInInput.find(name.getValue());
        return renamed == renamedInInput.end()
                   ? name
                   : mlir::StringAttr::get(&context, renamed->second);
      };
      for (StringRef listName : {cir::CIRDialect::getGlobalCtorsAttrName(),
                                 cir::CIRDialect::getGlobalDtorsAttrName()}) {
        auto incoming =
            input->getOperation()->getAttrOfType<ArrayAttr>(listName);
        if (!incoming || incoming.empty())
          continue;
        SmallVector<AccumulatedEntry> &accumulated =
            listName == cir::CIRDialect::getGlobalCtorsAttrName() ? ctorEntries
                                                                  : dtorEntries;
        for (mlir::Attribute entry : incoming) {
          mlir::StringAttr key;
          if (auto ctor = dyn_cast<cir::GlobalCtorAttr>(entry)) {
            key = renamedName(ctor.getAssociatedData());
            entry = cir::GlobalCtorAttr::get(renamedName(ctor.getName()),
                                            ctor.getPriority(), key);
          } else if (auto dtor = dyn_cast<cir::GlobalDtorAttr>(entry)) {
            key = renamedName(dtor.getAssociatedData());
            entry = cir::GlobalDtorAttr::get(renamedName(dtor.getName()),
                                            dtor.getPriority(), key);
          }
          if (key)
            keyFirstInput.try_emplace(key.getValue(), idx);
          accumulated.push_back({idx, entry});
        }
      }

      // __attribute__((annotate)) entries are per TU, like the constructor
      // lists, and name their symbol with a plain string too: every input's are
      // kept, under the names the merged module uses. Taking the first input's
      // list dropped every other TU's annotations.
      if (auto incoming =
              input->getOperation()->getAttrOfType<cir::GlobalAnnotationValuesAttr>(
                  cir::CIRDialect::getGlobalAnnotationsAttrName())) {
        for (mlir::Attribute entry : incoming.getAnnotations()) {
          auto pair = dyn_cast<ArrayAttr>(entry);
          if (pair && pair.size() == 2)
            if (auto symbol = dyn_cast<StringAttr>(pair[0]))
              entry = ArrayAttr::get(&context, {renamedName(symbol), pair[1]});
          annotationEntries.push_back({idx, entry});
        }
      }

      listsScope.reset();
      renameScope.reset();
      llvm::TimeTraceScope moveScope("moveOps");

      // Move rather than clone: the input module is discarded straight after,
      // and cloning every function body would double peak memory for no reason.
      SmallVector<Operation *> toMove;
      for (Operation &op : input->getBody()->getOperations())
        toMove.push_back(&op);
      // Before any of them moves: a definition compared below may refer to a
      // local its input has not handed over yet.
      if (recordResolutions)
        for (Operation *op : toMove)
          if (auto symbol = dyn_cast<SymbolOpInterface>(op);
              symbol && symbol.getNameAttr() && hasLocalLinkage(op))
            localSymbolNames.insert(symbol.getNameAttr().getValue());

      for (Operation *op : toMove) {
        auto symbol = dyn_cast<SymbolOpInterface>(op);
        if (!symbol || !symbol.getNameAttr()) {
          // Not a symbol at all: nothing can collide, so it just comes along.
          op->remove();
          dst->push_back(op);
          continue;
        }

        StringRef name = symbol.getNameAttr().getValue();
        bool isDef = isSymbolDefinition(op);
        bool isLocal = hasLocalLinkage(op);

        auto it = resolved.find(name);
        if (it == resolved.end()) {
          op->remove();
          dst->push_back(op);
          resolved[name] = Resolution{op,
                                      static_cast<unsigned>(idx),
                                      isDef,
                                      isWeakForLinker(op),
                                      isLocal,
                                      isExternalGlobalDeclaration(op),
                                      isExternalGlobalDeclaration(op) &&
                                          !noRTTIInputs.test(idx)};
          ++symbolsMerged;
          if (isDef)
            ++definitionsKept;
          continue;
        }

        Resolution &prev = it->second;

        // The name is held by a local symbol and an externally visible one has
        // turned up. The external symbol owns the name; the local one, already
        // in the output, steps aside. Rewriting uses in the merged module is
        // correct here because only that local's own TU ever referred to it.
        if (prev.isLocal && !isLocal) {
          renameToUnique(prev.op, merged->getOperation(),
                         SymbolTable::getSymbolName(prev.op).getValue(),
                         resolved);
          StringRef movedName = SymbolTable::getSymbolName(prev.op).getValue();
          resolved[movedName] = prev;
          resolved[movedName].isLocal = true;
          if (recordResolutions) {
            localSymbolNames.insert(movedName);
            localSymbolNames.erase(name);
          }
          // Any retention entry recorded under this name so far can only be the
          // local that is stepping aside: it held the name until now, which is
          // what put us in this branch. Entries for the arriving external are
          // collected after this point, under the name it keeps.
          renameRetained(name, movedName);
          renameAccumulated(prev.inputIdx, name, movedName);
          ++localsRenamed;

          op->remove();
          dst->push_back(op);
          resolved[name] = Resolution{op,
                                      static_cast<unsigned>(idx),
                                      isDef,
                                      isWeakForLinker(op),
                                      false,
                                      isExternalGlobalDeclaration(op),
                                      isExternalGlobalDeclaration(op) &&
                                          !noRTTIInputs.test(idx)};
          ++symbolsMerged;
          if (isDef)
            ++definitionsKept;
          continue;
        }

        if (!isDef) {
          // A function declaration for a function alias already merged: the
          // declaration takes the alias's place, and becomes it.
          if (aliasAsFunction(op, prev.op)) {
            op->remove();
            dst->getOperations().insert(Block::iterator(prev.op), op);
            bury(prev.op);
            prev.op = op;
            ++declarationsDropped;
            continue;
          }
          // A declaration for something already present. Nothing to add, but
          // remember that this input expected an owner elsewhere.
          prev.declaredExternal |= isExternalGlobalDeclaration(op);
          prev.declaredExternalWithRTTI |=
              isExternalGlobalDeclaration(op) && !noRTTIInputs.test(idx);
          ++declarationsDropped;
          bury(op);
          continue;
        }

        if (!prev.isDefinition && aliasAsFunction(prev.op, op)) {
          // A function alias for a function we only had declared: the
          // declaration becomes the alias.
          bury(op);
          prev.inputIdx = static_cast<unsigned>(idx);
          prev.isDefinition = true;
          prev.isWeak = isWeakForLinker(prev.op);
          ++definitionsKept;
          continue;
        }

        if (!prev.isDefinition) {
          // A definition arriving for something we only had declared. The
          // definition wins: drop the placeholder and append this one. Output
          // order is therefore input order, not the order the declarations were
          // first seen -- deterministic either way, which is what matters.
          //
          // `prev` is updated in place, so declaredExternal -- set by the
          // declaration this replaces -- carries over to the definition, and
          // deferDiscardableDataToOwners sees it whichever came first. The
          // linkage is the definition's: a declaration is never weak, and a
          // linkonce_odr definition taking its place still is, for a strong
          // one to displace. A local definition never gets here: one that
          // collides with a name already held is renamed on the way in.
          bury(prev.op);
          op->remove();
          dst->push_back(op);
          prev.op = op;
          prev.inputIdx = static_cast<unsigned>(idx);
          prev.isDefinition = true;
          prev.isWeak = isWeakForLinker(op);
          ++definitionsKept;
          continue;
        }

        // Two definitions. Which one survives depends on linkage, never on
        // whether the copies agree; every case is recorded (when asked) so that
        // what a disagreement means is decided by whoever reads the record.
        bool nowWeak = isWeakForLinker(op);

        if (prev.isWeak && !nowWeak) {
          // A strong definition displaces the weak placeholder we were holding.
          // Not an error: this is the ordinary outcome for an inline function
          // that also has an out-of-line definition somewhere.
          recordResolution(ResolutionKind::StrongOverWeak, name, prev, op,
                           static_cast<unsigned>(idx), prev.op, prev.inputIdx);
          bury(prev.op);
          op->remove();
          dst->push_back(op);
          prev.op = op;
          prev.inputIdx = static_cast<unsigned>(idx);
          prev.isDefinition = true;
          prev.isWeak = false;
          ++weakReplaced;
          continue;
        }

        if (prev.isWeak && nowWeak && isa<cir::GlobalOp>(op) &&
            noRTTIInputs.test(prev.inputIdx) && !noRTTIInputs.test(idx)) {
          // Two weak copies of a global, the one we hold from a TU built
          // without RTTI. Its vtables have no type_info slot filled in, and
          // libc++'s std::function policies carry nullptr for &typeid(T); the
          // RTTI copy works for both kinds of caller and the other copy does
          // not, so the RTTI copy wins whatever the input order. Functions
          // keep the first copy: RTTI does not change their bodies in
          // practice, and a -fno-rtti TU is often -fno-exceptions too, which
          // does -- see separateNoExceptionsInlines for that rule.
          recordResolution(ResolutionKind::RTTIOverNoRTTI, name, prev, op,
                           static_cast<unsigned>(idx), prev.op, prev.inputIdx);
          bury(prev.op);
          op->remove();
          dst->push_back(op);
          prev.op = op;
          prev.inputIdx = static_cast<unsigned>(idx);
          ++noRTTIReplaced;
          continue;
        }

        if (prev.isWeak || nowWeak) {
          // weak+weak, or a weak arriving after a strong. Either way the copies
          // are meant to be equivalent, so keep what we have and say nothing.
          recordResolution(prev.isWeak ? ResolutionKind::WeakWeak
                                       : ResolutionKind::StrongOverWeak,
                           name, prev, prev.op, prev.inputIdx, op,
                           static_cast<unsigned>(idx));
          ++weakDeduped;
          bury(op);
          continue;
        }

        // Strong against strong: the real thing. Permissive by design -- the
        // first wins -- but recorded, because this is the population an ODR
        // checker is built from.
        divergences.push_back(
            Divergence{name.str(), prev.inputIdx, static_cast<unsigned>(idx)});
        recordResolution(ResolutionKind::StrongStrong, name, prev, prev.op,
                         prev.inputIdx, op, static_cast<unsigned>(idx));
        bury(op);
      }
    }
    buryWindow();
  }
  buryWindow();
  if (burying.valid())
    burying.get();

  if (merged)
    deferredToOwners = deferDiscardableDataToOwners(
        resolved, noRTTIInputs, warnOdrDivergence ? &locator : nullptr,
        sharedLibrarySymbols.empty() ? nullptr : &sharedSymbolSet);
  if (merged)
    recordABIFacts.writeTo(*merged);

  assert(merged && "cl::OneOrMore guarantees an input");

  // Write the retention lists back. This overwrites what the first input's
  // setAttrs copied over, which is the intent: the accumulator already holds
  // that input's entries along with every other input's.
  {
    SymbolTable mergedSymbols(merged->getOperation());
    auto installRetained = [&](StringRef listName, const RetentionList &syms) {
      SmallVector<mlir::Attribute> entries;
      for (const std::string &name : syms) {
        // A retention entry names a symbol that was defined in the TU the list
        // came from, and the merge never removes a name -- when it drops a
        // duplicate, the survivor keeps it. So a miss here should not happen;
        // it is filtered rather than asserted because the alternative is
        // emitting a reference to a symbol that is not there, which fails much
        // later in cir-translate as a module that does not verify.
        if (mergedSymbols.lookup(name))
          entries.push_back(FlatSymbolRefAttr::get(&context, name));
      }
      if (entries.empty())
        merged->getOperation()->removeAttr(listName);
      else
        merged->getOperation()->setAttr(listName,
                                        ArrayAttr::get(&context, entries));
    };
    installRetained(cir::CIRDialect::getLLVMUsedAttrName(), usedSyms);
    installRetained(cir::CIRDialect::getLLVMCompilerUsedAttrName(),
                    compilerUsedSyms);

    // An entry with associated data runs only if that global's comdat is the
    // copy the link keeps -- LLVM drops the others' entries with their
    // comdats. Every TU that uses a template static member emits its own
    // guarded initializer keyed on the member, and the merge keeps one member,
    // so it keeps one TU's initializers for it: the TU whose definition of the
    // member survived, which is the first TU unless a strong definition
    // displaced a weak one. The others were harmless, since the guard lets one
    // run, but every one was a call made at load time: one library's 53
    // generated registrations came out as 155.
    //
    // Per input, not per entry: a TU may key several entries on one global,
    // and all of the kept TU's run, as all of a selected comdat's do.
    auto ownerOf = [&](StringRef key) -> size_t {
      auto it = resolved.find(key);
      if (it != resolved.end() && it->second.isDefinition)
        return it->second.inputIdx;
      return keyFirstInput.lookup(key);
    };
    auto installInitializerLists = [&](StringRef listName,
                                       ArrayRef<AccumulatedEntry> accumulated) {
      SmallVector<mlir::Attribute> entries;
      entries.reserve(accumulated.size());
      for (const AccumulatedEntry &acc : accumulated) {
        mlir::StringAttr key;
        if (auto ctor = dyn_cast<cir::GlobalCtorAttr>(acc.entry))
          key = ctor.getAssociatedData();
        else if (auto dtor = dyn_cast<cir::GlobalDtorAttr>(acc.entry))
          key = dtor.getAssociatedData();
        if (key && ownerOf(key.getValue()) != acc.input) {
          ++duplicateInitsDropped;
          continue;
        }
        entries.push_back(acc.entry);
      }
      if (entries.empty())
        merged->getOperation()->removeAttr(listName);
      else
        merged->getOperation()->setAttr(listName,
                                        ArrayAttr::get(&context, entries));
    };
    installInitializerLists(cir::CIRDialect::getGlobalCtorsAttrName(),
                            ctorEntries);
    installInitializerLists(cir::CIRDialect::getGlobalDtorsAttrName(),
                            dtorEntries);
    if (!annotationEntries.empty()) {
      SmallVector<mlir::Attribute> entries;
      entries.reserve(annotationEntries.size());
      for (const AccumulatedEntry &acc : annotationEntries)
        entries.push_back(acc.entry);
      merged->getOperation()->setAttr(
          cir::CIRDialect::getGlobalAnnotationsAttrName(),
          cir::GlobalAnnotationValuesAttr::get(ArrayAttr::get(&context, entries)));
    }
    finalizeMergedLanguage(*merged, anyCXX);
    redirectToNoExceptionsCopies(*merged, noExceptionsRenamed);
  }

  // Internalization is what turns "the merge lets the inliner see across TUs"
  // into "the merge makes the output smaller". A symbol that stays externally
  // visible has to be emitted even once every call to it has been inlined
  // away, because something outside the merged set might still call it. Only
  // once the linker asserts that nothing outside does can it be deleted.
  //
  // This is the same contract as LLVM's -internalize: the keep list *is* the
  // claim about what the merged unit exports, and getting it wrong drops
  // symbols the rest of the program needs. So there is no default keep set --
  // an empty one is a real answer ("nothing is exported"), not a mistake to
  // guess around.
  unsigned internalized = 0;
  if (internalize) {
    llvm::TimeTraceScope internalizeScope("internalize");
    llvm::StringSet<> keep;
    for (const std::string &sym : keepSymbols)
      keep.insert(sym);

    if (!keepListFile.empty()) {
      auto buf = llvm::MemoryBuffer::getFile(keepListFile);
      if (!buf) {
        mlir::emitError(fileLoc(&context, keepListFile))
            << "cannot read the keep list: " << buf.getError().message();
        return 1;
      }
      SmallVector<StringRef> lines;
      (*buf)->getBuffer().split(lines, '\n', /*MaxSplit=*/-1,
                                /*KeepEmpty=*/false);
      for (StringRef line : lines) {
        line = line.trim();
        if (!line.empty() && !line.starts_with("#"))
          keep.insert(line);
      }
    }

    // Everything named by llvm.used stays externally visible, whatever the
    // keep list says.
    //
    // llvm.used is the claim that something *outside anything the compiler or
    // the linker can see* refers to this symbol: a linker script, file-scope
    // inline asm, a dlsym by name, a JNI entry point the Java runtime looks up
    // as a string. Internalizing it satisfies the letter of the retention
    // contract -- it does survive DCE -- and defeats the point of it, because
    // the name stops being findable from outside. The failure is a lookup that
    // returns null at runtime, with a module that built and linked clean.
    //
    // llvm.compiler.used deliberately gets no such exemption. That list
    // promises only that *llvm* will not delete the symbol; the assembler and
    // the linker are free to, and LLVM's own -internalize makes exactly this
    // distinction, preserving llvm.used and internalizing llvm.compiler.used
    // (Internalize.cpp, "We must assume that globals in llvm.used have a
    // reference that not even the linker can see"). Exempting it too would
    // hold a large number of symbols externally visible for no reason, which
    // is the opposite of what --internalize is for: on one real merged library
    // that list has 134 entries against llvm.used's twelve.
    for (const std::string &sym : usedSyms)
      keep.insert(sym);

    for (Operation &op : merged->getBody()->getOperations()) {
      auto gv = dyn_cast<cir::CIRGlobalValueInterface>(&op);
      if (!gv || gv.hasLocalLinkage())
        continue;

      // A declaration has no body to hide. Marking one internal would leave a
      // symbol that is undefined and unreachable at the same time.
      if (!isSymbolDefinition(&op))
        continue;

      auto symbol = dyn_cast<SymbolOpInterface>(&op);
      if (!symbol || !symbol.getNameAttr())
        continue;
      if (keep.contains(symbol.getNameAttr().getValue()))
        continue;

      // Internalizing has to take the comdat with it.
      //
      // A comdat in CIR is a UnitAttr, meaning "this symbol is its own comdat
      // group", and it lowers to an ELF section group whose signature is the
      // symbol's name. Once the symbol is internal that group is not just
      // useless -- nothing outside can reference the symbol, so there is
      // nothing left to deduplicate -- it is a liability: any other object in
      // the final link carrying a group with the same signature wins, and
      // ours is discarded along with the section holding the body. The link
      // then fails with
      //
      //   `.text.inline_fn' referenced in section `.text' of x.o:
      //   defined in discarded section `.text.inline_fn[inline_fn]' of x.o
      //
      // which is a link error rather than a miscompile, but it is triggered
      // by an unrelated object having picked the same name -- exactly the
      // mixed CIR/LLVM-path container this is meant to support.
      if (auto func = dyn_cast<cir::FuncOp>(&op)) {
        func.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
        func.setComdat(false);
      } else if (auto global = dyn_cast<cir::GlobalOp>(&op)) {
        global.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
        global.setComdat(false);
      } else {
        continue;
      }

      // Linkage and symbol visibility are two separate things in MLIR, and
      // CIR's verifier requires them to agree: "public visibility not allowed
      // with 'internal' linkage". Setting only the linkage produces a module
      // that does not verify.
      SymbolTable::setSymbolVisibility(&op, SymbolTable::Visibility::Private);
      ++internalized;
    }
  }

  // Reconcile references whose type disagrees with the definition that won.
  //
  // clang has always given a global's *definition* the type of its constant
  // rather than of the variable -- `struct D : Base { }` becomes
  // `{ i32 } zeroinitializer`, a bitfield record becomes `{ i8, i8, i8, i8 }`.
  // Classical codegen does exactly the same. It is invisible in LLVM because
  // pointers are opaque and the IR linker maps types across modules; in CIR
  // pointers are typed and cir.get_global's result is checked against the
  // global, so the moment a defining TU and a referencing TU are merged the
  // module stops verifying.
  //
  // Reconciling that is a linker's job, which is why it is done here rather
  // than by changing CIRGen to diverge from clang. The definition is
  // authoritative: give the get_global the global's own type and bitcast to
  // whatever the uses expect, so nothing downstream has to be retyped.
  //
  // This and the call reconciliation below walk each top-level op on its own,
  // in parallel: a get_global or a call only ever sits in the body of a
  // function or of a global's initializer, and what they look up in the other
  // symbols -- a global's type, a function's signature -- neither of them
  // changes. The symbol table is only read, and the symbols do not change
  // until both are done.
  std::optional<llvm::TimeTraceScope> reconcileScope;
  reconcileScope.emplace("reconcileReferences");
  SymbolTable mergedSymbols(merged->getOperation());
  SmallVector<Operation *> mergedTopLevel;
  for (Operation &op : merged->getBody()->getOperations())
    mergedTopLevel.push_back(&op);
  unsigned referencesRetyped = 0;
  {
    const SymbolTable &symbolTable = mergedSymbols;
    // Found per top-level op, then fixed in module order.
    std::vector<SmallVector<cir::GetGlobalOp, 0>> toFixIn(
        mergedTopLevel.size());
    mlir::parallelFor(&context, 0, mergedTopLevel.size(), [&](size_t i) {
      mergedTopLevel[i]->walk([&](cir::GetGlobalOp op) {
        auto *target = symbolTable.lookup(op.getName());
        if (!target)
          return;
        auto global = dyn_cast<cir::GlobalOp>(target);
        if (!global)
          return;
        auto resultPtr = dyn_cast<cir::PointerType>(op.getType());
        if (!resultPtr || resultPtr.getPointee() == global.getSymType())
          return;
        toFixIn[i].push_back(op);
      });
    });
    SmallVector<cir::GetGlobalOp> toFix;
    for (auto &ops : toFixIn)
      toFix.append(ops.begin(), ops.end());

    for (cir::GetGlobalOp op : toFix) {
      auto global = cast<cir::GlobalOp>(symbolTable.lookup(op.getName()));
      auto oldPtr = cast<cir::PointerType>(op.getType());

      // A layout disagreement is the linker's to settle. An *address space*
      // disagreement is not: a bitcast across address spaces is a different
      // operation with different semantics, and CIR has a separate cast kind
      // for it. The bitcast verifier accepts any pointer-to-pointer pair, so
      // emitting one here would not be rejected -- it would just be wrong,
      // quietly. Refuse instead.
      if (global.getAddrSpaceAttr() != oldPtr.getAddrSpace()) {
        mlir::InFlightDiagnostic diag =
            mlir::emitError(op.getLoc())
            << "'" << op.getName()
            << "' is referenced in a different address space than it is "
               "defined in; refusing to reconcile";
        diag.attachNote(global.getLoc()) << "defined here";
        return 1;
      }

      auto newPtr =
          cir::PointerType::get(global.getSymType(), global.getAddrSpaceAttr());

      OpBuilder builder(op);
      // The tls flag has to come along. Dropping it turns a thread-local
      // access into an ordinary one, and nothing downstream notices: the
      // verifier only checks that a *tls* get_global targets a tls global, so
      // losing the flag makes the module verify and the program read the
      // wrong storage.
      auto retyped = cir::GetGlobalOp::create(
          builder, op.getLoc(), newPtr, op.getNameAttr(), op.getTlsAttr());
      retyped->setDiscardableAttrs(op->getDiscardableAttrDictionary());
      auto cast =
          cir::CastOp::create(builder, op.getLoc(), oldPtr,
                              cir::CastKind::bitcast, retyped.getResult());
      op.getResult().replaceAllUsesWith(cast.getResult());
      op.erase();
      ++referencesRetyped;
    }
  }

  // Reconcile call operands against the callee's signature.
  //
  // Two TUs can name one type differently -- aarch64 libc/libc++ give the same
  // record as `__va_list` in one and `std::__va_list` in the other -- so a
  // caller compiled against one spelling passes it to a callee declared with
  // the other. Nothing is wrong with either TU; they simply disagree, and
  // reconciling that is the linker's job, exactly as it is for a global whose
  // definition and references disagree.
  //
  // Name-based unification cannot fix this one: it merges same-name records,
  // and here the names differ while the bodies match. So the call is bridged
  // instead -- but not by a record-to-record cir.cast bitcast on the value.
  // Lowering that goes through memory: a bitwise copy into a temporary of the
  // other type. The object the callee built is then not the one the caller
  // holds, which a class with a non-trivial copy constructor does not
  // survive: an app crashed at startup because a record
  // returned that way kept a std::set whose root pointed into the callee's
  // dead frame (one TU's libc++ set body differed from another's, so the
  // merge had two copies of that record). A call whose record operands or
  // result differ only in name goes through a cast instead (below), and the
  // caller's own storage is the callee's return slot or argument, as in a
  // classic link.
  //
  // And sometimes the two sides really disagree. OpenBLAS defines `double
  // sdot_(...)` (the f2c convention) while PyTorch declares and calls `float
  // sdot_(...)`: no bitcast turns a double into a float, so bridging the value
  // made a module that did not verify, and the library could not take the CIR
  // relink. A classic link does not look: the call keeps the type its caller
  // was compiled with, and the definition is reached through it, whatever that
  // means at run time. That is what is done here too when a disagreement cannot
  // be bridged by a bitcast -- the call goes through the definition's address
  // cast to the function type the caller expects, so the link is the classic
  // link's, bug included, rather than a failure the classic build does not
  // have.
  std::atomic<unsigned> callOperandsRetyped = 0;
  std::atomic<unsigned> callResultsRetyped = 0;
  std::atomic<unsigned> callsThroughCast = 0;
  // try_calls whose records differ only in name, still bridged by value: the
  // copy the cast avoids for a plain call (see below).
  std::atomic<unsigned> tryCallRecordsBridged = 0;
  {
    const SymbolTable &symbolTable = mergedSymbols;
    // Turn a direct call into an indirect one through `fn`'s address, cast to
    // the function type the call was compiled against: its own operand and
    // result types, `fn`'s variadic-ness, and for a variadic `fn` at most as
    // many fixed parameters as `fn` declares.
    auto callThroughCast = [&](cir::CallOp call, cir::FuncOp fn,
                               mlir::TypeRange argTypes) {
      cir::FuncType defined = fn.getFunctionType();
      size_t fixed = argTypes.size();
      if (defined.isVarArg())
        fixed = std::min(fixed, defined.getInputs().size());
      SmallVector<mlir::Type> inputs(argTypes.begin(),
                                     argTypes.begin() + fixed);
      mlir::Type result = call->getNumResults() == 1
                              ? call->getResult(0).getType()
                              : cir::VoidType::get(&context);
      auto viewed = cir::FuncType::get(inputs, result, defined.isVarArg());
      OpBuilder builder(call);
      mlir::Value address = cir::GetGlobalOp::create(
                                builder, call.getLoc(),
                                cir::PointerType::get(defined), fn.getSymName())
                                .getAddr();
      mlir::Value callee = cir::CastOp::create(builder, call.getLoc(),
                                               cir::PointerType::get(viewed),
                                               cir::CastKind::bitcast, address);
      call->insertOperands(0, callee);
      call.removeCalleeAttr();
    };
    // Walked through the interface, not cir::CallOp: cir.try_call has the same
    // argument structure and the same exposure to a merged-away spelling, and
    // a call the merge broke is no more linkable for being in a try.
    auto reconcileCall = [&](cir::CIRCallOpInterface call) {
      // FlatSymbolRefAttr, not SymbolRefAttr: a nested reference would need a
      // nested lookup, and taking its root would silently resolve to the
      // enclosing symbol instead. CIR calls are flat today, so anything else
      // is left alone rather than half-resolved.
      auto ref =
          dyn_cast_if_present<SymbolRefAttr>(call.getCallableForCallee());
      auto callee = dyn_cast_if_present<FlatSymbolRefAttr>(ref);
      if (!callee)
        return; // indirect, or a nested reference we will not guess at.
      auto fn =
          dyn_cast_or_null<cir::FuncOp>(symbolTable.lookup(callee.getValue()));
      if (!fn)
        return;
      ArrayRef<mlir::Type> params = fn.getFunctionType().getInputs();
      mlir::Type declaredResult = fn.getFunctionType().getOptionalReturnType();

      // Whether a value of one type stands for the other through a bitcast:
      // one type under two names -- a record against a record, a pointer
      // against a pointer -- or two scalars of the same size.
      auto bridgeable = [&](mlir::Type from, mlir::Type to) {
        if (mlir::isa<cir::RecordType>(from) && mlir::isa<cir::RecordType>(to))
          return true;
        if (mlir::isa<cir::PointerType>(from) &&
            mlir::isa<cir::PointerType>(to))
          return true;
        if (!cir::isScalarType(from) || !cir::isScalarType(to))
          return false;
        mlir::DataLayout dataLayout(merged.get());
        return dataLayout.getTypeSize(from) == dataLayout.getTypeSize(to);
      };
      bool resultMismatch =
          (call->getNumResults() == 1) != bool(declaredResult) ||
          (declaredResult && call->getResult(0).getType() != declaredResult);
      bool unbridgeable =
          resultMismatch &&
          (!declaredResult || call->getNumResults() != 1 ||
           !bridgeable(declaredResult, call->getResult(0).getType()));
      for (auto [idx, arg] : llvm::enumerate(call.getArgOperands()))
        if (idx < params.size() && arg.getType() != params[idx] &&
            !bridgeable(arg.getType(), params[idx]))
          unbridgeable = true;
      // And a different number of arguments: a C caller that declared the
      // function without a prototype, or with other parameters.
      size_t numArgs = call.getArgOperands().size();
      if (fn.getFunctionType().isVarArg() ? numArgs < params.size()
                                          : numArgs != params.size())
        unbridgeable = true;
      // Records that differ only in name: through the cast as well, so no
      // record value is copied bitwise to change its type (see above).
      auto recordsDiffer = [](mlir::Type a, mlir::Type b) {
        return a != b && mlir::isa<cir::RecordType>(a) &&
               mlir::isa<cir::RecordType>(b);
      };
      bool recordRetyped =
          resultMismatch && declaredResult && call->getNumResults() == 1 &&
          recordsDiffer(declaredResult, call->getResult(0).getType());
      for (auto [idx, arg] : llvm::enumerate(call.getArgOperands()))
        if (idx < params.size() && recordsDiffer(arg.getType(), params[idx]))
          recordRetyped = true;
      // A plain call only: a try_call's successor operands would need their
      // segments adjusted, and none has been seen to need this. A try_call
      // whose records differ only in name is still bridged by value below.
      auto plainCall = dyn_cast<cir::CallOp>(call.getOperation());
      if ((unbridgeable || recordRetyped) && plainCall) {
        callThroughCast(plainCall, fn, plainCall.getArgOperands().getTypes());
        ++callsThroughCast;
        return;
      }
      if (recordRetyped)
        ++tryCallRecordsBridged;

      OpBuilder builder(call);
      // Set through the mutable range rather than by computing an operand
      // index: try_call carries successor operands too, so "total minus
      // arguments" is not where the arguments start.
      MutableOperandRange args = call.getArgOperandsMutable();
      for (auto [idx, arg] : llvm::enumerate(call.getArgOperands())) {
        if (idx >= params.size())
          break; // variadic tail: nothing declared to match against.
        if (arg.getType() == params[idx])
          continue;
        // Left alone if a bitcast cannot bridge it: that is a try_call the
        // fallback above does not take, and the verifier reports it.
        if (!bridgeable(arg.getType(), params[idx]))
          continue;
        auto cast = cir::CastOp::create(builder, call.getLoc(), params[idx],
                                        cir::CastKind::bitcast, arg);
        args[idx].set(cast.getResult());
        ++callOperandsRetyped;
      }

      // The result needs the mirror of that treatment. The call has to produce
      // what the surviving definition returns, so the value is retyped and a
      // cast put in front of everything that reads it -- the callers keep the
      // type they were compiled against.
      //
      // A void/non-void disagreement, or two types no bitcast relates, went
      // through the cast above; what is left here is what a bitcast bridges.
      // Not for a terminator. cir.try_call is one -- it has normal and unwind
      // successors -- so there is no "after the call" in its own block to put
      // the cast in, and its result is only live in a successor. Bridging it
      // would mean picking a successor block and reasoning about which uses
      // that dominates; until a merge actually produces such a mismatch, the
      // honest outcome is the verifier reporting it rather than this inserting
      // ill-formed IR. The operand side above still applies to try_call.
      if (call->getNumResults() == 1 && declaredResult &&
          !call->hasTrait<mlir::OpTrait::IsTerminator>()) {
        mlir::Value result = call->getResult(0);
        mlir::Type original = result.getType();
        if (original != declaredResult &&
            bridgeable(declaredResult, original)) {
          result.setType(declaredResult);
          OpBuilder after(call->getContext());
          after.setInsertionPointAfter(call);
          auto cast = cir::CastOp::create(after, call.getLoc(), original,
                                          cir::CastKind::bitcast, result);
          result.replaceAllUsesExcept(cast.getResult(), cast);
          ++callResultsRetyped;
        }
      }
    };
    mlir::parallelForEach(&context, mergedTopLevel,
                          [&](Operation *op) { op->walk(reconcileCall); });
  }
  reconcileScope.reset();

  unsigned odrErrors = 0;
  if (recordResolutions) {
    describeRecordRenames(context, recordLocations, recordRenames);
    odrErrors =
        reportResolutions(locator, symbolResolutions, recordRenames, odrPolicy);
  }

  if (warnOdrDivergence) {
    mlir::emitRemark(fileLoc(&context, outputFilename))
        << symbolsMerged << " symbols, " << definitionsKept << " definitions, "
        << declarationsDropped << " redundant declarations, " << localsRenamed
        << " locals renamed, " << recordsRenamed << " records renamed, "
        << anonCanonicalized << " anon records canonicalized, " << weakDeduped
        << " weak deduped, " << weakReplaced << " weak replaced by strong, "
        << noRTTIReplaced << " -fno-rtti copies replaced, "
        << divergences.size() << " ODR divergences, " << internalized
        << " internalized, " << referencesRetyped << " references retyped, "
        << callOperandsRetyped.load() << " call operands retyped, "
        << callResultsRetyped.load() << " call results retyped, "
        << callsThroughCast.load() << " calls through a cast, "
        << tryCallRecordsBridged.load()
        << " try_calls with records bridged by value, " << duplicateInitsDropped
        << " duplicate initializers dropped, " << deferredToOwners
        << " globals deferred to their owners, " << weakCopiesDiffer
        << " weak copies differ";
  }

  if (!odrEventsFile.empty()) {
    auto events = openOutput(context, odrEventsFile);
    if (!events)
      return 1;
    unsigned equalWeakCopies = 0;
    for (const auto &entry : resolved)
      equalWeakCopies += entry.second.equalWeakCopies;
    std::array<unsigned, 4> byKind = {};
    for (const SymbolResolutions &entry : symbolResolutions)
      for (const ResolutionEvent &event : entry.events)
        ++byKind[static_cast<size_t>(event.kind)];

    llvm::json::OStream json(events->os(), /*IndentSize=*/2);
    // Locations as the diagnostics give them; see DiagnosticLocator.
    auto writeLocation = [&](mlir::LocationAttr loc, unsigned input) {
      mlir::FileLineColLoc at = locator.at(loc, input);
      json.attributeObject("location", [&] {
        json.attribute("file", at.getFilename().getValue());
        json.attribute("line", at.getLine());
        json.attribute("column", at.getColumn());
      });
    };
    auto writeCopy = [&](StringRef key, const ResolvedCopy &copy) {
      json.attributeObject(key, [&] {
        json.attribute("input", copy.input);
        json.attribute("linkage", copy.linkage);
        json.attribute("digest", copy.digest);
        writeLocation(copy.loc, copy.input);
      });
    };
    auto writeDifference = [&](StringRef key, const Difference &difference,
                               unsigned keptInput, unsigned droppedInput) {
      json.attributeObject(key, [&] {
        json.attribute("what", difference.what);
        json.attribute("source_what", difference.sourceWhat);
        auto writeSite = [&](StringRef side, const DifferenceSite &site,
                             unsigned input) {
          json.attributeObject(side, [&] {
            writeLocation(site.loc, input);
            json.attribute("text", site.text);
            if (site.source)
              json.attribute("source_text", *site.source);
          });
        };
        writeSite("kept", difference.kept, keptInput);
        writeSite("dropped", difference.dropped, droppedInput);
      });
    };
    auto optionalText = [](const std::optional<std::string> &text) {
      return text ? llvm::json::Value(*text) : llvm::json::Value(nullptr);
    };
    json.object([&] {
      json.attribute("version", 1);
      json.attributeArray("inputs", [&] {
        for (size_t i = 0; i < inputFilenames.size(); ++i)
          json.object([&] {
            json.attribute("path", inputFilenames[i]);
            if (!locator.source(i).empty())
              json.attribute("source", locator.mapPath(locator.source(i)));
            json.attribute("no_rtti", noRTTIInputs.test(i));
            json.attribute("no_exceptions", noExceptionsInputs.test(i));
          });
      });
      json.attributeArray("symbols", [&] {
        for (const SymbolResolutions &entry : symbolResolutions)
          json.object([&] {
            json.attribute("symbol", entry.symbol);
            json.attribute("op", entry.opName);
            auto res = resolved.find(entry.symbol);
            json.attribute("equal_weak_copies",
                           res == resolved.end() ? 0u
                                                 : res->second.equalWeakCopies);
            json.attributeArray("events", [&] {
              for (const ResolutionEvent &event : entry.events)
                json.object([&] {
                  json.attribute("kind", resolutionKindName(event.kind));
                  writeCopy("kept", event.kept);
                  writeCopy("dropped", event.dropped);
                  json.attribute("same_type", event.sameType);
                  json.attribute("same_attributes", event.sameAttributes);
                  json.attribute("same_body", event.sameBody);
                  json.attribute("severity", odrSeverityName(event.severity));
                  if (event.typeDifference)
                    writeDifference("type_difference", *event.typeDifference,
                                    event.kept.input, event.dropped.input);
                  if (event.bodyDifference)
                    writeDifference("body_difference", *event.bodyDifference,
                                    event.kept.input, event.dropped.input);
                  if (!event.attributeDifferences.empty())
                    json.attributeArray("attribute_differences", [&] {
                      for (const AttributeDifference &d :
                           event.attributeDifferences)
                        json.object([&] {
                          json.attribute("name", d.name);
                          json.attribute("kept", optionalText(d.kept));
                          json.attribute("dropped", optionalText(d.dropped));
                        });
                    });
                });
            });
          });
      });
      json.attributeArray("records", [&] {
        for (const RecordRenameEvent &rename : recordRenames)
          json.object([&] {
            json.attribute("kind", "record-renamed");
            json.attribute("input", rename.input);
            json.attribute("record", rename.from);
            json.attribute("renamed_to", rename.to);
            json.attribute("severity", odrSeverityName(rename.severity));
            json.attribute("kept_input", rename.keptInput);
            writeLocation(rename.loc, rename.input);
            json.attributeObject("kept", [&] {
              writeLocation(rename.keptLoc, rename.keptInput);
            });
            if (rename.memberDifference)
              writeDifference("member_difference", *rename.memberDifference,
                              rename.keptInput, rename.input);
          });
      });
      json.attributeObject("stats", [&] {
        json.attribute("weak_copies_equal", equalWeakCopies);
        json.attribute("weak_copies_differ", weakCopiesDiffer);
        for (ResolutionKind kind :
             {ResolutionKind::WeakWeak, ResolutionKind::StrongOverWeak,
              ResolutionKind::RTTIOverNoRTTI, ResolutionKind::StrongStrong})
          json.attribute(resolutionKindName(kind),
                         byKind[static_cast<size_t>(kind)]);
        json.attribute("records_renamed", recordRenames.size());
      });
    });
    events->os() << "\n";
    events->keep();
  }
  // The findings are reported; an error among them fails the link here,
  // once the record of every choice is written.
  if (odrErrors)
    return 1;

  bool dumpAndFail = false;
  {
    llvm::TimeTraceScope verifyScope("verifyMerged");
    if (failed(verify(*merged)))
      dumpAndFail = true;
  }
  if (dumpAndFail) {
    // The diagnostics name the op but not what the merge did to get there,
    // and the module is gone by the time anyone reads them. Write it next to
    // the requested output -- under a name nothing will pick up as the real
    // artifact -- so the failure can be looked at rather than reproduced.
    //
    // As bytecode, not text: a module that does not verify prints in the
    // generic form, and one library's printed to 250 GB, filling the disk
    // under a build, where its bytecode is the size of the inputs. Read it
    // back with `cir-opt --mlir-very-unsafe-disable-verifier-on-parsing`.
    std::string invalidPath = outputFilename + ".invalid.mlirbc";
    mlir::InFlightDiagnostic diag =
        mlir::emitError(fileLoc(&context, outputFilename))
        << "the merged module does not verify";
    std::error_code ec;
    llvm::ToolOutputFile dump(invalidPath, ec, llvm::sys::fs::OF_None);
    if (ec) {
      // Say so rather than leaving the reader looking for a file that was
      // never written -- `-o -` alone makes this path unwritable.
      diag.attachNote() << "cannot write it to '" << invalidPath
                        << "': " << ec.message();
    } else if (failed(writeBytecodeToFile(*merged, dump.os()))) {
      // A partial file is not kept: it would be read as the module that
      // failed, and is not.
      diag.attachNote() << "cannot encode it; '" << invalidPath
                        << "' is not written";
    } else {
      dump.keep();
      diag.attachNote(fileLoc(&context, invalidPath))
          << "the merged module, as it failed to verify";
    }
    return 1;
  }

  llvm::TimeTraceScope writeOutputScope("writeOutput");
  auto output = openOutput(context, outputFilename);
  if (!output)
    return 1;

  // The output follows the input unless asked otherwise. The .cir section of a
  // fat object is bytecode, so the real link -- hundreds of objects in, one
  // module out -- was converting bytecode to text for the next stage to parse
  // straight back, which on one library meant writing 718 MB where 83 MB says
  // the same thing.
  //
  // And it did not say the same thing. The printer omits locations unless told
  // not to, so every source location in the program was dropped at this seam
  // and the next stage attributed all of it to the merged file: the annotation
  // strings in the lowered module named `merged.cir` instead of the file the
  // annotation came from. Locations are what CIR builds debug info out of, so
  // this is the seam that has to be lossless, not merely small.
  bool writeAsBytecode = emitBytecode.getNumOccurrences()
                             ? emitBytecode.getValue()
                             : inputWasBytecode;

  // Lowering happens here rather than in another process reading this file
  // back. The merged module is already in memory and already correct; writing
  // it out so cir-translate could parse it again moved 83 MB of bytecode one
  // way and 548 MB of textual LLVM IR the other, and carried nothing across
  // the boundary that was not already on this side of it.
  //
  // The merged CIR is still available with -emit-cir, because being able to
  // look at it is worth keeping. It is just not how the pipeline moves.
  if (emitLLVM) {
    if (!emitCIRPath.empty()) {
      llvm::TimeTraceScope sideScope("writeSideCIR");
      auto side = openOutput(context, emitCIRPath);
      if (!side)
        return 1;
      if (writeAsBytecode) {
        if (failed(writeBytecodeToFile(*merged, side->os()))) {
          mlir::emitError(fileLoc(&context, emitCIRPath))
              << "cannot write bytecode";
          return 1;
        }
      } else {
        merged->print(side->os(),
                      mlir::OpPrintingFlags().enableDebugInfo(/*enable=*/true));
        side->os() << "\n";
      }
      side->keep();
    }

    if (partitions > 1) {
      // Each piece is written by the thread that lowered it, as soon as it is
      // lowered: piece 0 to the output already open, the others next to it.
      auto writePiece = [&](unsigned piece, llvm::Module &module) {
        llvm::TimeTraceScope writeScope("writeLLVMIR");
        std::unique_ptr<ToolOutputFile> pieceOutput;
        raw_ostream *os = &output->os();
        if (piece != 0) {
          pieceOutput = openOutput(
              context, (Twine(outputFilename) + "." + Twine(piece)).str());
          if (!pieceOutput)
            return failure();
          os = &pieceOutput->os();
        }
        if (emitLLVMText)
          module.print(*os, nullptr);
        else
          cir::writeLLVMModuleBitcode(
              module, *os, {/*kind=*/lto, /*splitLTOUnit=*/splitLTOUnit});
        if (pieceOutput)
          pieceOutput->keep();
        return success();
      };
      std::optional<StringRef> requestedTriple;
      if (targetTriple.getNumOccurrences() > 0)
        requestedTriple = StringRef(targetTriple);
      llvm::TimeTraceScope lowerScope("lowerToLLVMIR");
      if (failed(cir::lowerCIRModuleToLLVMIRInPartitions(
              *merged, requestedTriple, getInlinerOptions(), partitions,
              writePiece))) {
        mlir::emitError(fileLoc(&context, outputFilename))
            << "cannot lower the merged module to LLVM IR";
        return 1;
      }
      output->keep();
      return 0;
    }

    llvm::LLVMContext llvmContext;
    std::unique_ptr<llvm::Module> llvmModule;
    {
      llvm::TimeTraceScope lowerScope("lowerToLLVMIR");
      std::optional<StringRef> requestedTriple;
      if (targetTriple.getNumOccurrences() > 0)
        requestedTriple = StringRef(targetTriple);
      llvmModule = cir::lowerCIRModuleToLLVMIR(
          *merged, llvmContext, requestedTriple, getInlinerOptions());
    }
    if (!llvmModule) {
      mlir::emitError(fileLoc(&context, outputFilename))
          << "cannot lower the merged module to LLVM IR";
      return 1;
    }

    // CIR is finished with, so it goes now rather than at exit. The module has
    // to be released before the context that uniqued everything in it, and the
    // context is much the larger of the two: every type and attribute the link
    // ever created is still sitting in its allocator.
    {
      llvm::TimeTraceScope dropScope("dropCIR");
      merged = OwningOpRef<ModuleOp>();
      diagnostics.reset();
      contextStorage.reset();
    }

    llvm::TimeTraceScope writeLLScope("writeLLVMIR");
    if (emitLLVMText)
      llvmModule->print(output->os(), nullptr);
    else
      cir::writeLLVMModuleBitcode(
          *llvmModule, output->os(),
          {/*kind=*/lto, /*splitLTOUnit=*/splitLTOUnit});
    output->keep();
    return 0;
  }

  if (writeAsBytecode) {
    if (failed(writeBytecodeToFile(*merged, output->os()))) {
      mlir::emitError(fileLoc(&context, outputFilename))
          << "cannot write bytecode";
      return 1;
    }
  } else {
    // Text is now a debugging format rather than a pipeline one, and a
    // debugging format that silently discards where everything came from is
    // worse than a slow one.
    merged->print(output->os(),
                  mlir::OpPrintingFlags().enableDebugInfo(/*enable=*/true));
    output->os() << "\n";
  }
  output->keep();
  return 0;
}
