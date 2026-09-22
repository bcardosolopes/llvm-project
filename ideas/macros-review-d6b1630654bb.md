# Review of `inject_members`

Implementation reviewed: `c077158019d6..d6b1630654bb`

## Verdict

The two-phase model is implemented in the right place and with the right
surface: `inject_members(info) -> token_sequence` runs after all written
members but before `ActOnFields` and `CheckCompletedCXXClass`, while the
existing `on_complete(info)` remains post-completion. Returning member tokens
instead of implicitly queueing them makes the destination unambiguous, and
resetting each callback to the class's default access avoids dependence on the
last access-specifier in the written body.

The motivating facility works: focused probes successfully injected a
defaulted hidden-friend `operator==` into both an ordinary class and a primary
class-template pattern. Two correctness issues remain, one of which can hang
Clang on malformed generated tokens.

## Findings

### 1. High: a stray `}` in returned tokens makes the parser stop making progress

The injected-member loop continues until `tok::eof`, but
`ParseCXXClassMemberDeclarationWithPragmas` treats `}` as the end of a class and
does not consume it. The loop repeatedly diagnoses the same token:

```cpp
struct bad {
  consteval auto inject_members(std::meta::info) const
      -> std::meta::token_sequence {
    return std::meta::tokenize("}");
  }
};

struct [[=bad{}]] X {};
```

With the normal error limit, Clang emits “too many errors” but remains in the
loop; a five-second timeout terminated the focused probe. With an unlimited
error count this produces unbounded diagnostics.

The reinjection loop needs an explicit progress guarantee. A stray `}` should
be diagnosed and consumed (not treated as the real class terminator), and a
general no-progress guard should consume or skip the offending token after any
member-parse failure. This logic should be shared with the existing
record-context branch of `ProcessTokenInjections`, which uses the same
`while (Tok.isNot(tok::eof))` pattern.

Relevant implementation:

- `clang/lib/Parse/ParseDeclCXX.cpp`, the new callback token loop in
  `ParseCXXMemberSpecification`
- `clang/lib/Parse/ParseDeclCXX.cpp`, record handling in
  `ProcessTokenInjections`

### 2. High: dependent annotation values silently lose `inject_members`

A dependent annotation is skipped at primary-template definition time, but
there is no corresponding pre-completion callback during specialization
instantiation:

```cpp
template <class T>
struct add_value {
  consteval auto inject_members(std::meta::info) const
      -> std::meta::token_sequence {
    return ^^{ T value; };
  }
};

template <class T>
struct [[=add_value<T>{}]] X {};

static_assert(requires(X<int> x) { x.value; }); // fails: no member value
```

This differs from `on_complete`, whose dependent annotation is evaluated for
each instantiated specialization. The current code comments that evaluation
cannot happen at definition time, returns success, and never revisits it.

Either support per-specialization member injection before `ActOnFields` and
`CheckCompletedCXXClass`, or diagnose the unsupported dependent form when the
specialization reveals an `inject_members` callback. Silently treating it like
an annotation without the callback is the worst outcome. The former is more
consistent with dependent consteval-block injection already supported during
class instantiation, but newly injected fields must also be incorporated into
the instantiation's field-completion path.

Relevant implementation:

- `clang/lib/Sema/SemaReflect.cpp`,
  `EvaluateInjectMembersAnnotation`
- `clang/lib/Sema/SemaTemplateInstantiate.cpp`, class completion before
  `ActOnFields`

### 3. Medium: the tests do not exercise the promised two-phase contract

`N5::validated` queues a namespace-scope `static_assert` from inside
`inject_members` and calls that the “post-completion validation idiom.” That is
the workaround the new split was intended to replace. It also tests only that
the queued tokens are parsed later, not that `on_complete` receives a complete
type after the returned members have participated in completion.

Give that annotation both callbacks instead:

```cpp
consteval token_sequence inject_members(info r) const {
  if (is_complete_type(r))
    throw "inject_members received a complete type";
  return ^^{ int second; };
}

consteval void on_complete(info r) const {
  if (!is_complete_type(r))
    throw "on_complete received an incomplete type";
  queue_injection(^^{
    static_assert(sizeof(\(r)) == 2 * sizeof(int));
  });
}
```

A focused version of this passed. It should be a committed regression test,
along with the motivating defaulted `operator==` for an ordinary class and a
class template. Those examples validate the lifecycle contract more directly
than another generic injected method.

Relevant test:

- `clang/test/Reflection/inject-members.cpp`

## Maintainability

`EvaluateInjectMembersAnnotation` repeats most of
`HandleAnnotationOnComplete`: annotation filtering, callback lookup, member
reference construction, call construction, constant evaluation, and diagnostic
replay. The parser-side token reinjection similarly duplicates the
record-context path in `ProcessTokenInjections`. Small shared helpers for
“build/evaluate an annotation callback” and “parse a token sequence as class
members” would reduce lifecycle drift; the non-progress bug above is already an
example of why that matters.

The second `while (Tok.isNot(tok::eof))` immediately following the injected
member loop is unreachable, because the first loop has the identical condition.
The injected loop should also mirror the normal member loop's
`MaybeDestroyTemplateIds()` cleanup.

## Minor notes

- Diagnostics from malformed returned tokens need a context note naming the
  annotation callback and target class. Token source locations identify the
  callback body, but multiple uses of one annotation are otherwise difficult
  to distinguish.
- The ordering rule is explicit and tested: callbacks run once in annotation
  order, later callbacks see earlier injected members, and access resets for
  each callback. That is a good, predictable choice; it should be stated in the
  design document as part of the public contract.

## Verification

- `./ninja.sh clang`: passed at `d6b1630654bb`.
- Relevant Clang language suites: 5,673 passed, 7 expected failures, 283
  unsupported; no unexpected failures.
- libc++ experimental reflection suite: 66/66 passed.
- The two directly touched Clang/libc++ tests passed together.
- Focused ordinary-class and class-template defaulted-`operator==` probes
  passed.
- A focused annotation implementing both `inject_members` and `on_complete`
  observed the expected incomplete/complete phases and passed.
- The dependent-annotation probe failed by omitting the member, confirming
  finding 2.
- The malformed-token probe timed out after five seconds while repeatedly
  diagnosing the same `}` token, confirming finding 1.
- `git diff --check c077158019d6..d6b1630654bb`: clean.
