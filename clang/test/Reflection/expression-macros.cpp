// RUN: %clang_cc1 %s -std=c++26 -freflection -fconsteval-operations -fsyntax-only -verify

using token_sequence = decltype(^^{ });

namespace N1 {

__macro id(int x) {  // expected-note {{passing argument to parameter 'x' here}}
  return ^^{ \(x) };
}

auto a = id!(1);
auto b = id!(2L);
constexpr auto c = id!(1 + 2) * 3;

static_assert(__is_same(decltype(a), int));
static_assert(__is_same(decltype(b), int));
static_assert(__is_same(decltype(c), int const));
static_assert(c == 9);

int f(int n) {  // expected-note {{declared here}}
  return id!(n + 2);
}

int g = id(1);     // expected-error {{'id' is an expression macro and must be invoked as id!(...)}}
auto h = &id;      // expected-error {{'id' is an expression macro and must be invoked as id!(...)}}
int i = f!(1);     // expected-error {{'f' is not an expression macro}}
int j = nope!(1);  // expected-error {{use of undeclared expression macro 'nope'}}
int k = id!("x");  // expected-error {{cannot initialize a parameter of type 'int' with an lvalue of type 'const char[2]'}}

}  // namespace N1

namespace N2 {

template <class T> struct is_lvalue_ref { static constexpr bool value = false; };
template <class T> struct is_lvalue_ref<T&> { static constexpr bool value = true; };

// Deduction sees the argument's value category; the parameter type is a
// contract, not an object.
template <class T>
__macro deduced(T&& t) {
  return ^^{ is_lvalue_ref<\(^^T)>::value };
}

void g(int x, int&& z) {
  static_assert(deduced!(x));
  static_assert(deduced!(z));  // a named rvalue reference is an lvalue
  static_assert(!deduced!(x + 1));
  static_assert(!deduced!(static_cast<int&&>(z)));
}

__macro id(auto&& x) { return ^^{ \(x) }; }

// Dependent invocations are expanded at instantiation.
template <class U>
constexpr int twice_plus(U&& u) {
  return id!(u) + N1::id!(u) + 1;
}
static_assert(twice_plus(3) == 7);

}  // namespace N2

namespace N3 {

// Each interpolated argument is evaluated exactly once.
__macro twice(int x) {
  return ^^{ \(x) + \(x) };
}
int t = twice!(1);  // expected-error {{expansion of expression macro would evaluate this argument more than once}}

__macro both(int x) {
  return ^^{ \(x), \(x) };
}
int u = both!(1);  // expected-error {{expansion of expression macro would evaluate this argument more than once}} \
                   // expected-warning {{left operand of comma operator has no effect}}

// Unevaluated operands don't count.
__macro sized(auto&& x) {
  return ^^{ sizeof(\(x)) + \(x) };
}
static_assert(sized!(1) == sizeof(int) + 1);

}  // namespace N3

namespace N4 {

// Raw parameters capture tokens; the last one is greedy.
__macro paste(token_sequence body) {
  return ^^{ (\(body)) };
}
static_assert(paste!(1 + 2) * 3 == 9);
static_assert(paste!(1, 2) == 2);  // expected-warning {{left operand of comma operator has no effect}}

__macro pair(token_sequence a, token_sequence b) {
  return ^^{ (\(a)) * (\(b)) };
}
static_assert(pair!(1 + 1, 2 + 2) == 8);
static_assert(pair!(f(1, 2), 3) == 0);  // expected-error {{use of undeclared identifier 'f'}} \
                                        // expected-note {{in expansion of expression macro 'pair'}}

// Mixed: an expression parameter followed by a raw one.
__macro apply(auto&& fn, token_sequence args) {
  return ^^{ \(fn)(\(args)) };
}
constexpr int add(int a, int b) { return a + b; }
static_assert(apply!(add, 1, 2) == 3);

// Overloads must agree on the shape.
__macro shape(int x) { return ^^{ \(x) }; }             // expected-note {{declared here}}
__macro shape(token_sequence x) { return ^^{ \(x) }; }  // expected-note {{declared here}}
int s = shape!(1);  // expected-error {{overloads of expression macro 'shape' disagree on which parameters are token sequences}}

}  // namespace N4

namespace N5 {

// The expansion must be a single expression.
__macro broken(int x) {
  return ^^{ \(x) ) };  // expected-error {{expansion of expression macro must form a single expression}}
}
int v = broken!(1);  // expected-note {{in expansion of expression macro 'broken'}}

}  // namespace N5
