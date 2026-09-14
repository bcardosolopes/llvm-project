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

Macros are only ever found by ordinary unqualified or qualified lookup, never
by argument-dependent lookup. A macro name may only appear as the callee of a
macro invocation (below); using it anywhere else is ill-formed, including
inside a token sequence produced by another macro. Both rules follow from the
invocation syntax: the parser has to find the macro before it parses the
arguments.

A macro cannot be a class member, and cannot declare a parameter pack (both
are diagnosed). Default arguments are allowed: `id!()` binds the default
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
A token can also be converted into the two vocabularies interpolation already
understands: `identifier_of(tok) -> info` (an identifier reflection, as
`id(...)` produces) and `operator_of(tok) -> operators` (for a token spelling a
complete operator; `(`, `[`, `new` do not qualify). Concatenation and
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
tokens, and constant values become literals. Additionally, a value of type
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

## Future directions

- Declaration-position macros (`define_op!` expanding to a struct); the
  declaration form is identical, only the expansion context differs. In the
  meantime `consteval { queue_injection(define_op(^^{name}, ^^{x << y})); }`
  works with a plain consteval function.
- A lazy parameter kind for `log_if!`-style macros.
- `parse_expression(ts)` to turn raw tokens into a bound expression on demand.
- Richer expression reflection: value category queries, unary operators,
  calls, member access, so `check!` can capture intermediates the way Swift
  Testing does.
- Reflection on the pre-conversion argument for typed parameters (`id!(2L)`
  discovering the `2L`).
