// RUN: %clang_cc1 -std=c++26 -freflection -fconsteval-operations -verify %s

// A token sequence is a scalar type, but it has no mangling, so two
// specializations chosen by distinct token-sequence arguments would share a
// mangled name and collide silently at link time. Token sequences are
// therefore not structural types, and cannot be non-type template parameters.

namespace std::meta {
  using info = decltype(^^::);
  using token_sequence = decltype(^^{ });
}
using std::meta::token_sequence;

// Directly.
template <token_sequence T> struct S { }; // expected-error {{of non-type template parameter is not a structural type}}

template <token_sequence T> void f() { } // expected-error {{of non-type template parameter is not a structural type}}

// Out-of-line member of such a template previously crashed in
// BuildExpressionFromNonTypeTemplateArgumentValue.
template <token_sequence T> struct S2 { static int v; }; // expected-error {{of non-type template parameter is not a structural type}}
template <token_sequence T> int S2<T>::v = 0;
// expected-error@-1 {{of non-type template parameter is not a structural type}}
// expected-error@-2 {{does not refer into a class}}

// As a subobject of a class type; this previously crashed in
// CXXNameMangler::isZeroInitialized. Non-structural-ness propagates through
// CXXRecordDecl::StructuralIfLiteral.
struct W { token_sequence t; };
// expected-note@-1 2 {{'W' is not a structural type because it has a non-static data member of non-structural type 'token_sequence'}}
template <W w> void g() { } // expected-error {{type 'W' of non-type template parameter is not a structural type}}

// Reached through a base class.
struct WBase : W { };
// expected-note@-1 {{'WBase' is not a structural type because it has a base class of non-structural type 'W'}}
template <WBase w> void h() { } // expected-error {{type 'WBase' of non-type template parameter is not a structural type}}

// Arrays of token sequences are equally unmanglable.
struct WArr { token_sequence t[2]; };
// expected-note@-1 {{'WArr' is not a structural type because it has a non-static data member of non-structural type 'token_sequence'}}
template <WArr w> void i() { } // expected-error {{type 'WArr' of non-type template parameter is not a structural type}}

// std::meta::info is still a perfectly good non-type template parameter.
template <std::meta::info R> struct Ok { };
using OkInt = Ok<^^int>;

// And token sequences remain usable as ordinary values.
constexpr token_sequence tok = ^^{ int x; };
static_assert(tok == ^^{ int x; });
constexpr token_sequence cat = tok + ^^{ int y; };
static_assert(cat == ^^{ int x; int y; });
