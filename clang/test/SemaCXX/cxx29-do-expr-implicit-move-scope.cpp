// RUN: %clang_cc1 -std=c++2d -verify -fsyntax-only %s

// expected-no-diagnostics

// [expr.prim.id.unqual]p15.4, as added by P2806, restricts implicit move on a
// `do_return` operand to entities that belong to the block scope of the
// do-expression body, or to a block scope contained by it. That restriction is
// the whole point: unlike a function, a do-expression does not end the lifetime
// of the surrounding function's variables, so moving from one would leave an
// object the caller can still see in its moved-from state.
//
// Every case below counts constructions with a constexpr counter class, which
// is how clang/test/SemaCXX/cxx29-do-expr-elision.cpp already tests the
// affirmative direction.

struct T {
  int copies = 0;
  int moves = 0;
  constexpr T() = default;
  constexpr T(const T &o) : copies(o.copies + 1), moves(o.moves) {}
  constexpr T(T &&o) noexcept : copies(o.copies), moves(o.moves + 1) {}
};

namespace enclosing_function_local {
  // `outer` belongs to the enclosing function, not to the do-expression body.
  // It is still alive -- and still readable -- after the do-expression, so the
  // operand is not move-eligible.
  constexpr T from_enclosing_local() {
    T outer;
    return do { do_return outer; };
  }
  static_assert(from_enclosing_local().copies == 1);
  static_assert(from_enclosing_local().moves == 0);

  // The distinguishing observation, stated directly: after the do-expression,
  // the enclosing variable is unchanged.
  constexpr bool outer_survives() {
    T outer;
    T t = do { do_return outer; };
    return outer.copies == 0 && outer.moves == 0 && t.copies == 1;
  }
  static_assert(outer_survives());

  // A parameter of the enclosing function is in the same position.
  constexpr T from_enclosing_parameter(T p) {
    return do { do_return p; };
  }
  static_assert(from_enclosing_parameter(T{}).copies == 1);
  static_assert(from_enclosing_parameter(T{}).moves == 0);

  // A body local, for contrast: it dies with the do-expression, so it is
  // move-eligible.
  constexpr T from_body_local() {
    T outer;
    return do { T r; do_return r; };
  }
  static_assert(from_body_local().moves == 1);
  static_assert(from_body_local().copies == 0);
}

namespace nested_block_scope {
  // "or to a block scope contained by that block scope": a variable declared in
  // a nested block of the body is still a body local.
  constexpr T from_nested_block() {
    return do {
      if (true) {
        T r;
        do_return r;
      }
      do_return T{};
    };
  }
  static_assert(from_nested_block().moves == 1);
  static_assert(from_nested_block().copies == 0);
}

namespace nested_do_expressions {
  // The outer body's local does not belong to the inner do-expression's body,
  // and it outlives the inner do-expression, so the inner `do_return` copies.
  constexpr T outer_local_from_inner_body() {
    return do {
      T outer_body;
      T inner = do { do_return outer_body; };
      do_return inner;
    };
  }
  // outer_body -> inner is a copy; inner -> the outer result is a move.
  static_assert(outer_local_from_inner_body().copies == 1);
  static_assert(outer_local_from_inner_body().moves == 1);

  // The inner body's own local is move-eligible in the inner do_return.
  constexpr T inner_local_from_inner_body() {
    return do {
      T t = do { T r; do_return r; };
      do_return t;
    };
  }
  static_assert(inner_local_from_inner_body().moves == 2);
  static_assert(inner_local_from_inner_body().copies == 0);
}

namespace in_a_template {
  // The check has to work during instantiation too, where the body no longer
  // has a parser scope.
  template <class U> constexpr U from_enclosing_local() {
    U outer;
    return do { do_return outer; };
  }
  static_assert(from_enclosing_local<T>().copies == 1);
  static_assert(from_enclosing_local<T>().moves == 0);

  template <class U> constexpr U from_body_local() {
    return do { U r; do_return r; };
  }
  static_assert(from_body_local<T>().moves == 1);
  static_assert(from_body_local<T>().copies == 0);
}

namespace explicit_move_still_works {
  // Nothing here stops the user from asking for a move explicitly.
  constexpr T explicitly_moved() {
    T outer;
    return do { do_return static_cast<T &&>(outer); };
  }
  static_assert(explicitly_moved().moves == 1);
  static_assert(explicitly_moved().copies == 0);
}
