#ifndef LLVM_CLANG_CIR_STDLIBSTATISTICS_H
#define LLVM_CLANG_CIR_STDLIBSTATISTICS_H

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ExprCXX.h"
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

  struct Context {
    Context(clang::SourceLocation loc) : loc(loc), caller(std::nullopt) {}
    Context(clang::SourceLocation loc, clang::FunctionDecl *caller)
        : loc(loc), caller(caller->getQualifiedNameAsString()) {}

    clang::SourceLocation loc;
    std::optional<std::string> caller;
  };

  struct Count {
    void add(clang::SourceLocation loc, clang::FunctionDecl *caller = nullptr);

    unsigned int count = 0;
    llvm::SmallVector<Context, 4> locations;
  };

  /// A two-level mapping from Record -> Function -> Count
  llvm::StringMap<llvm::StringMap<Count>> callCounts;

  /// Total number of valid calls.
  unsigned int total = 0;

  /// Current function.
  llvm::SmallVector<clang::FunctionDecl *> funcStack;

  /// Are we currently nested in std namespace?
  bool inStd = false;

  /// Optional AST context.
  clang::ASTContext *ast = nullptr;

public:
  /// Constructor.
  StdLibStats() = default;

  /// Destructor.
  ~StdLibStats();

  /// Initiailize with the AST context.
  void initialize(clang::ASTContext *astContext);

  /// Visit a top-level declaration.
  void visit(clang::Decl *decl);

  /// Gather statistics from the given CallExpr.
  void gather(clang::CallExpr *expr);

  /// Dump the gathered statistics.
  void dump(llvm::raw_ostream &os);

private:
  /// Traverse the AST.
  bool TraverseDecl(clang::Decl *decl);
  bool TraverseStmt(clang::Stmt *stmt);
  bool TraverseType(clang::QualType type, bool TraverseQualifier = true);
};

class StdLibStatsConsumer : public clang::ASTConsumer {
  StdLibStats stats;

  virtual void anchor();

public:
  void Initialize(clang::ASTContext &astContext) override;
  bool HandleTopLevelDecl(clang::DeclGroupRef decls) override;
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
