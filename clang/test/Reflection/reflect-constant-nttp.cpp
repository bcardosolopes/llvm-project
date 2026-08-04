// RUN: %clang_cc1 -std=c++2d -freflection -verify %s

// P4340 ext: identity-based conversion of constant template parameters
// through the reflect_constant customization point (stage 3: top-level
// semantics only; subobject recursion is tested separately).

namespace std::meta {
  using info = decltype(^^::);
}

consteval int gcd(int a, int b) {
  while (b) { int t = a % b; a = b; b = t; }
  return a;
}

struct Frac {
  int numer;
  int denom;
  consteval auto reflect_constant() const -> std::meta::info;
};

template <int N, int D> constexpr Frac interned = Frac{N, D};

consteval auto Frac::reflect_constant() const -> std::meta::info {
  int g = gcd(numer, denom);
  int n = numer / g, d = denom / g;
  // A minimal interning scheme (a real one would use substitute()).
  if (n == 1 && d == 2) return ^^interned<1, 2>;
  if (n == 3 && d == 4) return ^^interned<3, 4>;
  return ^^interned<0, 1>;
}

template <Frac F> constexpr int f() { return F.numer * 100 + F.denom; }
template <Frac F> constexpr const Frac* addr = &F;

// Value-equal arguments normalize to the same specialization...
static_assert(f<Frac{2, 4}> == f<Frac{1, 2}>);
// ...and distinct values stay distinct.
static_assert(f<Frac{2, 4}> != f<Frac{3, 4}>);

// The parameter binds to the returned object itself: address identity.
static_assert(addr<Frac{2, 4}> == &interned<1, 2>);
static_assert(addr<Frac{2, 4}> == addr<Frac{1, 2}>);

// The parameter carries the normalized value.
static_assert(f<Frac{2, 4}>() == 102);

// The customization runs on every argument, including one that names an
// interned object directly: interned<2,4>'s value {2,4} renormalizes.
static_assert(addr<interned<2, 4>> == &interned<1, 2>);

// ==== Rejected results ====

struct BadKind {
  int i;
  consteval auto reflect_constant() const -> std::meta::info {
    return ^^int; // a type, not an object
  }
};
template <BadKind B> void g(); // expected-note {{returned a reflection of something other than an object or variable}}
void test_bad_kind() { g<BadKind{1}>(); } // expected-error {{no matching function}}

struct WrongType {
  int i;
  consteval auto reflect_constant() const -> std::meta::info;
};
constexpr int not_a_wrongtype = 0;
consteval auto WrongType::reflect_constant() const -> std::meta::info {
  return ^^not_a_wrongtype; // object of type int, not WrongType const
}
template <WrongType W> void h(); // expected-note {{an object of type 'const int' rather than 'const WrongType'}}
void test_wrong_type() { h<WrongType{1}>(); } // expected-error {{no matching function}}

struct NoLinkage {
  int i;
  consteval auto reflect_constant() const -> std::meta::info;
};
// A local static has static storage duration and is usable in constant
// expressions, but has no linkage.
consteval auto NoLinkage::reflect_constant() const -> std::meta::info {
  static constexpr NoLinkage local{7};
  return ^^local;
}
template <NoLinkage N> void k(); // expected-note {{an object with no linkage}}
void test_no_linkage() { k<NoLinkage{1}>(); } // expected-error {{no matching function}}

struct NotIdempotent {
  int i;
  consteval auto reflect_constant() const -> std::meta::info;
};
template <int N> constexpr NotIdempotent ni = NotIdempotent{N};
consteval auto NotIdempotent::reflect_constant() const -> std::meta::info {
  // ni<100> has value {100}, which renormalizes to ni<200>: not idempotent.
  return i < 100 ? ^^ni<100> : ^^ni<200>;
}
template <NotIdempotent N> void m(); // expected-note {{is not idempotent}}
void test_not_idempotent() { m<NotIdempotent{5}>(); } // expected-error {{no matching function}}

// ==== Deleted opt-out ====

struct Deleted { // expected-note {{'Deleted' is not a structural type because its 'reflect_constant' customization point is deleted}}
  int i;
  consteval auto reflect_constant() const -> std::meta::info = delete;
};
template <Deleted D> void n(); // expected-error {{type 'Deleted' of non-type template parameter is not a structural type}}
