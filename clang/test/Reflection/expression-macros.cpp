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

namespace N16 {

// Case and ordinary labels do not introduce scopes: locals declared under
// them are visible to a deferred expansion in the same block.
__macro use(auto&& x) { return ^^{ local + \(x) }; }

template <class T>
constexpr int in_case(T t) {
  switch (0) {
  case 0:
    int local = 4;
    return use!(t);
  }
  return 0;
}
static_assert(in_case(3) == 7);

template <class T>
constexpr int in_label(T t) {
here:
  int local = 10;
  return use!(t);
}
static_assert(in_label(3) == 13);

// A raw member macro cannot be invoked through an object of unknown
// dependent type: the argument tokens were already parsed as an expression.
struct Raw {
  __macro raw(this Raw const&, token_sequence t) { return ^^{ (\(t)) }; }
  __macro expr(this Raw const&, int x) { return ^^{ \(x) }; }
};

template <class T>
constexpr int call_raw(T const& t) {
  return t.raw!(1 + 2);  // expected-error {{member expression macro 'raw' has a token sequence parameter, but the object's type was not known when the invocation was parsed, so the argument was parsed as an expression}} \
                         // expected-note {{invoke the macro on an object whose type is known at the point of invocation to pass raw tokens}}
}
constexpr int bad = call_raw(Raw{});  // expected-note {{in instantiation of function template specialization 'N16::call_raw<N16::Raw>' requested here}} \
                                      // expected-error {{constexpr variable 'bad' must be initialized by a constant expression}}

// An expression-parameter member macro through the same dependent path is
// fine, as is a raw member macro on a known (current-instantiation) type.
template <class T>
constexpr int call_expr(T const& t) { return t.expr!(5); }
static_assert(call_expr(Raw{}) == 5);

template <class T>
struct Holder {
  Raw r;
  constexpr int go() const { return r.raw!(2 + 3); }
};
static_assert(Holder<int>{}.go() == 5);

}  // namespace N16

namespace N17 {

// Declaration-position invocation: 'name!(args);' at namespace or class
// scope parses the expansion as a sequence of declarations in place. No
// consteval block, no queue_injection.
__macro gen_var(token_sequence name, int init) {
  return ^^{ constexpr int \(name) = \(init); };
}

gen_var!(seven, 7);
static_assert(seven == 7);

// Multiple declarations, and an empty expansion.
__macro two_vars() {
  return ^^{
    constexpr int one = 1;
    constexpr long two = 2;
  };
}
two_vars!();
static_assert(one + two == 3);

__macro nothing() { return ^^{}; }
nothing!();

// The same token-sequence literal evaluated in a loop yields declarations
// whose tokens share source locations; the expansion parser's progress
// guard must not mistake the repeats for a stuck parse.
__macro assert_twice() {
  token_sequence out = ^^{};
  for (int i = 0; i < 2; ++i)
    out += ^^{ static_assert(true); };
  return out;
}
assert_twice!();

// The expansion may itself invoke a declaration-position macro.
__macro outer_gen() { return ^^{ gen_var!(nested, 3); }; }
outer_gen!();
static_assert(nested == 3);

// Class scope: members are injected under the current access specifier;
// access changes inside the expansion do not leak past the invocation.
__macro members() {
  return ^^{
    int a = 1;
   private:
    int b = 2;  // expected-note {{declared private here}}
   public:
    constexpr int f() const { return a + b; }
  };
}

struct S {
  members!();
  int after = 3;  // still public: the expansion's 'private:' does not leak
};
static_assert(S{}.f() == 3);
constexpr int use_after = S{}.after;
constexpr int use_b = S{}.b;  // expected-error {{'b' is a private member of 'N17::S'}}

class C {
  members!();  // injected under the class's default 'private'
};

// A dependent context cannot form declarations from a macro; that is what
// consteval blocks with queue_injection are for.
template <class T>
struct DT {
  gen_var!(x, 1);  // expected-error {{macro 'gen_var' cannot form declarations in a dependent context; use 'consteval { queue_injection(...); }' to defer the injection to instantiation}}
};

// The expansion must consist of declarations.
__macro not_decls() { return ^^{ 1 + 2 }; }  // expected-error {{expected unqualified-id}}
not_decls!();

}  // namespace N17

namespace N18 {

// An interpolated type reflection followed by '::' is a nested-name-specifier
// in every position: qualified-ids in expressions and statements, the scope of
// a type in declarations, and qualified declarator-ids.

struct Traits {
  static constexpr int v = 7;
  static inline int counter = 0;
  using type = long;
  struct Inner { static constexpr int w = 9; };
  static constexpr int f(int x) { return x + 1; }
  template <int N> static constexpr int g() { return N; }
  static int out_of_line();
};

enum class Color { red, green };

// Expression position: member access, call, nested qualification, template
// member, scoped enumerator.
__macro exprs() {
  auto R = ^^Traits;
  return ^^{
    \(R)::v + \(R)::f(10) + \(R)::Inner::w + \(R)::template g<3>()
  };
}
static_assert(exprs!() == 7 + 11 + 9 + 3);

__macro hue() { return ^^{ \(^^Color)::green }; }
static_assert(hue!() == Color::green);

// Statement position: the disambiguator must classify a leading interpolated
// type followed by '::' and a non-type member as an expression-statement, not
// a declaration.
__macro poke() {
  auto R = ^^Traits;
  return ^^{
    [] {
      \(R)::counter = 41;
      \(R)::f(0);
      ++\(R)::counter;
      return \(R)::counter;
    }()
  };
}
int use_poke() { return poke!(); }

// Declaration-statement position: the interpolated type as the scope of the
// declared type, with and without 'typename'.
__macro decls() {
  auto R = ^^Traits;
  return ^^{
    [] {
      typename \(R)::type a = 5;
      \(R)::Inner b;
      return a + b.w;
    }()
  };
}
static_assert(decls!() == 14);

// Declaration position (namespace scope): leading scope for the type, and a
// qualified declarator-id after a decl-specifier.
__macro make_inner() { return ^^{ \(^^Traits)::Inner global_inner; }; }
make_inner!();
constexpr int use_global = decltype(global_inner)::w;

__macro define_out_of_line() {
  return ^^{ int \(^^Traits)::out_of_line() { return 55; } };
}
define_out_of_line!();

// A non-class, non-enum type cannot be a nested-name-specifier; the error
// says so instead of misparsing a function-style cast.
__macro bad() {
  return ^^{ \(^^int)::v };  // expected-error {{'int' is not a class, namespace, or enumeration}}
}
constexpr int use_bad = bad!();  // expected-note {{in expansion of expression macro 'bad'}}

}  // namespace N18

namespace N19 {

// Any bracket pair may delimit the argument list (as in Rust): name!(...),
// name!{...} and name![...] are the same invocation.
__macro two(int a, int b) { return ^^{ (\(a) * 10 + \(b)) }; }
static_assert(two!(1, 2) == 12);
static_assert(two!{1, 2} == 12);
static_assert(two![1, 2] == 12);
static_assert(N19::two![1, 2] == 12);
static_assert(::N19::two!{1, 2} == 12);

// A braced argument is still an argument, whichever bracket encloses the list.
struct P { int x, y; };
__macro second(P p) { return ^^{ \(p).y }; }
static_assert(second!({3, 4}) == 4);
static_assert(second!{{3, 4}} == 4);
static_assert(second![{3, 4}] == 4);

// A raw argument ends at the invocation's own closer; other brackets nest.
struct Seq {
  int v[3];
  constexpr int operator[](int i) const { return v[i]; }
};
__macro seq(token_sequence t) { return ^^{ Seq{{\(t)}} }; }
static_assert(seq![1, 2, 3][1] == 2);      // postfix on the expansion
static_assert(seq!{(1), [] { return 2; }(), 3}[2] == 3);
static_assert(seq!{1, 2, 3}.v[0] == 1);

// Member and statement forms.
struct S {
  int v;
  __macro get(this S const& self) { return ^^{ \(self).v }; }
};
constexpr S s{7};
static_assert(s.get!{} == 7);
static_assert(s.get![] == 7);

__macro inc(int& x) { return ^^{ ++\(x) }; }
constexpr int stmt() {
  int r = 0;
  inc!{r};
  inc![r];
  return r;
}
static_assert(stmt() == 2);

// Declaration position, at namespace and class scope.
__macro decl(token_sequence name, int n) {
  return ^^{ static constexpr int \(name) = \(n); };
}
decl!{a, 1};
decl![b, 2];
struct C { decl!{m, 3}; };
static_assert(a == 1 && b == 2 && C::m == 3);

// The closer has to match the opener.
constexpr int bad1 = two!(1, 2];  // expected-error {{expected ')'}} \
                                  // expected-note {{to match this '('}}

}  // namespace N19

namespace N20 {

// In the body a parameter is a prvalue naming what was bound to it, and its
// classification says so. Contexts that classify the operand (reference
// binding -- which is also what range-for's '__range' is -- and a
// parenthesized decltype) therefore work on a parameter directly. (Binding
// a reference to a parameter used to assert in ExprClassification.)
__macro first_plus(token_sequence body, int x) {
  static_assert(__is_same(decltype((body)), token_sequence));
  static_assert(__is_same(decltype((x)), decltype(^^int)));
  auto&& r = body;
  auto const& cx = x;
  token_sequence first = r[0];
  return ^^{ (\(first) * 10 + \(cx)) };
}
constexpr int a = 3;
static_assert(first_plus!(a b c, 7) == 37);

}  // namespace N20

namespace N21 {

// Splicing a saved fragment twice duplicates the interpolated argument's
// evaluation exactly as two interpolations would, and is diagnosed the same.
__macro twice_via_fragment(int x) {
  auto once = ^^{ \(x) };
  return once + ^^{ + } + once;
}
int a = twice_via_fragment!(1);  // expected-error {{expansion of expression macro would evaluate this argument more than once}}

__macro relay(int x) { return ^^{ (\(x)) }; }
__macro twice_nested_fragment(int x) {
  auto once = ^^{ relay!(\(x)) };
  return once + ^^{ + } + once;
}
int b = twice_nested_fragment!(1);  // expected-error {{expansion of expression macro would evaluate this argument more than once}}

// A GNU ?: binds its common operand once, in whichever position: not a
// duplicate evaluation.
__macro elvis(int x) { return ^^{ \(x) ?: 7 }; }
static_assert(elvis!(0) == 7);
static_assert(elvis!(3) == 3);
__macro elvis_nested(int x) { return ^^{ relay!(\(x)) ?: 7 }; }
static_assert(elvis_nested!(0) == 7);

}  // namespace N21

namespace N22 {

// Parameter packs: one bound expression per element. A fold over the pack in
// the body is the repetition syntax; each \(xs) inside it is its own
// interpolation, so each argument is still evaluated exactly once.
__macro sum(auto&&... xs) {
  token_sequence body = ^^{ 0 };
  ((body = body + ^^{ + (\(xs)) }), ...);
  return body;
}
static_assert(sum!() == 0);
static_assert(sum!(1, 2, 3) == 6);
static_assert(sum!{1, 2 * 3} == 7);

// The pack's arguments are expressions, with their types and value
// categories.
template <class T> struct is_lvalue_ref { static constexpr bool value = false; };
template <class T> struct is_lvalue_ref<T&> { static constexpr bool value = true; };
template <class... Ts>
__macro count_lvalues(Ts&&... xs) {
  int n = 0;
  ((n += is_lvalue_ref<Ts>::value), ...);
  return ^^{ \(n) };
}
void categories(int a, int&& b) {
  static_assert(count_lvalues!(a, b, a + 1, 2) == 2);
  static_assert(count_lvalues!() == 0);
}

// Mixed: leading parameters and a pack; a leading raw parameter is not
// greedy when a pack follows it.
__macro lead(token_sequence op, int first, auto&&... rest) {
  token_sequence r = ^^{ \(first) };
  ((r = r + op + ^^{ (\(rest)) }), ...);
  return r;
}
static_assert(lead!(*, 2, 3, 4) == 24);
static_assert(lead!(-, 10) == 10);

// Evaluate-once holds per element.
int next();
__macro dup(auto&&... xs) {
  token_sequence r = ^^{ 0 };
  ((r = r + ^^{ + \(xs) + \(xs) }), ...);
  return r;
}
int d = dup!(1, next());  // expected-error {{expansion of expression macro would evaluate this argument more than once}}

// An interpolation of the pack outside any expansion is the usual error.
__macro bad(auto&&... xs) {
  return ^^{ \(xs) };  // expected-error {{expression contains unexpanded parameter pack 'xs'}}
}

}  // namespace N22

namespace N23 {

// Lifetime analysis sees through an interpolated argument to the expression
// behind it: a forwarding macro does not hide a dangling reference (or a
// returned local's address) that the same code without the macro reports.
struct Box {
  int v;
  int& get() [[clang::lifetimebound]] { return v; }
};
int& first([[clang::lifetimebound]] Box&& b) { return b.v; }
template <class T>
__macro pass(T&& x) { return ^^{ static_cast<\(^^T)&&>(\(x)) }; }
__macro same(auto&& x) { return ^^{ \(x) }; }

void g() {
  int& r1 = first(pass!(Box{1}));  // expected-warning {{temporary bound to local reference 'r1' will be destroyed at the end of the full-expression}}
  int& r2 = pass!(Box{2}).get();   // expected-warning {{temporary bound to local reference 'r2' will be destroyed at the end of the full-expression}}
  Box b{3};
  int& ok = pass!(b).get();        // b outlives the reference: nothing to say
  (void)r1, (void)r2, (void)ok;
}

int* h() {
  int local = 0;
  return same!(&local);  // expected-warning {{address of stack memory associated with local variable 'local' returned}}
}

}  // namespace N23
