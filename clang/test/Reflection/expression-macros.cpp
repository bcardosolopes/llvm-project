// RUN: %clang_cc1 %s -std=c++26 -freflection -fconsteval-operations -fsyntax-only -fcxx-exceptions -verify

using token_sequence = decltype(^^{ });
namespace std { class type_info { public: virtual ~type_info(); }; }

namespace N1 {

__macro id(int x) {  // expected-note {{candidate function not viable}}
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
int k = id!("x");  // expected-error {{no matching function for call to 'id'}}

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

namespace N6 {

// Value-dependent (but not type-dependent) arguments defer expansion too.
__macro id(auto&& x) { return ^^{ \(x) }; }

template <int N>
constexpr int f() { return id!(N); }
static_assert(f<42>() == 42);

template <int N>
constexpr int g() { return id!(N + 1) * 2; }
static_assert(g<3>() == 8);

// The expansion of a deferred invocation sees the instantiated function's
// locals and parameters, as expansion-site lookup requires.
__macro use(auto&& x) { return ^^{ local + \(x) + y }; }

template <class T>
constexpr int h(T x, int y) {
  int local = 4;
  return use!(x);
}
static_assert(h(3, 10) == 17);

__macro use_local(auto&& x) { return ^^{ local + \(x) }; }

template <class T>
constexpr int in_lambda(T x) {
  int local = 100;
  return [&] { return use_local!(x); }();
}
static_assert(in_lambda(1) == 101);

}  // namespace N6

namespace N7 {

// The bang is required in generated code as well.
__macro id(int x) { return ^^{ \(x) }; }
__macro outer(int x) {
  return ^^{ id(\(x)) };  // expected-error {{'id' is an expression macro and must be invoked as id!(...)}}
}
int a = outer!(7);  // expected-note {{in expansion of expression macro 'outer'}}

__macro outer_ok(int x) { return ^^{ id!(\(x)) }; }
static_assert(outer_ok!(7) == 7);

// typeid of a non-polymorphic operand is unevaluated.
struct NonPoly {};
__macro tid(auto&& x) { return ^^{ (typeid(\(x)), \(x)) }; }
constexpr int n = tid!(9);
static_assert(n == 9);

// An argument cannot be interpolated into a lambda body.
__macro deferred(auto&& x) { return ^^{ [] { return \(x); } }; }
int d = deferred!(1)();  // expected-error {{an expression argument cannot be interpolated into the body of a lambda}}

}  // namespace N7

namespace N8 {

// Raw delimiters must match, not merely balance in count.
__macro ignore(token_sequence) { return ^^{ 0 }; }
static_assert(ignore!([]) == 0);
static_assert(ignore!(([{}])) == 0);
int a = ignore!([});  // expected-error {{expected ']'}}
int b = ignore!(({));  // expected-error {{expected '}'}}

// Default arguments are fine: the bound expression is the default argument.
__macro dflt(int x = 5) { return ^^{ \(x) * 2 }; }
static_assert(dflt!() == 10);
static_assert(dflt!(4) == 8);

struct S {
  __macro member(int x) { return ^^{ \(x) }; }  // expected-error {{an expression macro cannot be a class member}}
};

__macro pack(auto&&... xs) { return ^^{ 0 }; }  // expected-error {{an expression macro cannot have a parameter pack}}

}  // namespace N8

namespace N9 {

// The evaluate-once and lambda rules see through nested macro invocations:
// forwarding an argument into a nested macro is still an evaluation of the
// outer argument.
__macro relay(auto&& x) { return ^^{ \(x) }; }

__macro once_nested(auto&& x) { return ^^{ relay!(\(x)) }; }
static_assert(once_nested!(5) == 5);

__macro twice_nested(auto&& x) { return ^^{ relay!(\(x)) + relay!(\(x)) }; }
int next();
int a = twice_nested!(next());  // expected-error {{expansion of expression macro would evaluate this argument more than once}}

__macro lambda_nested(auto&& x) { return ^^{ [] { return relay!(\(x)); } }; }
int b = lambda_nested!(1)();  // expected-error {{an expression argument cannot be interpolated into the body of a lambda}}

}  // namespace N9

namespace N10 {

// Locals visible at a deferred invocation keep their lexical scope
// structure in the expansion.
__macro geta(auto&& p) { return ^^{ a + \(p) }; }
__macro getb(auto&& p) { return ^^{ b1 + \(p) }; }
__macro getx(auto&& p) { return ^^{ x + \(p) }; }
__macro gete(auto&& p) { return ^^{ e + \(p) }; }

// A name declared earlier in the same declaration-statement.
template <class T>
constexpr int same_stmt(T v) {
  int a = 4, b = geta!(v);
  return b;
}
static_assert(same_stmt(3) == 7);

// Structured bindings.
struct Pair { int first, second; };
template <class T>
constexpr int bindings(T v) {
  auto [b1, b2] = Pair{10, 20};
  return getb!(v) + b2 - b2;
}
static_assert(bindings(1) == 11);

// The innermost of two same-named declarations wins.
template <class T>
constexpr int shadowed(T v) {
  int x = 100;
  {
    int x = 1;
    return getx!(v) + (x - x);
  }
}
static_assert(shadowed(1) == 2);

// A catch parameter is visible inside its handler.
template <class T>
int catches(T v) {
  try {
    return 0;
  } catch (int e) {
    return gete!(v);
  }
}
template int catches<int>(int);

}  // namespace N10

namespace N11 {

// name!() is an empty argument list, never a single empty token sequence.
__macro raw(token_sequence x = ^^{ 42 }) { return x; }
static_assert(raw!() == 42);
static_assert(raw!(7) == 7);

__macro rawreq(token_sequence x) { return x; }  // expected-note {{candidate function not viable}}
int c = rawreq!();  // expected-error {{no matching function for call to 'rawreq'}}

}  // namespace N11
