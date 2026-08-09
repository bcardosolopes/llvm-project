// RUN: %clang_cc1 -std=c++2d -freflection -verify -fsyntax-only %s

// expected-no-diagnostics

template <class, class U>
inline constexpr bool is_rvalue_v = __is_rvalue_reference(U);

#define DECAY_XVALUE(expr) do -> decltype(auto) {                       \
    template for (auto _ : {0}) {                                       \
        if constexpr (is_rvalue_v<decltype(_), decltype((expr))>) {     \
           do_return auto(expr);                                        \
        } else {                                                        \
           do_return (expr);                                            \
        }                                                               \
    }                                                                   \
}

struct MoveOnly {
  MoveOnly(MoveOnly &&);
  MoveOnly(const MoveOnly &) = delete;
};

MoveOnly &&make_xvalue();
int &make_lvalue();

decltype(auto) decay_xvalue() {
  return DECAY_XVALUE(make_xvalue());
}
static_assert(__is_same(decltype(decay_xvalue()), MoveOnly));

decltype(auto) preserve_lvalue() {
  return DECAY_XVALUE(make_lvalue());
}
static_assert(__is_same(decltype(preserve_lvalue()), int &));

decltype(auto) nested_decay_xvalue() {
  return do -> decltype(auto) {
    do_return DECAY_XVALUE(make_xvalue());
  };
}
static_assert(__is_same(decltype(nested_decay_xvalue()), MoveOnly));
