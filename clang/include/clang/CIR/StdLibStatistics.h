#ifndef LLVM_CLANG_CIR_STDLIBSTATISTICS_H
#define LLVM_CLANG_CIR_STDLIBSTATISTICS_H

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtCXX.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Frontend/MultiplexConsumer.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

class StdLibStats : public clang::RecursiveASTVisitor<StdLibStats> {
  using Visitor = typename clang::RecursiveASTVisitor<StdLibStats>;
  friend Visitor;

public:
  /// How the report is delivered. In Embed mode the report is meant to be
  /// harvested from tens of thousands of object files, so the unbounded
  /// per-call-site location list is not recorded.
  enum class Mode { Json, Embed };

  /// What the callee is, as far as code generation is concerned. A census of
  /// call *expressions* is not a census of emitted calls: much of what a
  /// modern C++ codebase spells as a call is type machinery that costs
  /// nothing. This is the cheap, purely declarative part of telling them
  /// apart; see also Count::unevaluated.
  enum class Kind {
    /// An ordinary function that has to be called at run time.
    Runtime,
    /// Declared `constexpr`. May fold to nothing, may not: a constexpr
    /// function called with run-time arguments is still a call. This is the
    /// bucket std::move, std::forward and numeric_limits::max() land in.
    Constexpr,
    /// Declared `consteval`. Never emits a call.
    Consteval,
    /// A compiler builtin (memcpy, __builtin_*). Emits code, but not a call
    /// into the library, so it is not an optimization target the same way.
    Builtin,
  };

  static llvm::StringRef toString(Kind kind);

private:
  struct Context {
    Context(clang::SourceLocation loc) : loc(loc), caller(std::nullopt) {}
    Context(clang::SourceLocation loc, llvm::StringRef caller)
        : loc(loc), caller(caller.str()) {}

    clang::SourceLocation loc;
    std::optional<std::string> caller;
  };

  /// A caller of a std library entry point, aggregated over its call sites.
  struct Caller {
    /// Qualified name, for humans.
    std::string name;
    /// Linkage name, empty when the caller cannot be mangled (templates,
    /// dependent contexts). This is the join key against llvm-nm and DWARF.
    std::string mangled;
    unsigned int count = 0;
  };

  struct Count {
    unsigned int count = 0;

    /// Of \c count, how many call sites sit in an unevaluated operand
    /// (sizeof, decltype, noexcept(...), a requires-clause). Those emit
    /// nothing at all. std::declval is the archetype: it is ill-formed to
    /// odr-use it, so every one of its call sites lands here.
    unsigned int unevaluated = 0;

    /// Of \c count, how many call sites sit in a template PATTERN rather than
    /// in an instantiation -- that is, the enclosing caller has no linkage
    /// name. The pattern itself is never codegen'd: it is emitted only through
    /// its instantiations, which are censused separately under their own
    /// linkage names. So these are not emitted calls, and an entry-point total
    /// quoted without subtracting them is an overcount. isset_bitset::at read
    /// 899,245 that way, of which 98.4% was pattern.
    ///
    /// Deliberately the SAME test the join-side audit made by hand -- a null
    /// `mangled` on the caller -- so the compiler-emitted split and the
    /// hand-recovered one agree by construction rather than by coincidence.
    unsigned int pattern = 0;

    /// The library this entry belongs to: "std", "c", or one of the extra
    /// namespaces named by -fstd-lib-stats-libs.
    llvm::StringRef lib;

    /// What the callee is, for codegen purposes.
    Kind kind = Kind::Runtime;

    /// Callers keyed by mangled name, falling back to the qualified name.
    llvm::StringMap<Caller> callers;

    /// Individual call sites. Only populated in Json mode.
    llvm::SmallVector<Context, 4> locations;
  };

  /// A two-level mapping from Record -> Function -> Count
  llvm::StringMap<llvm::StringMap<Count>> callCounts;

  /// Total number of calls considered (before the std library filter).
  unsigned int total = 0;

  /// Calls dropped because the callee is not part of the std library.
  unsigned int notStdNamespace = 0;

  /// Calls dropped because they have no callee declaration, in code that is
  /// not a template pattern: through a function pointer, a std::function, or a
  /// pointer to member. This is the devirtualization surface, counted over
  /// every call, not just std ones.
  unsigned int indirectCall = 0;

  /// Calls in a template pattern that was parsed but never instantiated, and
  /// so are still instantiation-dependent. These resolve to no declaration just
  /// as an indirect call does, but no code is ever generated for them, and in
  /// header-heavy code they outnumber real indirect calls by an order of
  /// magnitude -- so they are counted apart rather than inflating `indirect`.
  unsigned int dependentCall = 0;

  /// Calls into a censused library whose callee has no spellable name --
  /// conversion operators, mostly. Before 2026-08-04 these were reported as
  /// `indirect`, which badly overstated the devirtualization surface.
  unsigned int unnamedCallee = 0;

  /// Censused call sites sitting in a template pattern: the TU-level total of
  /// Count::pattern, reported in the summary.
  unsigned int patternCalls = 0;

  /// Per-library call totals, keyed as Count::lib.
  llvm::StringMap<unsigned int> libCounts;

  /// Outermost namespace of every call NOT in a censused library, so the
  /// census reports which "std-like" libraries the code leans on rather than
  /// requiring them to be guessed in advance. Only the heaviest
  /// \c kMaxNamespaces are reported; this is a discovery aid, not a total.
  llvm::StringMap<unsigned int> namespaceCounts;

  /// Key used for callees at global scope in \c namespaceCounts.
  static constexpr llvm::StringLiteral kGlobalNamespace = "(global)";

  /// How many namespaces a report lists.
  static constexpr unsigned int kMaxNamespaces = 32;

  /// Extra top-level namespaces to census, from -fstd-lib-stats-libs. Owns
  /// the strings Count::lib points at.
  llvm::SmallVector<std::string> extraLibs;

  /// Current function.
  llvm::SmallVector<clang::FunctionDecl *> funcStack;

  /// Nesting depth inside unevaluated operands.
  unsigned int unevaluatedDepth = 0;

  /// Are we currently nested in std namespace?
  bool inStd = false;

  /// Optional AST context.
  clang::ASTContext *ast = nullptr;

  /// Produces linkage names for callers. Null until initialize().
  std::unique_ptr<clang::ASTNameGenerator> mangler;

  Mode mode = Mode::Json;

  /// Record one call to \p count made from \p caller.
  void record(Count &count, clang::SourceLocation loc,
              clang::FunctionDecl *caller);

  /// Which library \p decl belongs to, or an empty StringRef when it is not
  /// one we census.
  llvm::StringRef libraryOf(clang::CallExpr *expr,
                            clang::FunctionDecl *callee) const;

public:
  /// Constructor. \p libs is the comma-separated -fstd-lib-stats-libs value.
  StdLibStats(Mode mode = Mode::Json, llvm::StringRef libs = {});

  /// Destructor.
  ~StdLibStats();

  /// Initiailize with the AST context.
  void initialize(clang::ASTContext *astContext);

  /// Visit a top-level declaration.
  void visit(clang::Decl *decl);

  /// Gather statistics from the given CallExpr.
  void gather(clang::CallExpr *expr);

  /// Render the gathered statistics as a single-line JSON document. Returns
  /// "" when nothing was gathered.
  std::string render() const;

private:
  /// Traverse the AST.
  bool TraverseDecl(clang::Decl *decl);
  bool TraverseStmt(clang::Stmt *stmt);
  bool TraverseType(clang::QualType type, bool TraverseQualifier = true);
  bool TraverseTypeLoc(clang::TypeLoc loc, bool TraverseQualifier = true);
};

class StdLibStatsConsumer : public clang::ASTConsumer {
  clang::CompilerInstance &ci;
  StdLibStats stats;

  virtual void anchor();

public:
  StdLibStatsConsumer(clang::CompilerInstance &CI);

  void Initialize(clang::ASTContext &astContext) override;
  bool HandleTopLevelDecl(clang::DeclGroupRef decls) override;
  void HandleTranslationUnit(clang::ASTContext &astContext) override;
};

class StdLibStatsAction : public clang::FrontendAction {
public:
  std::unique_ptr<clang::ASTConsumer>
  CreateASTConsumer(clang::CompilerInstance &CI, llvm::StringRef) override;
};

class WrappingStdLibStatsAction : public clang::WrapperFrontendAction {
public:
  WrappingStdLibStatsAction(
      std::unique_ptr<clang::FrontendAction> WrappedAction);

  std::unique_ptr<clang::ASTConsumer>
  CreateASTConsumer(clang::CompilerInstance &CI,
                    llvm::StringRef InFile) override;
};

#endif // LLVM_CLANG_CIR_STDLIBSTATISTICS_H
