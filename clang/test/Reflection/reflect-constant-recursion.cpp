// RUN: %clang_cc1 -std=c++2d -freflection -verify %s

// P4340 ext: stage 4 of the reflect_constant design — subobject
// normalization, the defaulted customization (private members allowed,
// defaulted-as-deleted), and the copyability requirement for subobjects.

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
  if (n == 1 && d == 2) return ^^interned<1, 2>;
  if (n == 1 && d == 3) return ^^interned<1, 3>;
  return ^^interned<0, 1>;
}

// ==== Member normalization (the Stuff desugar) ====

struct Stuff {
  Frac frac;
  int x;
};
template <Stuff S> constexpr int f() {
  return S.frac.numer * 1000 + S.frac.denom * 100 + S.x;
}
static_assert(f<Stuff{{2, 4}, 6}>() == 1206);
static_assert(f<Stuff{{2, 4}, 6}> == f<Stuff{{1, 2}, 6}>);
static_assert(f<Stuff{{2, 4}, 6}> != f<Stuff{{1, 3}, 6}>);

// ==== Arrays, nesting, base classes ====

struct Arr { Frac fs[2]; };
template <Arr A> constexpr int g() { return A.fs[0].denom * 10 + A.fs[1].denom; }
static_assert(g<Arr{{{2, 4}, {2, 6}}}>() == 23);
static_assert(g<Arr{{{2, 4}, {2, 6}}}> == g<Arr{{{1, 2}, {1, 3}}}>);

struct Inner { Frac f; };
struct Outer { Inner in; int z; };
template <Outer O> constexpr int h() { return O.in.f.denom; }
static_assert(h<Outer{{{2, 4}}, 9}>() == 2);
static_assert(h<Outer{{{2, 4}}, 9}> == h<Outer{{{1, 2}}, 9}>);

struct WithBase : Frac { int tag; };
template <WithBase W> constexpr int k() { return W.denom; }
static_assert(k<WithBase{{2, 4}, 1}>() == 2);
static_assert(k<WithBase{{2, 4}, 1}> == k<WithBase{{1, 2}, 1}>);

// ==== Unions ====

union U {
  Frac f;
  int i;
};
template <U u> constexpr int m() { return u.f.denom; }
static_assert(m<U{.f = {2, 4}}>() == 2);
static_assert(m<U{.f = {2, 4}}> == m<U{.f = {1, 2}}>);

// ==== Defaulted customization: private members allowed, value semantics ====

class D {
  int i;
public:
  constexpr D(int i) : i(i) { }
  constexpr int get() const { return i; }
  consteval auto reflect_constant() const -> std::meta::info = default;
};
template <D d> constexpr int fd() { return d.get(); }
static_assert(fd<D(5)>() == 5);
static_assert(fd<D(5)> == fd<D(5)>);
static_assert(fd<D(5)> != fd<D(6)>);

// Defaulting exempts the access of DIRECT subobjects only. A private member
// whose TYPE is not itself structural (no customization point of its own)
// still deletes the default; the inner type must opt in separately.
class InnerNoOptIn {
  int i;
public:
  constexpr InnerNoOptIn(int i) : i(i) { }
};
class OuterShallow { // expected-note {{'OuterShallow' is not a structural type because its 'reflect_constant' customization point is deleted}}
  InnerNoOptIn in;
public:
  constexpr OuterShallow(int i) : in(i) { }
  consteval auto reflect_constant() const -> std::meta::info = default;
};
template <OuterShallow O> void shallow1(); // expected-error {{type 'OuterShallow' of non-type template parameter is not a structural type}}

// With the inner type opting in as well, the outer default is viable.
class InnerOptIn {
  int i;
public:
  constexpr InnerOptIn(int i) : i(i) { }
  constexpr int get() const { return i; }
  consteval auto reflect_constant() const -> std::meta::info = default;
};
class OuterOk {
  InnerOptIn in;
public:
  constexpr OuterOk(int i) : in(i) { }
  constexpr int get() const { return in.get(); }
  consteval auto reflect_constant() const -> std::meta::info = default;
};
template <OuterOk O> constexpr int shallow2() { return O.get(); }
static_assert(shallow2<OuterOk(5)>() == 5);
static_assert(shallow2<OuterOk(5)> == shallow2<OuterOk(5)>);
static_assert(shallow2<OuterOk(5)> != shallow2<OuterOk(6)>);

// A defaulted customization does not evade the mutable-member rule...
struct NotStructural {
  mutable int m;
};
// ...and over a non-structural member, it is defaulted as deleted, which is
// infectious.
class HasBad { // expected-note {{'HasBad' is not a structural type because its 'reflect_constant' customization point is deleted}}
  NotStructural ns;
public:
  consteval auto reflect_constant() const -> std::meta::info = default;
};
template <HasBad H> void bad1(); // expected-error {{type 'HasBad' of non-type template parameter is not a structural type}}

// ==== Copyability: required for subobject normalization only ====

struct NoCopy {
  int i;
  constexpr NoCopy(int i) : i(i) { }
  NoCopy(const NoCopy&) = delete;
  consteval auto reflect_constant() const -> std::meta::info;
};
template <int N> constexpr NoCopy nc{N};
consteval auto NoCopy::reflect_constant() const -> std::meta::info {
  return ^^nc<1>;
}

// Top level: identity semantics, no copy needed.
template <NoCopy N> constexpr int fnc() { return N.i; }
static_assert(fnc<nc<1>>() == 1);
static_assert(fnc<nc<1>> == fnc<nc<1>>);

// Subobject: the normalized value must be copied into the enclosing object.
struct Wrap { NoCopy inner; int t; };
template <Wrap W> void fw(); // expected-note {{subobject of type 'NoCopy' cannot be normalized through its 'reflect_constant' customization point because 'NoCopy' is not copyable in constant expressions}}
void test_wrap() { fw<Wrap{{5}, 2}>(); } // expected-error {{no matching function}}
