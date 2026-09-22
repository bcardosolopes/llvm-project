# Review of expression macros

Implementation reviewed: `37c09ed59838`

Design reviewed: [`ideas/macros.md`](macros.md)

## Verdict

The design is compelling: `name!(...)`, expression-vs-raw parameters,
ordinary overload resolution, and bounded expression reflection form a
coherent feature. The implementation is a strong prototype, but it does not
yet implement several central guarantees. There are two architectural blockers
and three concrete correctness bugs.

## Findings

### 1. Blocker: value-dependent invocations crash Clang

The design requires both type- and value-dependent calls to defer expansion.
The implementation expands value-dependent, non-type-dependent arguments
immediately, embeds them in an `OpaqueValueExpr`, and never transforms its
source.

This reproducer crashes in `EvaluateDependentExpr`:

```cpp
__macro id(auto&& x) { return ^^{ \(x) }; }

template <int N>
constexpr int f() { return id!(N); }

static_assert(f<42>() == 42);
```

Relevant implementation:

- `clang/lib/Sema/SemaExpr.cpp`, `BuildExpressionMacroExpansion`
- `clang/lib/Sema/TreeTransform.h`, `TransformOpaqueValueExpr`

### 2. Blocker: dependent expansions lose the caller's lexical scope

Expansion reparses tokens through the currently active parser scope, which is
not the original instantiated function's local scope.

```cpp
__macro use(auto&& x) { return ^^{ local + \(x) }; }

template <class T>
constexpr int f(T x) {
  int local = 4;
  return use!(x);
}

static_assert(f(3) == 7);
```

This reports `local` undeclared, contradicting the expansion-site lookup rule.

Relevant implementation:

- `clang/lib/Parse/ParseReflect.cpp`, `ParseExpressionMacroExpansion`
- `clang/lib/Sema/SemaExpr.cpp`, `BuildExpressionMacroExpansion`

### 3. High: generated code can invoke a macro without `!`

`AllowMacroCallee` remains true throughout `ActOnCallExpr`, including while
the returned tokens are reparsed. Consequently this incorrectly compiles:

```cpp
__macro id(int x) { return ^^{ \(x) }; }
__macro outer(int x) { return ^^{ id(\(x)) }; } // missing !
static_assert(outer!(7) == 7);
```

That defeats one of the design's most important readability guarantees.

Relevant implementation:

- `clang/lib/Sema/SemaExpr.cpp`, `ActOnMacroInvocation`
- `clang/lib/Sema/SemaExpr.cpp`, `BuildResolvedCallExpr`

### 4. High: the evaluate-once analysis mishandles unevaluated operands

The custom AST visitor manually skips only several unevaluated contexts. It
misses `typeid` of a non-polymorphic expression:

```cpp
__macro m(int x) { return ^^{ (typeid(\(x)), \(x)) }; }
constexpr int n = m!(9);
```

This is incorrectly rejected as evaluating `x` twice. This analysis should
reuse or extend Clang's established potentially-evaluated-expression machinery
rather than maintain a partial list.

Relevant implementation:

- `clang/lib/Sema/SemaExpr.cpp`, `MacroArgumentUseCounter`

### 5. Medium: raw delimiters are counted but not matched

`ParseMacroRawArgument` uses one depth counter for all delimiter kinds. Thus
this compiles despite the design requiring balanced delimiters:

```cpp
__macro ignore(token_sequence) { return ^^{ 0 }; }
static_assert(ignore!([}) == 0);
```

Use a delimiter-kind stack instead.

Relevant implementation:

- `clang/lib/Parse/ParseReflect.cpp`, `ParseMacroRawArgument`

## Recommended architecture

Introduce a first-class `ExpressionMacroInvocationExpr` containing:

- the explicit `!` syntax;
- selected or deferred overload information;
- expression and raw arguments;
- the invocation source range;
- optionally, the completed expansion.

That node can defer whenever any argument is type- or value-dependent,
transform its arguments normally during instantiation, and expand with the
correct semantic context. It also removes the global `AllowMacroCallee` state
and preserves useful AST and tooling provenance. Plain `OpaqueValueExpr`s
remain a good representation inside the completed expansion.

The design should also explicitly settle:

- syntax for explicit template arguments;
- permitted member, variadic, and default-argument declarations;
- whether raw tokens are captured before or after preprocessor expansion;
- interpolation of multi-token operators such as `()` and `[]`.

## Maintainability

The operator mapping is duplicated between `ExprConstant.cpp` and
`ExprConstantMeta.cpp`; it should have one source of truth. The unrelated
`formatter.template_string.verify.cpp` test should also be split from this
commit.

## Verification

The committed Clang expression-macro test and libc++ runtime test both pass.
The failures above came from additional focused probes.
