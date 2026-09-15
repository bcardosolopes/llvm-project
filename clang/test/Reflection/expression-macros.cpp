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
  __macro member(int x) { return ^^{ \(x) }; }  // expected-error {{a non-static member expression macro must have an explicit object parameter}}
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

namespace N12 {

// A member macro binds the object expression to its explicit object
// parameter: obj.name!(args), obj->name!(args), or name!(args) within a
// member function (an implicit member access, as for a function).
struct S {
  int v;  // expected-note {{declared here}}
  template <class Self>
  __macro get(this Self&& self) { return ^^{ \(self).v }; }
  __macro set(this S& self, int n) { return ^^{ (\(self).v = \(n)) }; }
  __macro plus(this S const& self, token_sequence t) {
    return ^^{ \(self).v + \(t) };
  }
  static __macro make(int n) { return ^^{ S{\(n)} }; }

  constexpr int via_this() { return set!(v + 1), get!(); }
};

constexpr int f() {
  S s{1};
  s.set!(2);
  S* p = &s;
  int a = p->get!();         // 2
  int b = s.plus!(3 * 4);    // 2 + 12
  int c = s.via_this();      // 3
  return a + b + c + S::make!(5).v;
}
static_assert(f() == 2 + 14 + 3 + 5);

// A dependent object expression defers the invocation to instantiation.
template <class T>
constexpr int g(T t) {
  int a = t.get!();
  t.set!(9);
  return a + t.get!();
}
static_assert(g(S{4}) == 13);

// Within a class template the class is the current instantiation, so the
// parameter shape (here, a raw parameter) is known even though 'this' is
// dependent.
template <class T>
struct CT {
  T v;
  __macro raw(this CT const& self, token_sequence t) {
    return ^^{ \(self).v + \(t) };
  }
  constexpr T go() const { return this->raw!(1 + 1) + raw!(1); }
};
static_assert(CT<int>{5}.go() == 7 + 6);

int bad1 = S{1}.make!(1);  // expected-error {{static member expression macro 'make' must be invoked as S::make!(...)}}
int bad2 = S{1}.v!(1);     // expected-error {{'v' is not an expression macro}}
int bad3 = S{1}.nope!(1);  // expected-error {{use of undeclared expression macro 'nope'}}

}  // namespace N12

namespace N13 {

using size_t = decltype(sizeof(0));

struct D {
  __macro implicit_this(int n) { return ^^{ \(n) }; }  // expected-error {{a non-static member expression macro must have an explicit object parameter}}
  __macro operator[](this D& self, token_sequence t) { return t; }  // expected-error {{an operator expression macro cannot have a token sequence parameter}}
  __macro operator=(this D& self, const D&) = default;  // expected-error {{an expression macro cannot be defaulted}}
  __macro operator=(this D& self, D&&) = delete;  // expected-error {{an expression macro cannot be deleted}}
  __macro operator co_await(this D& self) { return ^^{ 0 }; }  // expected-error {{operator co_await cannot be an expression macro}}
};

__macro operator""_x(unsigned long long) { return ^^{ 0 }; }  // expected-error {{a literal operator cannot be an expression macro}}
__macro operator new(size_t) { return ^^{ 0 }; }  // expected-error {{an allocation or deallocation function cannot be an expression macro}} \
                                                  // expected-error {{'operator new' cannot be declared consteval}}

}  // namespace N13

namespace N14 {

struct V { int x; };

// Non-member operator macros are found as operator functions are, including
// by argument-dependent lookup.
__macro operator+(V const& l, V const& r) { return ^^{ V{\(l).x + \(r).x} }; }
constexpr V a{1}, b{2};
static_assert((a + b).x == 3);

int oc = operator+(a, b);  // expected-error {{'operator+' is an expression macro and can only be invoked with operator syntax}}

// Every overloadable operator, as a member.
struct W {
  int x;
  template <class Self>
  __macro operator[](this Self&& self, int i) { return ^^{ \(self).x * \(i) }; }
  __macro operator()(this W const& self, int i, int j) {
    return ^^{ \(self).x + \(i) + \(j) };
  }
  __macro operator-(this W const& self) { return ^^{ W{-\(self).x} }; }
  __macro operator++(this W& self, int) { return ^^{ (\(self).x++) }; }
  __macro operator->(this W const& self) { return ^^{ &\(self) }; }
  __macro operator==(this W const& self, W const& o) {
    return ^^{ \(self).x == \(o).x };
  }
};

constexpr int t1() {
  W w{5};
  int r = w[2] + w(1, 1);  // 10 + 7
  r += (-w).x;             // - 5
  r += w++;                // + 5, w.x is now 6
  r += w->x;               // + 6
  if (!(w == W{6})) return -1;
  if (w != W{7}) r += 100;  // rewritten from the macro operator==
  return r;
}
static_assert(t1() == 10 + 7 - 5 + 5 + 6 + 100);

// Dependent operands defer the operator to instantiation.
template <class T>
constexpr auto sub(T const& t, int i) { return t[i]; }
static_assert(sub(W{5}, 3) == 15);

// Reversed and rewritten candidates.
struct X {
  int x;
  __macro operator==(this X const& self, int i) { return ^^{ \(self).x == \(i) }; }
};
static_assert(X{3} == 3);
static_assert(3 == X{3});
static_assert(X{3} != 4);
static_assert(4 != X{3});

// A rewritten operator== must expand to bool; that is only known after
// expansion.
struct Y {
  __macro operator==(this Y const&, Y const&) { return ^^{ 1 }; }  // expected-note {{declared here}}
};
int y1 = Y{} == Y{};
bool y2 = Y{} != Y{};  // expected-error {{return type 'int' of selected 'operator==' function for rewritten '!=' comparison is not 'bool'}}

// A macro and a function may overload; there is no tie-breaker between them.
struct M {
  int x;
  constexpr int operator[](int i) const { return x + i; }
  template <class Self>
  __macro operator[](this Self&& self, long i) { return ^^{ \(self).x * \(i) }; }
};
constexpr M m{2};
static_assert(m[1] == 3);   // int: the function
static_assert(m[1L] == 2);  // long: the macro

// With identical parameter types they are not even an overload set: the
// macro's declared return type is token_sequence, so this is two functions
// differing only in return type.
struct A0 {
  int operator[](int) const;  // expected-note {{previous declaration is here}}
  __macro operator[](this A0 const& self, int i) { return ^^{ 1 }; }  // expected-error {{functions that differ only in their return type cannot be overloaded}}
};

struct A {
  int operator[](long) const { return 0; }  // expected-note {{candidate function}}
  __macro operator[](this A const& self, long long i) { return ^^{ 1 }; }  // expected-note {{candidate function}}
};
int amb = A{}[0];  // expected-error {{use of overloaded operator '[]' is ambiguous (with operand types 'A' and 'int')}}

// A binary operator macro sees its operands unevaluated, so it can be lazy.
struct B { bool b; };
template <class R>
__macro operator&&(B const& l, R&& r) {
  return ^^{ (\(l).b ? static_cast<bool>(\(r)) : false) };
}
constexpr bool lazy() {
  int n = 0;
  bool r = B{false} && (++n, true);
  return !r && n == 0;
}
static_assert(lazy());

}  // namespace N14

namespace N15 {

// P2758 in a macro body: a constexpr_error is the macro explicitly declining
// to produce an expansion. In a plain context that is an error carrying the
// macro's message; during substitution it makes the invocation an invalid
// expression, so a requires-expression evaluates to false instead.
// (The message note points at the __builtin_constexpr_diag call.)

__macro nope(int x) {  // expected-note {{in call to 'nope(^^(expression))'}}
  __builtin_constexpr_diag(2, "", 0, "nope cannot be invoked", 22);  // expected-note {{constexpr message: nope cannot be invoked}}
  return ^^{ \(x) };
}

int a = nope!(1);  // expected-error {{expression macro 'nope' reported an error}}

template <class T>
__macro sometimes(T&& x) {  // expected-note {{in call to 'sometimes<long>(^^(expression))'}}
  if constexpr (!__is_same(__remove_cvref(T), int))
    __builtin_constexpr_diag(2, "", 0, "only int is supported", 21);  // expected-note {{constexpr message: only int is supported}}
  return ^^{ \(x) };
}

int b = sometimes!(2);
long c = sometimes!(3L);  // expected-error {{expression macro 'sometimes<long>' reported an error}}

template <class T>
concept can_nope = requires(T t) { nope!(t); };
template <class T>
concept can_sometimes = requires(T t) { sometimes!(t); };

static_assert(!can_nope<int>);        // explicit failure -> unsatisfied
static_assert(can_sometimes<int>);
static_assert(!can_sometimes<long>);  // ...not a hard error

// A macro body can also emit a warning; the expansion is still produced.
__macro warned(int x) {
  __builtin_constexpr_diag(1, "macro-warn", 10, "think twice", 11);  // expected-warning {{constexpr message with tag 'macro-warn': think twice}}
  return ^^{ \(x) };
}
int d = warned!(4);

}  // namespace N15
