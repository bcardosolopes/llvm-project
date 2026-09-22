<!-- Copyright 2026 Jump Trading LLC. -->

# Review of declaration_of v1 at 6a36f57b88a7

Reviewed commit `6a36f57b88a7618873a91c3d35620d7316e4a0db`
(`First implementation of declaratoin_of()`), against its parent
`0c10f034d860`, on 2026-09-16.

The three existing tests pass. I found five reproducible implementation issues:
two P1 findings and three P2 findings. There is also a runtime-test
failure-reporting issue.

## Scope

This review uses the **[v1]** contract in [declaration_of.md](declaration_of.md).
It does not count the following as missing implementation:

- Namespace-scope injection, `virtual`/`override`/`= 0` syntax, and operator
  renames.
- Forwarding explicit-object members, C-style variadics, or nonterminal
  template packs; the implemented refusals are intentional.
- The documented `&&` receiver-spelling limitation and by-value forwarding
  limitations.
- Access/ADL questions and redeclaration/overload checking against existing
  destination members, explicitly deferred in v1.
- PCH or modules.

The findings below do not require any of those extensions.

## Reproducing the findings

Each example below is a separate translation unit with this common prelude:

```cpp
#include <meta>
consteval auto forward_member(std::meta::info m,
    std::meta::token_sequence receiver = ^^{ impl })
    -> std::meta::token_sequence {
  auto d = std::meta::declaration_of(m);
  return ^^{ \(d) { return \(std::meta::forwarding_call_for(d, receiver)); } };
}
```

From the repository root, compile the combined source on standard input:

```bash
build/bin/clang++ -std=c++2d -freflection -Wno-ignored-attributes \
  -nostdinc++ \
  -Ibuild/runtimes/runtimes-bins/libcxx/test-suite-install/include/x86_64-unknown-linux-gnu/c++/v1 \
  -Ibuild/runtimes/runtimes-bins/libcxx/test-suite-install/include/c++/v1 \
  -fsyntax-only -x c++ -
```

For finding 2, replace `-fsyntax-only` with
`-S -emit-llvm -o /dev/null`; the failure occurs during code generation.

## 1. [P1] Uninstantiated defaults lose the enclosing class's template arguments

Location: [SemaReflect.cpp](../clang/lib/Sema/SemaReflect.cpp#L1883),
`ActOnInjectedFunctionDeclSpec`'s default-argument substitution.

```cpp
template<class T> struct U {
  template<class V>
  constexpr int f(int n = sizeof(T) + sizeof(V)) const { return n; }
};
struct W {
  U<long> impl;
  consteval { queue_injection(forward_member(^^U<long>::f)); }
};
static_assert(W{}.f<int>() == sizeof(long) + sizeof(int));
```

**Observed:** the assertion fails with `8 == 12` on this x86-64 build. The
clone's default behaves as `sizeof(int) + sizeof(int)`, rather than retaining
`T = long` from `U<long>`.

The code obtains the source's uninstantiated default expression and calls
`SubstExpr(OldDefault, Args)`. However, `Args` contains only the new member
template parameters. That expression can still refer to the enclosing class
template's parameters; those must be substituted using the source
specialization's arguments, not rebound to the cloned member's parameters.

This affects the supported combination of member templates, concrete enclosing
specializations, and function defaults. It is a wrong-result bug even when the
injected body and forwarding call otherwise work.

**Suggested fix:** distinguish an already-instantiated default from an
uninstantiated pattern, and build the complete substitution environment for
the latter: source enclosing-specialization arguments plus the rewrite mapping
for the member template. Preserve lazy instantiation of dependent defaults.

**Regression coverage:** use different-sized outer and inner types, and include
a default depending only on the outer type. Keep a case where the default is
not used, so fixing this does not eagerly instantiate it.

## 2. [P1] Declaration descriptions with different sources collide in mangling

Location: [ItaniumMangle.cpp](../clang/lib/AST/ItaniumMangle.cpp#L5074),
the `ReflectionKind::DeclarationSpec` case.

```cpp
struct U { int a(); int b(); };
constexpr auto a = std::meta::declaration_of(^^U::a);
constexpr auto b = std::meta::declaration_of(^^U::b);
static_assert(a != b);
template<std::meta::info D> int tag() {
  if constexpr (D == a) return 1;
  else return 2;
}
int first() { return tag<a>(); }
int second() { return tag<b>(); }
```

**Observed:** LLVM IR generation fails with:

```text
definition with same mangled name '_Z3tagIMsfd_ZTSFivET$T$P$p$EEiv' as another definition
```

The mangling encodes the source's **function type**, replacement name if any,
and naming prefixes. It never encodes which member is being cloned.
Consequently `U::a` and `U::b` produce the same symbol, even though their
descriptions compare unequal and instantiate distinct specializations.

This prevents normal use of descriptions as `info` template arguments.
Across translation units, a collision could instead merge distinct
instantiations at link time; the single-translation-unit failure above is
directly reproduced.

**Suggested fix:** encode the source member/template identity, including its
enclosing specialization, followed by the naming policy. Keep the identity
used by equality, profiling, and mangling consistent.

**Regression coverage:** distinct same-signature members in one class, and
same-named/same-signature members in different classes. Exercise code generation,
not just constant evaluation or `-fsyntax-only`.

## 3. [P2] Cloned array and function parameters are not adjusted to pointers

Location: [SemaReflect.cpp](../clang/lib/Sema/SemaReflect.cpp#L1877),
construction of `NewParm` and `NewParamTys`.

```cpp
struct U {
  constexpr int f(int values[2]) const { return values ? values[0] : 7; }
};
struct W {
  U impl;
  consteval { queue_injection(forward_member(^^U::f)); }
};
static_assert(U{}.f(nullptr) == 7);
static_assert(W{}.f(nullptr) == 7);
```

**Observed:** the source call passes, but the cloned call is rejected with
`array initializer must be an initializer list`.

The original parameter is semantically an `int*`, while its type source
information retains the written array type. The clone uses
`NewTSI->getType()` directly both for the new parameter and the function
prototype, bypassing ordinary function-parameter adjustment.

The same issue is independently reproduced with:

```cpp
constexpr int f(int callback()) const {
  return callback ? callback() : 7;
}
```

The source accepts `nullptr`, but the clone rejects it as an argument to
`int ()` instead of accepting an `int (*)()`.

**Suggested fix:** apply normal array/function parameter adjustment after
substitution, and use the adjusted types for the parameter declarations and
prototype. Preserve written type information separately. Deduction-guide
synthesis already explicitly handles this adjustment.

**Regression coverage:** concrete and dependent array parameters, function
parameters, and references to arrays/functions (which must remain references).

## 4. [P2] Forwarding concatenates the receiver without preserving its grouping

Location: [ExprConstantMeta.cpp](../clang/lib/AST/ExprConstantMeta.cpp#L3607),
the receiver emission in `forwarding_call_for`.

```cpp
struct U { constexpr int f() const { return 7; } };
struct W {
  U* ptr;
  consteval { queue_injection(forward_member(^^U::f, ^^{ *ptr })); }
};
```

**Observed:** the generated call is effectively `*ptr.f()`. Clang diagnoses
member access on a pointer and indirection of the returned `int`. The intended
call is `(*ptr).f()`.

Likewise, `^^{ choose ? left : right }` as the receiver produces
`choose ? left : right.f()`, attaching the call to only one branch.
This is independent of the documented `&&`/`decltype` limitation: the
example clones an ordinary `const` member with no ref-qualifier.

**Suggested fix:** group the receiver in the emitted member-access expression.
Keep any deliberate unparenthesized `decltype` operand separate from that
expression; its special type rule should not dictate the grouping of the
actual call.

**Regression coverage:** dereference, conditional, and comma-expression
receivers, checking that the receiver is evaluated exactly once.

## 5. [P2] Const clones can silently dispatch to the non-const overload

Location: [ExprConstantMeta.cpp](../clang/lib/AST/ExprConstantMeta.cpp#L3595),
receiver adjustment in `forwarding_call_for`.

```cpp
struct U {
  constexpr int f() { return 1; }
  constexpr int f() const { return 2; }
};
consteval auto const_f() -> std::meta::info {
  for (auto m : members_of(^^U, std::meta::access_context::current()))
    if (is_function(m) && !is_special_member_function(m) &&
        is_const(type_of(m)))
      return m;
  return {};
}
struct W {
  mutable U impl;
  consteval { queue_injection(forward_member(const_f())); }
};
static_assert(W{}.f() == 2);
```

**Observed:** the assertion fails with `1 == 2`. Only the const source method
was cloned, but forwarding selects `U::f()`, not `U::f() const`.

The recipe handles an `&&` ref-qualifier but does not apply the described
method's cv-qualifiers to the receiver. An ordinary non-mutable object member
happens to work because the wrapper's constness propagates to it. A mutable
member, reference member, or pointer to mutable storage does not have that
property.

There is only one injected method in `W`; this is not the deferred issue of
checking redeclarations or overloads already present in the destination.
The declaration description identifies the const interface, and forwarding
must preserve that receiver requirement.

**Suggested fix:** form a receiver expression with the description's cv/ref
requirements, retaining any additional cv-qualification already present.
Continue to use structural member lookup on that adjusted receiver.

**Regression coverage:** mutable and reference receivers, plus parenthesized
pointer dereferences, with distinguishable const/non-const overload results.
Also cover volatile qualification if retained by the supported clone contract.

## Additional test issue: main reports runtime failures as success

Location: [declaration-of.pass.cpp](../libcxx/test/std/experimental/reflection/declaration-of.pass.cpp#L33),
`CHECK` and its uses in `main`.

`CHECK` returns `0` on failure. That is useful inside `use_all` and
`use_vec`, whose expected result is `1`, but `main` uses the same macro.
If either runtime check fails, the executable exits successfully with status
zero. Its success path also returns zero.

The static assertions still validate constant evaluation. The runtime checks
do not currently detect a discrepancy caused by code generation.

Use assertions in `main`, or return nonzero when either helper returns a
failure result.

## Validation performed

The existing build passed all three focused tests:

```bash
# From build/
./bin/llvm-lit -sv \
  ./runtimes/runtimes-bins/libcxx/test/std/experimental/reflection/declaration-of.pass.cpp \
  ./runtimes/runtimes-bins/libcxx/test/std/experimental/reflection/declaration-of.verify.cpp \
  ./runtimes/runtimes-bins/libcxx/test/std/experimental/reflection/logging-vector.pass.cpp
```

Result: **3 passed, 0 failed**.

All five numbered reproductions above were compiled separately with the
existing compiler. Array and function parameter adjustment were checked
independently. Additional positive probes passed for a simple dependent
default left unused, mixed type/value template parameters, a constrained
`auto` NTTP, a simple template-template parameter, and a requires-expression
referring to a function parameter.

No compiler or test implementation was modified, and the full language/library
suite was not run for this review.
