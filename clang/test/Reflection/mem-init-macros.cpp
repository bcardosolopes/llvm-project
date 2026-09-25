// Copyright 2026 Jump Trading, LLC
//
// RUN: %clang_cc1 %s -std=c++26 -freflection -fconsteval-operations -fsyntax-only -Wunused-parameter -Wreorder-ctor -verify

// An expression macro invoked as an entry of a ctor-initializer expands to
// zero or more mem-initializers. (See libcxx's mem-init-macro.pass.cpp for
// the motivating use, initializing conditionally injected members.)

using token_sequence = decltype(^^{ });

__macro keep(token_sequence t) { return t; }
__macro drop(token_sequence) { return ^^{ }; }

namespace N1 {

// One, several, or no mem-initializers; in any position; in-class and
// out-of-line; with any bracket for the invocation.
struct S {
  int a, b, c;
  constexpr S(int v) : keep!(a(v)), keep!{b{v + 1}}, drop![c(0)], c(v + 2) {}
  constexpr S(int x, int y);
  constexpr S() : drop!(a(1)), a(1), b(2), c(3), drop!(b(4)) {}
};
constexpr S::S(int x, int y) : keep!(a(x), b(y)), c(x + y) {}

static_assert(S(1).a == 1 && S(1).b == 2 && S(1).c == 3);
static_assert(S(1, 2).a == 1 && S(1, 2).b == 2 && S(1, 2).c == 3);
static_assert(S().a == 1 && S().c == 3);

// The only entry, expanding to nothing.
struct T {
  int n = 5;
  constexpr T([[maybe_unused]] int v) : drop!(n(v)) {}
};
static_assert(T(1).n == 5);

// An expansion may itself invoke a mem-initializer macro.
__macro nested(token_sequence t) { return ^^{ keep!(\(t)) }; }
struct U {
  int a;
  constexpr U(int v) : nested!(a(v)) {}
};
static_assert(U(9).a == 9);

}  // namespace N1

namespace N2 {

// In a dependent constructor the expansion happens at instantiation, where
// template parameters and constructor parameters are substituted.
template <class T>
struct S {
  T a, b;
  constexpr S(T v) : keep!(a(v * 2)), b(v) {}
  template <class V>
  constexpr S([[maybe_unused]] V v, [[maybe_unused]] V w) : keep!(a(T(v)), b{T(w)}) {}
};
static_assert(S<int>(3).a == 6 && S<int>(3).b == 3);
static_assert(S<long>(1, 2).a == 1 && S<long>(1, 2).b == 2);

// A base-class initializer can come from an expansion too.
struct B { int x; };
template <class T>
struct D : B {
  T y;
  constexpr D([[maybe_unused]] T v) : keep!(B{int(v)}, y(v)) {}
};
static_assert(D<int>(4).x == 4 && D<int>(4).y == 4);

// Naming a parameter in a raw argument does not use it: only an expansion
// that refers to it does. In a template the pattern never expands, so a
// parameter used only by mem-initializer macros is marked [[maybe_unused]].
template <class T>
struct E {
  T x;
  constexpr E(T v) : keep!(x(v)) {}  // expected-warning {{unused parameter 'v'}}
  constexpr E(T v, [[maybe_unused]] T w) : keep!(x(w)) { (void)v; }
};
constexpr E<int> e1(1), e2(1, 2);

struct F {
  int x = 0;
  F(int v) : keep!(x(v)) {}  // OK: the expansion uses 'v'
  F(long v) : drop!(x(v)) {}  // expected-warning {{unused parameter 'v'}}
  F([[maybe_unused]] char v) : drop!(x(v)) {}
};

}  // namespace N2

namespace N3 {

// The usual checks run on the expanded list.
struct S {
  int a, b;
  S(int v) : keep!(b(v)), a(v) {}  // expected-warning {{field 'b' will be initialized after field 'a'}}
  S() : keep!(a(1)), a(2) {}  // expected-error {{multiple initializations given for non-static member 'a'}} \
                              // expected-note {{previous initialization is here}}
  S(char) : keep!(nope(1)) {}  // expected-error {{member initializer 'nope' does not name a non-static data member or base class}}
};

template <class T>
struct TS {
  T a;
  TS([[maybe_unused]] T v) : keep!(nope(v)) {}  // expected-error {{member initializer 'nope' does not name a non-static data member or base class}}
};
TS<int> ts(1);  // expected-note {{in instantiation of member function 'N3::TS<int>::TS' requested here}}

}  // namespace N3

namespace N4 {

// The expansion must be a mem-initializer-list (possibly empty), without a
// trailing comma.
__macro comma(token_sequence t) { return ^^{ \(t), }; }
__macro garbage() { return ^^{ 1 + 2 }; }
__macro two_no_comma() { return ^^{ a(1) b(2) }; }

struct S {
  int a, b;
  S(int) : comma!(a(1)) {}  // expected-error {{expected class member or base class name}}
  S(char) : garbage!() {}   // expected-error {{expected class member or base class name}}
  S(long) : two_no_comma!() {}  // expected-error {{expected ','}}
};

// Macro lookup and argument errors.
struct T {
  int a;  // expected-note {{declared here}}
  T(int) : nope!(a(1)) {}  // expected-error {{use of undeclared expression macro 'nope'}}
  T(char) : a!(1) {}  // expected-error {{'a' is not an expression macro}}
};

}  // namespace N4

namespace N5 {

// Qualified macro names.
namespace ns {
__macro keep(token_sequence t) { return t; }
namespace inner {
__macro drop(token_sequence) { return ^^{ }; }
}  // namespace inner
}  // namespace ns

template <class T>
struct Holder {
  static __macro keep(token_sequence t) { return t; }
};

struct S {
  int a, b, c;
  constexpr S(int v)
      : ns::keep!(a(v)), ::N5::ns::keep!{b(v)}, ns::inner::drop![c(0)], c(v) {}
  constexpr S();
};
constexpr S::S() : ns::keep!(a(1), b(2)), Holder<int>::keep!(c(3)) {}
static_assert(S(1).a == 1 && S(1).b == 1 && S(1).c == 1);
static_assert(S().a == 1 && S().b == 2 && S().c == 3);

// In a dependent constructor, the qualifier is kept for the deferred
// expansion.
template <class T>
struct D {
  T a, b;
  constexpr D(T v) : ns::keep!(a(v)), ns::inner::drop!(nope(v)), b(v * 2) {}
  template <class V>
  constexpr D([[maybe_unused]] V v, [[maybe_unused]] V w)
      : ::N5::ns::keep!(a(T(v))), Holder<long>::keep!(b(T(w))) {}
};
static_assert(D<int>(3).a == 3 && D<int>(3).b == 6);
static_assert(D<long>(1, 2).a == 1 && D<long>(1, 2).b == 2);

// With a dependent qualifier, the macros (and so their parameter shape,
// which decides how the arguments are parsed) are unknown until
// instantiation: the argument list is kept as tokens and parsed then.
struct Macros {
  static __macro keep(token_sequence t) { return t; }
  static __macro init(token_sequence member, int v) {
    return ^^{ \(member)(\(v)) };
  }
};

template <class H, class T>
struct E {
  int a, b, c;
  constexpr E([[maybe_unused]] int v)
      : H::keep!(a(v)), Holder<T>::keep!{b{v + 1}}, H::init!(c, v * 2) {}
};
static_assert(E<Macros, int>(1).a == 1 && E<Macros, int>(1).b == 2 &&
              E<Macros, int>(1).c == 2);

template <class H>
struct Bad {
  int a;
  Bad([[maybe_unused]] int v) : H::nope!(a(v)) {}  // expected-error {{use of undeclared expression macro 'nope'}}
};
Bad<Macros> bad(1);  // expected-note {{in instantiation of member function 'N5::Bad<N5::Macros>::Bad' requested here}}

// Qualified lookup errors.
struct F {
  int a;
  F([[maybe_unused]] int v) : ns::nope!(a(v)) {}  // expected-error {{use of undeclared expression macro 'nope'}}
  F() : zork::keep!(a(1)) {}  // expected-error {{use of undeclared identifier 'zork'}}
};

}  // namespace N5

namespace N6 {
// Tokens name what they named where they were written: an out-of-line
// constructor may rename the template parameters it declares.
struct S {
  int n;
  template <class T> constexpr S(T v);
};
template <class U> constexpr S::S([[maybe_unused]] U v) : keep!(n(sizeof(U))) {}
static_assert(S(1).n == sizeof(int));
static_assert(S('a').n == 1);

template <class T> struct X {
  int n;
  constexpr X();
};
template <class U> constexpr X<U>::X() : keep!(n(sizeof(U))) {}
static_assert(X<int>().n == sizeof(int));

// A member name that shares a parameter's spelling is a member name.
struct P { int N; };
template <int N> struct M {
  int a, b;
  constexpr M([[maybe_unused]] P p) : keep!(a(p.N + N)), keep!(b(N)) {}
};
static_assert(M<1>(P{41}).a == 42 && M<1>(P{41}).b == 1);

// No one token can stand for a pack's elements.
template <class... Ts> struct Pack {
  int a;
  Pack() : keep!(a(sizeof...(Ts))) {}  // expected-error {{template parameter pack 'Ts' cannot be substituted into a token sequence}}
};
Pack<int> pack;  // expected-note {{in instantiation of member function 'N6::Pack<int>::Pack' requested here}}

// An unqualified name that finds non-static member macros is an implicit
// member access on '*this', as it is in an expression.
struct Mem {
  int x, y;
  __macro init(this Mem &, token_sequence t) { return t; }
  constexpr Mem() : init!(x(1)), y(init!(2)) {}
  constexpr Mem(int v);
};
constexpr Mem::Mem(int v) : init!(x(v)), init!{y(v + 1)} {}
static_assert(Mem().x == 1 && Mem().y == 2);
static_assert(Mem(5).x == 5 && Mem(5).y == 6);

template <class T> struct DMem {
  T x;
  __macro init(this DMem const &, token_sequence t) { return t; }
  constexpr DMem([[maybe_unused]] T v) : init!(x(v)) {}
  template <class U>
  constexpr DMem([[maybe_unused]] T v, U) : init!(x(v * 2)) {}
};
static_assert(DMem<int>(3).x == 3 && DMem<int>(3, 'a').x == 6);

// A static member macro needs no object.
struct StaticMem {
  int x;
  static __macro init(token_sequence t) { return t; }
  constexpr StaticMem() : init!(x(4)) {}
};
static_assert(StaticMem().x == 4);

// Arguments captured with a dependent qualifier are parsed as part of the
// constructor template, where 'N' is the parameter and 'p.N' the member.
struct Macros {
  static __macro keep(token_sequence t) { return t; }
  static __macro init(token_sequence member, int v) {
    return ^^{ \(member)(\(v)) };
  }
};
template <class H, int N> struct Dep {
  int a, b;
  constexpr Dep([[maybe_unused]] P p) : H::keep!(a(p.N + N)), H::init!(b, p.N * N) {}
};
static_assert(Dep<Macros, 2>(P{3}).a == 5 && Dep<Macros, 2>(P{3}).b == 6);

}  // namespace N6

namespace std::meta {
template <class... Ts> consteval auto tokenize(Ts const &...) -> token_sequence;
}

namespace N7 {
// An expansion is parsed on its own, up to its end: it is empty or a
// mem-initializer-list, nothing else. It cannot open the constructor's body,
// close the class, or otherwise reach past its own tokens -- even if they are
// unbalanced -- and the surrounding code parses as written.
__macro body() { return ^^{ a(1) { a = 2; } }; }
__macro unbalanced() {
  return std::meta::tokenize("a(1) { a = 2; } int f() {");
}
__macro close_class() { return std::meta::tokenize("a(1) {} };"); }
__macro statements() { return ^^{ a(1); return; }; }
__macro trailing_comma() { return ^^{ a(1), }; }

struct S1 { int a; S1() : body!() {} };            // expected-error {{expected ','}}
struct S2 { int a; S2() : unbalanced!() {} };      // expected-error {{expected ','}}
struct S3 { int a; S3() : close_class!() {} };     // expected-error {{expected ','}}
struct S4 { int a; S4() : statements!() {} };      // expected-error {{expected ','}}
struct S5 { int a; S5() : trailing_comma!() {} };  // expected-error {{expected class member or base class name}}

// Braces are fine where an initializer has them.
__macro braced() { return ^^{ a{[] { int x = 3; return x; }()} }; }
struct OK { int a; constexpr OK() : braced!() {} };
static_assert(OK().a == 3);

// Each bad class is still a complete, usable class.
static_assert(sizeof(S1) == sizeof(int) && sizeof(S3) == sizeof(int));
}  // namespace N7
