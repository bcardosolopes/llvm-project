# Second review of expression macros

Implementation range reviewed: `2790caa5aac7..b952d5d00413`

Design reviewed: [`ideas/macros.md`](macros.md)

## Verdict

This version is substantially improved. The original five issues are fixed in
their covered forms, and the new `CXXMacroInvocationExpr` is the right
architecture. Four issues remain.

## Findings

### 1. Blocker: nested macros hide argument provenance

Each interpolation creates another `OpaqueValueExpr`, while the outer safety
analysis tracks the original opaque nodes by identity.

Consequently, nested macros can bypass the disjoint/evaluate-once rule:

```cpp
__macro relay(auto&& x) { return ^^{ \(x) }; }

__macro twice_nested(auto&& x) {
  return ^^{ relay!(\(x)) + relay!(\(x)) };
}
```

`twice_nested!(next())` compiles and calls `next()` twice.

More seriously:

```cpp
__macro lambda_nested(auto&& x) {
  return ^^{ [] { return relay!(\(x)); } };
}
```

This bypasses the lambda-body prohibition. Syntax checking succeeds, then code
generation crashes with:

```text
DeclRefExpr for Decl not entered in LocalDeclMap?
UNREACHABLE executed at CGExpr.cpp
```

Relevant implementation:

- `clang/lib/AST/ExprConstant.cpp`, expression-reflection interpolation
- `clang/lib/Sema/SemaExpr.cpp`, `CheckMacroArgumentEvaluation`

The use counter and containment check need to traverse unique
`OpaqueValueExpr::getSourceExpr()` chains or retain explicit root-argument
provenance.

### 2. High: reconstructed lookup scope is incomplete and flattened

`collectVisibleLocalDeclsBefore` gathers declarations into one synthetic
scope. This does not fully reproduce C++ lexical lookup.

Focused probes found:

- `int a = 4, b = macro!(x);` cannot find `a`;
- a preceding structured binding cannot find its `BindingDecl`s;
- a macro inside `catch (int e)` cannot find `e`;
- nested variables with the same name become ambiguous instead of selecting
  the innermost declaration.

The last case shows that merely collecting more declaration kinds is
insufficient: scope boundaries and hiding must also be retained.

Relevant implementation:

- `clang/lib/Sema/SemaTemplateInstantiateDecl.cpp`,
  `collectVisibleLocalDeclsBefore`
- `clang/lib/Parse/ParseReflect.cpp`, `ParseExpressionMacroExpansion`

A robust approach would capture the visible lexical scope structure at the
original invocation, then transform those declarations during instantiation.

### 3. High: deferred macros do not survive PCH or modules

The invocation node itself serializes, but macro bodies rely on token
sequences, which deliberately deserialize as empty.

A header containing:

```cpp
template <class T>
constexpr int f(T x) {
  int local = 4;
  return use!(x);
}
```

builds as a PCH, but instantiating `f<int>` from that PCH fails with `expected
expression` in the macro expansion. Proper `TokenSequenceData` serialization
is now load-bearing.

Relevant implementation:

- `clang/lib/Serialization/ASTWriterStmt.cpp`,
  `VisitCXXTokenSequenceExpr`

### 4. Medium: empty raw arguments conflict with defaults

The document says defaults bind exactly as an ordinary call, but the parser
always turns `raw!()` into one empty raw argument:

```cpp
__macro raw(token_sequence x = ^^{ 42 }) { return x; }
static_assert(raw!() == 42); // fails: expansion is empty
```

This needs a design choice: either defaults win, or the document must state
that `!()` explicitly supplies an empty sequence to a sole raw parameter.

Relevant implementation:

- `clang/lib/Parse/ParseReflect.cpp`, `ParseMacroInvocation`

## Minor notes

- "An argument cannot be interpolated into a lambda" should say "an
  expression argument"; `lambda!` deliberately interpolates a raw argument
  into a lambda body.
- `CXXMacroInvocationExpr` stores its parentheses but not the `!` location.
- `RecursiveASTVisitor.h` remains unused and misplaced among LLVM includes in
  `SemaExpr.cpp`.

## Verification

Clang rebuilt successfully at `b952d5d00413`. Both committed expression-macro
tests pass. The failures above came from additional focused probes.
