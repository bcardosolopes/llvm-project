// RUN: %clang_cc1 -std=c++2d -verify -fsyntax-only %s

// expected-no-diagnostics

namespace function_template_deduced {
  template <class T>
  constexpr T identity(T x) {
    return do { do_return x; };
  }
  static_assert(identity(42) == 42);
  static_assert(identity(3.14) == 3.14);
  static_assert(identity('a') == 'a');
}

namespace function_template_explicit {
  template <class T>
  constexpr long as_long() {
    return do -> long { do_return T{42}; };
  }
  static_assert(as_long<int>() == 42L);
  static_assert(as_long<short>() == 42L);
  static_assert(__is_same(decltype(as_long<int>()), long));
}

namespace nontype_template {
  template <int N>
  constexpr int triangular() {
    return do {
      int total = 0;
      for (int i = 1; i <= N; ++i) total += i;
      do_return total;
    };
  }
  static_assert(triangular<0>() == 0);
  static_assert(triangular<5>() == 15);
  static_assert(triangular<10>() == 55);
}

namespace dependent_operands {
  // Multiple do_return statements with type-dependent operands.
  template <class T, class U>
  constexpr auto pick(bool b, T t, U u) {
    return do {
      if (b) do_return t;
      do_return u;
    };
  }
  // Both branches must agree once instantiated.
  static_assert(pick(true, 1, 2) == 1);
  static_assert(pick(false, 1, 2) == 2);
}

namespace class_template_member {
  template <class T>
  struct Wrapper {
    T value;
    constexpr Wrapper(T v) : value(v) {}
    constexpr T compute() const {
      return do { do_return value * value; };
    }
  };
  static_assert(Wrapper<int>(7).compute() == 49);
  static_assert(Wrapper<long>(11).compute() == 121L);
}

namespace member_template {
  struct S {
    template <class T>
    constexpr T as(int v) const {
      return do { do_return T(v); };
    }
  };
  static_assert(S{}.as<int>(5) == 5);
  static_assert(S{}.as<double>(5) == 5.0);
}

namespace multiple_instantiations {
  template <class T>
  constexpr T scale(T x, T k) {
    return do {
      if (k == T{}) do_return T{};
      do_return x * k;
    };
  }
  static_assert(scale(3, 4) == 12);
  static_assert(scale(3.0, 4.0) == 12.0);
  static_assert(scale(0, 100) == 0);
  static_assert(scale(2, 0) == 0);
}

namespace outer_break_in_template {
  // Outer-flow break works through template instantiation + constant eval.
  template <class T>
  constexpr int count_until_zero(const T *p, int n) {
    int count = 0;
    for (int i = 0; i < n; ++i) {
      int v = do {
        if (p[i] == T{}) break;
        do_return 1;
      };
      count += v;
    }
    return count;
  }
  constexpr int data[] = {1, 2, 3, 0, 5};
  static_assert(count_until_zero(data, 5) == 3);
}

namespace recursive_template {
  template <int N>
  constexpr int fact() {
    return do {
      if constexpr (N <= 1)
        do_return 1;
      else
        do_return N * fact<N - 1>();
    };
  }
  static_assert(fact<0>() == 1);
  static_assert(fact<5>() == 120);
}

namespace dependent_explicit_type {
  template <class T>
  constexpr T compute(int x) {
    return do -> T { do_return x + 1; };
  }
  static_assert(compute<int>(5) == 6);
  static_assert(compute<double>(5) == 6.0);
  static_assert(compute<long>(5) == 6L);
}

namespace dependent_explicit_void {
  // A dependent explicit result type might instantiate as void, so a bare
  // do_return must be checked at instantiation rather than rejected at template
  // definition time.
  template <class T>
  constexpr int f() {
    (void)(do -> T { do_return; });
    return 0;
  }
  static_assert(f<void>() == 0);
}

namespace nested_in_template {
  template <class T>
  constexpr T compute(T base) {
    return do {
      T inner = do { do_return base * 2; };
      do_return inner + 1;
    };
  }
  static_assert(compute(5) == 11);
  static_assert(compute(3.0) == 7.0);
}
