// RUN: %clang_cc1 -std=c++2d -fcxx-exceptions -fexceptions -verify -fsyntax-only %s

namespace outside_use {
  // 'do_return' is a Sema error outside any do-expression.
  int bad() {
    do_return 1; // expected-error {{'do_return' can only appear inside a do-expression}}
    return 0;
  }
}

namespace inside_lambda {
  // A lambda introduces a function-scope barrier that 'do_return' must not
  // cross to reach an enclosing do-expression.
  int test() {
    return do {
      auto inner = []() {
        do_return 1; // expected-error {{'do_return' cannot appear inside a nested function or lambda body of an enclosing do-expression}}
      };
      inner();
      do_return 2;
    };
  }
}

namespace empty_body_no_explicit_type {
  // No do_return reachable and no explicit type deduces void.
  int bad() {
    return do { }; // expected-error {{cannot initialize return object of type 'int' with an rvalue of type 'void'}}
  }
}

namespace empty_body_constrained_placeholder {
  // With no do_return, the result type deduces to void; as with functions,
  // the type as written must then be 'auto' or 'decltype(auto)' (possibly
  // cv-qualified or constrained), not 'auto&'/'auto*'/etc.
  void bad() {
    (void)do -> auto & { }; // expected-error {{cannot deduce result type 'auto &' for do-expression with no 'do_return' statements}}
    (void)do -> auto * { }; // expected-error {{cannot deduce result type 'auto *' for do-expression with no 'do_return' statements}}
    (void)do -> auto * { do_return; }; // expected-error {{cannot deduce result type 'auto *' from 'do_return' with omitted expression}}
  }

  template <class T> concept NotVoid = !__is_same(T, void); // expected-note {{because '!__is_same(void, void)' evaluated to false}}
  void bad_constraint() {
    (void)do -> NotVoid auto { }; // expected-error {{deduced type 'void' does not satisfy 'NotVoid'}}
  }

  // In a template, deduction is deferred; the error fires at instantiation.
  template <class T>
  void bad_template() {
    (void)do -> auto * { }; // expected-error {{cannot deduce result type 'auto *' for do-expression with no 'do_return' statements}}
  }
  template void bad_template<int>(); // expected-note {{in instantiation of function template specialization 'empty_body_constrained_placeholder::bad_template<int>' requested here}}
}

namespace deduction_conflict {
  int bad() {
    return do {
      do_return 1;     // deduces int
      do_return 2.0;   // expected-error {{'do_return' yields type 'double', conflicting with previously deduced type 'int'}}
    };
  }

  int bad2() {
    return do {
      do_return 1;
      do_return "x";  // expected-error {{'do_return' yields type 'const char *', conflicting with previously deduced type 'int'}}
    };
  }
}

namespace explicit_type_void_with_value {
  // 'do_return value;' is rejected when the explicit type is void.
  void bad() {
    (void)(do -> void {
      do_return 1; // expected-error {{cannot initialize return object of type 'void' with an rvalue of type 'int'}}
    });
  }
}

namespace explicit_type_nonvoid_no_value {
  // 'do_return;' (no value) is rejected when the explicit type isn't void.
  int bad() {
    return do -> int {
      do_return; // expected-error {{'do_return' without a value requires the enclosing do-expression to have type 'void'}}
      do_return 0; // suppress fall-off-end
    };
  }
}

namespace deduced_type_nonvoid_no_value {
  // The same rule applies after a non-void type has been deduced.
  int bad(bool b) {
    return do {
      if (b) do_return 1;
      do_return; // expected-error {{'do_return' without a value requires the enclosing do-expression to have type 'void'}}
      do_return 0; // suppress fall-off-end
    };
  }
}

namespace nested_do_returns_match_inner {
  // An inner do-expression's `do_return` yields to the inner one. After the
  // inner yields, the outer must still see a `do_return`. Here, the outer
  // has none reachable in the dependent-on-bool branch — but Sema only
  // requires at least one do_return statically.
  constexpr int outer(bool) {
    return do {
      int x = do { do_return 5; };  // inner do_return → x
      do_return x;                   // outer do_return → outer
    };
  }
  static_assert(outer(true) == 5);
}

namespace incompat_with_explicit_type {
  // Explicit type drives copy-init. A non-convertible operand is rejected.
  struct S {};
  int bad() {
    return do -> int {
      do_return S{}; // expected-error {{no viable conversion from returned value of type 'S' to function return type 'int'}}
      do_return 0;   // suppress fall-off-end
    };
  }
}

namespace does_not_introduce_function_scope {
  // 'this' inside the body of a do-expression refers to the enclosing
  // member function's `this`, NOT a new one.
  struct S {
    int x = 7;
    constexpr int get() const {
      return do {
        do_return this->x;
      };
    }
  };
  static_assert(S{}.get() == 7);
}

namespace fall_off_end {
  extern bool cond;

  // Non-void result type and a path that falls off the end is ill-formed —
  // do-expressions are stricter than functions/lambdas (where this would be
  // UB rather than ill-formed).
  int bad_no_else() {
    return do -> int {
      if (cond) do_return 1;
      // expected-error@+1 {{control may fall off the end of do-expression with non-void result type 'int'}}
    };
  }

  // An if without else where both reachable continuations terminate is fine.
  int ok_throw() {
    return do {
      if (cond) do_return 1;
      throw 2;  // terminates → no fall-through
    };
  }

  [[noreturn]] void fatal();
  int ok_noreturn() {
    return do {
      if (cond) do_return 1;
      fatal();  // [[noreturn]] → no fall-through
    };
  }

  // 'return' from the enclosing function counts as exiting the do-expression.
  int ok_outer_return(bool b) {
    return do {
      if (b) do_return 1;
      return -1;  // exits the function (and hence the do-expression)
    };
  }

  // 'break' inside an enclosing loop also terminates the do-expression.
  int ok_outer_break() {
    int total = 0;
    while (true) {
      total += do {
        if (cond) do_return 1;
        break;  // exits the loop
      };
    }
    return total;
  }

  // Both branches of an if-with-else terminate → OK.
  int ok_both_branches() {
    return do {
      if (cond) do_return 1;
      else do_return 2;
    };
  }

  // Void result: falling off the end is allowed.
  void ok_void() {
    (void)(do -> void {
      if (cond) do_return;
    });
  }

  // Empty body with a non-void deduced type is rejected by the
  // no-do_return check first; with explicit non-void type, fall-through fires.
  int bad_empty() {
    return do -> int {
      // expected-error@+1 {{control may fall off the end of do-expression with non-void result type 'int'}}
    };
  }

  // A goto to a label inside the do-expression does not by itself exit the
  // do-expression; execution may resume at the label and fall off the end.
  int bad_local_goto() {
    return do -> int {
      goto L;
    L:
      // expected-error@+1 {{control may fall off the end of do-expression with non-void result type 'int'}}
    };
  }

  // A break that exits an inner loop leaves the do-expression body and can then
  // fall off the end.
  int bad_inner_loop_break() {
    return do -> int {
      while (true) {
        break;
      }
      // expected-error@+1 {{control may fall off the end of do-expression with non-void result type 'int'}}
    };
  }

  int bad_do_while_continue(bool b) {
    return do -> int {
      do {
        if (b)
          continue;
        do_return 1;
      } while (false);
      // expected-error@+1 {{control may fall off the end of do-expression with non-void result type 'int'}}
    };
  }

  int bad_if_consteval_in_runtime_context() {
    return do -> int {
      if consteval {
        do_return 1;
      }
      // expected-error@+1 {{control may fall off the end of do-expression with non-void result type 'int'}}
    };
  }

  // A switch with no 'default' can fall through when no case matches, even if
  // every case arm terminates.
  int bad_switch_no_default(int x) {
    return do -> int {
      switch (x) {
        case 1: do_return 1;
        case 2: do_return 2;
      }
      // expected-error@+1 {{control may fall off the end of do-expression with non-void result type 'int'}}
    };
  }

  // A switch with a 'default' but an arm that breaks out of the switch can
  // still fall off the end of the do-expression.
  int bad_switch_arm_breaks(int x) {
    return do -> int {
      switch (x) {
        case 1: do_return 1;
        default: break;  // breaks the switch, then falls off the end
      }
      // expected-error@+1 {{control may fall off the end of do-expression with non-void result type 'int'}}
    };
  }
}

namespace jump_into_do_expr {
  int bad_goto() {
    int x;
    goto L; // expected-error {{cannot jump from this goto statement to its label}}
    x = do -> int { // expected-note {{jump enters a do-expression}}
    L:
      do_return 1;
    };
    return x;
  }

  int bad_switch(int n) {
    int x = 0;
    switch (n) {
      x = do -> int { // expected-note {{jump enters a do-expression}}
      case 1: // expected-error {{cannot jump from switch statement to this case label}}
        do_return 1;
        do_return 2;
      };
      return x;
    }
    return 0;
  }
}

namespace namespace_scope_control_flow {
  int bad_return = do -> int {
    return; // expected-error {{'return' cannot be used outside a function}}
    do_return 1;
  };

  int bad_return_value = do -> int {
    return 0; // expected-error {{'return' cannot be used outside a function}}
    do_return 1;
  };

  int bad_coreturn = do -> int {
    co_return; // expected-error {{'co_return' cannot be used outside a function}}
    do_return 1;
  };

  int bad_coawait = do -> int {
    co_await 0; // expected-error {{'co_await' cannot be used outside a function}}
    do_return 1;
  };

  int bad_coyield = do -> int {
    co_yield 0; // expected-error {{'co_yield' cannot be used outside a function}}
    do_return 1;
  };
}

namespace old_init_expression_syntax {
  int test() {
    return (int x = 1; x); // expected-error {{expected ')'}} expected-note {{to match this '('}} expected-error {{expected expression}} expected-error {{use of undeclared identifier 'x'}}
  }
}
