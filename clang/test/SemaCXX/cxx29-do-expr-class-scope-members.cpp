// RUN: %clang_cc1 -std=c++2d -verify -fsyntax-only %s

// Naming a member of the enclosing class from a do-expression body that is
// itself in that class -- an implicit member access, or an access-controlled
// one.
//
// Both go through Sema::getFunctionLevelDeclContext, which walks out of the
// contexts that are not really function scopes (a BlockDecl, an EnumDecl, a
// RequiresExprBodyDecl) to answer "which function am I in?". The synthetic
// context of a do-expression body belongs on that list for exactly the same
// reason those do: it is a place to hang declarations, not a scope the
// language knows about. Without it, `a` below is "invalid use of non-static
// data member".

namespace implicit_member_access {
  struct S {
    int a = 1;
    int x = do { int j = a + 10; do_return j; };
  };
  int use() { return S{}.x; }
}

namespace explicit_this {
  struct S {
    int a = 2;
    int x = do { do_return this->a + 10; };
  };
  int use() { return S{}.x; }
}

namespace member_function_call {
  struct S {
    constexpr int a() const { return 3; }
    int x = do { do_return a() + 10; };
  };
  int use() { return S{}.x; }
}

namespace private_member {
  // The access check must be performed as if from the class, not from the
  // synthetic context.
  struct S {
  private:
    static constexpr int secret = 3;
    constexpr int hidden() const { return 4; }

  public:
    int x = do { int j = secret; do_return j; };
    int y = do { do_return hidden(); };
  };
  int use() { return S{}.x + S{}.y; }
}

namespace static_member {
  struct S {
    static constexpr int k = 5;
    int x = do { do_return k; };
  };
  static_assert(S{}.x == 5);
}

namespace member_in_default_argument {
  struct S {
    static constexpr int k = 6;
    static constexpr int f(int v = do { do_return k; }) { return v; }
  };
  static_assert(S::f() == 6);
}

namespace member_through_nested_do {
  struct S {
    int a = 7;
    int x = do { do_return do { do_return a; }; };
  };
  int use() { return S{}.x; }
}

namespace lambda_inside_a_body_in_a_dmi {
  // A lambda *does* introduce a function scope, and capturing `this` through
  // one from inside a do-expression body has to keep working.
  struct S {
    int a = 8;
    int x = do { do_return [this] { return a; }(); };
  };
  int use() { return S{}.x; }
}

namespace constant_evaluation_of_this {
  // FIXME: constant-evaluating a default member initializer whose
  // do-expression body names a *non-static* member does not work. The
  // evaluator enters the body as a synthetic call frame ("in call to
  // '<expression body>'") that carries no object, so the implicit `this` is
  // unavailable even though the initializer is being evaluated for a concrete
  // object. Sema is happy -- the namespaces above compile -- and the same
  // body inside a constexpr member function, or the equivalent
  // immediately-invoked lambda in the same default member initializer, both
  // evaluate fine, so this is specific to the do-expression's synthetic frame
  // rather than to default member initializers.
  //
  // The diagnostics below record today's behavior so that fixing it is
  // noticed here.
  struct S {
    int a = 1;
    int x = do { do_return a + 10; }; // expected-note {{implicit use of 'this' pointer is only allowed within the evaluation of a call to a 'constexpr' member function}}
                                      // expected-note@-1 {{in call to '<expression body>'}}
  };
  static_assert(S{}.x == 11); // expected-error {{static assertion expression is not an integral constant expression}}

  // The controls, which all work.
  struct InConstexprMemberFn {
    int a = 1;
    constexpr int f() const { return do { do_return a + 10; }; }
  };
  static_assert(InConstexprMemberFn{}.f() == 11);

  struct AsLambda {
    int a = 1;
    int x = [this] { return a + 10; }();
  };
  static_assert(AsLambda{}.x == 11);
}
