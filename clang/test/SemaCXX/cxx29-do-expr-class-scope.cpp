// RUN: %clang_cc1 -std=c++2d -verify -fsyntax-only %s

// expected-no-diagnostics

// A do-expression body parsed while CurContext is a class: a default member
// initializer, or a default argument of a member function. Neither supplies a
// DeclContext that a block-scope declaration can live in, and a do-expression
// creates no function scope of its own, so it has to synthesize one -- see
// Sema::ActOnStartDoExpr. Without that, a declaration in the body is built as
// a member of the class: a bogus "non-const static data member must be
// initialized out of line" for a plain local, and an assertion for a const
// local or a local class.
//
// P2806R5 leans on "works in every expression context" as the advantage over
// statement-expressions, and the default member initializer is one of the
// places statement-expressions cannot go, so these are load-bearing cases for
// the proposal rather than corner cases.
//
// These namespaces only *declare*. Reading the value back is a separate
// question -- the odr-use of a body local is marked later, from the implicit
// default constructor -- and lives in cxx29-do-expr-class-scope-use.cpp.

namespace dmi_plain_local {
  struct S { int x = do { int j = 10; do_return j; }; };
}

namespace dmi_const_local {
  // A const local is the shape that told us the synthetic context needs an
  // access specifier of its own when it is parented in a class.
  struct S { int x = do { const int j = 6; do_return j; }; };
}

namespace dmi_local_class {
  // A local class records its enclosing function, and the Itanium mangler
  // casts that function's type to FunctionProtoType unconditionally, so the
  // synthetic decl needs a prototype type rather than a no-proto one.
  struct S { int x = do { struct L { int a; }; L l{7}; do_return l.a; }; };
}

namespace dmi_static_local {
  struct S { int x = do { static int j = 4; do_return j; }; };
}

namespace dmi_init_capture {
  struct S { int x = do [k = 5] { do_return k; }; };
}

namespace dmi_nested_do {
  struct S {
    int x = do {
      int outer = do { int j = 2; do_return j * 3; };
      do_return outer + 1;
    };
  };
}

namespace default_argument {
  // A default argument declared inside a class is parsed with the same
  // CurContext as a default member initializer.
  struct S { static int f(int v = do { int j = 8; do_return j; }) { return v; } };

  struct WithLocalClass {
    static int f(int v = do { struct L { int a; }; L l{9}; do_return l.a; }) {
      return v;
    }
  };
}

namespace nested_class {
  struct Outer {
    struct Inner { int x = do { int j = 3; do_return j; }; };
    Inner i;
  };
}

namespace class_template {
  template <class T>
  struct S { T x = do { T j = T{11}; do_return j; }; };
  template struct S<int>;
  template struct S<long>;
}

// -- contexts that already worked. Kept here so that a change to the
//    condition above cannot regress them without a test noticing. ------------

namespace ctor_mem_initializer {
  // By the time a mem-initializer is parsed CurContext is the constructor, so
  // this path never needed a synthetic context.
  struct S {
    int m;
    constexpr S() : m(do { int j = 20; do_return j; }) {}
  };
  static_assert(S{}.m == 20);

  struct WithConst {
    int m;
    constexpr WithConst() : m(do { const int j = 15; do_return j * 2; }) {}
  };
  static_assert(WithConst{}.m == 30);
}

namespace dmi_no_declaration {
  // No declaration in the body: nothing to misparent, so these worked before
  // and must keep working.
  struct Simple { int x = do { do_return 5; }; };
  static_assert(Simple{}.x == 5);

  struct Branchy { int x = do { if (true) { do_return 1; } do_return 2; }; };
  static_assert(Branchy{}.x == 1);
}

namespace dmi_lambda_equivalent {
  // The equivalent immediately-invoked lambda, which gets its DeclContext from
  // the call operator and has never had this problem.
  struct S { int x = []{ int j = 5; return j; }(); };
  static_assert(S{}.x == 5);
}
