// RUN: %clang_cc1 -std=c++2d -fsyntax-only -verify %s
// expected-no-diagnostics

// A variable declared in a do-expression body is the expression's own, so
// constant evaluation may modify it.
//
// Sema gives the body a synthetic DeclContext only when the do-expression is
// not already inside a function, so at block scope the body's locals belong to
// the enclosing function and the evaluator could not match them to the frame
// it had pushed for the body. With no frame they looked like objects existing
// outside the expression: readable, not modifiable.

struct B {
  int v = 0;
  constexpr B &operator&=(const B &o) {
    v &= o.v;
    return *this;
  }
};

// Namespace scope: worked before, because the body has a DeclContext of its
// own there.
constexpr B at_namespace_scope = do {
  B x{6};
  x &= B{3};
  do_return x;
};
static_assert(at_namespace_scope.v == 2);

constexpr B from_a_constexpr_function() {
  return do {
    B x{6};
    x &= B{3};
    do_return x;
  };
}
static_assert(from_a_constexpr_function().v == 2);

void at_block_scope() {
  // The case that failed.
  constexpr B one_candidate = do {
    B x{6};
    x &= B{3};
    do_return x;
  };
  static_assert(one_candidate.v == 2);

  // Two candidates, so no elision -- worked before, must keep working.
  constexpr B two_candidates = do {
    B a{6};
    B b{7};
    a &= B{3};
    if (a.v)
      do_return a;
    do_return b;
  };
  static_assert(two_candidates.v == 2);

  // A local of a nested lambda is the lambda's, not the body's; mutating it is
  // allowed for its own reasons, and it must not be confused with ours.
  constexpr B through_a_lambda = do {
    do_return [] {
      B y{6};
      y &= B{3};
      return y;
    }();
  };
  static_assert(through_a_lambda.v == 2);

  // Reading one still works.
  constexpr int read_only = do {
    int k = 5;
    do_return k;
  };
  static_assert(read_only == 5);
}
