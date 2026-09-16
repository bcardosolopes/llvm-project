// RUN: %clang_cc1 %s -std=c++26 -freflection -fconsteval-operations -fsyntax-only -verify

// The inject_members annotation callback: invoked right before the annotated
// class is completed, its returned tokens are parsed as additional members.
// Multiple annotations run in order, each seeing the previous injections.

namespace std::meta {
    using info = decltype(^^::);
    using token_sequence = decltype(^^{ });

    consteval auto queue_injection(token_sequence) -> void;
    consteval auto queue_injection(info target_ns, token_sequence) -> void;

    template <class... Ts>
    consteval auto tokenize(Ts const&...) -> token_sequence;

    template <class... Ts>
    consteval auto id(Ts const&...) -> info;
}
using std::meta::info;
using std::meta::token_sequence;

namespace N1 {

// The injected member participates in the class's completion: it is there
// for layout, and usable like any written member.
struct add_tail {
  consteval auto inject_members(info) const -> token_sequence {
    return ^^{
      int tail = 20;
      constexpr int total() const { return head + tail; }
    };
  }
};

struct [[=add_tail{}]] S {
  int head = 22;
};

static_assert(sizeof(S) == 2 * sizeof(int));
static_assert(S{}.total() == 42);

// An annotation without the callback injects nothing.
struct inert {};
struct [[=inert{}]] T { int x; };
static_assert(sizeof(T) == sizeof(int));

}  // namespace N1

namespace N2 {

// Ordering: callbacks run in annotation order, and a later callback's tokens
// can name members injected by an earlier one (a static member initializer
// is parsed immediately, so this is parse-order sensitive).
struct first {
  consteval auto inject_members(info) const -> token_sequence {
    return ^^{ static constexpr int a = 1; };  // expected-note {{'Ordered::a' declared here}}
  }
};
struct second {
  consteval auto inject_members(info) const -> token_sequence {
    return ^^{ static constexpr int b = a + 1; };  // expected-error {{use of undeclared identifier 'a'}}
  }
};

struct [[=first{}]] [[=second{}]] Ordered {};
static_assert(Ordered::b == 2);

struct [[=second{}]] [[=first{}]] Reversed {};  // expected-note {{in member declarations injected into 'Reversed'}}

}  // namespace N2

namespace N3 {

// Injected members start from the class's default access.
struct add_secret {
  consteval auto inject_members(info) const -> token_sequence {
    return ^^{ int secret = 5; };  // expected-note {{implicitly declared private here}}
  }
};

class [[=add_secret{}]] C {};
int probe = C{}.secret;  // expected-error {{'secret' is a private member of 'N3::C'}}

struct [[=add_secret{}]] D {};
int ok = D{}.secret;  // public in a struct

}  // namespace N3

namespace N4 {

// On a class template the callback fires per specialization, with the
// concrete type -- never on the dependent pattern, where the member types
// would be dependent and any decision computed from them garbage. The
// subject's template parameter names are not in scope in the injected
// tokens (deduce, or interpolate reflections of the template arguments);
// an annotation's own template parameters are substituted into its token
// literals as usual (see N7).
struct add_first {
  consteval auto inject_members(info) const -> token_sequence {
    return ^^{
      constexpr auto first() const { return value[0]; }
    };
  }
};

template <class T>
struct [[=add_first{}]] Wrap {
  T value[3];
};

static_assert(Wrap<int>{{7, 8, 9}}.first() == 7);
static_assert(Wrap<char>{{'a', 'b', 'c'}}.first() == 'a');

}  // namespace N4

namespace N5 {

// The callback can additionally queue injections outside the class; those
// drain only after the class completes, so they observe the complete type.
// (The natural home for post-completion logic is on_complete; this checks
// the drain timing of injections queued from the pre-completion window.)
struct validated {
  consteval auto inject_members(info r) const -> token_sequence {
    std::meta::queue_injection(^^{
      static_assert(sizeof(\(r)) == 2 * sizeof(int));
    });
    return ^^{ int second; };
  }
};

struct [[=validated{}]] V { int one; };

}  // namespace N5

namespace N6 {

// The callback must return a token sequence...
struct wrong_type {
  consteval auto inject_members(info) const -> int { return 5; }
};
struct [[=wrong_type{}]] W {};  // expected-error {{'inject_members' callback of annotation type 'wrong_type' must return a token sequence}}

// ...and the tokens must form member declarations.
struct not_members {
  consteval auto inject_members(info) const -> token_sequence {
    return ^^{ 1 + 2 };  // expected-error {{expected member name or ';' after declaration specifiers}}
  }
};
struct [[=not_members{}]] X {};  // expected-note {{in member declarations injected into 'X'}}

}  // namespace N6

namespace N7 {

// A dependent annotation cannot run at pattern-definition time; it fires for
// each specialization instead, right before that specialization's fields are
// finished, so the injected members are per-specialization and participate
// in layout.
template <class T>
struct add_value {
  consteval auto inject_members(info) const -> token_sequence {
    return ^^{ T value = T(); };
  }
};

template <class T>
struct [[=add_value<T>{}]] X {
  T first = T();
};

static_assert(requires(X<int> x) { x.value; });
static_assert(sizeof(X<double>) == 2 * sizeof(double));
constexpr X<int> x{};
static_assert(x.first == 0 && x.value == 0);

}  // namespace N7

namespace N8 {

// Malformed returned tokens cannot stall the compiler: a stray '}' is
// diagnosed and consumed, with a note identifying the injection.
struct bad {
  consteval auto inject_members(info) const -> token_sequence {
    return std::meta::tokenize("}");  // expected-error {{extraneous closing brace ('}')}}
  }
};
struct [[=bad{}]] Y {};  // expected-note {{in member declarations injected into 'Y'}}

}  // namespace N8

namespace N9 {

// One token-sequence literal evaluated repeatedly injects several copies of
// tokens with identical source locations; the parser's progress detection
// must not mistake the second copy for a stuck parse (it treats a parse as
// stuck only if it also produced no declaration).
struct add_pair {
  consteval auto inject_members(info) const -> token_sequence {
    token_sequence out = ^^{};
    for (int i = 0; i < 2; ++i) {
      // the same literal, evaluated twice -- member templates, the hard case
      out += ^^{
        template <class X>
        constexpr X \(std::meta::id("m", i))(X x) const { return x + \(i); }
      };
    }
    return out;
  }
};

struct [[=add_pair{}]] Twice {};
static_assert(Twice{}.m0(5) == 5);
static_assert(Twice{}.m1(5) == 6);

}  // namespace N9
