# Third review of expression macros

Implementation range reviewed: `b952d5d00413..c295870eadce`

Design reviewed: [`macros.md`](macros.md)

## Verdict

This is another substantial step forward. Nested argument provenance now works,
the common deferred-lookup cases are covered, `CXXMacroInvocationExpr` has grown
cleanly into member syntax, and routing operator overload winners through the
same expansion machinery is the right overall shape. Returning the already
parsed expression from `test_expression` is also exactly the right foundation
for specification ladders: it avoids both reparsing and accidentally evaluating
an argument twice.

There is still one architectural blocker and three high-priority correctness
gaps. The `ranges::begin` example demonstrates the new facility well, but does
not yet model `[range.access.begin]` closely enough to validate that use case.

## Findings

### 1. Blocker: deferred macros still do not survive PCH or modules

This remains unchanged from the previous review. `CXXMacroInvocationExpr` now
serializes its member state, but the macro body ultimately depends on a
`CXXTokenSequenceExpr`, and token sequences are still deliberately serialized
as empty:

```cpp
// FIXME: Token sequences currently round-trip through PCH/modules as an
// empty token sequence ...
```

Consequently, a macro declared in a PCH can be found and its invocation can be
deserialized, but instantiating a deferred use still has no body tokens from
which to produce the expansion. This is now load-bearing for all named, member,
and operator macros as well as for `test_expression` probes.

Relevant implementation:

- `clang/lib/Serialization/ASTWriterStmt.cpp`,
  `VisitCXXTokenSequenceExpr`
- `clang/lib/Serialization/ASTWriterStmt.cpp` and `ASTReaderStmt.cpp`,
  `CXXMacroInvocationExpr` serialization

### 2. High: `test_expression` accepts an invalid expression after typo correction

Speculative parsing installs a `SFINAETrap` and suppresses diagnostics, but it
neither disables typo correction nor checks `Trap::hasErrorOccurred()`. A
corrected expression is a valid AST without `containsErrors()`, so a misspelling
can make a probe succeed and the corrected expression can then be interpolated:

```cpp
constexpr int existing() { return 7; }

__macro typo(auto&& x) {
  if (auto e = std::meta::test_expression(^^{ existin() }))
    return ^^{ \(*e) };
  return ^^{ 0 };
}

static_assert(typo!(0) == 0); // fails: the expansion evaluates existing()
```

This violates the central contract of `test_expression`: the supplied tokens
do not form a valid expression at the expansion site. It is particularly risky
in a specification ladder, where an accidentally similar name can silently
select the wrong rung.

Use the machinery intended for provisional semantic analysis (at minimum,
disable typo correction) and make any trapped error force a null result, even
when Sema recovered a superficially usable expression. Add this exact typo case
to `test-expression.verify.cpp`.

Relevant implementation:

- `clang/lib/Parse/ParseReflect.cpp`,
  `ParseExpressionMacroExpansion(..., Speculative=true)`
- `clang/include/clang/Sema/Sema.h`, `TentativeAnalysisScope` and `SFINAETrap`

### 3. High: raw member macros cannot be called through an unknown dependent type

When a member base has an unknown dependent type, the parser cannot look up the
member macro to discover its parameter shape. It currently parses every
argument as an expression. If instantiation later selects a member macro with a
raw parameter, overload resolution sees an ordinary expression instead of a
`token_sequence`:

```cpp
struct S {
  __macro raw(this S const&, std::meta::token_sequence t) {
    return ^^{ (\(t)) };
  }
};

template <class T>
constexpr int call(T const& t) { return t.raw!(1 + 2); }

static_assert(call(S{}) == 3);
```

The specialization fails with no conversion from `int` to `token_sequence`.
Raw syntax that is not itself an expression fails even earlier, during template
parsing.

This needs an explicit design decision; lookup after parsing cannot recover the
lost tokens. Reasonable choices include retaining the original argument token
ranges in the deferred member node and parsing them after lookup, requiring an
explicit raw-argument delimiter at such call sites, or disallowing raw member
macros on unknown dependent bases with a targeted diagnostic. The current
behavior looks supported and then fails indirectly.

Relevant implementation:

- `clang/lib/Parse/ParseReflect.cpp`, `ParseMemberMacroInvocation`
- `clang/lib/Sema/SemaExpr.cpp`, `GetMemberMacroParameterShape`
- `clang/lib/Sema/TreeTransform.h`, `TransformCXXMacroInvocationExpr`

### 4. High: reconstructed expansion-site lookup still misses valid scopes

The common cases from the previous review are fixed, including same-statement
declarations, structured bindings, catch parameters, and shadowing. The
replacement is nevertheless a hand-maintained statement-tree reconstruction,
not the lexical scope captured at the invocation. It already misses declarations
whose `DeclStmt` is wrapped by a label or case label:

```cpp
__macro use(auto&& x) { return ^^{ local + \(x) }; }

template <class T>
constexpr int f(T t) {
  switch (0) {
  case 0:
    int local = 4;
    return use!(t);
  }
}

static_assert(f(3) == 7); // expansion reports local undeclared
```

`collectVisibleLocalDeclsBefore` only accumulates direct `DeclStmt` children of
a `CompoundStmt`; it skips the declaration under `CaseStmt` while walking past
it. An ordinary label produces the same failure. More statement-specific holes
are likely as the language grows.

The robust direction remains capturing the visible lexical scope structure at
the original dependent invocation and transforming its declarations, rather
than rediscovering C++ scope rules from the completed statement AST. If the
walker remains for the prototype, it should at least be factored around a
generic “walk preceding substatements and collect declarations without
inventing scopes” operation and receive coverage for labels, cases, control
statement initializers/conditions, range-for, lambdas, and local declarations.

Relevant implementation:

- `clang/lib/Sema/SemaTemplateInstantiateDecl.cpp`,
  `collectVisibleLocalDeclsBefore`
- `clang/lib/Sema/SemaExpr.cpp`, `BuildMacroInvocation`
- `clang/lib/Parse/ParseReflect.cpp`, `ParseExpressionMacroExpansion`

### 5. Medium: the `ranges::begin` ladder probes the wrong final expression

The current presentation is a good start, and the parse/inspect/interpolate
flow should stay. Two semantic details keep it from being a close translation
of `[range.access.begin]`:

1. The standard probes and returns `auto(t.begin())` / `auto(begin(t))`, i.e.
   decay-copy. The example probes and returns the call directly. A `begin()`
   returning `int*&` therefore makes `rng::begin(r)` return `int*&`, whereas
   `std::ranges::begin(r)` returns `int*`. This is also inconsistent with the
   `ExprConstantMeta.cpp` comment, which already describes probing
   `auto((e).begin())`.
2. The specification's `t` is a named forwarding-reference parameter and is
   therefore an lvalue. Macro parameter `r` is a reflection of the caller's
   expression, and `\(r)` preserves its original value category. On an rvalue,
   the example probes `begin() &&`; the CPO probes `begin() &` after binding its
   parameter.

The rung can be selected directly with an ordinary requires-expression; it
does not need `test_expression` at all:

```cpp
if constexpr (requires (R&& rr) {
                { auto(rr.begin()) } -> std::input_or_output_iterator;
              })
  return ^^{ ::rng::impl::member_begin(\(r)) };
```

`rr` is a named variable and therefore an lvalue even though its declared type
is `R&&`; this is the same formulation as the standard CPO. `auto(...)`
performs the required decay-copy before the compound requirement applies the
iterator constraint. (`R& rr` produces the same expression category, but
`R&&` mirrors the actual parameter declaration and reference collapsing.)

The emitted expression still needs to reproduce those named-parameter
semantics. As a stopgap, a SFINAE-friendly helper does that, just as the example
already does for ADL:

```cpp
template <class R>
constexpr auto member_begin(R&& r)
    noexcept(noexcept(auto(r.begin())))
    -> decltype(auto(r.begin())) {
  return auto(r.begin());
}
```

The helper's named `r` has the lvalue semantics required by the specification,
and `auto(...)` performs the decay-copy. The analogous ADL requirement can live
in `rng::impl`, alongside the poison pill, and the existing `adl_begin` helper
can remain the emitted expression. This leaves small helper calls in the
selected AST, but they are semantically honest and evaluate the caller's
argument exactly once.

A better language/library direction is an expression-reflection operation such
as `as_lvalue(r)`: produce a reflection behaving as if the reflected expression
had been bound to `auto&& tmp` and then named. The member expansion could then
be the direct and unsurprising

```cpp
return ^^{ auto(\(as_lvalue(r)).begin()) };
```

For an lvalue this is essentially identity; for an xvalue it changes the view
to an lvalue; for a prvalue it must materialize a temporary and return an lvalue
view of it. It must also retain the source-expression chain used by the
evaluate-once analysis. This is more than changing an `ExprValueKind`, but it
fits the existing `OpaqueValueExpr`/`MaterializeTemporaryExpr` machinery and
captures a generally useful operation that C++ cannot otherwise spell without
introducing a name.

This also changes what the example demonstrates: `ranges::begin` is not a
compelling use of `test_expression`, because member and ADL validity are fully
expressible from `R` in normal requires-expressions. `test_expression` remains
valuable when validity genuinely depends on the exact expansion-site
expression or on names that cannot be represented from the macro's template
arguments.

A faithful validation example should also cover `__can_borrow`, the class/enum
restriction on the ADL rung, incomplete array elements, `noexcept`, and the real
`input_or_output_iterator` concept. If the intent is only a reduced teaching
example, its comments should call that out instead of claiming the full
`[range.access.begin]` ladder.

Relevant implementation and test:

- `libcxx/test/std/experimental/reflection/expression-macros.pass.cpp`,
  `rng::begin_fn`
- `clang/lib/AST/ExprConstantMeta.cpp`, `test_expression`
- `libcxx/include/__ranges/access.h`, the existing CPO for comparison

## Minor notes

- `readConstexprDiagString` turns an arbitrary integer length into `uint64_t`
  with `getZExtValue()` without first rejecting negative values or checking a
  useful bound. The library facade always supplies `size_t`, but malformed
  direct builtin calls should fail immediately instead of attempting a walk
  with a wrapped length.
- `expression-macros.pass.cpp` now covers several largely independent systems
  in one 637-line executable. Moving the operator/member suite and the
  `ranges::begin` case into separate tests would make regressions and review
  substantially easier to localize.
- The scope collector has many repeated, oddly braced branches. Even before an
  architectural replacement, a formatting/refactoring pass would make its
  implied scope rules easier to audit.

## Previous-finding status

- Nested macro argument provenance: fixed for the reported cases by following
  `OpaqueValueExpr::getSourceExpr()` chains.
- Deferred lexical lookup: substantially improved, but still incomplete as
  finding 4 shows.
- PCH/modules: still open (finding 1).
- Empty raw argument versus defaults: fixed; `name!()` is now unambiguously an
  empty argument list.
- Duplicated operator mapping: improved by centralizing token-to-operator
  mapping in `Reflection.cpp`.

## Verification

- `./ninja.sh clang`: passed at `c295870eadce`.
- Relevant Clang language suites: 5,672 passed, 7 expected failures, 283
  unsupported; no unexpected failures.
- libc++ experimental reflection suite: 64/64 passed.
- The five directly touched Clang/libc++ tests passed together.
- Focused probes confirmed the typo-correction false positive, dependent raw
  member failure, case/label lookup failure, and missing decay-copy behavior.
- `git diff --check b952d5d00413..HEAD`: clean.
