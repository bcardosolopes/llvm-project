// RUN: %clang_cc1 -std=c++2d -verify -fsyntax-only %s

// P2806 specifies a do-expression as having the semantics of an immediately
// invoked lambda, and the body of a lambda is transparent to pack expansion:
// naming a pack inside one defers to the expansion that encloses the lambda,
// and the body is duplicated once per element. A do-expression body behaves the
// same way, so every case below has an immediately-invoked-lambda spelling that
// means the same thing.
//
// This is *not* the GNU statement-expression rule. A statement expression may
// contain labels, so it cannot be duplicated and a pack cannot be expanded
// across one; `stmt_expr_is_still_a_barrier` below keeps that distinction.

namespace fold_over_a_body {
  template <class... Ts>
  constexpr int sum(Ts... ts) {
    return (do { do_return ts * 2; } + ... + 0);
  }
  static_assert(sum(1, 2, 3) == 12);
  static_assert(sum() == 0);
  static_assert(sum(7) == 14);
}

namespace expand_into_arguments {
  constexpr int add3(int a, int b, int c) { return a + b + c; }

  template <class... Ts>
  constexpr int call(Ts... ts) {
    return add3(do { do_return ts + 1; }...);
  }
  static_assert(call(10, 20, 30) == 63);
}

namespace pack_in_a_body_statement {
  // The pack is named by a statement of the body rather than by the
  // `do_return` operand. Collection has to search the whole body once the
  // do-expression is known to contain a pack, exactly as it does for a lambda:
  // pruning at the first non-expression statement would find nothing to expand.
  template <class... Ts>
  constexpr int sum(Ts... ts) {
    return (do {
      int acc = 0;
      for (int i = 0; i < 3; ++i)
        acc += ts;
      do_return acc;
    } + ... + 0);
  }
  static_assert(sum(1, 2) == 9);
}

namespace body_declares_locals {
  // Each expansion of the pattern produces its own copy of every declaration in
  // the body, so the body needs an instantiation scope of its own -- otherwise
  // the second expansion collides with the first.
  template <class... Ts>
  constexpr int sum(Ts... ts) {
    return (do {
      auto v = ts;
      auto w = v + 1;
      do_return static_cast<int>(w);
    } + ... + 0);
  }
  static_assert(sum(1, 2, 3) == 9);
}

namespace type_of_the_body {
  // The pack appears only in the explicit result type.
  template <class... Ts>
  constexpr int count() {
    return (0 + ... + do -> int { do_return static_cast<int>(sizeof(Ts)); });
  }
  static_assert(count<char, char, char>() == 3);
  static_assert(count<char, int>() == 1 + sizeof(int));
}

namespace nested_in_a_lambda {
  // The expansion is outside the lambda, which is outside the do-expression.
  // Both have to carry the pack out.
  template <class... Ts>
  constexpr int sum(Ts... ts) {
    return ([&] { return do { do_return ts * 3; }; }() + ... + 0);
  }
  static_assert(sum(1, 2) == 9);

  // ... and the other nesting order.
  template <class... Ts>
  constexpr int sum2(Ts... ts) {
    return (do { do_return [&] { return ts * 3; }(); } + ... + 0);
  }
  static_assert(sum2(1, 2) == 9);
}

namespace nested_do_expressions {
  template <class... Ts>
  constexpr int sum(Ts... ts) {
    return (do { do_return do { do_return ts + 1; } * 2; } + ... + 0);
  }
  static_assert(sum(1, 2) == 10);
}

namespace genuinely_unexpanded {
  // No enclosing expansion, so the pack really is unexpanded and has to be
  // diagnosed. Nothing downstream looks at a `do_return` operand as a whole,
  // so the diagnosis has to happen when the statement is built.
  template <class... Ts>
  constexpr int bad(Ts... ts) {
    return do { do_return ts; }; // expected-error {{expression contains unexpanded parameter pack 'ts'}}
  }

  // A statement expression may contain labels, so it cannot be duplicated and a
  // pack cannot be expanded across one -- not even by way of a do-expression
  // inside it. An immediately-invoked lambda in the same position is diagnosed
  // the same way.
  template <class... Ts>
  int stmt_expr_is_still_a_barrier(Ts... ts) {
    return (({ int r = do { do_return ts; }; r; }) // expected-error {{expression contains unexpanded parameter pack 'ts'}}
            + ... + 0);                            // expected-error {{pack expansion does not contain any unexpanded parameter packs}}
  }

  // A pack named by a body statement is reported at the do-expression, with the
  // reference itself as the highlighted range -- the same shape a lambda gets,
  // because it is the whole construct that would have to be expanded.
  template <class... Ts>
  constexpr int bad_in_a_statement(Ts... ts) {
    return do { // expected-error {{expression contains unexpanded parameter pack 'ts'}}
      int acc = ts;
      do_return acc;
    };
  }
}
