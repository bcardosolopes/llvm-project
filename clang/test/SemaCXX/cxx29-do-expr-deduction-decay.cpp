// RUN: %clang_cc1 -std=c++2d -fsyntax-only -verify %s

// The result type is deduced by the rules in [dcl.spec.auto.general], i.e. as
// if by `auto x = e;`, so a by-value deduction never yields a cv-qualified
// type. Keeping the operand's own qualification made a `do_return` of a
// `const S` lvalue deduce `const S` and conflict with a later one that a
// lambda unifies.

struct S {
  int x;
};
const S *cget();
S *get();

// Two operands a lambda agrees on, so the do-expression must agree too.
S both_ways(bool b) {
  return do {
    if (b)
      do_return *cget();
    do_return S{};
  };
}

// A single const operand deduces the unqualified type, so the result is an
// ordinary prvalue that can initialize a non-const object.
void single_const_operand() {
  S s = do { do_return *cget(); };
  static_assert(__is_same(decltype(do { do_return *cget(); }), S));
  (void)s;
}

// Pointer cv is *not* top-level, so [dcl.spec.auto.general] keeps it and these
// two genuinely deduce different types -- as they do for a lambda and for a
// plain `auto` function.
const S *pointer_qualification_still_conflicts(bool b) {
  return do {
    if (b)
      do_return cget();
    do_return get(); // expected-error {{'do_return' yields type 'S *', conflicting with previously deduced type 'const S *'}}
  };
}

// An explicit trailing return type keeps whatever it says; it is not deduced.
const S &explicit_type_is_not_deduced() {
  return do -> const S & { do_return *cget(); };
}
