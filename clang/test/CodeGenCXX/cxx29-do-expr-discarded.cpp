// RUN: %clang_cc1 -std=c++2d -emit-llvm -triple x86_64-linux-gnu -fcxx-exceptions -fexceptions -O0 -disable-O0-optnone -o - %s | FileCheck %s

// The result temporary of a do-expression that nobody else owns.
//
// A do-expression yielding a class prvalue produces a temporary exactly as a
// function call does: `(void)e` materializes it and it dies at the end of the
// full-expression. That only happens if Sema wraps the do-expression in the
// `CXXBindTemporaryExpr` that owns a call's result, which it did not, so a
// discarded do-expression of non-trivially-destructible type simply leaked.
//
// The contexts where the result *is* owned by somebody else -- a named
// variable, a call argument, a bound reference -- have to keep exactly one
// destructor, so half of this file is about the double destruction that the
// obvious fix would introduce.

struct T {
  T();
  T(const T &);
  T(T &&);
  ~T();
  int x;
};

struct Trivial {
  Trivial();
  int x;
};

// The operand's own temporary elides into the result slot, so the result slot
// is the only object, and it is destroyed at the end of the full-expression.
// CHECK-LABEL: define {{.*}} @_Z17discarded_prvaluev()
// CHECK: %[[SLOT:.*]] = alloca %struct.T
// CHECK: call void @_ZN1TC1Ev(ptr {{.*}} %[[SLOT]])
// CHECK: doexpr.end:
// CHECK-NEXT: call void @_ZN1TD1Ev(ptr {{.*}} %[[SLOT]])
// CHECK-NEXT: ret void
void discarded_prvalue() { (void)(do { do_return T{}; }); }

// An NRVO candidate is still built straight into the result slot -- the
// temporary is the elision target, not a second object -- and the candidate's
// own destructor is suppressed by the NRVO flag on the do_return path, leaving
// the one call at the end of the full-expression.
// CHECK-LABEL: define {{.*}} @_Z14discarded_nrvov()
// CHECK: %[[SLOT:.*]] = alloca %struct.T
// CHECK: call void @_ZN1TC1Ev(ptr {{.*}} %[[SLOT]])
// CHECK-NOT: call void @_ZN1TC1EOS_
// CHECK: doexpr.end:
// CHECK-NEXT: call void @_ZN1TD1Ev(ptr {{.*}} %[[SLOT]])
// CHECK-NEXT: ret void
void discarded_nrvo() { (void)(do { T r; do_return r; }); }

// Two candidates defeat NRVO: the local is moved into the result slot, both
// locals die at the end of the body, and the result dies after. This is the
// shape that survives a fix that only handles the elided case.
// CHECK-LABEL: define {{.*}} @_Z13discarded_twob(
// CHECK: %[[SLOT:.*]] = alloca %struct.T
// CHECK: %[[A:.*]] = alloca %struct.T
// CHECK: %[[C:.*]] = alloca %struct.T
// CHECK: invoke void @_ZN1TC1EOS_(ptr {{.*}} %[[SLOT]], ptr {{.*}} %[[A]])
// CHECK: invoke void @_ZN1TC1EOS_(ptr {{.*}} %[[SLOT]], ptr {{.*}} %[[C]])
// CHECK: cleanup:
// CHECK-NEXT: call void @_ZN1TD1Ev(ptr {{.*}} %[[C]])
// CHECK-NEXT: call void @_ZN1TD1Ev(ptr {{.*}} %[[A]])
// CHECK: doexpr.end:
// CHECK-NEXT: call void @_ZN1TD1Ev(ptr {{.*}} %[[SLOT]])
// CHECK-NEXT: ret void
void discarded_two(bool b) {
  (void)(do {
    T a;
    T c;
    if (b)
      do_return a;
    do_return c;
  });
}

// A trivial destructor needs no cleanup at all: MaybeBindToTemporary declines
// to bind, so nothing here changed.
// CHECK-LABEL: define {{.*}} @_Z17discarded_trivialv()
// CHECK: call void @_ZN7TrivialC1Ev
// CHECK-NOT: call
// CHECK: ret void
void discarded_trivial() { (void)(do { do_return Trivial{}; }); }

// The result object is not constructed until `do_return` runs, so an exception
// escaping the body first must not destroy it. Here the local is the elision
// target, so the landing pad destroys it once as the local -- and there is no
// second cleanup for a result that was never produced.
// CHECK-LABEL: define {{.*}} @_Z16discarded_unwindv()
// CHECK: %[[SLOT:.*]] = alloca %struct.T
// CHECK: call void @_ZN1TC1Ev(ptr {{.*}} %[[SLOT]])
// CHECK: invoke void @_Z4boomv()
// CHECK: lpad:
// CHECK: call void @_ZN1TD1Ev(ptr {{.*}} %[[SLOT]])
// CHECK-NEXT: br label %eh.resume
void boom();
void discarded_unwind() {
  (void)(do {
    T t;
    boom();
    do_return t;
  });
}

//===----------------------------------------------------------------------===//
// Contexts where the result is owned elsewhere: exactly one destructor.
//===----------------------------------------------------------------------===//

// Initializing a variable: the variable owns the object, so the do-expression
// must not push a cleanup of its own.
// CHECK-LABEL: define {{.*}} @_Z12owned_by_varv()
// CHECK: %[[V:.*]] = alloca %struct.T
// CHECK: call void @_ZN1TC1Ev(ptr {{.*}} %[[V]])
// CHECK: doexpr.end:
// CHECK-NEXT: call void @_ZN1TD1Ev(ptr {{.*}} %[[V]])
// CHECK-NEXT: ret void
void owned_by_var() { T v = do { do_return T{}; }; }

// A by-value argument: destroyed after the call returns, and on the call's
// unwind edge, but only once on each.
// CHECK-LABEL: define {{.*}} @_Z11as_argumentv()
// CHECK: %[[ARG:.*]] = alloca %struct.T
// CHECK: call void @_ZN1TC1Ev(ptr {{.*}} %[[ARG]])
// CHECK: invoke void @_Z3arg1T(ptr {{.*}} %[[ARG]])
// CHECK: invoke.cont:
// CHECK-NEXT: call void @_ZN1TD1Ev(ptr {{.*}} %[[ARG]])
// CHECK-NEXT: ret void
// CHECK: lpad:
// CHECK: call void @_ZN1TD1Ev(ptr {{.*}} %[[ARG]])
// CHECK-NEXT: br label %eh.resume
void arg(T);
void as_argument() { arg(do { do_return T{}; }); }

// A member access materializes the temporary; the materialization owns it.
// CHECK-LABEL: define {{.*}} @_Z13member_accessv()
// CHECK: %[[TMP:.*]] = alloca %struct.T
// CHECK: call void @_ZN1TC1Ev(ptr {{.*}} %[[TMP]])
// CHECK: %[[X:.*]] = getelementptr {{.*}} %[[TMP]]
// CHECK: load i32, ptr %[[X]]
// CHECK-NEXT: call void @_ZN1TD1Ev(ptr {{.*}} %[[TMP]])
// CHECK-NOT: call void @_ZN1TD1Ev
// CHECK: ret i32
int member_access() { return (do { do_return T{}; }).x; }

// Lifetime extension through a bound reference still works: one construction,
// one destruction at the end of the enclosing block rather than the
// full-expression.
// CHECK-LABEL: define {{.*}} @_Z14bound_to_a_refv()
// CHECK: %[[R:.*]] = alloca %struct.T
// CHECK: call void @_ZN1TC1Ev(ptr {{.*}} %[[R]])
// CHECK: invoke void @_Z4usesRK1T(
// CHECK: invoke.cont:
// CHECK-NEXT: call void @_ZN1TD1Ev(ptr {{.*}} %[[R]])
// CHECK-NEXT: ret void
// CHECK: lpad:
// CHECK: call void @_ZN1TD1Ev(ptr {{.*}} %[[R]])
// CHECK-NEXT: br label %eh.resume
void uses(const T &);
void bound_to_a_ref() {
  const T &r = do { do_return T{}; };
  uses(r);
}

// A glvalue do-expression yields a reference to somebody else's object, so
// there is nothing to bind and nothing to destroy.
// CHECK-LABEL: define {{.*}} @_Z13discarded_refv()
// CHECK-NOT: call void @_ZN1TD1Ev
// CHECK: ret void
T &ref();
void discarded_ref() {
  (void)(do -> T & { do_return ref(); });
}

// A void do-expression has no result object either; the body's locals are
// still destroyed by the body's own scope.
// CHECK-LABEL: define {{.*}} @_Z14discarded_voidv()
// CHECK: %[[L:.*]] = alloca %struct.T
// CHECK: call void @_ZN1TC1Ev(ptr {{.*}} %[[L]])
// CHECK: call void @_ZN1TD1Ev(ptr {{.*}} %[[L]])
// CHECK-NOT: call void @_ZN1TD1Ev
// CHECK: ret void
void discarded_void() {
  (void)(do { T local; });
}

// One object through two levels: the inner result elides into the outer's slot
// and only the outer temporary is destroyed.
// CHECK-LABEL: define {{.*}} @_Z16discarded_nestedv()
// CHECK: %[[SLOT:.*]] = alloca %struct.T
// CHECK: call void @_ZN1TC1Ev(ptr {{.*}} %[[SLOT]])
// CHECK-NOT: call void @_ZN1TC1E
// CHECK: call void @_ZN1TD1Ev(ptr {{.*}} %[[SLOT]])
// CHECK-NOT: call void @_ZN1TD1Ev
// CHECK: ret void
void discarded_nested() {
  (void)(do { do_return(do { do_return T{}; }); });
}

// Instantiation goes through BuildDoExpr again, so the binding has to happen
// per instantiation rather than on the template.
// CHECK-LABEL: define {{.*}} @_Z13in_a_templateI1TEvv()
// CHECK: %[[SLOT:.*]] = alloca %struct.T
// CHECK: call void @_ZN1TC1Ev(ptr {{.*}} %[[SLOT]])
// CHECK: doexpr.end:
// CHECK-NEXT: call void @_ZN1TD1Ev(ptr {{.*}} %[[SLOT]])
// CHECK-NEXT: ret void
template <typename U> void in_a_template() { (void)(do { do_return U{}; }); }
template void in_a_template<T>();
