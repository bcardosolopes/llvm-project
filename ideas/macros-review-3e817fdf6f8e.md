<!-- Copyright 2026 Jump Trading LLC. -->

# Token sequence / macro review at 3e817fdf6f8e

Reviewed on 2026-09-21 against commit
`3e817fdf6f8e28b7fb3c8ecd54b72474b10eadb8`.

**Seven reproducible findings: five P1 and two P2.** They include compiler
crashes on valid input, a remaining declaration-description mangling collision,
and a way to bypass the macro argument evaluation check. The existing
reflection suites pass.

## Scope and validation

The main diff review covers the 19 non-P2806/non-clang-tidy commits in
`c295870eadce..3e817fdf6f8e`, with supporting inspection of the existing macro
and token evaluator. In particular:

| Area | Commits |
| --- | --- |
| Macro probing and declaration invocation | `99e8ee5bbac2`, `c077158019d6` |
| Member injection and its follow-up fixes/examples | `cd3837f18b32`, `1e1dd6b4e439`, `421c715a9abd`, `6f0deea78ad1` |
| Expansion context and interpolated scope names | `9f8762be09d0` |
| Declaration cloning, logging, and mocks | `6a36f57b88a7`, `e0d3a8cdd28d`, `541a093f3484`, `5690a8035c79` |
| Token iteration and template-head fragments | `124270afca4e`, `5d5a08e51c99` |
| Identifier tokens, argument lists, and macro delimiters | `f933bf4310ac`, `bc243ed668c5`, `0b24ec85fe7c`, `3e817fdf6f8e` |
| Design-only changes | `8ec6bdd0a1e2`, `0c10f034d860` |

P2806 and clang-tidy changes were excluded from code review. PCH/modules and
documented v1 refusals are not findings. Earlier review reports were consulted
to distinguish follow-up gaps from already-fixed examples. Findings 5 and 7
identify surviving issues in supporting code; they are not claimed to have
originated in the newest commits.

Validation on the rebuilt compiler:

- `cd build && ./ninja.sh clang`: passed.
- `./ninja.sh cxx runtimes-test-depends`: passed.
- Clang Reflection tests plus `SemaCXX/cxx29-token-seq.cpp`: **24 passed**.
- libc++ experimental reflection suite: **76 passed**. This includes the
  pre-existing untracked `identifier-of-token.verify.cpp`.
- Full requested language suite, with the prescribed filter: **5,685 passed,
  7 expected failures, 283 unsupported**; no unexpected failures. Output is
  recorded in
  [language-suite.log](../build/review-3e817fdf6f8e/language-suite.log).
- Focused reproducers and handwritten/control variants described below.

No compiler or library sources were changed. Probe sources and logs are in
[build/review-3e817fdf6f8e](../build/review-3e817fdf6f8e/).

## Reproduction commands

Each code block below is a separate translation unit. From the repository root:

```bash
build/bin/clang++ -std=c++2d -freflection -Wno-ignored-attributes \
  -nostdinc++ \
  -Ibuild/runtimes/runtimes-bins/libcxx/test-suite-install/include/x86_64-unknown-linux-gnu/c++/v1 \
  -Ibuild/runtimes/runtimes-bins/libcxx/test-suite-install/include/c++/v1 \
  -fsyntax-only example.cpp
```

For finding 4, replace `-fsyntax-only` with
`-S -emit-llvm -o /dev/null`; it requires code generation.

The saved [compile.sh](../build/review-3e817fdf6f8e/compile.sh) supplies these
common options, disables crash artifacts, and applies a 20-second timeout.
Pass it `-fsyntax-only` or the code-generation options explicitly.

## 1. [P1] Indexed token sequences crash in conditional expressions

Locations:
[ExprClassification.cpp:287](../clang/lib/AST/ExprClassification.cpp#L287),
[SemaExpr.cpp:5610](../clang/lib/Sema/SemaExpr.cpp#L5610).

Related commit: `124270afca4e`. The classification fix in
`3e817fdf6f8e` handles macro parameters but leaves this analogous case.

```cpp
#include <meta>

constexpr auto ts = ^^{ a b };
constexpr auto selected = true ? ts[0] : ts[1];
static_assert(selected == ^^{ a });
```

**Observed:** Clang aborts in `Expr::ClassifyImpl` with
`Assertion 'isLValue()' failed`.

Sema constructs token-sequence subscripts as prvalues, as intended. However,
`ClassifyInternal` treats every non-array/non-vector subscript as an lvalue.
The conditional expression triggers the consistency assertion between those
two classifications.

This also makes malformed input such as `ts[0] = ts[1]` crash instead of
diagnosing assignment to a temporary. Ordinary indexing and passing its result
to a function work, explaining why the current iteration tests miss it.

**Suggested fix:** classify token-sequence subscripts as prvalues in the
array-subscript classification path.

**Regression coverage:** a valid conditional over indexed tokens, plus an
invalid assignment that must issue an ordinary diagnostic.

Saved reproducer:
[index-conditional.cpp](../build/review-3e817fdf6f8e/index-conditional.cpp).

## 2. [P1] New cloning paths substitute without an instantiation context

Locations:
[SemaReflect.cpp:1823](../clang/lib/Sema/SemaReflect.cpp#L1823),
[SemaReflect.cpp:1873](../clang/lib/Sema/SemaReflect.cpp#L1873),
[SemaReflect.cpp:1957](../clang/lib/Sema/SemaReflect.cpp#L1957).

Related commits: `5d5a08e51c99` and `e0d3a8cdd28d`.

The simplest template-head reproducer uses the accessor's default options:

```cpp
#include <meta>

template<class T = int> struct source {};

consteval {
  auto d = std::meta::declaration_of(^^source);
  auto h = std::meta::template_parameter_list_for(d);
  queue_injection(^^{
    template<\(h)> struct target {};
  });
}

target<> value;
```

**Observed:** Clang aborts in `TemplateInstantiator`:

```text
Cannot perform an instantiation without some context on the instantiation stack
```

The same assertion occurs with a dependent parameter type,
`template<class T, T N>`, even with `{.defaults = false}`.
It also occurs when cloning a non-template member with an uninstantiated
default from a concrete class-template specialization:

```cpp
#include <meta>

template<class T> struct U {
  constexpr int f(int x = sizeof(T)) { return x; }
};

struct W {
  consteval {
    auto d = std::meta::declaration_of(^^U<int>::f);
    queue_injection(^^{ \(d) { return p0; } });
  }
};

static_assert(W{}.f() == sizeof(int));
```

`ActOnInjectedTemplateParameters` establishes a `LocalInstantiationScope`
but no code-synthesis context before calling substitution routines.
`ActOnInjectedFunctionDeclSpec` establishes its synthesis context only for
a member function template, although the new default-argument path also
substitutes for non-template members.

These are two entry points with the same missing precondition. An unrelated
outer instantiation can mask the problem, so testing only generation within
template instantiation is insufficient.

**Suggested fix:** establish an appropriate synthesis context in every cloning
entry point that can substitute. Keep local declaration mappings and synthesis
contexts conceptually separate; one does not provide the other.

**Regression coverage:** namespace-scope template fragments with defaults and
dependent NTTP types, and non-template members with class-dependent defaults.

Saved reproducers:
[head-default.cpp](../build/review-3e817fdf6f8e/head-default.cpp),
[head-dependent.cpp](../build/review-3e817fdf6f8e/head-dependent.cpp),
[default-simple.cpp](../build/review-3e817fdf6f8e/default-simple.cpp).
The handwritten function-default control passes.

## 3. [P1] Nested template-template parameter heads retain old parameter indices

Location:
[SemaReflect.cpp:1776](../clang/lib/Sema/SemaReflect.cpp#L1776).

Related commit: `5d5a08e51c99`, which exposes the existing cloning helper
to parameter lists with prepended parameters.

```cpp
#include <meta>

template<class T, template<T> class C> struct source {};
template<int> struct int_arg {};
template<int, class> struct trait;

consteval {
  auto d = std::meta::declaration_of(^^source);
  auto h = std::meta::template_parameter_list_for(d, {.defaults = false});
  auto a = std::meta::template_argument_list_for(d);

  queue_injection(^^{
    template<int I, \(h)>
    struct trait<I, source<\(a)>> {};
  });
}

trait<0, source<int, int_arg>> value;
```

**Observed:** Clang aborts while matching the partial specialization with:

```text
Template argument kind mismatch
```

The intended head is:

```cpp
template<int I, class T0, template<T0> class T1>
```

The helper renames/reindexes the outer template-template parameter but reuses
`TTPD->getTemplateParameters()` unchanged. Its nested `template<T>` still
refers to the original outer type parameter at index zero. After prepending
`I`, index zero holds an integer argument, causing the type-kind assertion.

The comment that nested parameter names belong to a separate scope does not
justify sharing that AST: their types, defaults, and constraints can refer to
outer parameters.

**Suggested fix:** recursively clone nested parameter lists, including depth
adjustment and substitution of references to the enclosing parameters.

**Regression coverage:** this partial specialization, a nested default referring
to an outer parameter, and injection at another template depth.

Saved reproducer:
[nested-ttp.cpp](../build/review-3e817fdf6f8e/nested-ttp.cpp).
The [handwritten equivalent](../build/review-3e817fdf6f8e/nested-ttp-control.cpp)
passes.

## 4. [P1] Different template overloads still produce the same description mangling

Location:
[ItaniumMangle.cpp:5091](../clang/lib/AST/ItaniumMangle.cpp#L5091).

Related commit: `e0d3a8cdd28d`. This is a gap in the earlier mangling fix,
rather than a repeat of the previously reported different-member-name case.

```cpp
#include <meta>

struct U {
  template<class> int f();
  template<int> int f();
};

constexpr auto mem =
  std::meta::members_of(^^U, std::meta::access_context::unchecked());
constexpr auto a = std::meta::declaration_of(mem[0]);
constexpr auto b = std::meta::declaration_of(mem[1]);
static_assert(a != b);

template<std::meta::info D>
int tag() {
  if constexpr (D == a) return 1;
  else return 2;
}

int one() { return tag<a>(); }
int two() { return tag<b>(); }
```

**Observed during LLVM IR generation:**

```text
definition with same mangled name
'_Z3tagIMsfd_ZTS1UM$f$_ZTSFivEH1T$T$P$p$EEiv'
as another definition
```

The encoding now includes the enclosing class, member name, function type,
and template parameter count. Those are identical for these two overloads.
Template parameter kinds are absent; constraints can also distinguish
declarations and need consideration in the encoding.

Description equality distinguishes these sources, but emitted symbols do not.
The reproduced outcome is a same-TU code-generation error; cross-TU merging of
different specializations is also a risk.

**Suggested fix:** use a complete encoding of the source declaration/template
identity, consistent with equality and profiling, instead of reconstructing
identity from the function type and head arity.

**Regression coverage:** code generation for type-vs-value parameter overloads,
and constrained overloads with otherwise identical heads/signatures.

Saved reproducer:
[mangle-template-kinds.cpp](../build/review-3e817fdf6f8e/mangle-template-kinds.cpp).

## 5. [P1] Reusing an interpolated token bypasses the evaluate-once check

Location:
[SemaExpr.cpp:7652](../clang/lib/Sema/SemaExpr.cpp#L7652),
with token copying in
[ExprConstant.cpp:22632](../clang/lib/AST/ExprConstant.cpp#L22632).

This survives in the existing use counter from `7a10ea9d4576`; it is
particularly relevant to the newly iterable/composable token sequences.

```cpp
#include <meta>

__macro twice(auto&& x) {
  auto once = ^^{ \(x) };
  return once + ^^{ + } + once;
}

constexpr int f() {
  int n = 0;
  int sum = twice!(++n);
  return n;
}

static_assert(f() == 2); // Accepted: the argument really executes twice.
```

**Observed:** the assertion passes. LLVM IR generation also succeeds and emits
two increments. Writing two separate interpolations,
`return ^^{ \(x) + \(x) };`, correctly diagnoses repeated evaluation.

Concatenating or reusing a sequence copies the annotation token and its
`OpaqueValueExpr*`. The counter's global `Visited` set treats multiple
occurrences of that pointer as one evaluation. That treatment is intended for
a shared, bound opaque value in constructs such as GNU `?:`, but macro
interpolation creates unique opaque values emitted at each use.

**Suggested fix:** distinguish unique, evaluated-in-place opaque values from
bound opaque values. Count actual evaluated occurrences of the former, while
retaining the required single-binding treatment of the latter. Alternatively,
give each reinjected expression occurrence fresh nodes with preserved
argument provenance.

**Regression coverage:** duplicating a saved one-token fragment, both through
concatenation and through repeated interpolation; retain the GNU conditional
control so fixing this does not restore its previous false positive.

Saved reproducer:
[macro-duplicate-token.cpp](../build/review-3e817fdf6f8e/macro-duplicate-token.cpp).
The [two-interpolation control](../build/review-3e817fdf6f8e/macro-twice-control.cpp)
is rejected as expected.

## 6. [P2] Cloning eagerly instantiates an unused function default

Location:
[SemaReflect.cpp:1948](../clang/lib/Sema/SemaReflect.cpp#L1948).

Related commit: `e0d3a8cdd28d`. This remains after supplying a synthesis
context; it is separate from finding 2.

```cpp
#include <meta>

template<class T> struct U {
  template<class V>
  constexpr int f(int x = T::missing) { return x; }
};

static_assert(U<int>{}.f<void>(7) == 7); // Valid: the default is unused.

struct W {
  consteval {
    auto d = std::meta::declaration_of(^^U<int>::f);
    queue_injection(^^{ \(d) { return p0; } });
  }
};

static_assert(W{}.f<void>(7) == 7);
```

**Observed:** injection rejects `T::missing` after substituting `T = int`,
then reports that it could not declare the clone.

The original function is callable when its argument is supplied. The clone
must not require an unused default to be valid merely to reproduce the
declaration. The fix for binding outer template parameters invokes
`SubstExpr` eagerly, so it solves the wrong-binding case by introducing an
earlier instantiation point for defaults dependent only on the source class.

The existing laziness example exercises dependence on the cloned function's
own parameters, which remains dependent after rewriting and therefore does
not catch this case.

**Suggested fix:** retain the original uninstantiated default with the required
source-class substitution environment and clone-parameter mapping, and perform
its semantic instantiation when the default is used.

**Regression coverage:** this explicit-argument call must compile; an omitted
argument must diagnose the invalid default. Keep the outer/inner
different-size default tests from the previous fix.

Saved reproducer:
[default-lazy-template.cpp](../build/review-3e817fdf6f8e/default-lazy-template.cpp).

## 7. [P2] Interpolation still performs a premature allocation-leak check

Location:
[ExprConstant.cpp:22629](../clang/lib/AST/ExprConstant.cpp#L22629);
the safe operand helper added by `124270afca4e` is at
[ExprConstant.cpp:23040](../clang/lib/AST/ExprConstant.cpp#L23040).

This is a surviving instance of the evaluator problem that the token-iteration
commit fixes for concatenation.

```cpp
#include <meta>
#include <vector>

consteval auto f() {
  std::vector<int> v{1, 2};
  auto ts = ^^{ x };
  return ^^{ \(ts) };
}

static_assert(f() == ^^{ x });
```

**Observed:** constant evaluation fails with
`allocation performed here was not deallocated`, pointing to the vector
allocation.

The vector is local and would be destroyed when `f` returns. Interpolation
calls the top-level `EvaluateAsRValue` entry point midway through the
enclosing evaluation, which performs a leak check while that vector is still
alive. Replacing the return expression with `ts + ^^{ }` passes, using the
new safe concatenation path.

This affects ordinary generators that collect information in a vector before
interpolating a token fragment. The live allocation does not have to belong to
the interpolated operand.

**Suggested fix:** use subexpression evaluation for token interpolation too,
and audit the adjacent reflection interpolation call for the same mistake.
Reuse/generalize the operand helper rather than adding another independent
evaluation path.

**Regression coverage:** interpolation while an unrelated allocation is live,
checking successful destruction at the end of the enclosing evaluation.

Saved reproducer:
[interpolation-allocation.cpp](../build/review-3e817fdf6f8e/interpolation-allocation.cpp).
The [concatenation control](../build/review-3e817fdf6f8e/interpolation-allocation-control.cpp)
passes.

## Review notes

The argument-list extraction now shares its emission helper with
`forwarding_call_for`, and the delimiter update shares opener/closer handling.
The committed positive and negative tests for these changes passed; no separate
finding was established for their advertised supported cases.

The significant maintainability issue is the mismatch between the cloning
helper's advertised semantic remapping and its treatment of nested template
heads in finding 3. Centralizing recursive head cloning and its synthesis
context would address both that issue and part of finding 2. Likewise, the
existing subexpression evaluator should be shared by all interpolation and
concatenation paths covered by finding 7.
