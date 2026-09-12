// RUN: %clang_cc1 -std=c++26 -freflection -fconsteval-operations -verify -verify-ignore-unexpected=note %s

using size_t = decltype(sizeof(0));

// Forward declaration of std::meta functions (will be intercepted by compiler).
namespace std::meta {
    using info = decltype(^^::);
    using token_sequence = decltype(^^{ });

    template <class... Ts>
    consteval auto id(Ts const&...) -> info;

    template <class... Ts>
    consteval auto str_lit(Ts const&...) -> token_sequence;

    template <class... Ts>
    consteval auto tokenize(Ts const&...) -> token_sequence;

    consteval auto stringize(token_sequence) -> char const*;

    consteval auto queue_injection(token_sequence) -> void;
    consteval auto queue_injection(info target_ns, token_sequence) -> void;

    template <class M>
    consteval auto report_tokens(M const& msg, token_sequence tokens) -> void;
}

using std::meta::info;
using std::meta::token_sequence;
using std::meta::id;
using std::meta::str_lit;

struct string_view {
private:
    char const* p_;
    size_t len_;

public:
    constexpr string_view(char const* p) : p_(p), len_(__builtin_strlen(p)) { }
    constexpr auto data() const -> char const* { return p_; }
    constexpr auto size() const -> size_t { return len_; }
};

namespace N1 {
    token_sequence a;
    token_sequence b = ^^{ }; // ok: token sequences persist to runtime

    constexpr token_sequence tok = ^^{ constexpr int x = 42; };
    static_assert(x == 42); // expected-error {{use of undeclared}}
    consteval {
        queue_injection(tok);
    }
    static_assert(x == 42);
}

namespace N2 {
    constexpr int value = 5;
    constexpr token_sequence tok = ^^{ constexpr int y = \(value); };
    static_assert(y == value); // expected-error {{use of undeclared}}
    consteval {
        queue_injection(tok);
    }
    static_assert(y == 5);

    struct Point { int x, y; };
    constexpr Point p = {.x=1, .y=2};
    consteval {
        queue_injection(^^{
            constexpr int px = \(p).x;
        });
    }
    static_assert(px == 1);

    consteval auto make_seq(int i) -> token_sequence {
        return ^^{
            constexpr int z = \(i);
        };
    }
    consteval {
        queue_injection(make_seq(10));
    }
    static_assert(z == 10);

    enum Color { red, green, blue };
    consteval { queue_injection(^^{ constexpr auto e = \(^^Color::red); }); }
    static_assert(__is_same(decltype(e), Color const));
    static_assert(e == Color::red);
}

namespace N3 {
    constexpr info r = ^^int;
    consteval {
        queue_injection(^^{
            constexpr \(r) v = 12;
        });
    }
    static_assert(v == 12);
    static_assert(^^decltype(v) == ^^int const);

    consteval auto make_variable(info ty) -> token_sequence {
        return ^^{
            \(ty) var = {};
        };
    }
    consteval {
        queue_injection(make_variable(^^char));
    }
    static_assert(^^decltype(var) == ^^char);
}

namespace N4 {
    template <bool B, typename T>
    struct enable_if {
        consteval {
            if (B) {
                queue_injection(^^{ using type1 = T; });
                queue_injection(^^{ using type2 = \(^^T); });
            }
        }
    };

    using A1 = enable_if<true, int>::type1; // ok
    using B1 = enable_if<false, int>::type1; // expected-error {{no type named}}
    using A2 = enable_if<true, int>::type2; // ok
    using B2 = enable_if<false, int>::type2; // expected-error {{no type named}}

    template <auto V>
    struct constant {
        consteval {
            queue_injection(^^{
                static constexpr \(^^decltype(V)) value = \(V);
            });
        }
    };

    static_assert(constant<5>::value == 5);
}

namespace N5 {
    template <typename... Ts>
    struct tuple {
        consteval {
            info types[] = {^^Ts...};
            for (size_t k = 0; k != sizeof...(Ts); ++k) {
                queue_injection(^^{
                    \(types[k]) \(id("_", k)) = \(types[k])();
                });
            }
        }

        template <size_t I>
        constexpr auto get() const -> auto const& {
            consteval {
                queue_injection(^^{
                    return \(id("_", I));
                });
            }
        }
    };

    constexpr int i = 1;
    constexpr auto xs = tuple<int, int const*>{._0 = 2, ._1 = &i};
    static_assert(xs._0 == 2);
    static_assert(xs._1 == &i);
    static_assert(&xs.get<0>() == &xs._0);
    static_assert(&xs.get<1>() == &xs._1);

    struct Stringish {
        constexpr auto data() const -> char const* { return "hello"; }
        constexpr auto size() const -> size_t { return 2; }
    };

    consteval {
        queue_injection(^^{
            constexpr int \(id(Stringish{})) = 2;
        });
    }
    static_assert(he == 2);

    // Test char arguments in id and str_lit
    consteval {
        queue_injection(^^{
            constexpr int \(id('_', 5)) = 55;
            constexpr char const* \(id("ch", 'a', 'r', '_', "str")) = \(str_lit("hea", 'd'));
        });
    }
    static_assert(_5 == 55);
    static_assert(__builtin_strcmp(char_str, "head") == 0);
}

namespace N6 {
    consteval auto make_var(int X) -> token_sequence {
        return ^^{
            constexpr int injected_value = \(X);
        };
    }

    namespace inner { }

    consteval {
        queue_injection(^^inner, make_var(42));
    }
    static_assert(inner::injected_value == 42);

    consteval auto make_type(int X) -> token_sequence {
        auto L = [=] { return X;};
        return ^^{
            struct A {
                \(^^decltype(L)) l = \(L);
            };
        };
    }

    auto check() -> void {
        consteval {
            queue_injection(^^inner, make_type(10));
        }
        inner::A a;
        a.l();
    }
}

namespace N7 {
    template <class S>
    consteval auto make_field(info type, S name, int val) -> token_sequence {
        return ^^{ \(type) \(id(name)) = \(val); };
    }

    consteval auto make_field2(info type, string_view name, token_sequence init) -> token_sequence {
        return ^^{ \(type) \(id(name)) \(init); };
    }

    struct S {
        consteval {
            queue_injection(make_field(^^int, "x", 1));
            queue_injection(make_field(^^int, string_view("y"), 2));
            queue_injection(make_field2(^^int, "z", ^^{ = 3 }));
        }
    };

    static_assert(sizeof(S) == 3 * sizeof(int));
    constexpr auto check() -> int {
        S s = {};
        return s.x + s.y + s.z;
    }
    static_assert(check() == 6);
}

namespace N8 {
    template <int I, class... Ts>
    constexpr auto nth(Ts... ts) {
        consteval {
            queue_injection(^^{
                return ts...[\(I)];
            });
        }
    }

    template <int I, class... Ts>
    constexpr auto nth_local(Ts... ts) {
        consteval {
            info vars[] = {^^ts...};
            queue_injection(^^{
                return \(vars[I]);
            });
        }
    }

    constexpr int v = 1;
    static_assert(nth<0>(1) == 1);
    static_assert(nth<0>(&v) == &v);
    static_assert(nth<0>(1, &v) == 1);
    static_assert(nth<1>(1, &v) == &v);

    static_assert(nth_local<0>(1) == 1);
    static_assert(nth_local<0>(&v) == &v);
    static_assert(nth_local<0>(1, &v) == 1);
    static_assert(nth_local<1>(1, &v) == &v);
}

namespace N9 {
    struct A { int m; };
    struct B { int m; };

    template <class T>
    consteval auto get_m(T x) -> int {
        consteval {
            auto r = ^^A::m;
            queue_injection(^^{
                return x.\(r); // expected-error {{class 'B' not derived from}}
            });
        }
    }

    static_assert(get_m(A{.m=1}) == 1);
    static_assert(get_m(B{.m=1}) == 1); // expected-error {{static assert}}
}

namespace N10 {
    consteval void good_report() {
        string_view sv("from string_view");
        report_tokens(sv, ^^{ int x; }); // works with user-defined string types
    }

    consteval void call_report() {
        good_report();
    }

    consteval int force_report = (call_report(), 0);
}

namespace N11 {
    static_assert(^^{ } == ^^{ });
    static_assert(^^{ } == token_sequence());
    static_assert(^^{ } == []{ token_sequence ts; return ts; }());
    static_assert(^^{ , } == ^^{ , });
    static_assert(^^{ \(id("x", 1)) } == ^^{ x1 });

    // Token sequence concatenation with +
    static_assert(^^{ a } + ^^{ b } == ^^{ a b });
    static_assert(^^{ } + ^^{ x } == ^^{ x });
    static_assert(^^{ x } + ^^{ } == ^^{ x });
    static_assert(^^{ } + ^^{ } == ^^{ });
    static_assert(token_sequence() + ^^{ x } == ^^{ x });
    static_assert(^^{ x } + token_sequence() == ^^{ x });
    static_assert(token_sequence() + token_sequence() == ^^{ });

    // += for token_sequence (local variable)
    consteval auto concat_test() -> token_sequence {
        token_sequence ts = ^^{ a };
        ts += ^^{ b };
        ts += ^^{ c };
        return ts;
    }
    static_assert(concat_test() == ^^{ a b c });

    // += for token_sequence (member, direct access)
    struct S1 {
        token_sequence body = ^^{ x };
    };
    consteval auto member_direct_compound() -> token_sequence {
        S1 s;
        s.body += ^^{ y };
        return s.body;
    }
    static_assert(member_direct_compound() == ^^{ x y });

    // += for token_sequence (member, through method/this)
    struct S2 {
        token_sequence body = ^^{};
        consteval void add(token_sequence tok) {
            body += tok;
        }
    };
    consteval auto member_method_compound() -> token_sequence {
        S2 s;
        s.add(^^{ a });
        s.add(^^{ b });
        return s.body;
    }
    static_assert(member_method_compound() == ^^{ a b });

    // += for token_sequence (through reference)
    consteval void add_via_ref(token_sequence& ts, token_sequence tok) {
        ts += tok;
    }
    consteval auto ref_compound() -> token_sequence {
        token_sequence ts = ^^{ p };
        add_via_ref(ts, ^^{ q });
        return ts;
    }
    static_assert(ref_compound() == ^^{ p q });

    // += for token_sequence (member through reference)
    consteval void add_member_via_ref(S1& s, token_sequence tok) {
        s.body += tok;
    }
    consteval auto member_ref_compound() -> token_sequence {
        S1 s;
        add_member_via_ref(s, ^^{ z });
        return s.body;
    }
    static_assert(member_ref_compound() == ^^{ x z });

    consteval auto default_compound() -> token_sequence {
        token_sequence ts;
        ts += ^^{ x };
        return ts;
    }
    static_assert(default_compound() == ^^{ x });

    consteval auto default_compound_empty_rhs() -> token_sequence {
        token_sequence ts;
        ts += token_sequence();
        return ts;
    }
    static_assert(default_compound_empty_rhs() == ^^{ });
}

namespace N12 {
    // Test that queue_injection handles operator token_sequence() conversion.
    struct Builder {
        token_sequence body = ^^{};
        consteval auto operator+=(token_sequence tok) -> void {
            body = ^^{ \(body) \(tok) };
        }
        consteval operator token_sequence() const { return body; }
    };

    template <class T>
    struct S {
        consteval {
            Builder b;
            b += ^^{ static constexpr int x = 1; };
            std::meta::queue_injection(b);
        }
    };

    static_assert(S<int>::x == 1);

    namespace Target { }

    struct TargetWrapper {
        consteval operator info() const {
            return ^^Target;
        }
    };

    consteval {
        queue_injection(TargetWrapper(),
                        ^^{ inline constexpr int target_ns_conv_ok = 1; });
    }

    static_assert(Target::target_ns_conv_ok == 1);
}

namespace N13 {
    // Injected member function templates should correctly substitute their
    // own template arguments, not the enclosing class template's arguments.
    template <class Outer> struct S {
        template <class T> static constexpr int val = sizeof(T);

        consteval {
            queue_injection(^^{
                template <class T> constexpr auto get(T ) -> int {
                    return val<T>;
                }
            });
        }
    };

    static_assert(S<char>::val<int> == sizeof(int));
    static_assert(S<char>{}.get(42) == sizeof(int));
    static_assert(S<char>{}.get('x') == sizeof(char));
}

namespace N14 {
    struct Base {
        consteval operator token_sequence() const {
            return ^^{ static constexpr int inherited_conv_ok = 1; };
        }
    };
    struct Derived : Base { };

    consteval {
        Derived d;
        std::meta::queue_injection(d);
    }
    static_assert(inherited_conv_ok == 1);

    struct OnlyRvalue {
        consteval operator token_sequence() && {
            return ^^{ static constexpr int refqual_conv_bug = 1; };
        }
    };

    consteval { // expected-error {{evaluating expression of a consteval block must be a constant expression}}
        OnlyRvalue x;
        std::meta::queue_injection(x); // expected-error {{no matching function for call}}
    }
    static_assert(refqual_conv_bug == 1); // expected-error {{use of undeclared identifier 'refqual_conv_bug'}}
}

namespace N15 {
    // Conversion overload ranking should pick the non-const overload for a
    // non-const lvalue object.
    struct X {
        consteval operator token_sequence() const {
            return ^^{ static constexpr int choose = 1; };
        }
        consteval operator token_sequence() {
            return ^^{ static constexpr int choose = 2; };
        }
    };

    consteval {
        X x;
        std::meta::queue_injection(x);
    }
    static_assert(choose == 2);
}

namespace N16 {
    // Conversion function templates to token_sequence should participate.
    struct X {
        template <class T>
        requires __is_same(T, token_sequence)
        consteval operator T() const {
            return ^^{ static constexpr int conv_template_ok = 1; };
        }
    };

    consteval {
        X x;
        std::meta::queue_injection(x);
    }
    static_assert(conv_template_ok == 1);
}

namespace N17 {
    // Using-declarations that introduce operator token_sequence() should be considered.
    struct B {
        consteval operator token_sequence() const {
            return ^^{ static constexpr int using_conv_ok = 1; };
        }
    };

    struct D : B {
        using B::operator token_sequence;
    };

    consteval {
        D d;
        std::meta::queue_injection(d);
    }
    static_assert(using_conv_ok == 1);
}

namespace N18 {
    // Empty token sequences are a no-op when injected.
    consteval { queue_injection(^^{}); }
    consteval { queue_injection(^^{ }); }
    consteval { queue_injection(token_sequence()); }
    consteval { report_tokens("empty token_sequence()", token_sequence()); }
    static_assert(true);
}

namespace N19 {
    // Nested braces inside ^^{ ... } are captured as part of the token stream.
    consteval int n_made = 0;
    consteval {
        queue_injection(^^{
            constexpr int nested_block_value = []{
                int x = 1;
                { int y = 2; x += y; { x += 7; } }
                return x;
            }();
        });
    }
    static_assert(nested_block_value == 10);
}

namespace N20 {
    // Negative: injecting a non-token-sequence reflection.
    constexpr info type_refl = ^^int;
    consteval { // expected-error {{evaluating expression of a consteval block must be a constant expression}}
        queue_injection(type_refl); // expected-error {{no matching function for call}}
    }
}

namespace N21 {
    // queue_injection is consteval, so calling it outside a consteval context
    // is an error (unlike the old __builtin_inject keyword).
    token_sequence make_seq() {
        return ^^{ int q = 1; };
    }
    void caller() {
        queue_injection(make_seq()); // expected-error {{cannot take address of consteval function}}
    }
}

namespace N22 {
    // Interpolating various reflection kinds.
    constexpr int x = 7;
    constexpr float f = 1.5f;
    consteval auto cstr() -> token_sequence { return ^^{ "hello" }; }
    consteval {
        // int, float, type, identifier, function-name
        queue_injection(^^{ constexpr int kInt = \(x); });
        queue_injection(^^{ constexpr float kFloat = \(f); });
        queue_injection(^^{ using KType = \(^^int); });
        queue_injection(^^{ constexpr auto kStr = \(cstr()); });
    }
    static_assert(kInt == 7);
    static_assert(kFloat == 1.5f);
    static_assert(sizeof(KType) == sizeof(int));
}

namespace N23 {
    // Block-scope injection in a non-dependent function makes the injected
    // local visible to following statements in the same scope.
    consteval auto add_x() -> token_sequence { return ^^{ int x = 5; (void)x; }; }
    constexpr int caller() {
        consteval { queue_injection(add_x()); }
        return x;
    }
    static_assert(caller() == 5);

    template <typename T>
    consteval auto add_template_x() -> token_sequence {
        return ^^{ int template_x = 5; };
    }

    template <typename T>
    constexpr int templated_lookup() {
        consteval { queue_injection(add_template_x<T>()); }
        return template_x; // expected-error {{use of undeclared identifier 'template_x'}}
    }

    template <typename T>
    consteval token_sequence return_stmt() {
        return ^^{ return template_x; };
    }

    template <typename T>
    constexpr int templated_lookup2() {
        consteval { queue_injection(add_template_x<T>()); }
        consteval { queue_injection(return_stmt<T>()); }
    }
    static_assert(templated_lookup2<int>() == 5);

    template <int N>
    constexpr int templated_lookup3() {
        int local = 5;
        consteval { queue_injection(^^{ return local + \(N); }); }
    }
    static_assert(templated_lookup3<2>() == 7);

    template <int N>
    constexpr int templated_lookup4() {
        {
            int hidden = 5;
            (void)hidden;
        }
        consteval { queue_injection(^^{ return hidden + \(N); }); } // expected-error {{use of undeclared identifier 'hidden'}}
        return 0;
    }
    constexpr int hidden_lookup = templated_lookup4<2>();

    struct Guard {
        constexpr Guard(int &n) : p(&n) { n = 1; }
        constexpr ~Guard() { *p = 2; }
        int *p;
    };

    template <typename T>
    consteval auto add_guard() -> token_sequence {
        return ^^{ Guard guard(n); };
    }

    template <typename T>
    constexpr int templated_lifetime(T n) {
        consteval { queue_injection(add_guard<T>()); }
        return n;
    }

    static_assert(templated_lifetime(0) == 1);
}

namespace N24 {
    // Dependent token sequences capture template parameters and resolve
    // at instantiation.
    template <typename T, T V>
    struct holder {
        consteval {
            queue_injection(^^{ using element = T; });
            queue_injection(^^{ static constexpr T value = V; });
        }
    };
    using H = holder<int, 42>;
    static_assert(H::value == 42);
    static_assert(sizeof(H::element) == sizeof(int));
}

namespace N25 {
    // Negative: malformed injected tokens diagnose at re-parse time.
    consteval {
        queue_injection(^^{ int int q; }); // expected-error {{cannot combine with previous 'int' declaration specifier}}
    }
}

namespace N26 {
    // Two-arg queue_injection with a non-namespace target diagnoses.
    struct S {};
    consteval { // expected-error {{evaluating expression of a consteval block must be a constant expression}}
        queue_injection(^^S, ^^{ int z = 0; });
    }
}

namespace N27 {
    // Direct token_sequence interpolation into another token_sequence.
    constexpr token_sequence inner = ^^{ constexpr int ts_x = 1; };
    constexpr token_sequence outer = ^^{ \(inner) constexpr int ts_y = 2; };
    consteval {
        queue_injection(outer);
    }
    static_assert(ts_x == 1);
    static_assert(ts_y == 2);

    // Nested token_sequence interpolation.
    constexpr token_sequence a = ^^{ constexpr int a_var = 10; };
    constexpr token_sequence b = ^^{ \(a) constexpr int b_var = 20; };
    constexpr token_sequence c = ^^{ \(b) constexpr int c_var = 30; };
    consteval {
        queue_injection(c);
    }
    static_assert(a_var == 10);
    static_assert(b_var == 20);
    static_assert(c_var == 30);

    // Token sequence interpolation in function.
    consteval auto wrap(token_sequence ts) -> token_sequence {
        return ^^{ struct Wrapped { \(ts) }; };
    }
    consteval {
        queue_injection(wrap(^^{ int member = 42; }));
    }
    static_assert(Wrapped{}.member == 42);
}

namespace N28 {
    // str_lit produces a token_sequence with a string literal.
    static_assert(str_lit("hello") == ^^{ "hello" });

    // Concatenation of multiple string arguments.
    static_assert(str_lit("he", "llo") == ^^{ "hello" });

    // Integer arguments (unlike id, can start with int).
    static_assert(str_lit(123) == ^^{ "123" });
    static_assert(str_lit(0) == ^^{ "0" });

    // Mixed string and integer arguments.
    static_assert(str_lit("x", 42, "y") == ^^{ "x42y" });
    static_assert(str_lit("item_", 1) == ^^{ "item_1" });

    // Empty string.
    static_assert(str_lit("") == ^^{ "" });

    // Two tokens != one token (exact token form comparison).
    static_assert(^^{ "hel" "lo" } != ^^{ "hello" });

    // User-defined string-like type works with str_lit.
    static_assert(str_lit(string_view("world")) == ^^{ "world" });
}

namespace N29 {
    // Failed consteval-block evaluation must not commit injections that were
    // queued before the failure.
    int runtime();
    consteval { // expected-error {{evaluating expression of a consteval block must be a constant expression}}
        queue_injection(^^{ constexpr int leaked = 42; });
        runtime();
    }
    static_assert(leaked == 42); // expected-error {{use of undeclared identifier 'leaked'}}
}

namespace N30 {
    // Injected locals from an outer block remain visible to injected code in a
    // nested block during template instantiation.
    template <class T>
    constexpr int nested_injected_lookup() {
        consteval { queue_injection(^^{ int x = 1; }); }
        {
            consteval { queue_injection(^^{ return x; }); }
        }
    }
    static_assert(nested_injected_lookup<int>() == 1);
}

namespace N31 {
    // Invalid interpolation substitution should stop instantiation cleanly
    // rather than leaving the old dependent expression to be evaluated.
    template <class T>
    constexpr int invalid_interpolation() {
        consteval {
            // expected-error@+1 {{type 'int' cannot be used prior to '::' because it has no members}}
            queue_injection(^^{ return \(typename T::nope{}); });
        }
        return 0;
    }
    void trigger_invalid_interpolation() {
        (void)invalid_interpolation<int>();
    }
}

namespace N32 {
    // Late-parsed pieces of injected class members need to see later members
    // of the same class, like ordinary inline member definitions and default
    // member initializers do.
    struct C {
        consteval {
            queue_injection(^^{
                int a = sizeof(b);
                constexpr int f() const { return g(); }
            });
        }
        int b = 1;
        constexpr int g() const { return b; }
    };
    static_assert(C{}.a == sizeof(int));
    static_assert(C{}.f() == 1);
}

namespace N33 {
    using std::meta::tokenize;

    // Basic tokenize test - single identifier
    static_assert(tokenize("x") == ^^{ x });

    // Multiple tokens
    static_assert(tokenize("a + b") == ^^{ a + b });

    // Numeric literals - various bases
    static_assert(tokenize("123") == ^^{ 123 });
    static_assert(tokenize("0xCAFE") == ^^{ 0xCAFE });
    static_assert(tokenize("0b1010") == ^^{ 0b1010 });
    static_assert(tokenize("0777") == ^^{ 0777 });

    // Numeric literals with suffixes
    static_assert(tokenize("123u") == ^^{ 123u });
    static_assert(tokenize("123ul") == ^^{ 123ul });
    static_assert(tokenize("123ull") == ^^{ 123ull });

    // Floating point literals
    static_assert(tokenize("3.14") == ^^{ 3.14 });
    static_assert(tokenize("3.14f") == ^^{ 3.14f });
    static_assert(tokenize("1e10") == ^^{ 1e10 });

    // Keywords
    static_assert(tokenize("int") == ^^{ int });
    static_assert(tokenize("return") == ^^{ return });
    static_assert(tokenize("constexpr") == ^^{ constexpr });

    // Operators
    static_assert(tokenize("++") == ^^{ ++ });
    static_assert(tokenize("->") == ^^{ -> });
    static_assert(tokenize("::") == ^^{ :: });
    static_assert(tokenize("...") == ^^{ ... });

    // Complex expressions
    static_assert(tokenize("x + y * z") == ^^{ x + y * z });
    static_assert(tokenize("foo(1, 2)") == ^^{ foo(1, 2) });
    static_assert(tokenize("a->b.c") == ^^{ a->b.c });

    // Empty tokenize
    static_assert(tokenize("") == ^^{ });

    // Use tokenize with injection
    struct S {
        consteval {
            queue_injection(tokenize("int x = 42;"));
        }
    };
    static_assert(S{}.x == 42);

    // tokenize with string_view (via consteval function)
    consteval token_sequence make_tokens() {
        string_view sv = "int y = 100;";
        return tokenize(sv);
    }
    struct S2 {
        consteval { queue_injection(make_tokens()); }
    };
    static_assert(S2{}.y == 100);

    // Concatenating tokenize results
    static_assert(tokenize("a") + tokenize("b") == ^^{ a b });
    static_assert(tokenize("int") + tokenize("x") + tokenize(";") == ^^{ int x ; });

    // Multi-argument tokenize (concatenates before lexing)
    static_assert(tokenize("12", "3u") == ^^{ 123u });
    static_assert(tokenize(12, "3u") == ^^{ 123u });
    static_assert(tokenize(12, '3', 'u') == ^^{ 123u });
    static_assert(tokenize("0x", "CAFE") == ^^{ 0xCAFE });
    static_assert(tokenize("foo", "_", "bar") == ^^{ foo_bar });
    static_assert(tokenize("int", " ", "x", ";") == ^^{ int x ; });

    // Multi-arg with string_view
    consteval token_sequence make_multi_tokens() {
        string_view type = "int";
        string_view name = "z";
        return tokenize(type, " ", name, " = 200;");
    }
    struct S3 {
        consteval { queue_injection(make_multi_tokens()); }
    };
    static_assert(S3{}.z == 200);
}

namespace N34 {
    using std::meta::tokenize;
    using std::meta::stringize;
    using std::meta::id;

    // Basic stringize test
    static_assert(__builtin_strcmp(stringize(^^{ hello }), "hello") == 0);

    // Multiple tokens
    static_assert(__builtin_strcmp(stringize(^^{ a b c }), "a b c") == 0);

    // Whitespace preservation
    static_assert(__builtin_strcmp(stringize(^^{ a + b }), "a + b") == 0);
    static_assert(__builtin_strcmp(stringize(^^{ a+b }), "a+b") == 0);
    static_assert(__builtin_strcmp(stringize(^^{ a  +  b }), "a + b") == 0);

    // Operators
    static_assert(__builtin_strcmp(stringize(^^{ ++ }), "++") == 0);
    static_assert(__builtin_strcmp(stringize(^^{ -> }), "->") == 0);
    static_assert(__builtin_strcmp(stringize(^^{ :: }), "::") == 0);

    // Keywords
    static_assert(__builtin_strcmp(stringize(^^{ int }), "int") == 0);
    static_assert(__builtin_strcmp(stringize(^^{ constexpr }), "constexpr") == 0);

    // Numeric literals
    static_assert(__builtin_strcmp(stringize(^^{ 123 }), "123") == 0);
    static_assert(__builtin_strcmp(stringize(^^{ 0xCAFE }), "0xCAFE") == 0);
    static_assert(__builtin_strcmp(stringize(^^{ 3.14f }), "3.14f") == 0);

    // Complex expression
    static_assert(__builtin_strcmp(stringize(^^{ int x = 42; }), "int x = 42;") == 0);
    static_assert(__builtin_strcmp(stringize(^^{ int \(id("x")) = \(tokenize(42)); }), "int x = 42;") == 0);

    // Empty token sequence
    static_assert(__builtin_strcmp(stringize(^^{ }), "") == 0);

    // Round-trip: tokenize(stringize(tokens)) == tokens
    static_assert(tokenize(stringize(^^{ x })) == ^^{ x });
    static_assert(tokenize(stringize(^^{ a + b })) == ^^{ a + b });
    static_assert(tokenize(stringize(^^{ int x = 42; })) == ^^{ int x = 42; });
    static_assert(tokenize(stringize(^^{ foo(1, 2) })) == ^^{ foo(1, 2) });
    static_assert(tokenize(stringize(^^{ })) == ^^{ });

    // Stringize of tokenize result
    static_assert(__builtin_strcmp(stringize(tokenize("hello")), "hello") == 0);
    static_assert(__builtin_strcmp(stringize(tokenize("a + b")), "a + b") == 0);
}

// Interpolating a template reflection yields a template-name token: followed
// by a template-argument-list it forms a template-id exactly as the spelled
// name would. Here that names every specialization of a template known only
// by reflection in an injected partial specialization; the two templates must
// not collide.
namespace N35 {
    template <class T> struct is_wide { static constexpr bool value = false; };

    template <class T> struct wide_result { T hi; T lo; static constexpr int elems = 2; };
    template <class T> struct other { T x; static constexpr int elems = 1; };

    consteval auto derive(info tmpl) -> void {
        queue_injection(^^N35, ^^{
            template <class... Ts>
            struct is_wide<\(tmpl)<Ts...>> {
                static constexpr bool value = true;
                static constexpr int elems = \(tmpl)<Ts...>::elems;
            };
        });
    }

    consteval { derive(^^wide_result); }
    consteval { derive(^^other); }

    static_assert(!is_wide<int>::value);
    static_assert(is_wide<wide_result<int>>::value);
    static_assert(is_wide<wide_result<int>>::elems == 2);
    static_assert(is_wide<other<char>>::value);
    static_assert(is_wide<other<char>>::elems == 1);

    // An interpolated template or type may also be the operand of an
    // explicit splice.
    template <class T> struct is_other { static constexpr bool value = false; };
    consteval auto derive2(info tmpl, info ty) -> void {
        queue_injection(^^N35, ^^{
            template <class... Ts>
            struct is_other<typename [:\(tmpl):]<Ts...>> {
                static constexpr bool value = true;
                static constexpr int n = template [:\(tmpl):]<Ts...>::elems + [:\(ty):]::elems;
            };
        });
    }
    consteval { derive2(^^other, ^^wide_result<int>); }
    static_assert(is_other<other<int>>::value);
    static_assert(is_other<other<int>>::n == 3);
    static_assert(!is_other<wide_result<int>>::value);
}

// An interpolated template name behaves like the spelled name in every
// position; an interpolated namespace behaves like a splice of it.
namespace N36 {
    template <class T> struct W { T v; static constexpr int n = 1; using type = T; };
    template <template <class> class Z> struct holder { static constexpr int k = 7; };
    template <class T> constexpr int vt = sizeof(T);
    template <class T> constexpr T ft(T x) { return x + 1; }
    namespace NS { constexpr int q = 42; }

    consteval auto inject(info tmpl, info vtmpl, info ftmpl, info ns) -> void {
        queue_injection(^^N36, ^^{
            constexpr \(tmpl)<int> g1{5};              // decl-specifier
            using A = \(tmpl)<char>;                   // alias
            constexpr int g2 = \(tmpl)<int>::n;        // nested-name-specifier in an expression
            using B = \(tmpl)<long>::type;             // nested-name-specifier in a type-only context
            constexpr int g3 = holder<\(tmpl)>::k;     // template template argument
            constexpr int g4 = \(vtmpl)<double>;       // variable template
            constexpr int g5 = \(ftmpl)<int>(1);       // function template
            constexpr int g6 = \(ns)::q;               // namespace as nested-name-specifier
            constexpr auto g7 = \(tmpl)<int>{9}.v;     // functional cast
            constexpr int body() {                     // declaration statement
                \(tmpl)<int> x{3};
                return x.v + \(tmpl)<int>::n;
            }
            namespace inner {
                using namespace \(ns);                 // using-directive
                constexpr int g8 = q;
            }
            constexpr int g9 = [:\(ns):]::q;           // interpolated namespace as splice operand
        });
    }
    consteval { inject(^^W, ^^vt, ^^ft, ^^NS); }

    static_assert(g1.v == 5);
    static_assert(__is_same(A, W<char>));
    static_assert(g2 == 1);
    static_assert(__is_same(B, long));
    static_assert(g3 == 7);
    static_assert(g4 == 8);
    static_assert(g5 == 2);
    static_assert(g6 == 42);
    static_assert(g7 == 9);
    static_assert(body() == 4);
    static_assert(inner::g8 == 42);
    static_assert(g9 == 42);

    // A bare interpolated template is not an expression.
    consteval auto bad(info tmpl) -> void {
        queue_injection(^^N36, ^^{ constexpr int e = \(tmpl); }); // expected-error {{expected expression}}
    }
    consteval { bad(^^W); }
}

// An annotation's on_template_defined callback fires once when the annotated
// class template's definition completes, receiving a reflection of the
// template itself. Injections are deferred until the enclosing template
// declaration is finished, so injected templates are parsed at depth zero.
namespace N37 {
    template <class T> struct traits { static constexpr int v = 0; };

    struct ann {
        consteval auto on_template_defined(info tmpl) const -> void {
            queue_injection(^^N37, ^^{
                template <class... Ts> struct traits<\(tmpl)<Ts...>> {
                    static constexpr int v = \(tmpl)<Ts...>::id;
                };
            });
        }
    };

    template <class T> struct [[=ann{}]] W { static constexpr int id = 1; };
    template <class T> struct [[=ann{}]] V { static constexpr int id = 2; };

    // The bridge exists before any specialization is instantiated, and the
    // two templates' bridges coexist.
    static_assert(traits<W<int>>::v == 1);
    static_assert(traits<V<char>>::v == 2);
    static_assert(traits<int>::v == 0);

    // The callback does not fire for the pattern's specializations being
    // instantiated (only on_complete does that), nor for a template without
    // the annotation.
    template <class T> struct plain { };
    static_assert(traits<plain<int>>::v == 0);

    // An annotation without on_template_defined is fine on a class template.
    struct empty_ann { };
    template <class T> struct [[=empty_ann{}]] quiet { };
    static_assert(traits<quiet<int>>::v == 0);

    // on_template_defined on a *non-template* class does not fire.
    struct [[=ann{}]] not_a_template { static constexpr int id = 9; };

    // A value-dependent annotation argument is skipped at definition time.
    template <int N> inline constexpr ann pick{};
    template <int N> struct [[=pick<N>]] dep { static constexpr int id = 3; };
}
