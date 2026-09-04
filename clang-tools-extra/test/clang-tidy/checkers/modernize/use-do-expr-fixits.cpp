// RUN: %check_clang_tidy -std=c++23 %s modernize-use-do-expr %t

// The codemod half of the check. Everything rewritten here has been compiled
// by a `do`-expression clang and checked to behave identically to the lambda
// it replaced; see the campaign's differential harness.

int g(int);

//===----------------------------------------------------------------------===//
// The rewrite
//===----------------------------------------------------------------------===//

int in_initializer(int a) {
  int x = [&] { return a + 1; }();
  // CHECK-MESSAGES: :[[@LINE-1]]:11: warning: immediately-invoked lambda can be replaced by a 'do' expression (category: simple) [modernize-use-do-expr]
  // CHECK-FIXES: int x = do { do_return a + 1; };
  return x;
}

int several_returns(int a, int b) {
  const int x = [&] {
    if (a > b)
      return a;
    return b;
  }();
  // CHECK-MESSAGES: :[[@LINE-5]]:17: warning: immediately-invoked lambda can be replaced by a 'do' expression (category: control-flow-workaround) [modernize-use-do-expr]
  // CHECK-FIXES:      const int x = do {
  // CHECK-FIXES-NEXT:   if (a > b)
  // CHECK-FIXES-NEXT:     do_return a;
  // CHECK-FIXES-NEXT:   do_return b;
  // CHECK-FIXES-NEXT: };
  return x;
}

// An explicit trailing return type carries over; it is not always deducible
// from the body, so dropping it can change the type of the expression.
long trailing_return_type(int a) {
  return [&]() -> long { return a; }();
  // CHECK-MESSAGES: :[[@LINE-1]]:10: warning: immediately-invoked lambda can be replaced by a 'do' expression (category: simple) [modernize-use-do-expr]
  // CHECK-FIXES: return do -> long { do_return a; };
}

// Only the outer body's returns are rewritten: the inner ones belong to the
// inner lambda and must be left exactly as they are.
int nested_lambda(int a) {
  return [&] {
    auto f = [&](int v) { return v + 1; };
    return f(a);
  }();
  // CHECK-MESSAGES: :[[@LINE-4]]:10: warning: immediately-invoked lambda can be replaced by a 'do' expression (category: simple) [modernize-use-do-expr]
  // CHECK-FIXES:      return do {
  // CHECK-FIXES-NEXT:   auto f = [&](int v) { return v + 1; };
  // CHECK-FIXES-NEXT:   do_return f(a);
  // CHECK-FIXES-NEXT: };
}

// Same for a nested function's returns.
int nested_function(int a) {
  return [&] {
    struct S {
      static int h(int v) { return v; }
    };
    return S::h(a);
  }();
  // CHECK-MESSAGES: :[[@LINE-6]]:10: warning: immediately-invoked lambda can be replaced by a 'do' expression (category: simple) [modernize-use-do-expr]
  // CHECK-FIXES:      static int h(int v) { return v; }
  // CHECK-FIXES: do_return S::h(a);
}

// A `do` expression is not required to produce a value.
void void_body(int a) {
  [&] { g(a); }();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: immediately-invoked lambda can be replaced by a 'do' expression (category: simple) [modernize-use-do-expr]
  // CHECK-FIXES: (do { g(a); });
}

//===----------------------------------------------------------------------===//
// Statement position
//===----------------------------------------------------------------------===//

// A bare `do {` where a statement is expected is a do-while loop, so the
// rewrite has to be parenthesized -- and a discarded value would newly trip
// -Wunused-value, which a discarded call never did, so it is cast away.
void discarded_value(int a) {
  [&] { return g(a); }();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: immediately-invoked lambda can be replaced by a 'do' expression (category: simple) [modernize-use-do-expr]
  // CHECK-FIXES: (void)(do { do_return g(a); });
}

void as_a_branch(int a) {
  if (a)
    [&] { return g(a); }();
  // CHECK-MESSAGES: :[[@LINE-1]]:5: warning: immediately-invoked lambda can be replaced by a 'do' expression (category: simple) [modernize-use-do-expr]
  // CHECK-FIXES: (void)(do { do_return g(a); });
}

// A statement whose *outermost* expression starts at the lambda still needs the
// parentheses, even though the value is used and so no cast is wanted: a bare
// `do {` there would still parse as a do-while loop.
struct S {
  int m() const;
  int f;
};
void statement_starts_with_the_lambda() {
  [] { return S{}; }().m();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: immediately-invoked lambda can be replaced by a 'do' expression (category: simple) [modernize-use-do-expr]
  // CHECK-FIXES: (do { do_return S{}; }).m();
}

// The whole statement is discarded, but this call's value is not: it is an
// argument, or the object of a member access. `f((void)(do { ... }))` does not
// compile, so the cast must key on the value being unused rather than on the
// statement position that the (void) heuristic used to look at.
void takes(S s);
void takes_int(int i);
void value_is_used_in_a_discarded_statement() {
  takes([] { return S{}; }());
  // CHECK-MESSAGES: :[[@LINE-1]]:9: warning: immediately-invoked lambda can be replaced by a 'do' expression (category: simple) [modernize-use-do-expr]
  // CHECK-FIXES: takes(do { do_return S{}; });
  takes_int([] { return S{}; }().f);
  // CHECK-MESSAGES: :[[@LINE-1]]:13: warning: immediately-invoked lambda can be replaced by a 'do' expression (category: simple) [modernize-use-do-expr]
  // CHECK-FIXES: takes_int(do { do_return S{}; }.f);
}

// An explicit cast to void in the source already discards the value; a second
// one would only add noise.
void already_discarded(int a) {
  (void)([&] { return g(a); }());
  // CHECK-MESSAGES: :[[@LINE-1]]:10: warning: immediately-invoked lambda can be replaced by a 'do' expression (category: simple) [modernize-use-do-expr]
  // CHECK-FIXES: (void)(do { do_return g(a); });
}

// The left operand of a comma is discarded, and warns about it, so it gets the
// cast even though it is not the whole statement.
void comma_left_operand(int a) {
  [&] { return g(a); }(), g(a);
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: immediately-invoked lambda can be replaced by a 'do' expression (category: simple) [modernize-use-do-expr]
  // CHECK-FIXES: (void)(do { do_return g(a); }), g(a);
}

// A void-returning lambda never needed the cast, only the parentheses.
void discarded_void() {
  [] {}();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: immediately-invoked lambda can be replaced by a 'do' expression (category: simple) [modernize-use-do-expr]
  // CHECK-FIXES: (do {});
}

// Not statement position: no parentheses needed, and the value is used.
int as_a_subexpression(int a) {
  return [&] { return a; }() + 1;
  // CHECK-MESSAGES: :[[@LINE-1]]:10: warning: immediately-invoked lambda can be replaced by a 'do' expression (category: simple) [modernize-use-do-expr]
  // CHECK-FIXES: return do { do_return a; } + 1;
}

//===----------------------------------------------------------------------===//
// Gates: diagnosed, never rewritten
//===----------------------------------------------------------------------===//

// Falling off the end is undefined behaviour in the lambda and ill-formed as a
// do-expression, so the rewrite would turn a latent bug into a build failure.
// It is also worth counting on its own.
int falls_off_end(int a) {
  return [&]() -> int {
    if (a)
      return 1;
  }();
  // CHECK-MESSAGES: :[[@LINE-4]]:10: warning: immediately-invoked lambda cannot be replaced by a 'do' expression (category: falls-off-end) [modernize-use-do-expr]
  // CHECK-FIXES: return [&]() -> int {
}

// A label is function-scoped: moving one out of the lambda can collide with a
// label already in the enclosing function, and the `goto` stops being confined.
int label_in_body(int a) {
  return [&] {
  again:
    if (a-- > 0)
      goto again;
    return a;
  }();
  // CHECK-MESSAGES: :[[@LINE-6]]:10: warning: immediately-invoked lambda cannot be replaced by a 'do' expression (category: label-or-goto) [modernize-use-do-expr]
  // CHECK-FIXES: return [&] {
}

// Dropping the `noexcept` turns a call to std::terminate into a propagating
// exception.
int explicit_noexcept(int a) {
  return [&]() noexcept { return a; }();
  // CHECK-MESSAGES: :[[@LINE-1]]:10: warning: immediately-invoked lambda cannot be replaced by a 'do' expression (category: explicit-specifier) [modernize-use-do-expr]
  // CHECK-FIXES: return [&]() noexcept { return a; }();
}

int consteval_lambda() {
  return []() consteval { return 1; }();
  // CHECK-MESSAGES: :[[@LINE-1]]:10: warning: immediately-invoked lambda cannot be replaced by a 'do' expression (category: explicit-specifier) [modernize-use-do-expr]
  // CHECK-FIXES: return []() consteval { return 1; }();
}

// An implicitly constexpr lambda -- every qualifying lambda since C++17 -- is
// not a gate: a do-expression is usable in a constant expression too.
constexpr int implicitly_constexpr() {
  return [] { return 1; }();
  // CHECK-MESSAGES: :[[@LINE-1]]:10: warning: immediately-invoked lambda can be replaced by a 'do' expression (category: simple) [modernize-use-do-expr]
  // CHECK-FIXES: return do { do_return 1; };
}

// A rewrite inside a macro body would corrupt every other expansion of it.
#define IIL(x) [&] { return (x); }()
int in_macro(int a) {
  return IIL(a);
  // CHECK-MESSAGES: :[[@LINE-1]]:10: warning: immediately-invoked lambda cannot be replaced by a 'do' expression (category: in-macro) [modernize-use-do-expr]
  // CHECK-FIXES: return IIL(a);
}
