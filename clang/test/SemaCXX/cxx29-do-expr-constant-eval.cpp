// RUN: %clang_cc1 -std=c++2d -verify -fsyntax-only %s
// expected-no-diagnostics

// Constant-evaluating a do-expression body. The evaluator enters the body as a
// synthetic call frame, and the two things that frame has to get right are an
// init-hoist declaration's lifetime and the object whose initializer is being
// evaluated.

namespace init_capture_lifetime {
  // An init-hoist declaration is initialized before the body is entered and
  // lives to the end of the enclosing full-expression, so its storage belongs
  // to the frame *enclosing* the body, not to the body's own frame -- which is
  // popped as soon as the body ends.
  constexpr int ns = do [k = 5] { do_return k; };
  static_assert(ns == 5);

  struct S {
    int x = do [k = 5] { do_return k; };
  };
  static_assert(S{}.x == 5);

  // Several of them, and one reading another.
  constexpr int several = do [a = 2, b = a * 3] { do_return a + b; };
  static_assert(several == 8);

  // An init-hoist declaration read after a body local shadow-free reference,
  // and one whose value the body mutates through a copy.
  constexpr int with_body_local = do [k = 5] {
    int local = k * 2;
    do_return local + k;
  };
  static_assert(with_body_local == 15);

  // Nested do-expressions, each with their own init-hoist declaration.
  constexpr int nested = do [outer = 10] {
    do_return outer + do [inner = 4] { do_return inner; };
  };
  static_assert(nested == 14);

  // The controls, which worked before and must keep working.

  // A plain body local, rather than an init-hoist declaration.
  constexpr int body_local = do { int k = 5; do_return k; };
  static_assert(body_local == 5);

  // The same init-hoist declaration, in a constexpr function.
  constexpr int in_a_constexpr_fn() { return do [k = 5] { do_return k; }; }
  static_assert(in_a_constexpr_fn() == 5);

  // The same init-hoist declaration, at runtime.
  int at_runtime() { return do [k = 5] { do_return k; }; }
}

// The other half -- a body that names a non-static member of the class whose
// default member initializer it is -- is in
// cxx29-do-expr-class-scope-members.cpp, namespace constant_evaluation_of_this,
// next to the shapes that establish it is only constant evaluation that failed.
