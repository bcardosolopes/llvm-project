// RUN: %clang_cc1 -std=c++2d -verify -fsyntax-only %s

// expected-no-diagnostics

// Reading back the value of a do-expression that lives in a default member
// initializer. Declaring one is cxx29-do-expr-class-scope.cpp; using one is a
// separate question, because a default member initializer is not odr-used
// where it is written -- the marking happens later, from the implicit default
// constructor, with CurContext set to that constructor rather than to the
// do-expression body.
//
// tryCaptureVariable therefore sees a DeclRefExpr to a variable whose
// DeclContext is not the current function and, absent an exemption, reports
// "reference to local variable 'j' declared in enclosing function". There is
// no boundary to cross: a do-expression body is block scope, and its
// synthetic DeclContext only looks like a function.

namespace read_plain_local {
  struct S { int x = do { int j = 10; do_return j; }; };
  static_assert(S{}.x == 10);
}

namespace read_const_local {
  struct S { int x = do { const int j = 6; do_return j; }; };
  static_assert(S{}.x == 6);
}

namespace read_local_class {
  struct S { int x = do { struct L { int a; }; L l{7}; do_return l.a; }; };
  static_assert(S{}.x == 7);
}

namespace read_nested_do {
  struct S {
    int x = do {
      int outer = do { int j = 2; do_return j * 3; };
      do_return outer + 1;
    };
  };
  static_assert(S{}.x == 7);
}

namespace read_through_another_class {
  // The odr-use is marked from *this* class's implicit constructor, one
  // further removed from where the body was written.
  struct Inner { int x = do { int j = 11; do_return j; }; };
  struct Outer { Inner i; int y = i.x; };
  static_assert(Outer{}.y == 11);
}

namespace read_default_argument {
  struct S {
    static constexpr int f(int v = do { int j = 8; do_return j; }) { return v; }
  };
  static_assert(S::f() == 8);
}

namespace read_in_a_template {
  template <class T>
  struct S { T x = do { T j = T{11}; do_return j; }; };
  static_assert(S<int>{}.x == 11);
  static_assert(S<long>{}.x == 11L);
}

namespace body_local_still_not_visible_outside {
  // The exemption must not make body locals nameable from outside; it only
  // says they are never *captured*.
  struct S { int x = do { int j = 1; do_return j; }; };
  int outside() { return sizeof(S); }
}
