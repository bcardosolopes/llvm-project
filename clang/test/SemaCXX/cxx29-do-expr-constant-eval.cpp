// RUN: %clang_cc1 -std=c++2d -verify -fsyntax-only %s

// Constant-evaluating a do-expression body. The evaluator enters the body as a
// synthetic call frame, and two things that frame should carry, it does not:
// an init-capture's lifetime, and the object whose initializer is being
// evaluated.
//
// Neither is fixed here. This file records what the compiler does today, with
// the working controls beside it, so that fixing either one fails loudly here
// rather than quietly starting to pass.

namespace init_capture_lifetime {
  // FIXME: an init-capture is initialized before the body is entered and lives
  // to the end of the enclosing full-expression. The evaluator appears to end
  // its lifetime with the wrong frame, so reading it from the body is a read
  // of a destroyed object. This is not specific to class scope -- the
  // namespace-scope case below is the whole bug -- and it predates the
  // class-scope fixes.
  constexpr int ns = do [k = 5] { do_return k; }; // expected-error {{constexpr variable 'ns' must be initialized by a constant expression}}
                                                  // expected-note@-1 {{destroying object 'k' whose lifetime has already ended}}

  struct S {
    int x = do [k = 5] { do_return k; }; // expected-note {{destroying object 'k' whose lifetime has already ended}}
  };
  static_assert(S{}.x == 5); // expected-error {{static assertion expression is not an integral constant expression}}

  // The controls, which all work.

  // A plain body local, rather than an init-capture.
  constexpr int body_local = do { int k = 5; do_return k; };
  static_assert(body_local == 5);

  // The same init-capture, in a constexpr function.
  constexpr int in_a_constexpr_fn() { return do [k = 5] { do_return k; }; }
  static_assert(in_a_constexpr_fn() == 5);

  // The same init-capture, at runtime.
  int at_runtime() { return do [k = 5] { do_return k; }; }
}

// The other half -- a body that names a non-static member of the class whose
// default member initializer it is -- is recorded in
// cxx29-do-expr-class-scope-members.cpp, namespace constant_evaluation_of_this,
// next to the shapes that establish it is only constant evaluation that fails.
