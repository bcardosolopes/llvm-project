// RUN: %clang_cc1 -std=c++2d -verify -fsyntax-only %s

// expected-no-diagnostics

namespace basic {
  constexpr int basic_value() { return do { do_return 1; }; }
  static_assert(basic_value() == 1);

  constexpr int basic_arith() { return do { do_return 2 + 3; }; }
  static_assert(basic_arith() == 5);

  // Implicit do_return: trailing expression without semicolon
  constexpr int implicit_simple() { return do { 1 }; }
  static_assert(implicit_simple() == 1);

  constexpr int implicit_with_stmt() { return do { int x = 2; x + 3 }; }
  static_assert(implicit_with_stmt() == 5);

  // Trailing expression with explicit type
  constexpr auto implicit_typed() { return do -> long { 42 }; }
  static_assert(__is_same(decltype(implicit_typed()), long));
  static_assert(implicit_typed() == 42);
}

namespace explicit_type {
  constexpr auto y() { return do -> long { do_return 1; }; }
  static_assert(__is_same(decltype(y()), long));

  // Explicit type drives conversion of operand.
  constexpr auto dval() { return do -> double { do_return 1; }; }
  static_assert(dval() == 1.0);

  // Narrowing conversions are allowed via copy-init.
  constexpr auto ival() { return do -> int { do_return 1.5; }; }
  static_assert(ival() == 1);
}

namespace explicit_decltype_auto {
  constexpr decltype(auto) explicit_prvalue() {
    return do -> decltype(auto) { do_return 1; };
  }
  static_assert(__is_same(decltype(explicit_prvalue()), int));
  static_assert(explicit_prvalue() == 1);

  constexpr decltype(auto) implicit_prvalue() {
    return do -> decltype(auto) { 1 };
  }
  static_assert(__is_same(decltype(implicit_prvalue()), int));
  static_assert(implicit_prvalue() == 1);

  int value;

  decltype(auto) explicit_lvalue() {
    return do -> decltype(auto) { do_return (value); };
  }
  static_assert(__is_same(decltype(explicit_lvalue()), int &));

  decltype(auto) implicit_lvalue() {
    return do -> decltype(auto) { (value) };
  }
  static_assert(__is_same(decltype(implicit_lvalue()), int &));

  template <class T>
  constexpr decltype(auto) explicit_with_discard() {
    return do -> decltype(auto) {
      if constexpr (__is_same(T, int)) {
        do_return 1;
      } else {
        do_return 'x';
      }
    };
  }
  static_assert(explicit_with_discard<int>() == 1);
  static_assert(explicit_with_discard<char>() == 'x');
}

namespace multiple_do_return {
  constexpr int pick(int n) {
    return do {
      if (n < 0) do_return -1;
      if (n == 0) do_return 0;
      do_return 1;
    };
  }
  static_assert(pick(-5) == -1);
  static_assert(pick(0) == 0);
  static_assert(pick(7) == 1);
}

namespace deduce_strips_reference {
  // Deduction across `do_return x` where x is an lvalue must yield the
  // value type, not a reference. Otherwise the second do_return below
  // (with a different lvalue) would see a deduction conflict.
  constexpr int test(int a, int b) {
    return do {
      if (a > b) do_return a;
      do_return b;
    };
  }
  static_assert(test(1, 2) == 2);
  static_assert(test(5, 3) == 5);
}

namespace outer_return {
  // 'return' inside do-expression returns from the enclosing function,
  // not from the do-expression. (Compile-only: runtime semantics tested
  // separately via codegen.)
  int f(bool b) {
    int x = do {
      if (!b) return -1;
      do_return 1;
    };
    return x + 100;
  }

  constexpr int constexpr_f(bool b) {
    int x = do -> int {
      if (b) return -1;
      do_return 1;
    };
    return x + 100;
  }
  static_assert(constexpr_f(true) == -1);
  static_assert(constexpr_f(false) == 101);
}

namespace outer_break {
  // 'break' inside a do-expression body breaks the enclosing loop.
  constexpr int sum_until_neg(const int *p, int n) {
    int total = 0;
    for (int i = 0; i < n; ++i) {
      int v = do {
        if (p[i] < 0) break;
        do_return p[i];
      };
      total += v;
    }
    return total;
  }

  constexpr int arr[] = {1, 2, 3, -4, 5};
  static_assert(sum_until_neg(arr, 5) == 6);
}

namespace outer_continue {
  // 'continue' inside do-expression continues the enclosing loop.
  constexpr int sum_skip_neg(const int *p, int n) {
    int total = 0;
    for (int i = 0; i < n; ++i) {
      int v = do {
        if (p[i] < 0) continue;
        do_return p[i];
      };
      total += v;
    }
    return total;
  }

  constexpr int arr[] = {1, -2, 3, -4, 5};
  static_assert(sum_skip_neg(arr, 5) == 9);
}

namespace outer_flow_subexpression {
  // 'break'/'continue'/'return' inside a do-expression used as a
  // SUB-expression (not a full-expression) must transfer control before the
  // rest of the enclosing full-expression is evaluated, matching runtime
  // semantics. '&&' guarantees left-to-right evaluation, so the right operand
  // must NOT run once the do-expression escapes.
  constexpr int break_before_side_effect() {
    int se = 0;
    for (;;) {
      bool b = (do -> bool { break; do_return true; }) && (se = 7);
      (void)b;
    }
    return se;  // break fires before 'se = 7'
  }
  static_assert(break_before_side_effect() == 0);

  constexpr int continue_skips_side_effect(int n) {
    int se = 0;
    int iters = 0;
    for (int i = 0; i < n; ++i) {
      ++iters;
      bool b = (do -> bool { continue; do_return true; }) && (se += 10);
      (void)b;
    }
    return se * 1000 + iters;  // 'se += 10' never runs; se stays 0
  }
  static_assert(continue_skips_side_effect(3) == 3);

  constexpr int return_before_side_effect(int x) {
    int se = 0;
    bool b = (do -> bool { return x * 2; do_return true; }) && (se = 7);
    (void)b;
    return se;  // the do-expression's 'return' exits the function with x*2
  }
  static_assert(return_before_side_effect(21) == 42);
}

namespace switch_break_inside {
  // 'break' inside a switch inside a do-expression breaks the SWITCH
  // (not the enclosing loop and not the do-expression itself), so the
  // body continues past the switch.
  constexpr int classify(int x) {
    return do {
      switch (x) {
        case 1: do_return 100;
        case 2: break;  // breaks switch only; do_return 0 below runs
        default: do_return 999;
      }
      do_return 0;
    };
  }
  static_assert(classify(1) == 100);
  static_assert(classify(2) == 0);
  static_assert(classify(3) == 999);
}

namespace switch_covers_all_paths {
  // A switch whose every arm terminates (with a 'default' so the switch is
  // exhaustive) does NOT fall off the end, so no trailing do_return is needed.
  // The reachability analysis is CFG-based, so it models switch coverage
  // accurately (a purely syntactic walk would reject this).
  constexpr int classify(int x) {
    return do -> int {
      switch (x) {
        case 1: do_return 1;
        default: do_return 2;
      }
    };
  }
  static_assert(classify(1) == 1);
  static_assert(classify(7) == 2);

  // [[fallthrough]] between arms is handled too.
  constexpr int with_fallthrough(int x) {
    return do -> int {
      switch (x) {
        case 1:
          [[fallthrough]];
        default:
          do_return 9;
      }
    };
  }
  static_assert(with_fallthrough(1) == 9);
  static_assert(with_fallthrough(2) == 9);
}

namespace nested {
  constexpr int compute() {
    return do {
      int inner = do { do_return 5; };
      do_return inner * 2;
    };
  }
  static_assert(compute() == 10);

  // Three levels of nesting; each do_return yields to its lexically
  // immediate enclosing do-expression.
  constexpr int triple() {
    return do {
      int x = do { do_return 7; };
      int y = do {
        int z = do { do_return 3; };
        do_return z * x;
      };
      do_return y;
    };
  }
  static_assert(triple() == 21);
}

namespace recursive {
  constexpr int fact(int n) {
    return do {
      if (n <= 1) do_return 1;
      do_return n * fact(n - 1);
    };
  }
  static_assert(fact(0) == 1);
  static_assert(fact(5) == 120);
  static_assert(fact(7) == 5040);
}

namespace loop_termination {
  constexpr int while_true() {
    return do -> int {
      while (true)
        do_return 42;
    };
  }
  static_assert(while_true() == 42);

  constexpr int for_ever() {
    return do -> int {
      for (;;)
        do_return 17;
    };
  }
  static_assert(for_ever() == 17);

  int infinite_empty() {
    return do -> int {
      while (true) {
      }
    };
  }

  int switch_break_in_infinite_loop(int n) {
    return do -> int {
      while (true) {
        switch (n) {
        case 0:
          break; // exits the switch, not the loop
        default:
          break; // exits the switch, not the loop
        }
      }
    };
  }

  int nested_loop_break_then_yield() {
    return do -> int {
      while (true) {
        while (true)
          break; // exits the inner loop only
        do_return 42;
      }
    };
  }

  int do_while_body_always_yields(bool b) {
    return do -> int {
      do {
        do_return 42;
      } while (b);
    };
  }
}

namespace as_subexpression {
  constexpr int f(int x) { return x * 2; }
  constexpr int test_arg() { return f(do { do_return 21; }); }
  static_assert(test_arg() == 42);

  constexpr int test_arith() {
    return (do { do_return 5; }) + (do { do_return 3; }) * 2;
  }
  static_assert(test_arith() == 11);

  constexpr int test_compound_assign() {
    int n = 10;
    n += do { do_return 5; };
    n *= do { do_return 3; };
    return n;
  }
  static_assert(test_compound_assign() == 45);
}

namespace aggregates {
  struct Pair { int a, b; };
  constexpr Pair make() {
    return do { do_return Pair{1, 2}; };
  }
  static_assert(make().a == 1);
  static_assert(make().b == 2);

  // Multiple do_return paths with aggregate type.
  constexpr Pair pick(bool b) {
    return do {
      if (b) do_return Pair{10, 20};
      do_return Pair{30, 40};
    };
  }
  static_assert(pick(true).a == 10);
  static_assert(pick(false).b == 40);
}

namespace raii_destruction {
  struct Counter {
    int *p;
    constexpr Counter(int &n) : p(&n) { ++*p; }
    constexpr ~Counter() { --*p; }
  };

  // Body-local RAII object is destroyed when do_return fires.
  constexpr int test_do_return() {
    int live = 0;
    int v = do {
      Counter c(live);  // live becomes 1
      do_return live;   // observed value is 1
    };
    // After do_return, c is destroyed; live is back to 0.
    return v + live * 1000;
  }
  static_assert(test_do_return() == 1);

  // Body-local RAII object is destroyed when 'break' (outer) fires.
  constexpr int test_break() {
    int live = 0;
    for (int i = 0; i < 3; ++i) {
      int v = do {
        Counter c(live);
        if (i == 1) break;
        do_return live;
      };
      (void)v;
    }
    return live;  // must be 0
  }
  static_assert(test_break() == 0);
}

namespace void_do_expr {
  // Void do-expression: falling off the end with no do_return deduces void.
  static_assert(__is_same(decltype(do {
    int x = 0;
    (void)x;
  }), void));

  // 'auto' and 'decltype(auto)' (possibly cv-qualified) deduce void the same
  // way, with or without a bare do_return.
  static_assert(__is_same(decltype(do -> auto { }), void));
  static_assert(__is_same(decltype(do -> const auto { }), void));
  static_assert(__is_same(decltype(do -> decltype(auto) { }), void));
  static_assert(__is_same(decltype(do -> auto { do_return; }), void));
  static_assert(__is_same(decltype(do -> decltype(auto) { do_return; }), void));

  constexpr int test_deduced_void_falloff() {
    int x = 0;
    (void)(do {
      x = 5;
      // No do_return; deduces void.
    });
    return x;
  }
  static_assert(test_deduced_void_falloff() == 5);

  // Void do-expression: explicit `do -> void` allows falling off the end.
  constexpr int test_explicit_void_falloff() {
    int x = 0;
    (void)(do -> void {
      x = 5;
      // No do_return; OK because explicit void.
    });
    return x;
  }
  static_assert(test_explicit_void_falloff() == 5);

  constexpr int test_explicit_do_return_void() {
    int x = 0;
    (void)(do -> void {
      x = 7;
      do_return;
    });
    return x;
  }
  static_assert(test_explicit_do_return_void() == 7);
}

namespace lambda_inside {
  // Lambda's own `return` returns from the lambda, not the do-expression.
  constexpr int test() {
    return do {
      auto f = [](int x) { return x * 2; };
      do_return f(7);
    };
  }
  static_assert(test() == 14);
}

namespace namespace_scope {
  // A do-expression as a constexpr namespace-scope variable initializer.
  constexpr int x = do { do_return 42; };
  static_assert(x == 42);

  // Locals declared inside the body are expression-local, even when the
  // do-expression appears in a namespace-scope initializer.
  constexpr int local = do {
    int y = 1;
    y += 2;
    do_return y;
  };
  static_assert(local == 3);

  // Reading another namespace-scope constexpr from inside the body works.
  constexpr int y = do { do_return x + 1; };
  static_assert(y == 43);

  // Multiple do_return paths and explicit type at namespace scope.
  constexpr long z = do -> long {
    if (x > 0) do_return x;
    do_return 0;
  };
  static_assert(z == 42L);
}

namespace exception_spec {
  void may_throw();
  void no_throw() noexcept;

  static_assert(!noexcept(do { do_return (may_throw(), 1); }));
  static_assert(noexcept(do { do_return (no_throw(), 1); }));
}

namespace consteval_reachability {
  int runtime_if_not_consteval() {
    return do -> int {
      if ! consteval {
        do_return 1;
      }
    };
  }
}

namespace init_statements {
  constexpr int basic() {
    return do [y = 1] { y + 2 };
  }
  static_assert(basic() == 3);

  struct S {
    int value;
    constexpr S(int v) : value(v) {}
  };

  constexpr int copy_init() {
    return do [s = S(42)] { s.value };
  }
  static_assert(copy_init() == 42);

  constexpr int brace_init() {
    return do [s = S{42}] { s.value };
  }
  static_assert(brace_init() == 42);

  // `decltype((auto))` preserves value category: an lvalue initializer yields a
  // reference capture.
  constexpr int references() {
    return do [x = 42, r = x] { r };
  }
  static_assert(references() == 42);

  constexpr int multiple_decls() {
    return do [a = 1, b = 2, c = 3] { a + b + c };
  }
  static_assert(multiple_decls() == 6);

  struct Tracker {
    int id;
    int *log;
    int *index;

    constexpr Tracker(int i, int *l, int *idx) : id(i), log(l), index(idx) {
      log[(*index)++] = id;
    }
    constexpr ~Tracker() { log[(*index)++] = -id; }
    constexpr int value() const { return id * 10; }
  };

  constexpr bool destructor_timing() {
    int log[10] = {};
    int index = 0;
    int result = do [t = Tracker(1, log, &index)] { t.value() };
    return result == 10 && log[0] == 1 && log[1] == -1;
  }
  static_assert(destructor_timing());

  constexpr int consume_while_live(int value, int *log, int *index) {
    log[(*index)++] = 99;
    return log[1] == 99 ? value : -1;
  }

  constexpr bool subexpression_destructor_timing() {
    int log[10] = {};
    int index = 0;
    int result = consume_while_live(
        do [t = Tracker(1, log, &index)] { t.value() }, log, &index);
    return result == 10 && log[0] == 1 && log[1] == 99 && log[2] == -1;
  }
  static_assert(subexpression_destructor_timing());

  constexpr bool reference_chain() {
    int log[10] = {};
    int index = 0;
    int result = do [a = Tracker(1, log, &index),
                     b = Tracker(2, log, &index)] {
      a.value() + b.value()
    };
    return result == 30 && log[0] == 1 && log[1] == 2 && log[2] == -2 &&
           log[3] == -1;
  }
  static_assert(reference_chain());

  constexpr int reference_to_local() {
    return do [x = 100, r = x] { r + 5 };
  }
  static_assert(reference_to_local() == 105);

  constexpr int nested_init() {
    return do [a = 1] {
      do_return do [b = 2] { a + b };
    };
  }
  static_assert(nested_init() == 3);

  struct TempMaker {
    constexpr TempMaker(int v) : value(v) {}
    int value;
  };

  constexpr int temp_lifetime() {
    return do [t = TempMaker(42)] { t.value };
  }
  static_assert(temp_lifetime() == 42);

  constexpr int outer_scope() {
    int outer = 10;
    return do [inner = outer * 2] { inner + outer };
  }
  static_assert(outer_scope() == 30);

  constexpr int decl_chain() {
    return do [x = 10, y = x * 2, z = y + x] { z };
  }
  static_assert(decl_chain() == 30);

  constexpr int pointer_to_local() {
    return do [x = 42, p = &x] { *p };
  }
  static_assert(pointer_to_local() == 42);
}

namespace init_statement_exception_spec {
  void may_throw();
  void no_throw() noexcept;

  static_assert(!noexcept(do [x1 = (may_throw(), 1)] { x1 }));
  static_assert(noexcept(do [x2 = (no_throw(), 1)] { x2 }));
}

namespace init_statement_jump_scope {
  struct S {
    S();
    ~S();
  };

  void goto_over_do_init() {
    goto L;
    (void)do [s = S()] { 0 };
  L:;
  }
}

namespace postfix_suffix {
  struct String {
    constexpr auto operator[](int) const -> char { return 'a'; }
    constexpr auto size() const -> int { return 3; }
    int field = 7;
  };

  constexpr String make() { return String{}; }

  // A do-expression is a primary-expression and can be followed by
  // postfix-expression suffixes.
  constexpr char subscript() { return do { make() }[0]; }
  static_assert(subscript() == 'a');

  constexpr int member_call() { return do { make() }.size(); }
  static_assert(member_call() == 3);

  constexpr int member_access() { return do { make() }.field; }
  static_assert(member_access() == 7);
}
