// RUN: %clang_cc1 -std=c++2d -ast-dump -ast-dump-filter=fn %s | FileCheck %s
//
// Which do-expressions get a copy-elision candidate, as Sema decides it.
// The candidate is the body local that every value-yielding `do_return`
// names; CodeGen then builds it in the do-expression's result slot. See
// CodeGenCXX/cxx29-do-expr-nrvo.cpp for the emission itself.

struct T {
  T();
  T(const T &);
  T(T &&) noexcept;
  ~T();
};

// CHECK-LABEL: FunctionDecl {{.*}} fn_one_candidate
// CHECK: DoExpr {{.*}} nrvo_candidate(Var {{.*}} 'r' 'T')
T fn_one_candidate() {
  return do {
    T r;
    do_return r;
  };
}

// Several `do_return`s naming the same variable keep it.
// CHECK-LABEL: FunctionDecl {{.*}} fn_same_candidate_twice
// CHECK: DoExpr {{.*}} nrvo_candidate(Var {{.*}} 'r' 'T')
T fn_same_candidate_twice(bool b) {
  return do {
    T r;
    if (b)
      do_return r;
    do_return r;
  };
}

// A const local is not move-eligible, but it is still copy-elidable.
// CHECK-LABEL: FunctionDecl {{.*}} fn_const_candidate
// CHECK: DoExpr {{.*}} nrvo_candidate(Var {{.*}} 'r' 'const T')
T fn_const_candidate() {
  return do {
    const T r;
    do_return r;
  };
}

// Two locals would have to share the one result slot.
// CHECK-LABEL: FunctionDecl {{.*}} fn_two_candidates
// CHECK-NOT: DoExpr {{.*}} nrvo_candidate
T fn_two_candidates(bool b) {
  return do {
    T a;
    T b2;
    if (b)
      do_return a;
    do_return b2;
  };
}

// So would a prvalue operand on another path.
// CHECK-LABEL: FunctionDecl {{.*}} fn_mixed_with_prvalue
// CHECK-NOT: DoExpr {{.*}} nrvo_candidate
T fn_mixed_with_prvalue(bool b) {
  return do {
    T r;
    if (b)
      do_return T();
    do_return r;
  };
}

// A variable declared outside the body outlives the do-expression, so it is
// neither move-eligible nor elidable ([expr.prim.id.unqual]p15.4).
// CHECK-LABEL: FunctionDecl {{.*}} fn_enclosing_local
// CHECK-NOT: DoExpr {{.*}} nrvo_candidate
T fn_enclosing_local() {
  T outer;
  return do { do_return outer; };
}

// An outer-scope `return` claims the variable for the function's return slot
// first, and the two slots cannot both alias it.
// CHECK-LABEL: FunctionDecl {{.*}} fn_outer_return_wins
// CHECK-NOT: DoExpr {{.*}} nrvo_candidate
T fn_outer_return_wins(bool b) {
  T t = do {
    T r;
    if (b)
      return r;
    do_return r;
  };
  return t;
}

// A reference result has no result object to build into.
// CHECK-LABEL: FunctionDecl {{.*}} fn_reference_result
// CHECK-NOT: DoExpr {{.*}} nrvo_candidate
T &fn_reference_result(T &g) {
  return do -> T & { do_return g; };
}

// A candidate whose type differs from the result type is at most
// move-eligible, never elidable.
struct U {
  U(T);
};
// CHECK-LABEL: FunctionDecl {{.*}} fn_converted
// CHECK-NOT: DoExpr {{.*}} nrvo_candidate
U fn_converted() {
  return do -> U {
    T r;
    do_return r;
  };
}

// The decision is made per instantiation, on the instantiated variable.
template <class X> X fn_template() {
  return do {
    X r;
    do_return r;
  };
}
template T fn_template<T>();
// CHECK-LABEL: FunctionDecl {{.*}} fn_template 'T ()' explicit_instantiation_definition
// CHECK: DoExpr {{.*}} nrvo_candidate(Var {{.*}} 'r' 'T')
