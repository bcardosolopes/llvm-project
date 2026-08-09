// RUN: %clang_cc1 -std=c++2d -verify -fsyntax-only %s

// expected-no-diagnostics

namespace move_eligible_named_local {
  // Counter-style class to verify ctor selection at constexpr time.
  struct T {
    int copies = 0;
    int moves = 0;
    constexpr T() = default;
    constexpr T(const T &o) : copies(o.copies + 1), moves(o.moves) {}
    constexpr T(T &&o) noexcept : copies(o.copies), moves(o.moves + 1) {}
  };

  // do_return on a named local triggers implicit move (C++20
  // [class.copy.elision]p3), the same as `return r;`.
  constexpr T via_do_return() {
    return do {
      T r;
      do_return r;
    };
  }
  // The do-expression's prvalue result is materialized directly into `s`
  // via mandatory copy-elision (C++17). The only construction step that
  // counts is r → result via implicit-move.
  static_assert(via_do_return().moves == 1);
  static_assert(via_do_return().copies == 0);

  // For comparison, returning a const lvalue cannot move-eligible (move
  // overload resolution falls back to copy).
  constexpr T via_const_local() {
    return do {
      const T r;
      do_return r;
    };
  }
  static_assert(via_const_local().copies == 1);
  static_assert(via_const_local().moves == 0);
}

namespace materialized_inplace {
  // Variable initialized from a do-expression: the do-expression body
  // constructs directly into the variable's storage (mandatory elision
  // for prvalues). do_return of a named local is an implicit-move.
  struct T {
    int moves = 0;
    constexpr T() = default;
    constexpr T(const T &) = delete;  // forbid copy entirely
    constexpr T(T &&o) noexcept : moves(o.moves + 1) {}
  };

  constexpr int test() {
    T s = do {
      T r;
      do_return r;
    };
    return s.moves;
  }
  // r → s is one move (implicit-move from named local). The prvalue
  // step is elided.
  static_assert(test() == 1);
}

namespace deduced_type_does_not_force_copy {
  // Deduction must not insert an lvalue-to-rvalue conversion on a class
  // type. Otherwise, returning a const named local would silently materialize
  // a copy via implicit conversion, breaking subsequent move-eligibility.
  struct T {
    int copies = 0;
    constexpr T() = default;
    constexpr T(const T &o) : copies(o.copies + 1) {}
  };

  constexpr int test() {
    auto v = do {
      T r;
      do_return r;
    };
    return v.copies;
  }
  static_assert(test() == 1);  // exactly one copy (implicit-move falls back)
}

namespace explicit_type_still_move_eligible {
  // Even with an explicit `do -> Type`, named-local move-eligibility applies.
  struct T {
    int moves = 0;
    constexpr T() = default;
    constexpr T(const T &) = delete;
    constexpr T(T &&o) noexcept : moves(o.moves + 1) {}
  };

  constexpr int test() {
    T s = do -> T {
      T r;
      do_return r;
    };
    return s.moves;
  }
  static_assert(test() == 1);
}

namespace prvalue_operand_no_extra_move {
  // A prvalue do_return operand: the temporary materializes directly in
  // the result slot. No copy or move involved.
  struct T {
    int copies = 0, moves = 0;
    constexpr T() = default;
    constexpr T(const T &o) : copies(o.copies + 1), moves(o.moves) {}
    constexpr T(T &&o) noexcept : copies(o.copies), moves(o.moves + 1) {}
  };

  constexpr T make_prvalue() { return T{}; }

  constexpr int test() {
    T s = do { do_return T{}; };       // prvalue
    return s.copies + s.moves;
  }
  static_assert(test() == 0);

  constexpr int test2() {
    T s = do { do_return make_prvalue(); };  // prvalue from function
    return s.copies + s.moves;
  }
  static_assert(test2() == 0);
}
