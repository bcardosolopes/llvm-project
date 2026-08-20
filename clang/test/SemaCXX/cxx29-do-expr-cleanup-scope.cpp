// RUN: %clang_cc1 -std=c++2d -fcxx-exceptions -fexceptions -ast-dump -ast-dump-filter=fn %s | FileCheck %s
//
// Where the ExprWithCleanups lands when a do-expression is part of a larger
// full-expression.
//
// The body is parsed in the middle of the enclosing full-expression but is
// itself a sequence of statements, each with full-expressions of its own. If
// the body inherits the enclosing full-expression's pending cleanup state, the
// first nested full-expression in the body consumes it and becomes the
// ExprWithCleanups that the enclosing full-expression needed -- and the
// enclosing temporaries are then destroyed at the end of the block instead of
// at the end of the full-expression. See CodeGenCXX/cxx29-do-expr-cleanup-scope.cpp
// for what that costs at runtime.

struct T {
  int i;
  ~T();
};

int sink(T a, int b);
int val(const T &t);
void next();

// The enclosing full-expression owns the sibling argument's temporary, so the
// ExprWithCleanups belongs directly above the call -- not inside the body, on
// the `if` condition that happens to be the body's first full-expression.
// CHECK-LABEL: FunctionDecl {{.*}} fn_sibling_argument
// CHECK:      DeclStmt
// CHECK-NEXT:   VarDecl {{.*}} v 'int' cinit
// CHECK-NEXT:     ExprWithCleanups {{.*}} 'int'
// CHECK-NEXT:       CallExpr {{.*}} 'int'
// CHECK:            CXXBindTemporaryExpr
// CHECK:            DoExpr
// CHECK:              IfStmt
// CHECK-NEXT:           CXXBoolLiteralExpr {{.*}} 'bool' false
void fn_sibling_argument() {
  int v = sink(T{1}, do {
    if (false)
      throw 7;
    do_return 3;
  });
  next();
}

// The same, with a `while` in the body: any statement whose condition is a
// full-expression can do the stealing, not just `if`.
// CHECK-LABEL: FunctionDecl {{.*}} fn_body_has_a_loop
// CHECK:      ExprWithCleanups {{.*}} 'int'
// CHECK-NEXT:   CallExpr {{.*}} 'int'
// CHECK:        DoExpr
// CHECK:          WhileStmt
// CHECK-NEXT:       CXXBoolLiteralExpr {{.*}} 'bool' false
void fn_body_has_a_loop() {
  int v = sink(T{1}, do {
    while (false) {
    }
    do_return 3;
  });
  next();
}

// A body statement with a temporary of its own still gets its own
// ExprWithCleanups, nested inside the enclosing one. Isolating the body must
// not mean the body's statements stop cleaning up after themselves.
// CHECK-LABEL: FunctionDecl {{.*}} fn_body_statement_temporary
// CHECK:      ExprWithCleanups {{.*}} 'int'
// CHECK-NEXT:   CallExpr {{.*}} 'int'
// CHECK:        DoExpr
// CHECK:          CompoundStmt
// CHECK-NEXT:       ExprWithCleanups {{.*}} 'int'
void fn_body_statement_temporary() {
  int v = sink(T{1}, do {
    val(T{2});
    do_return 3;
  });
  next();
}

// A `do_return` operand is not finished as a full-expression of its own, so
// its temporaries belong to the enclosing full-expression. Resetting the
// cleanup state for the body must therefore *merge* on the way out rather than
// restore: this needs an ExprWithCleanups even though nothing outside the
// do-expression created a temporary.
// CHECK-LABEL: FunctionDecl {{.*}} fn_do_return_operand_temporary
// CHECK:      VarDecl {{.*}} v 'int' cinit
// CHECK-NEXT:   ExprWithCleanups {{.*}} 'int'
// CHECK-NEXT:     DoExpr
void fn_do_return_operand_temporary() {
  int v = do { do_return val(T{1}) + 10; };
  next();
}

// Instantiation re-runs every full-expression in the body, so it can reproduce
// the same theft; the body gets its own context there too.
template <typename U>
void fn_in_a_template() {
  int v = sink(T{1}, do {
    if (false)
      throw U{};
    do_return 3;
  });
  next();
}

// CHECK-LABEL: FunctionDecl {{.*}} fn_in_a_template 'void ()' implicit_instantiation
// CHECK:      VarDecl {{.*}} v 'int' cinit
// CHECK-NEXT:   ExprWithCleanups {{.*}} 'int'
// CHECK-NEXT:     CallExpr {{.*}} 'int'
void fn_instantiate() { fn_in_a_template<int>(); }
