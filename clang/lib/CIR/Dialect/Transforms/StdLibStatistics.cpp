#include "clang/CIR/StdLibStatistics.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/JSON.h"

using namespace clang;

// === Construction ===========================================================
StdLibStats::StdLibStats(Mode mode, llvm::StringRef libs) : mode(mode) {
  llvm::SmallVector<llvm::StringRef> parts;
  libs.split(parts, ',', /*MaxSplit=*/-1, /*KeepEmpty=*/false);
  for (llvm::StringRef part : parts) {
    part = part.trim();
    // std and the C standard library are always censused.
    if (part.empty() || part == "std" || part == "c")
      continue;
    extraLibs.push_back(part.str());
  }
}

llvm::StringRef StdLibStats::toString(Kind kind) {
  switch (kind) {
  case Kind::Runtime:
    return "runtime";
  case Kind::Constexpr:
    return "constexpr";
  case Kind::Consteval:
    return "consteval";
  case Kind::Builtin:
    return "builtin";
  }
  llvm_unreachable("unhandled StdLibStats::Kind");
}

// === Recording ==============================================================
/// Can \p decl be given a linkage name? Mangling a template pattern or
/// anything in a dependent context is not meaningful (and asserts).
static bool canMangle(const FunctionDecl *decl) {
  return decl && !decl->isTemplated() && !decl->isDependentContext();
}

void StdLibStats::record(Count &count, SourceLocation loc,
                         FunctionDecl *caller) {
  ++count.count;
  if (unevaluatedDepth)
    ++count.unevaluated;

  if (!caller) {
    if (mode == Mode::Json)
      count.locations.emplace_back(loc);
    return;
  }

  std::string name = caller->getQualifiedNameAsString();
  if (mode == Mode::Json)
    count.locations.emplace_back(loc, name);

  std::string mangled;
  if (mangler && canMangle(caller)) {
    llvm::raw_string_ostream os(mangled);
    if (mangler->writeName(caller, os))
      mangled.clear();
  }

  // An empty linkage name means the caller is a template pattern or sits in a
  // dependent context. Code is generated for the instantiations, not for the
  // pattern, and each instantiation is visited separately with a mangled name
  // of its own -- so this call site is not an emitted call. Counted here, at
  // the one place that already knows, and with exactly the condition the
  // consumer would otherwise have to re-derive from a null `mangled`.
  if (mangled.empty()) {
    ++count.pattern;
    ++patternCalls;
  }

  // Prefer the linkage name as the key so that overloads sharing a qualified
  // name stay separate.
  auto &entry = count.callers[mangled.empty() ? name : mangled];
  if (entry.count == 0) {
    entry.name = name;
    entry.mangled = mangled;
  }
  ++entry.count;
}

// === Destructor =============================================================
// The report is emitted from StdLibStatsConsumer::HandleTranslationUnit, not
// from here: in embed mode the code generator has to be able to pick it up
// while it is still building the LLVM module.
StdLibStats::~StdLibStats() = default;

void StdLibStats::initialize(ASTContext *astContext) {
  ast = astContext;
  mangler = std::make_unique<ASTNameGenerator>(*astContext);
}

// === RecursiveASTVisitor ====================================================
void StdLibStats::visit(Decl *decl) { TraverseDecl(decl); }

bool StdLibStats::TraverseDecl(Decl *decl) {
  if (!decl)
    return true;

  auto *func = llvm::dyn_cast<FunctionDecl>(decl);
  if (func)
    funcStack.push_back(func);

  auto wasStd = inStd;
  if (auto *namespaceDecl = llvm::dyn_cast<NamespaceDecl>(decl))
    inStd |= namespaceDecl->isStdNamespace();

  Visitor::TraverseDecl(decl);

  inStd = wasStd;

  if (func)
    funcStack.pop_back();

  return true;
}

/// Does \p stmt introduce an unevaluated operand? Everything below it is
/// type machinery: it is never codegen'd, so a call expression in there is
/// not a call. std::declval lives entirely in here.
static bool introducesUnevaluatedOperand(const Stmt *stmt) {
  if (llvm::isa_and_nonnull<UnaryExprOrTypeTraitExpr>(stmt))
    return true; // sizeof / alignof / ...
  if (llvm::isa_and_nonnull<CXXNoexceptExpr>(stmt))
    return true; // the noexcept(expr) operator
  if (llvm::isa_and_nonnull<RequiresExpr>(stmt))
    return true;
  if (const auto *typeidExpr = llvm::dyn_cast_or_null<CXXTypeidExpr>(stmt))
    return !typeidExpr->isPotentiallyEvaluated();
  return false;
}

bool StdLibStats::TraverseStmt(Stmt *stmt) {
  if (auto *callExpr = llvm::dyn_cast_or_null<CallExpr>(stmt))
    gather(callExpr);

  bool unevaluated = introducesUnevaluatedOperand(stmt);
  if (unevaluated)
    ++unevaluatedDepth;

  Visitor::TraverseStmt(stmt);

  if (unevaluated)
    --unevaluatedDepth;

  return true;
}

/// decltype(expr) and typeof(expr) are the other half of the unevaluated
/// surface, and the one declval is usually written in.
static bool introducesUnevaluatedOperand(const Type *ty) {
  return ty && (llvm::isa<DecltypeType>(ty) || llvm::isa<TypeOfExprType>(ty));
}

bool StdLibStats::TraverseType(QualType type, bool TraverseQualifier) {
  bool unevaluated = introducesUnevaluatedOperand(type.getTypePtrOrNull());
  if (unevaluated)
    ++unevaluatedDepth;

  Visitor::TraverseType(type, TraverseQualifier);

  if (unevaluated)
    --unevaluatedDepth;

  return true;
}

bool StdLibStats::TraverseTypeLoc(TypeLoc loc, bool TraverseQualifier) {
  // Written types reach the visitor as TypeLocs, not QualTypes -- a type alias,
  // a trailing return type or a template argument all land here -- so the
  // TraverseType override above would miss them.
  bool unevaluated =
      introducesUnevaluatedOperand(loc.getType().getTypePtrOrNull());
  if (unevaluated)
    ++unevaluatedDepth;

  Visitor::TraverseTypeLoc(loc, TraverseQualifier);

  if (unevaluated)
    --unevaluatedDepth;

  return true;
}

// === Helpers ================================================================
static bool isInStdNamespace(const Decl *decl) {
  if (!decl)
    return false;

  const auto *parent = decl->getDeclContext();
  while (parent) {
    if (parent->isStdNamespace())
      return true;
    parent = parent->getParent();
  }

  return false;
}

/// The outermost named namespace enclosing \p decl, e.g. "folly" for
/// folly::detail::foo and "absl" for absl::lts_20240116::bar. Empty when
/// \p decl is not in a namespace.
static llvm::StringRef outermostNamespace(const Decl *decl) {
  if (!decl)
    return {};

  llvm::StringRef name;
  const DeclContext *parent = decl->getDeclContext();
  while (parent) {
    if (const auto *namespaceDecl = llvm::dyn_cast<NamespaceDecl>(parent))
      if (!namespaceDecl->isAnonymousNamespace() &&
          namespaceDecl->getIdentifier())
        name = namespaceDecl->getName();
    parent = parent->getParent();
  }

  return name;
}

static bool isReservedFunction(FunctionDecl *decl) {
  if (!decl)
    return false;

  if (decl->getIdentifier() && decl->getName().starts_with("__"))
    return true;

  // If the function name begins with __, it is reserved.
  auto *parent = decl->getDeclContext();
  while (parent) {
    if (auto *namespaceDecl = llvm::dyn_cast<NamespaceDecl>(parent))
      if (namespaceDecl->getName().starts_with("__"))
        return true;

    parent = parent->getParent();
  }

  return false;
}

/// The declaration that decides which library a call belongs to: the record
/// for a member call, the callee otherwise. Null when neither is available.
static const Decl *calleeOwner(CallExpr *expr) {
  if (auto *memberCall = llvm::dyn_cast<CXXMemberCallExpr>(expr))
    return memberCall->getRecordDecl();
  return expr->getDirectCallee();
}

static bool isInCStdlib(FunctionDecl *fnDecl) {
  if (!fnDecl || !fnDecl->getIdentifier())
    return false;

  // Match on C++ std library functions.
  auto name = fnDecl->getName();
  if (name.starts_with("mem")) {
    auto rem = name.substr(3);
    if (rem.compare("cpy") == 0 || rem.compare("move") == 0 ||
        rem.compare("cmp") == 0 || rem.compare("set") == 0 ||
        rem.compare("chr") == 0)
      return true;
  } else if (name.starts_with("str")) {
    auto rem = name.substr(3);
    if (rem.compare("cpy") == 0 || rem.compare("ncpy") == 0 ||
        rem.compare("cat") == 0 || rem.compare("ncat") == 0 ||
        rem.compare("cmp") == 0 || rem.compare("ncmp") == 0 ||
        rem.compare("len") == 0 || rem.compare("chr") == 0 ||
        rem.compare("rchr") == 0 || rem.compare("str") == 0 ||
        rem.compare("nstr") == 0 || rem.compare("tok") == 0 ||
        rem.compare("spn") == 0 || rem.compare("cspn") == 0 ||
        rem.compare("pbrk") == 0 || rem.compare("cbrk") == 0 ||
        rem.compare("find") == 0 || rem.compare("rfind") == 0 ||
        rem.compare("error") == 0 || rem.compare("coll") == 0 ||
        rem.compare("dup") == 0 || rem.compare("xfrm") == 0)
      return true;
  } else if (name.compare("bsearch") == 0 || name.compare("qsort") == 0) {
    return true;
  }

  return false;
}

/// The FunctionDecl whose properties describe the callee.
static FunctionDecl *calleeDecl(CallExpr *expr) {
  if (auto *memberCall = llvm::dyn_cast<CXXMemberCallExpr>(expr))
    return memberCall->getMethodDecl();
  return expr->getDirectCallee();
}

/// What \p decl costs at run time, as far as its declaration can tell.
static StdLibStats::Kind kindOf(const FunctionDecl *decl) {
  if (!decl)
    return StdLibStats::Kind::Runtime;

  // isConstexpr() is also true for consteval, so test consteval first.
  if (decl->isConsteval())
    return StdLibStats::Kind::Consteval;

  // A "predefined library function" (memcpy, strlen) has a builtin id but is
  // a real library entry point that emits real code; only a genuine builtin
  // such as __builtin_expect is free.
  if (unsigned id = decl->getBuiltinID())
    if (!decl->getASTContext().BuiltinInfo.isPredefinedLibFunction(id))
      return StdLibStats::Kind::Builtin;

  if (decl->isConstexpr())
    return StdLibStats::Kind::Constexpr;

  return StdLibStats::Kind::Runtime;
}

llvm::StringRef StdLibStats::libraryOf(CallExpr *expr,
                                       FunctionDecl *callee) const {
  const Decl *owner = calleeOwner(expr);

  if (isInStdNamespace(owner))
    return "std";
  if (isInCStdlib(callee))
    return "c";

  llvm::StringRef ns = outermostNamespace(owner ? owner : callee);
  if (ns.empty())
    return {};
  for (const std::string &lib : extraLibs)
    if (ns == lib)
      return lib;

  return {};
}

static std::optional<std::string> getParamsAsString(FunctionDecl *fnDecl) {
  if (!fnDecl)
    return std::nullopt;

  std::string params = "(";
  bool first = true;
  for (auto *param : fnDecl->parameters()) {
    if (first)
      first = false;
    else
      params += ", ";

    params += param->getType().getAsString();
  }
  params += ")";

  return params;
}

static std::optional<std::string> getDirectCalleeName(CallExpr *expr) {
  std::string name;

  // Get the direct callee from a C++ member call.
  FunctionDecl *calleeDecl = expr->getDirectCallee();
  if (auto memberCall = llvm::dyn_cast<CXXMemberCallExpr>(expr)) {
    calleeDecl = memberCall->getMethodDecl();
    if (!calleeDecl || !calleeDecl->getIdentifier())
      return std::nullopt;
    name = calleeDecl->getName().str();
  } else if (auto operatorCall = llvm::dyn_cast<CXXOperatorCallExpr>(expr)) {
    name = "operator";
    std::string operatorKind = getOperatorSpelling(operatorCall->getOperator());
    name += operatorKind;
  } else {
    // Get the direct callee from a simple call.
    if (!calleeDecl || !calleeDecl->getIdentifier())
      return std::nullopt;
    name = calleeDecl->getName().str();
  }

  // Append the param string, if it exists.
  if (auto params = getParamsAsString(calleeDecl))
    name += params.value();

  return name;
}

static std::optional<llvm::StringRef> getMemberCallRecordName(CallExpr *expr) {

  CXXRecordDecl *recordDecl = nullptr;
  if (auto *memberCall = llvm::dyn_cast<CXXMemberCallExpr>(expr)) {
    recordDecl = memberCall->getRecordDecl();
  } else if (auto *methodDecl = llvm::dyn_cast_or_null<CXXMethodDecl>(
                 expr->getDirectCallee())) {
    recordDecl = methodDecl->getParent();
  }
  if (!recordDecl || !recordDecl->getIdentifier())
    return std::nullopt;
  return recordDecl->getName();
}

// === Statistic gathering ====================================================
void StdLibStats::gather(CallExpr *expr) {
  FunctionDecl *caller = nullptr;
  if (funcStack.size() > 0)
    caller = funcStack.back();

  // If this is called from within std::, don't count it.
  if (inStd || isInStdNamespace(caller) || isReservedFunction(caller) ||
      isInCStdlib(caller)) {
    return;
  }

  ++total;

  FunctionDecl *callee = calleeDecl(expr);
  if (!callee) {
    // Two very different things reach this point, and only one of them is a
    // call through a pointer. A call written in a template pattern that was
    // parsed but never instantiated also resolves to no declaration -- yet no
    // code is ever generated for it, and in header-heavy code there are far
    // more of them, so folding the two together would drown the real signal.
    //
    // Ask the call expression, not the callee: overload resolution is deferred
    // whenever an *argument* is dependent, so `over(t)` in a template pattern
    // has a callee that is not itself type-dependent and would otherwise be
    // miscounted as indirect. Each instantiation is visited separately and does
    // resolve, so nothing is lost by skipping the pattern.
    if (expr->isInstantiationDependent()) {
      ++dependentCall;
      return;
    }

    // Genuinely unresolved outside a template pattern: a call through a
    // function pointer, a std::function, or a pointer to member. This is the
    // devirtualization surface, and it is counted over every call, not just
    // std ones.
    ++indirectCall;
    return;
  }

  llvm::StringRef lib = libraryOf(expr, callee);
  if (lib.empty()) {
    ++notStdNamespace;
    // Not a library we census in detail -- but record which namespace it was,
    // so the census can say what the app actually leans on instead of that
    // having to be guessed before choosing -fstd-lib-stats-libs.
    const Decl *owner = calleeOwner(expr);
    llvm::StringRef ns = outermostNamespace(owner ? owner : callee);
    ++namespaceCounts[ns.empty() ? llvm::StringRef(kGlobalNamespace) : ns];
    return;
  }

  auto calleeName = getDirectCalleeName(expr);
  if (!calleeName) {
    // In a library we census, but with no spellable name: a conversion
    // operator, mostly.
    ++unnamedCallee;
    return;
  }

  auto recordName = getMemberCallRecordName(expr);

  Count &count = callCounts[recordName.value_or("")][calleeName.value()];
  if (count.count == 0) {
    count.lib = lib;
    count.kind = kindOf(callee);
  }
  ++libCounts[lib];

  record(count, expr->getExprLoc(), caller);
}

// === Statistics reporting ===================================================
/// StringMap iteration order is a hash order, which would make reports from
/// two builds of the same TU gratuitously different. Sort every level.
template <typename MapT>
static llvm::SmallVector<llvm::StringRef> sortedKeys(const MapT &map) {
  llvm::SmallVector<llvm::StringRef> keys;
  keys.reserve(map.size());
  for (const auto &entry : map)
    keys.push_back(entry.getKey());
  llvm::sort(keys);
  return keys;
}

std::string StdLibStats::render() const {
  if (callCounts.empty())
    return {};

  std::string report;
  llvm::raw_string_ostream os(report);
  llvm::json::OStream json(os);

  json.object([&] {
    json.attribute("version", 1);
    json.attribute("kind", "stdlib_stats");
    if (ast) {
      const SourceManager &sm = ast->getSourceManager();
      if (const FileEntry *main = sm.getFileEntryForID(sm.getMainFileID()))
        json.attribute("tu", main->tryGetRealPathName());
    }

    unsigned int unevaluated = 0;
    for (const auto &record : callCounts)
      for (const auto &func : record.getValue())
        unevaluated += func.getValue().unevaluated;

    unsigned int stdCalls = 0;
    for (llvm::StringRef lib : {"std", "c"})
      if (auto it = libCounts.find(lib); it != libCounts.end())
        stdCalls += it->getValue();

    json.attributeObject("summary", [&] {
      // Every call considered, i.e. the denominator.
      json.attribute("total", total);
      // Calls attributed to a std or C library entry point below.
      json.attribute("std_calls", stdCalls);
      // Calls to something outside every censused library.
      json.attribute("not_std", notStdNamespace);
      // Calls with no callee declaration, outside template patterns: the
      // devirtualization surface.
      json.attribute("indirect", indirectCall);
      // Calls in never-instantiated template patterns. Unresolved for a
      // completely different reason, and never codegen'd -- see the header.
      json.attribute("dependent", dependentCall);
      // Calls into a censused library with no spellable callee name.
      json.attribute("unnamed", unnamedCallee);
      // Of the attributed calls, how many sit in an unevaluated operand and
      // so emit nothing at all.
      json.attribute("unevaluated", unevaluated);
      // Of the attributed calls, how many sit in a template pattern rather
      // than an instantiation, and so are never codegen'd under this caller.
      json.attribute("pattern", patternCalls);
      // Per-library totals. "std" + "c" is std_calls; the rest come from
      // -fstd-lib-stats-libs and are a subset of what not_std used to cover.
      json.attributeObject("libs", [&] {
        for (llvm::StringRef lib : sortedKeys(libCounts))
          json.attribute(lib, libCounts.find(lib)->getValue());
      });
      // The heaviest namespaces making up not_std: which "std-like" libraries
      // this TU leans on, and therefore what is worth adding to
      // -fstd-lib-stats-libs. Truncated, so these do not sum to not_std.
      json.attributeObject("namespaces", [&] {
        llvm::SmallVector<llvm::StringRef> names = sortedKeys(namespaceCounts);
        llvm::stable_sort(names, [&](llvm::StringRef a, llvm::StringRef b) {
          return namespaceCounts.find(a)->getValue() >
                 namespaceCounts.find(b)->getValue();
        });
        if (names.size() > kMaxNamespaces)
          names.truncate(kMaxNamespaces);
        for (llvm::StringRef ns : names)
          json.attribute(ns, namespaceCounts.find(ns)->getValue());
      });
    });

    json.attributeArray("calls", [&] {
      for (llvm::StringRef record : sortedKeys(callCounts)) {
        const auto &funcCounts = callCounts.find(record)->getValue();
        for (llvm::StringRef func : sortedKeys(funcCounts)) {
          const Count &count = funcCounts.find(func)->getValue();
          json.object([&] {
            // Record name, empty when this is not a member call.
            json.attribute("record", record);
            json.attribute("function", func);
            json.attribute("count", count.count);
            json.attribute("lib", count.lib);
            json.attribute("kind", toString(count.kind));
            json.attribute("unevaluated", count.unevaluated);
            json.attribute("pattern", count.pattern);

            json.attributeArray("callers", [&] {
              for (llvm::StringRef key : sortedKeys(count.callers)) {
                const Caller &caller = count.callers.find(key)->getValue();
                json.object([&] {
                  json.attribute("name", caller.name);
                  if (caller.mangled.empty())
                    json.attribute("mangled", nullptr);
                  else
                    json.attribute("mangled", caller.mangled);
                  json.attribute("count", caller.count);
                });
              }
            });

            if (mode == Mode::Json && ast) {
              json.attributeArray("locations", [&] {
                for (const Context &context : count.locations) {
                  json.object([&] {
                    json.attribute("loc",
                                   ast->getFullLoc(context.loc)
                                       .printToString(ast->getSourceManager()));
                    if (context.caller)
                      json.attribute("caller", *context.caller);
                  });
                }
              });
            }
          });
        }
      }
    });
  });
  os << "\n";

  return report;
}

// === ASTConsumer ============================================================
StdLibStatsConsumer::StdLibStatsConsumer(CompilerInstance &CI)
    : ci(CI), stats(CI.getFrontendOpts().StdLibStatsMode == "embed"
                        ? StdLibStats::Mode::Embed
                        : StdLibStats::Mode::Json,
                    CI.getFrontendOpts().StdLibStatsLibs) {}

void StdLibStatsConsumer::Initialize(ASTContext &astContext) {
  stats.initialize(&astContext);
}

bool StdLibStatsConsumer::HandleTopLevelDecl(DeclGroupRef decls) {
  for (auto *decl : decls)
    stats.visit(decl);
  return true;
}

void StdLibStatsConsumer::HandleTranslationUnit(ASTContext &) {
  std::string report = stats.render();
  if (report.empty())
    return;

  // In embed mode, park the report on the CompilerInstance. This consumer is
  // multiplexed ahead of the code generator's, so the report is in place by
  // the time the code generator builds the LLVM module and embeds it.
  if (ci.getFrontendOpts().StdLibStatsMode == "embed")
    ci.setStdLibStatsReport(std::move(report));
  else
    llvm::errs() << report;
}

void StdLibStatsConsumer::anchor() {}

// === FrontendAction ========================================================
std::unique_ptr<clang::ASTConsumer>
StdLibStatsAction::CreateASTConsumer(clang::CompilerInstance &CI,
                                     llvm::StringRef) {
  return std::make_unique<StdLibStatsConsumer>(CI);
}

// === WrapperFrontendAction =================================================
WrappingStdLibStatsAction::WrappingStdLibStatsAction(
    std::unique_ptr<clang::FrontendAction> WrappedAction)
    : WrapperFrontendAction(std::move(WrappedAction)) {}

std::unique_ptr<clang::ASTConsumer>
WrappingStdLibStatsAction::CreateASTConsumer(clang::CompilerInstance &CI,
                                             llvm::StringRef InFile) {
  auto otherConsumer = WrapperFrontendAction::CreateASTConsumer(CI, InFile);
  if (!otherConsumer)
    return nullptr;

  std::vector<std::unique_ptr<clang::ASTConsumer>> consumers;
  // The stats consumer must run first: in embed mode the code generator's
  // HandleTranslationUnit reads the report this one produces.
  consumers.push_back(std::make_unique<StdLibStatsConsumer>(CI));
  consumers.push_back(std::move(otherConsumer));

  return std::make_unique<clang::MultiplexConsumer>(std::move(consumers));
}
