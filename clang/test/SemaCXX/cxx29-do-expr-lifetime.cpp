// RUN: %clang_cc1 -std=c++2d -verify -fsyntax-only %s

// Reference-typed do-expressions follow the same lifetime semantics as a
// function returning a reference: a temporary materialized in `do_return`
// is *not* lifetime-extended through the do-expression boundary. This
// makes the do-expression "the same as the equivalent IIFE" for binding —
// no surprise extension, and the existing return-binding diagnostics fire.

struct T { int v; };
T prvalue();
T &lvalue();
const T &const_lvalue();

void direct_binding_still_works() {
  // Sanity: direct prvalue binding to a reference *does* extend (not via
  // do-expression — just regular C++).
  const T &r1 = prvalue();
  (void)r1;
}

void r3_prvalue_dangles() {
  // r3-style: prvalue operand to a reference-typed do-expression. Since
  // C++26 ([class.temporary]p6.11) this is ill-formed, just like the
  // analogous IIFE.
  const T &r3 = do -> const T & {
    do_return prvalue(); // expected-error {{returning reference to local temporary object}}
  };
  (void)r3;
}

void r4_mixed_paths() {
  // Mixed paths: the lvalue path is fine, the prvalue path dangles. Each
  // path is checked independently; only the prvalue path diagnoses.
  bool c = true;
  const T &r4 = do -> const T & {
    if (c) do_return lvalue();
    do_return prvalue(); // expected-error {{returning reference to local temporary object}}
  };
  (void)r4;
}

void r5_body_local_dangles() {
  // r5-style: do_return of a body-local lvalue. The local dies at the end
  // of its enclosing CompoundStmt; the outer reference would dangle.
  const T &r5 = do -> const T & {
    T x{1};
    do_return x; // expected-warning {{reference to stack memory associated with local variable 'x' returned}}
  };
  (void)r5;
}

void rvalue_ref_dangles_too() {
  // The same rules apply to T&& result types.
  T &&r = do -> T && {
    do_return prvalue(); // expected-error {{returning reference to local temporary object}}
  };
  (void)r;
}

void lvalue_binding_works() {
  // Binding to an lvalue returned from a function: no temporary, no dangle.
  // (Compile-only here; runtime correctness is exercised by the codegen test.)
  const T &r = do -> const T & { do_return lvalue(); };
  (void)r;
  const T &rc = do -> const T & { do_return const_lvalue(); };
  (void)rc;
}

void temp_local_workaround() {
  // The prvalue→reference dangle can be avoided by stashing in a local
  // declared OUTSIDE the do-expression body — the local outlives the
  // do-expression and the reference binds normally. No diagnostic: the
  // do-expression's lifetime boundary is the do-expression itself, not the
  // enclosing function, so yielding a reference to an enclosing local is
  // fine here (and checked at the consumer if it escapes further).
  T outer{5};
  const T &r = do -> const T & { do_return outer; };
  (void)r;
}

const T &outer_local_escaping_function_dangles() {
  // But the deferred check still fires where the completed do-expression
  // escapes the function.
  T outer{5};
  return do -> const T & { do_return outer; }; // expected-warning {{reference to stack memory associated with local variable 'outer' returned}}
}

const T &outer_local_escaping_through_ref_dangles() {
  T outer{5};
  const T &r = do -> const T & { do_return outer; };
  return r; // expected-warning {{reference to stack memory associated with local variable 'outer' returned}}
  // expected-note@-2 {{binding reference variable 'r' here}}
}

// An init-capture is a variable whose lifetime extends to the end of the
// full-expression containing the do-expression, *not* until the `do_return` or
// the `}` (unlike an ordinary block-scoped local). So a reference into an
// init-capture only dangles if the do-expression's *result* escapes the
// full-expression. Whether that happens is determined at the binding site, not
// at the `do_return`.
int sink(T);                                      // by value
int sink_ref(const T &);                          // by reference, not lifetimebound
const T &id([[clang::lifetimebound]] const T &x) { return x; }

void init_capture_escapes_dangles() {
  // The do-expression yields a reference into init-capture `x`; binding it to a
  // reference that outlives the full-expression dangles.
  auto &&r = do [x = prvalue()] -> const T & { do_return id(x); }; // expected-warning {{temporary bound to local reference 'r' will be destroyed at the end of the full-expression}}
  (void)r;
}

void init_capture_consumed_by_value_ok() {
  // The reference into `x` is read within the full-expression (passed by value),
  // while `x` is still alive. No dangle.
  auto &&r = sink(do [x = prvalue()] -> const T & { do_return id(x); });
  (void)r;
}

void init_capture_consumed_by_ref_ok() {
  // Passed to a non-lifetimebound reference parameter and consumed within the
  // full-expression. No dangle.
  auto &&r = sink_ref(do [x = prvalue()] -> const T & { do_return id(x); });
  (void)r;
}

void init_capture_discarded_ok() {
  // The do-expression's result does not escape the full-expression at all.
  (void)(do [x = prvalue()] -> const T & { do_return id(x); });
}

void init_capture_to_init_capture_ok() {
  // Two init-captures of the same do-expression are co-extensive, so binding
  // one to another (by reference or by address) never dangles.
  int v = do [x = 1, r = x] { r };
  int w = do [x = 1, p = &x] { *p };
  (void)v;
  (void)w;
}
