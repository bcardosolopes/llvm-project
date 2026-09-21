// RUN: %clang_cc1 -std=c++2d -verify -fsyntax-only %s
// expected-no-diagnostics

// A class-type do-expression result is constructed in place, in the
// do-expression's result object -- not evaluated into a temporary of the
// do_return statement, whose scope would destroy it before the value is used.
// Likewise a `return` reached inside a do-expression body constructs the
// enclosing function's result in place.

struct Buf {
  int *p;
  constexpr Buf(int v) : p(new int(v)) {}
  constexpr Buf(const Buf &o) : p(new int(*o.p)) {}
  constexpr Buf(Buf &&o) : p(o.p) { o.p = nullptr; }
  constexpr ~Buf() { delete p; }
  constexpr int get() const { return *p; }
};

constexpr int named() {
  auto b = do -> Buf { Buf w(1); do_return w; };
  return b.get();
}
constexpr int moved() {
  auto b = do -> Buf { Buf w(2); do_return static_cast<Buf &&>(w); };
  return b.get();
}
constexpr int prvalue() {
  auto b = do -> Buf { do_return Buf(3); };
  return b.get();
}
constexpr int deduced() {
  auto b = do { do_return Buf(4); };
  return b.get();
}
// Nested do-expressions: each do_return yields to its own.
constexpr int nested() {
  auto b = do -> Buf {
    auto inner = do -> Buf { do_return Buf(5); };
    do_return inner;
  };
  return b.get();
}
// A `return` in the body returns the function's class-type result.
constexpr Buf via_return(bool early) {
  int k = do -> int {
    if (early)
      return Buf(6);
    do_return 7;
  };
  return Buf(k);
}
// A discarded class-type result is destroyed (no leaked allocation).
constexpr int discarded() {
  (void)(do -> Buf { do_return Buf(8); });
  return 9;
}
// The result initializes a member directly.
struct Holder {
  Buf b;
  constexpr Holder() : b(do -> Buf { do_return Buf(10); }) {}
};

// A do-expression written in a macro body: its `do` and its locals share one
// expansion, and a local is still recognized as declared in the body (so the
// do_return operand is move-eligible -- required here).
struct MoveOnly {
  int v;
  constexpr MoveOnly(int v) : v(v) {}
  MoveOnly(const MoveOnly &) = delete;
  constexpr MoveOnly(MoveOnly &&) = default;
};
#define MAKE(n) do -> MoveOnly { MoveOnly m(n); do_return m; }
constexpr int via_c_macro() {
  auto m = MAKE(11);
  return m.v;
}

static_assert(named() == 1);
static_assert(moved() == 2);
static_assert(prvalue() == 3);
static_assert(deduced() == 4);
static_assert(nested() == 5);
static_assert(via_return(true).get() == 6);
static_assert(via_return(false).get() == 7);
static_assert(discarded() == 9);
static_assert(Holder().b.get() == 10);
static_assert(via_c_macro() == 11);
