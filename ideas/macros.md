# Expression macros

We're going to implement a new language feature that is similar to Swift
expression macros: a macro is declared in a way that looks like a function or
function template, but the body runs at translation time and returns a token
sequence that is expanded at the invocation site.

For now, macros expand to a single expression. Eventually, we may want macros
that can inject declarations or other syntactic forms too, but this design is
limited to expression macros.

Here is a simple example:

```cpp
__macro id(int x) {
    return ^^{ \(x) };
}

auto a = id!(1);
auto b = id!(2L);
auto c = id!(1 + 2) * 3;
```

The declarations of `a`, `b`, and `c` all have type `int`, and `c` is `9`, not
`7`: interpolation preserves the grouping of the argument expression.

## Declaration

`__macro` is a function specifier. A macro declaration is otherwise an ordinary
function declaration: it can be a template, it can be constrained, it can be
overloaded. The return type is not written; it is always
`std::meta::token_sequence`. The body is a consteval function body.

```cpp
template <class T>
    requires std::is_class_v<T>
__macro trace(T&& value) { ... }
```

Macros invoked by name are only ever found by ordinary unqualified or qualified
lookup, never by argument-dependent lookup. A macro name may only appear as the
callee of a macro invocation (below); using it anywhere else is ill-formed,
including inside a token sequence produced by another macro. Both rules follow
from the invocation syntax: the parser has to find the macro before it parses
the arguments. (Operator macros are the exception on both counts: they have no
name at the use site and are found however operator functions are found; see
[Member macros and operator macros](#member-macros-and-operator-macros).)

A macro cannot declare a parameter pack, cannot be virtual, defaulted or
deleted, and a non-static member macro must have an explicit object parameter
(all diagnosed). Default arguments are allowed: `id!()` binds the default
argument expression exactly as a call would. `name!()` is an empty argument
list — never a single empty token sequence — so a sole raw parameter needs a
default argument (`token_sequence body = ^^{}`) for an empty invocation to be
viable. Explicit template arguments
cannot be written at an invocation; the only spelling that fits the
`name!(` recognition is `name<Args>!(...)`, which is left for a later
iteration. Macro templates deduce everything from the arguments for now.

## Invocation

A macro is invoked as `name!(args)` (or `ns::name!(args)`). The `!` is required
and is what distinguishes a macro invocation from a function call. It is
grammatically free: `f!(x)` is not valid C++ today, and `f!=(x)` still lexes as
`!=`.

The `!` is there for the reader as much as for the parser. It says: the
arguments may be captured rather than evaluated, evaluated in rewritten form,
inspected for their spelling, or (for raw parameters) not parsed as expressions
at all. That's true of `check!(a == b)` just as much as of `λ!(_1 > _2)`.

On seeing `name!(`, the parser looks up `name`, determines the parameter shape
of the macro (which positions are raw, see below), and parses each argument
accordingly. All macros in an overload set must agree on the shape.

## Two kinds of parameters

There is one kind of macro, one call syntax, and two kinds of *parameter*,
distinguished by the declared type.

### Expression parameters

Any parameter whose type is not `token_sequence` is an expression parameter.
The corresponding argument is parsed as an assignment-expression in the
caller's context and fully semantically analyzed there, before the macro body
runs. Overload resolution, template argument deduction, and constraint checking
work exactly as for a function call with the same parameter list: `id!(2L)` is
viable because `2L` converts to `int`.

The declared type is a *contract*. It determines viability and the conversion
sequence applied to the argument; it does not create a parameter object.
Nothing is initialized, copied, or materialized. For `int x` and the argument
`2L`, the bound expression is the converted `(int)2L`. For a reference
parameter, the bound expression is the argument itself (after any derived-to-
base or qualification adjustment) with its original value category; no
temporary is materialized for a prvalue argument. `T&& t` deduces `T` the usual
way, so `T` tells the body the value category, as it would in a function.

The idiom is therefore `auto&&` / `T&&`: it binds anything, applies no
conversion, and leaves the argument exactly as written for inspection. Typed
parameters are for the minority of macros that actually want overloading or
conversion.

Because there is no parameter object, some things a `bool` parameter would
seem to give you it does not: a `bool` parameter only admits *implicit*
conversions, so `check!(opt)` with an `explicit operator bool` would fail
overload resolution. A macro that wants contextual conversion asks for it in
the expansion (`if (!\(cond))`) or in a constraint.

### Raw (`token_sequence`) parameters

A parameter of type `token_sequence` is raw. The corresponding argument is not
parsed as an expression; its tokens are captured as written and the body
receives them as a `token_sequence` value. This is the escape hatch for
anaphoric macros (`λ!(_1 > _2)`, where `_1` must see a name the macro
introduces) and for tiny DSLs (`define_op!(left_shift, x << y)`). It is exactly
as unhygienic as a preprocessor macro, and that is the point.

Delimiting is preprocessor-style: `()`, `[]`, `{}` must nest properly (a
mismatched closer is an error); a top-level comma ends the argument. `<>` is
not balanced (same wart as the preprocessor). The tokens are captured after
preprocessing: object-like and function-like macros in a raw argument have
already been expanded, `#` and `##` mean nothing, and `-E` output round-trips.
A raw parameter in the *last* position is greedy: it consumes everything up to
the closing paren, commas included, like `__VA_ARGS__`. That is what lets
`λ!(std::pair<int, int>{_1, _2})` work without the author thinking about it.

A macro may mix the two kinds. Overloads must agree on which positions are
raw.

## Inside the body

Within the macro body, a parameter name does not denote a value of its declared
type. It denotes a `std::meta::info` reflecting the bound expression (for an
expression parameter) or a `token_sequence` (for a raw parameter). The body is
a consteval function body: it can compute, branch, build token sequences, call
metafunctions. It just can't evaluate the arguments, because they aren't values
yet.

Template parameters of a macro template are ordinary template parameters and
are usable in the body as usual (`sizeof(T)`, `^^T`, `std::is_same_v<T, ...>`).
Note that spelling `T` inside `^^{ }` pastes the *token* `T`, which is looked up
at the expansion site and won't be found; write `\(^^T)`.

### Reflections of expressions

The reflection of a bound expression supports:

- `type_of(e)`: `decltype(e)` of the argument as written: the declared type
  for an unparenthesized id-expression or member access, otherwise the
  expression's type adjusted for value category. This is what makes `fwd!`
  work (below); note that `decltype(\(e))` in the expansion sees only an
  opaque expression and so reports `int&` for a named `int&&` parameter.
- `source_text_of(e)`: the spelling of the argument as written at the call
  site, as a `string_view`.
- `source_location_of(e)`: its location.
- `is_binary_operation(e)`: whether the expression is a built-in or overloaded
  binary operator application (including rewritten comparisons).
- `operator_of(e)`: for a binary operation, which operator, as the existing
  `std::meta::operators` enumeration.
- `operands_of(e)`: for a binary operation, the two operand expressions as
  reflections, with implicit conversions stripped so that they are the
  operands as written. Each is itself interpolable.

This is deliberately one step of navigation, not a full AST visitor. A macro
decomposes the shapes it recognizes and interpolates everything else whole.
Swift Testing's `#expect` works this way on the syntax tree; we get to do it on
the semantically analyzed tree, so `f<a>(b) == c` has already been resolved.

### Tokens

For raw parameters the only additional surface is a token list plus
classification: `tokens_of(ts)` returns each token of the sequence as its own
`token_sequence`; `stringize` gives a token's spelling. A single token's
lexical category is `token_kind_of(tok) -> token_kind`, where `token_kind` is
`{ identifier, keyword, literal, punctuator, annotation, unknown }`. All
operators and punctuators collapse into `punctuator` — to match a *specific*
operator, compare the token directly (`tok == ^^{ + }`), since token sequences
compare equal by content. `unknown` covers empty and multi-token sequences.
Classify by `token_kind_of`, not by `stringize` string-sniffing. Note that
alternative tokens keep their spelling, so `^^{ or } != ^^{ || }` even though
both are `punctuator`; kind-based checks see through this, `==` does not.
To match a specific identifier, compare against `id(...)` directly
(`tok == id("name")` — `id` produces a single-identifier-token
`token_sequence`, the identifier sibling of `str_lit`; it originally
returned an identifier *reflection*, a vestige removed once tokens became
directly comparable). `operator_of(tok) -> operators` converts a token
spelling a complete operator (`(`, `[`, `new` do not qualify) into the
interpolable `operators` vocabulary. Concatenation and
interpolation of `token_sequence` values already exist. There are no grammar
fragment parameters and no parser-combinator API; if a macro wants to treat
raw tokens as an expression, that is a future `parse_expression(ts)`
conversion, not something either of the motivating examples needs.

## Interpolation and evaluation

`\(e)` inside a returned token sequence, where `e` is a reflection of a bound
expression, interpolates that expression as a single primary expression. The
grouping of the argument is preserved (`id!(1 + 2) * 3` is `9`), it is not an
id-expression (so `decltype(\(t))` reports the expression's type and value
category, `int&` for an lvalue `int`), and it keeps the caller's name lookup:
`\(x)` always means the `x` the caller wrote, regardless of what the macro
declares around it.

Each interpolated expression is evaluated exactly once, at the point where its
interpolation lands in the expansion. This is the rule that makes `check!`
correct, and it has one consequence: **the interpolated nodes of one argument's
expression tree must be disjoint**. Interpolating `cond` and also
`operands_of(cond)[0]` would evaluate the left operand twice, and is
ill-formed. Un-interpolated ancestors are simply discarded (nothing evaluates
the `==` node once its operands have been pulled out). Interpolating the same
reflection twice in *potentially evaluated* positions is likewise ill-formed;
`decltype(\(t))` alongside `\(t)` is fine.

If a macro wants laziness (`log_if!(cond, expensive())`), that is a future
parameter kind, not a change to this default.

An *expression* argument cannot be interpolated into the body of a lambda
inside the expansion (`^^{ [&] { return \(x); } }` is ill-formed). The
argument's names were bound in the enclosing function and were never
captured; evaluating it from a different function would be unsound. Use a
`do` expression for statements. Raw token arguments have no bindings and can
be pasted anywhere — that is how `λ!` builds its lambda body.

Both this rule and evaluate-once see through nested macro invocations:
forwarding an argument into a nested macro's expansion is still an
evaluation of the outer argument, and still cannot land it inside a lambda.

Other interpolations behave as they already do for token injection:
`token_sequence` values are concatenated in place, reflections of types,
templates, namespaces and declarations materialize as the corresponding
tokens, and constant values become literals. An interpolated type works in
every position a named type would, including as a nested-name-specifier:
`\(CT)::should_continue(r)` qualifies into the reflected class directly, with
no `[: :]` wrapper — in expressions, statements (the declaration/expression
disambiguator understands it), declared types (`\(CT)::Inner x;`,
`typename \(CT)::type`), and qualified declarator-ids
(`int \(CT)::member() { ... }` in a declaration-position expansion). Additionally, a value of type
`std::meta::operators` interpolates as the operator's token, so a decomposed
comparison can be re-applied with `\(operator_of(cond))`. Only operators that
are a single token can be interpolated this way; `()`, `[]`, `new`, `delete`
and friends are rejected during evaluation. Use `str_lit` to
turn a `string_view` (such as `source_text_of(e)`) into a string literal token.

## Expansion

The returned token sequence is parsed at the invocation site and must form
exactly one expression; if it does not, the program is ill-formed and the
diagnostic says so. The expression is then used in place of the invocation, as
a primary expression (so `id!(a) * 3` and `λ!(_1 > _2)(1, 2)` group as
expected). `do` expressions are the vehicle for expansions that need
statements.

Names spelled literally in the token sequence are looked up from the expansion
context. Names local to the macro body are not visible to the expansion; to
carry information from the body into the expansion, interpolate it.

If any argument is type-, value-, or otherwise instantiation-dependent, the
invocation is kept as a `CXXMacroInvocationExpr` (callee, arguments, and
locations) and overload resolution and expansion are deferred to instantiation,
exactly as for a call. The expansion is then parsed in the instantiated
context: the parser is given a scope for the instantiated function with its
parameters and with the instantiations of the locals visible before the
invocation, so unqualified names in the macro's tokens resolve as they would
have at the invocation site. This is the case that matters most, because
`fwd!(x)` lives in generic code.

## Member macros and operator macros

A macro can be a class member. A non-static member macro must take its object
through an explicit object parameter; there is no implicit-`this` form, because
the expansion is parsed at the call site (where `this` means the *caller's*
`this`, if any) and the only way the macro can refer to the object is as an
expression it was handed:

```cpp
struct counter {
  int n = 0;
  template <class Self>
  __macro bump(this Self&& self, int by) { return ^^{ (\(self).n += \(by)) }; }
  static __macro make(int n) { return ^^{ counter{\(n)} }; }
};
c.bump!(2);   p->bump!(3);   counter::make!(1)
```

`obj.name!(args)` and `obj->name!(args)` look `name` up in the object's class
(so a base class's macro is found through a derived object, and `->` follows
an `operator->` chain first), bind the object expression to the explicit
object parameter, and expand as usual; the object expression is evaluated once
like any other argument. An unqualified `name!(args)` inside a member function
that finds non-static member macros is an implicit member access on `*this`,
as it would be for a function. A static member macro is a namespace-scope
macro with a different scope, invoked as `C::name!(args)`; `obj.make!()` is
rejected rather than given the evaluate-and-discard semantics of
`obj.staticfn()`.

Forwarding the object is `fwd!(\(self))` — the same `fwd!` as everywhere
else, composed with interpolation. This works because two rules line up: the
nested invocation binds not to the caller's object expression itself but to
the opaque value that `\(self)` interpolates, and `type_of` on anything that
is not an id-expression or member access is reference-qualified by value
category (decltype semantics). So an lvalue object yields `counter&`, an
rvalue `counter&&`, exactly what `static_cast<\(^^Self)&&>(\(self))` would
produce (also valid, with `Self` interpolated as a reflection since the
macro's template parameters are not in scope at the expansion site). The two
`type_of` regimes agree by construction: for a named forwarding reference the
declared type already encodes the deduced category; for an interpolated
expression the opaque value preserves the category directly.

### Operators

Any overloadable operator can be a macro, except conversion functions (an
implicit conversion sequence cannot expand a macro), allocation and
deallocation functions and literal operators (their "arguments" are not
operands of an expression), and `co_await` (not yet). Operator macros have
only expression parameters, since operator syntax supplies expressions.

An operator macro participates in overload resolution exactly as an operator
function would — member and non-member candidates, argument-dependent lookup,
built-in candidates, and (for `==` and `<=>`) reversed and rewritten
candidates. There is no tie-breaker between a macro and a function: a class
may provide both, and if they tie the operator is ambiguous. The only
difference is what happens once a macro is selected: the operands are bound to
its parameters unevaluated and the expansion replaces the operator expression.
For a rewritten candidate the rewrite applies to the expansion (`a != b` is
`!(expansion)`), and the requirement that a rewritten `operator==` yield
`bool` is checked on the expansion's type, which is only known then.

There is no `!` at an operator use site, and for the motivating case that is
semantically free. `elems[0]` on a tuple:

```cpp
template <class Self, class I>
__macro operator[](this Self&& self, I&& i) {
  if (!is_constant_expression(i))
    return ^^{ runtime_get(\(self), \(i)) };
  return ^^{ get<\(constant_of(i))>(fwd!(\(self))) };
}
```

If `constant_of(i)` succeeds, `i` is a constant expression and not evaluating
it at run time is unobservable; otherwise the macro interpolates it and it is
evaluated once, like a function argument. Either way the caller cannot tell it
from a function — which is also why this beats constexpr function parameters
for this problem: the value-dependent return type is simply the type of the
expansion, with no new kind of function to invent.

The one operator where the missing `!` hides something is sequencing. `<<`,
`=`, `[]` and `->*` guarantee left-before-right even when overloaded; a macro
evaluates its operands in whatever order it interpolates them. For `&&`, `||`
and `,` that is the point — an `operator&&` macro can be lazy, which no
overloaded `operator&&` can — and for the rest it is on the macro's author.

Calling an operator macro with function-call syntax (`operator+(a, b)`) is an
error, as is naming any macro.

### Explicit failure

A macro can decline to produce an expansion by calling
`std::constexpr_error_str` (P2758, `<debugging>`) in its body. Everywhere
else `constexpr_error_str` follows the paper: the message is emitted, the
program is ill-formed, and the evaluation *remains constant* — but a macro
body evaluation that reports an error produces no expansion, so the
invocation is an **invalid expression**. During substitution that is a
substitution failure: `requires { v[i]; }` is `false` rather than an error.
In a plain context the invocation reports the macro's message:

```
error: expression macro 'operator[]' reported an error
note: constexpr message: index must be a constant expression
```

This is what lets `ranges::begin`-style ladders be written as literal
if-else: the "ill-formed" rungs are `constexpr_error_str` calls, observable
through `requires` exactly as the CPO machinery's constrained overload sets
are. `std::constexpr_warning_str` and `std::constexpr_print_str` work in
macro bodies too (a warning does not suppress the expansion), which gives
macros user-authored warnings under `-Wconstexpr-messages`.

### Reproducing named-parameter semantics: `as_lvalue`

A specification like [range.access.begin] speaks about a *named parameter*
`t` — a named forwarding reference, hence an lvalue no matter how the
argument was passed. A macro's `\(r)` preserves the argument's original
value category instead, so an expansion that writes `\(r).begin()` on an
rvalue argument would select `begin() &&` where the CPO selects `begin() &`.
The primitive that restores the function's semantics is

```cpp
as_lvalue(info) -> info
```

a reflection of the expression viewed as if it had been bound to
`auto&& tmp` and then named: identity for an lvalue, a change of view for an
xvalue, a materialized temporary (living to the end of the enclosing
full-expression) for a prvalue. It preserves the source-expression chain, so
the evaluate-once analysis still counts one evaluation.

With that, [range.access.begin] reads off the page — every rung's validity
is expressible from `R`, so ordinary requires-expressions ask the questions
and the expansion answers them:

```cpp
inline constexpr struct begin_fn {
  template <class Self, class R>
  __macro operator()(this Self, R&& r) {
    if constexpr (std::is_array_v<std::remove_reference_t<R>>) {
      return ^^{ (\(as_lvalue(r)) + 0) };
    } else if constexpr (requires(R&& t) {
                           { auto(t.begin()) } -> std::input_or_output_iterator;
                         }) {
      return ^^{ auto(\(as_lvalue(r)).begin()) };
    } else if constexpr (requires(R&& t) {
                           { impl::adl_begin(t) } -> std::input_or_output_iterator;
                         }) {
      return ^^{ ::rng::impl::adl_begin(\(as_lvalue(r))) };
    } else {
      std::constexpr_error_str("no-begin", "no viable begin for this type");
    }
  }
} begin{};
```

`auto(...)` is the spec's decay-copy; the ADL rung is spelled through a
qualified helper (`adl_begin`, next to the `void begin(auto&) = delete`
poison pill) rather than a bare `begin(...)`, because expansion tokens are
looked up at the call site, where a bare `begin` would find the CPO object
itself. The usual hygiene idiom — refer to your own things by qualification
or reflection, not by spelling — is load-bearing here.

### Probing validity

When validity genuinely depends on the exact expansion-site expression — on
names or scopes that cannot be represented from the macro's template
arguments — requires-expressions cannot ask the question. For those cases:

```cpp
test_expression(token_sequence) -> optional<info>
```

speculatively parses the tokens as a single expression at the expansion
site, with all diagnostics suppressed and typo correction disabled. On
failure it returns `nullopt`; on success, a reflection of the
*already-parsed* expression, so the macro can inspect it (`type_of` gives
its decltype) and interpolate it — reusing the parse rather than re-emitting
the tokens, and evaluating the interpolated arguments inside it exactly
once. The evaluate-once analysis sees through the probed expression, so
probing an argument and also interpolating it directly is still diagnosed.

Probing is SFINAE-flavored: template instantiations it triggers are
permanent, and an error outside the probed expression's immediate context is
(deliberately) swallowed rather than diagnosed — validity means "parsed and
type-checked", the same contract as `requires`.

### Seeing the invocation site: `macro_expansion_context`

A macro conceptually expands where it is invoked, so its body should be able
to ask about that context, not just about its arguments. Swift gives macros a
`MacroExpansionContext` parameter; the reflection spelling of the same idea is
a getter:

```cpp
consteval auto macro_expansion_context() -> info;
```

It reflects the context the expansion lands *in*: the enclosing function for
an invocation in expression position, the class for a member declaration,
otherwise the enclosing namespace. A macro that only makes sense in one of
those checks the kind it got and declines otherwise — which is a better
contract than a narrower query like `current_function()` that has no answer
in two of the three positions.

`try_!` is the motivating case. The C-macro spelling had to inject

```cpp
using RT = try_traits<typename [: return_type_of(std::meta::current_function()) :]>;
```

into the expansion, because textual expansion is the only way a C macro can
observe its caller. As an expression macro, both trait specializations are
just values in the body:

```cpp
template <class T>
__macro try_(T&& e) {
  info where = macro_expansion_context();
  if (!is_function(where))
    std::constexpr_error_str("bad-try-context",
                             "try_ must be invoked inside a function");

  info CT = substitute(^^try_traits, {remove_cvref(type_of(e))});
  info RT = substitute(^^try_traits, {return_type_of(where)});

  // \(CT) and \(RT) interpolate as the concrete specializations, usable
  // directly as nested-name-specifiers.
  return ^^{
    do -> decltype(auto) {
      auto&& __r = \(e);
      if (not \(CT)::should_continue(__r)) [[unlikely]] {
        return \(RT)::from_break(
            \(CT)::extract_break(static_cast<decltype(__r)&&>(__r)));
      }
      do_return \(CT)::extract_continue(static_cast<decltype(__r)&&>(__r));
    }
  };
}
```

The expansion carries no `using` declarations and no reflection calls — only
spliced concrete types. It also gets hygiene for free: `__r` is the macro's
own local, and `\(e)` is an already-bound expression that cannot see it.

Two properties worth naming, both verified:

- A plain `return` inside a `do`-expression body returns from the *enclosing
  function*, which is exactly the control flow `try_` needs; `do_return`
  yields the do-expression's value. This is what replaces the C macro's
  `match`/`case`.
- The context tracks the invocation, not the definition: one macro definition
  invoked from functions returning `void`, `int`, `double`, `Widget`, and a
  member returning `bool` reports each of those in turn; the same definition
  invoked in declaration position at namespace scope, in a class, and in a
  default member initializer reports namespace, class, and class.

Implementation: `EvaluateMacroBody` records the invocation's enclosing
function/class/namespace (walked out of `Sema::CurContext`) in a dedicated
`EvalInfo::MacroExpansionContext`. Metafunctions opt into receiving it with
`Metafunction::wantsMacroExpansionContext` — the existing `ContainingDecl`
channel could not be reused, because it carries the injection target that
`define_aggregate` and `annotate` depend on, and is separately set to the
`VarDecl` being initialized during ordinary constant evaluation.

Evaluating it outside a macro expansion is an error rather than a guess,
since there is no expansion whose context could be described.

## Name lookup and hygiene

Expression parameters give most of hygiene for free, in both directions:
because `\(x)` interpolates an already-bound expression, a local the macro
declares in a `do` block cannot capture a name inside the caller's argument.
What remains is that the macro's own free names (`helper`, `std::move`) are
looked up at the expansion site, so the caller can shadow them. The idiom to
avoid that is to interpolate a reflection (`[:^^helper:]`, `\(^^std::move)`)
rather than spell the name. Raw parameters are not hygienic at all, by design.

## Examples

### `id!`

As above. Demonstrates that the declared type is honored (`id!(2L)` is an
`int`) and that grouping is preserved.

### `fwd!`

```cpp
template <class T>
__macro fwd(T&& t) {
    return ^^{ static_cast<\(type_of(t))&&>(\(t)) };
}

void g(int x, int& y, int&& z) {
    decltype(auto) fx = fwd!(x);     // int&&
    decltype(auto) fy = fwd!(y);     // int&
    decltype(auto) fz = fwd!(z);     // int&&  (declared type of z, as FWD)
    decltype(auto) fp = fwd!((z));   // int&   (parenthesized: an lvalue)
}
```

This is exactly the preprocessor `FWD(x)`, `static_cast<decltype(x)&&>(x)`,
and it has to be: forwarding depends on the *declared* type of the argument,
and a named rvalue reference is an lvalue expression. That is why `type_of`
on an expression follows `decltype`, and why `\(^^T)` (deduced from the
expression's value category) would be wrong here: `fwd!(z)` would never
move.

### `check!`

```cpp
template <class T>
    requires requires (T&& t) { static_cast<bool>(static_cast<T&&>(t)); }
__macro check(T&& cond) {
    auto text = str_lit(source_text_of(cond));
    auto loc  = source_location_of(cond);

    if (is_binary_operation(cond) && is_comparison(operator_of(cond))) {
        auto ops = operands_of(cond);
        return ^^{ do {
            auto&& l = \(ops[0]);
            auto&& r = \(ops[1]);
            if (!(fwd!(l) \(operator_of(cond)) fwd!(r)))
                ::test::fail(\(text), \(loc), l, r);
        } };
    }
    return ^^{ do {
        if (!static_cast<bool>(\(cond)))
            ::test::fail(\(text), \(loc));
    } };
}
```

`check!(x == y)` reports `"x == y"` with both values; `check!(a == b && c)`
falls through to the whole-expression form instead of failing to compile, as
it does in Catch2. `fwd!(l)` restores the operand's value category so
`check!(std::move(s) == t)` still moves; macros compose, since an expansion is
parsed like any other code and `fwd!` is just a name in it. This is what Catch2
builds with `Decomposer <= a == b` and a page of operator overloads.

### `λ!`

```cpp
__macro λ(token_sequence body) {
    int arity = 0;
    for (token_sequence tok : tokens_of(body))
        if (auto n = placeholder_index(stringize(tok)))   // "_1".."_9"
            arity = std::max(arity, *n);

    list_builder params(^^{ , });
    for (int i = 1; i <= arity; ++i)
        params += ^^{ auto&& \(id("_", i)) };

    return ^^{ [](\(params)) -> decltype(auto) { return \(body); } };
}

std::ranges::sort(v, λ!(_1 > _2));
```

Pure pasting plus one scan to count placeholders. The raw parameter is what
makes this possible; the body must see the lambda's `_1`, not the caller's.

## Implementation notes

The `macro-experiment` branch (May 2026) established the skeleton: `__macro`
as a function specifier, macros as `FunctionDecl`s found and overloaded like
functions, parameter references in the body rewritten to `info`-typed
expressions, a `ReflectionKind::Expression` wrapping an `Expr*`, and a
Sema-to-Parser bridge for parsing the expansion. It did not execute the body
(it pattern-matched a fixed shape), it spliced argument expressions directly
rather than through an evaluate-once opaque value, and it did not handle
dependent invocations. Those are the pieces this design adds.

Interpolated expressions materialize as unique `OpaqueValueExpr`s (source
expression attached, emitted in place), which is how the once-evaluation rule
is realized in the AST and what makes `decltype(\(t))` report value category.

## Declaration-position invocation

`name!(args);` may also appear where a declaration can: at namespace scope
and at class scope. There the expansion is parsed as a *sequence of
declarations* in place of the invocation — the invocation context decides how
the expansion is parsed, exactly as the expression form's single-expression
rule does. This is what makes `define_op` read the way it means:

```cpp
__macro define_op(std::meta::token_sequence name,
                  std::meta::token_sequence pattern) {
  ...
  return ^^{
    struct \(name) { ... };
  };
}

define_op!(left_shift, x << y);   // at namespace scope; no consteval block,
                                  // no queue_injection wrapping
```

At class scope the members are injected under the access specifier in force
at the invocation; the expansion may contain its own access-specifier labels,
and those do not leak past the invocation. An expansion may itself contain
declaration-position invocations, and an empty expansion injects nothing
(useful for conditional injection). The trailing `;` is required.

There is no deferral: a declaration-position invocation in a dependent
context (a class template, most commonly) is an error. That is what
`consteval { queue_injection(...); }` is for — it remains the programmable
form, both for dependent contexts and for loops
(`for (auto m : members) queue_injection(gen(m))`). Declaration position is
the one-shot sugar; the block is the general tool.

## Annotation callbacks: `inject_members`

Class annotations support three callbacks, split by phase:

- `on_template_defined(info tmpl)` — after a class template's definition,
  once, with the template; for namespace-scope injections that must precede
  every specialization (`std::tuple_size` et al.).
- `inject_members(info type) -> token_sequence` — **right before the class
  is completed** (after all written members, before field completion and the
  class checks). The returned tokens are parsed as additional members, which
  participate fully in completion: layout, triviality, implicit members.
- `on_complete(info type)` — after completion, with the complete type;
  for measurement (layout asserts, registration) and external injection.

The `inject_members` contract: callbacks of multiple annotations run once
each, in annotation order, and each is interleaved with the parsing of its
predecessor's tokens, so a later callback's members can name an earlier
one's. Each callback's members start from the class's default access
(`private` for `class`, `public` for `struct`), regardless of the access in
force at the end of the written body; the tokens may contain their own
access-specifier labels. `inject_members` always receives a **non-dependent,
being-completed type**: for a class template it fires per specialization,
right before that specialization's completion — never on the dependent
pattern, whose member types are dependent (and whose member walk sees
nothing), so any decision computed from them would be garbage. A consequence
is that the subject's template parameter names are not spellable in the
returned tokens: deduce (`auto`), or interpolate reflections computed from
the concrete type (`template_arguments_of`); an annotation's *own* template
parameters are substituted into its token literals as usual. Injections
queued from inside the callback drain only after the class completes.

Two rules of thumb: never evaluate completeness-sensitive predicates
(concepts, `sizeof`) on the subject inside `inject_members` — satisfaction
is cached, and asking early poisons the answer ([temp.constr.atomic]);
inject the question or use `on_complete` instead. And the callback that owns
a phase should do that phase's work: mutate in `inject_members`, measure in
`on_complete`.

## Future directions

- A lazy parameter kind for `log_if!`-style macros.
- `parse_expression(ts)` to turn raw tokens into a bound expression on demand.
- Richer expression reflection: value category queries, unary operators,
  calls, member access, so `check!` can capture intermediates the way Swift
  Testing does.
- Reflection on the pre-conversion argument for typed parameters (`id!(2L)`
  discovering the `2L`).
