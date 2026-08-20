// RUN: %clang_cc1 -std=c++2d -emit-llvm -triple x86_64-linux-gnu -fcxx-exceptions -fexceptions -O0 -disable-O0-optnone -o - %s | FileCheck %s

// When a full-expression's temporaries are destroyed, when part of that
// full-expression is a do-expression.
//
// A by-value argument with a non-trivial destructor is destroyed by the caller
// at the end of the full-expression ([expr.call], Itanium ABI). It was instead
// destroyed at the end of the enclosing *block*, arbitrarily far later, as soon
// as the do-expression's body contained a nested full-expression -- an `if`
// condition is enough. The body inherited the enclosing full-expression's
// pending cleanup state, the body's first full-expression consumed it, and the
// enclosing one never got an ExprWithCleanups. See
// SemaCXX/cxx29-do-expr-cleanup-scope.cpp for the AST that shows it.
//
// A lock guard, a string buffer, or anything else whose destructor has side
// effects therefore ran later than the program said it did.

struct T {
  int i;
  ~T();
};

int sink(T a, int b);
int two(T a, T b, int c);
int val(const T &t);
void next();

// The argument temporary dies before the next statement, not after it.
// CHECK-LABEL: define {{.*}} @_Z20sibling_is_a_do_exprv()
// CHECK: %[[ARG:.*]] = alloca %struct.T
// CHECK: invoke {{.*}} @_Z4sink1Ti(ptr {{.*}} %[[ARG]],
// CHECK: invoke.cont:
// CHECK-NEXT: call void @_ZN1TD1Ev(ptr {{.*}} %[[ARG]])
// CHECK: call void @_Z4nextv()
// CHECK: ret void
void sibling_is_a_do_expr() {
  int v = sink(T{1}, do {
    if (false)
      throw 7;
    do_return 3;
  });
  next();
}

// A potentially-throwing call in the body reaches the same code path as a
// `throw` statement: both need a nested full-expression, which is what did the
// stealing.
// CHECK-LABEL: define {{.*}} @_Z25body_calls_something_thatv()
// CHECK: %[[ARG:.*]] = alloca %struct.T
// CHECK: invoke {{.*}} @_Z4sink1Ti(ptr {{.*}} %[[ARG]],
// CHECK: invoke.cont{{[0-9]*}}:
// CHECK-NEXT: call void @_ZN1TD1Ev(ptr {{.*}} %[[ARG]])
// CHECK: call void @_Z4nextv()
// CHECK: ret void
void body_calls_something_that() {
  int v = sink(T{1}, do {
    if (val(T{2}))
      next();
    do_return 3;
  });
  next();
}

// Both siblings, destroyed in reverse construction order, before the next
// statement.
// CHECK-LABEL: define {{.*}} @_Z12two_siblingsv()
// CHECK: %[[A:.*]] = alloca %struct.T
// CHECK: %[[B:.*]] = alloca %struct.T
// CHECK: invoke {{.*}} @_Z3two1TS_i(ptr {{.*}} %[[A]], ptr {{.*}} %[[B]],
// CHECK: invoke.cont:
// CHECK-NEXT: call void @_ZN1TD1Ev(ptr {{.*}} %[[B]])
// CHECK-NEXT: call void @_ZN1TD1Ev(ptr {{.*}} %[[A]])
// CHECK: call void @_Z4nextv()
// CHECK: ret void
void two_siblings() {
  int v = two(T{1}, T{2}, do {
    if (false)
      throw 7;
    do_return 3;
  });
  next();
}

// The do-expression evaluated first was already correct, because the sibling's
// temporary is created after the body has been emitted -- the enclosing
// cleanup state is still clean when the body is parsed. Kept as a regression
// guard: the fix must not move this one.
// CHECK-LABEL: define {{.*}} @_Z16do_expr_is_firstv()
// CHECK: %[[ARG:.*]] = alloca %struct.T
// CHECK: invoke {{.*}} @_Z3revi1T(i32 {{.*}}, ptr {{.*}} %[[ARG]])
// CHECK: invoke.cont:
// CHECK-NEXT: call void @_ZN1TD1Ev(ptr {{.*}} %[[ARG]])
// CHECK: call void @_Z4nextv()
// CHECK: ret void
int rev(int a, T b);
void do_expr_is_first() {
  int v = rev(do {
    if (false)
      throw 7;
    do_return 3;
  }, T{1});
  next();
}

// A temporary in a body *statement* still dies at the end of that statement.
// Isolating the body must not mean its statements stop cleaning up after
// themselves: `dtor 2` runs before the call, not with the argument.
// CHECK-LABEL: define {{.*}} @_Z20body_statement_temp_v()
// CHECK: %[[ARG:.*]] = alloca %struct.T
// CHECK: %[[LOCAL:.*]] = alloca %struct.T
// CHECK: invoke {{.*}} @_Z3valRK1T(ptr {{.*}} %[[LOCAL]])
// CHECK: call void @_ZN1TD1Ev(ptr {{.*}} %[[LOCAL]])
// CHECK: invoke {{.*}} @_Z4sink1Ti(ptr {{.*}} %[[ARG]],
// CHECK: invoke.cont{{[0-9]*}}:
// CHECK-NEXT: call void @_ZN1TD1Ev(ptr {{.*}} %[[ARG]])
// CHECK: ret void
void body_statement_temp_() {
  int v = sink(T{1}, do {
    val(T{2});
    do_return 3;
  });
  next();
}

// A `do_return` operand is not a full-expression of its own, so its temporaries
// belong to the enclosing full-expression -- they must survive the body's
// reset, which is why the body's context merges out rather than restoring.
// The temporary dies at the end of the initialization, before next().
// CHECK-LABEL: define {{.*}} @_Z23do_return_operand_temp_v()
// CHECK: %[[TMP:.*]] = alloca %struct.T
// CHECK: invoke {{.*}} @_Z3valRK1T(ptr {{.*}} %[[TMP]])
// CHECK: call void @_ZN1TD1Ev(ptr {{.*}} %[[TMP]])
// CHECK: call void @_Z4nextv()
// CHECK: ret void
void do_return_operand_temp_() {
  int v = do { do_return val(T{1}) + 10; };
  next();
}

// Instantiation rebuilds every full-expression in the body, so it can steal the
// same way; the template path needs the same isolation.
// CHECK-LABEL: define {{.*}} @_Z13in_a_templateIiEvv()
// CHECK: %[[ARG:.*]] = alloca %struct.T
// CHECK: invoke {{.*}} @_Z4sink1Ti(ptr {{.*}} %[[ARG]],
// CHECK: invoke.cont:
// CHECK-NEXT: call void @_ZN1TD1Ev(ptr {{.*}} %[[ARG]])
// CHECK: call void @_Z4nextv()
// CHECK: ret void
template <typename U> void in_a_template() {
  int v = sink(T{1}, do {
    if (false)
      throw U{};
    do_return 3;
  });
  next();
}
template void in_a_template<int>();
