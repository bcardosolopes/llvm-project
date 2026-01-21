#include "clang/CIR/StdLibStatistics.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "llvm/Support/Casting.h"

using namespace clang;

// === Count ==================================================================
void StdLibStats::Count::add(SourceLocation loc, FunctionDecl *caller) {
  ++count;
  if (caller)
    locations.emplace_back(loc, caller);
  else
    locations.emplace_back(loc);
}

// === Destructor =============================================================
StdLibStats::~StdLibStats() { dump(llvm::dbgs()); }

void StdLibStats::initialize(ASTContext *astContext) { ast = astContext; }

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

bool StdLibStats::TraverseStmt(Stmt *stmt) {
  if (auto *callExpr = llvm::dyn_cast_or_null<CallExpr>(stmt))
    gather(callExpr);

  Visitor::TraverseStmt(stmt);
  return true;
}

bool StdLibStats::TraverseType(QualType type, bool TraverseQualifier) {
  Visitor::TraverseType(type, TraverseQualifier);
  return true;
}

// === Helpers ================================================================
static bool isInStdNamespace(Decl *decl) {
  if (!decl)
    return false;

  auto *parent = decl->getDeclContext();
  while (parent) {
    if (parent->isStdNamespace())
      return true;
    parent = parent->getParent();
  }

  return false;
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

static bool isCalleeInStdNamespace(CallExpr *expr) {
  // If this is a member call, check if the record is in std namespace.
  if (auto *memberCall = llvm::dyn_cast<CXXMemberCallExpr>(expr)) {
    auto *recordDecl = memberCall->getRecordDecl();
    if (!recordDecl)
      return false;
    return isInStdNamespace(recordDecl);
  }
  // Check if the direct callee is in std namespace.
  auto *fnDecl = expr->getDirectCallee();
  if (!fnDecl)
    return false;
  return isInStdNamespace(fnDecl);
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

static bool isCalleeInCStdlib(CallExpr *expr) {
  return isInCStdlib(expr->getDirectCallee());
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
int notStdNamespace = 0, indirectCall = 0;
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

  if (!isCalleeInStdNamespace(expr) && !isCalleeInCStdlib(expr)) {
    ++notStdNamespace;
    return;
  }

  auto calleeName = getDirectCalleeName(expr);
  if (!calleeName) {
    ++indirectCall;
    return;
  }

  auto recordName = getMemberCallRecordName(expr);

  callCounts[recordName.value_or("")][calleeName.value()].add(
      expr->getExprLoc(), caller);
}

// === Statistics reporting ===================================================
void StdLibStats::dump(llvm::raw_ostream &os) {
  if (callCounts.empty())
    return;

  os << "{ \"total\": " << total // total # of calls found.
     << ", \"calls\": [ ";
  bool first = true;
  for (const auto &[record, funcCounts] : callCounts) {
    for (const auto &[func, count] : funcCounts) {
      if (first)
        first = false;
      else
        os << ", ";
      os << "{ \"record\": \"" << record << "\"" // record name (if member call)
         << ", \"function\": \"" << func << "\"" // function name
         << ", \"count\": " << count.count       // # of calls
         << ", \"locations\": [ ";               // list of call locations
      if (ast) {
        bool firstLoc = true;
        for (const auto &[loc, caller] : count.locations) {
          if (firstLoc)
            firstLoc = false;
          else
            os << ", ";

          os << "{ \"loc\": \""
             << ast->getFullLoc(loc).printToString(ast->getSourceManager())
             << "\"";
          if (caller) {
            os << ", \"caller\": \"" << caller.value() << "\"";
          }
          os << " }"; // end of location
        }
        os << " ]"; // end of locations
      }
      os << " }"; // end of object
    }
  }
  os << " ] }\n";
}

// === ASTConsumer ============================================================
void StdLibStatsConsumer::Initialize(ASTContext &astContext) {
  stats.initialize(&astContext);
}

bool StdLibStatsConsumer::HandleTopLevelDecl(DeclGroupRef decls) {
  for (auto *decl : decls)
    stats.visit(decl);
  return true;
}

void StdLibStatsConsumer::anchor() {}

// === FrontendAction ========================================================
std::unique_ptr<clang::ASTConsumer>
StdLibStatsAction::CreateASTConsumer(clang::CompilerInstance &CI,
                                     llvm::StringRef) {
  return std::make_unique<StdLibStatsConsumer>();
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
  consumers.push_back(std::make_unique<StdLibStatsConsumer>());
  consumers.push_back(std::move(otherConsumer));

  return std::make_unique<clang::MultiplexConsumer>(std::move(consumers));
}
