# `declaration_of`: cloning declarations wholesale

Status: design sketch, not implemented. Grew out of the `LoggingVector`
exercise (2026-09-15); see `libcxx/test/std/experimental/reflection/
logging-vector.pass.cpp` for the current state of the art it improves on.

## The problem

The forwarding-wrapper exercise (Alexandrescu's `LoggingVector<T>`: wrap
`std::vector<T>`, log every member function call, forward everything) has to
handle member function *templates*:

```cpp
template <class... Args>
auto emplace_back(Args&&...) -> reference;

template <container_compatible_range<T> R>
auto append_range(R&&) -> void;

template <class... Args>
auto emplace(const_iterator position, Args&&... args) -> iterator;
```

Reflection can enumerate these (and `identifier_of` works), but P2996
deliberately provides no introspection of *dependent* entities: no template
parameter lists, no requires-clauses, no dependent signatures.

### What works without any new features: the per-name behavioral forwarder

One deducing-this forwarder per member *name* reproduces the *behavior* of
the whole overload set without reproducing its spelling:

```cpp
template <class Self, class... Args>
constexpr auto emplace_back(this Self&& self, Args&&... args)
  noexcept(noexcept(((Self&&)self).impl.emplace_back((Args&&)args...)))
  -> decltype(((Self&&)self).impl.emplace_back((Args&&)args...)) {
  log("emplace_back");
  return ((Self&&)self).impl.emplace_back((Args&&)args...);
}
```

- variadic packs: subsumed; mixed concrete+pack parameters (`emplace`): the
  concrete argument rides in the pack, the *inner* call's overload
  resolution converts it;
- constraints: the `decltype` return mirrors the constraint's *extension*
  exactly (SFINAE) — `requires { lv.append_range(3); }` is false;
- alias return types (`reference`, `iterator`): `decltype` never spells them;
- cv/ref-qualified overloads collapse (`((Self&&)self).impl` propagates the
  object's category); whole overload sets collapse (one forwarder for all of
  `insert`); default arguments are free.

Losses: braced-init-list arguments don't deduce (`lv.assign({1,2,3})`);
**explicit template arguments break** (`lv.get<0>()` feeds `0` to `Self`);
diagnostics degrade from named constraints to substitution-failure notes.

### Why explicit template arguments are the hard wall

D's `opDispatch` handles `x.get!0()` by rewriting to
`x.opDispatch!("get", 0)()` — and it works because **D template parameters
are kind-uniform** (one variadic absorbs types, values, aliases). C++ has no
kind-polymorphic template parameter: `class... Ts` takes only types,
`auto... Vs` only values. No single forwarder head can absorb arbitrary
explicit arguments.

Partial workaround (verified working): a per-name overload set of
kind-prefixed variants:

```cpp
template <auto... Vs, class Self, class... Args> requires (sizeof...(Vs) > 0)
auto get(this Self&& self, Args&&... args)
  -> decltype(((Self&&)self).impl.template get<Vs...>((Args&&)args...));
// + the `class... Ts` twin, + mixed prefixes as needed
```

Wrong-kind explicit args cause deduction failure (variant dropped); the
`sizeof... > 0` constraints keep prefixed variants out of plain calls;
`decltype` kills the rest. Covers homogeneous prefixes (`get<0>`, `as<T>`) —
enough for tuple-protocol/structured-binding wrappers — but mixed kinds
(`f<int, 3>`) need enumerated variants; parity with D honestly ends there.

### The rejected middle: token-level head cloning over introspection
### primitives

A `template_parameters_of(info) -> vector<info>` plus per-parameter
predicates (`is_type_template_parameter`, `is_value_template_parameter`,
`is_template_template_parameter`, `is_parameter_pack`,
`has_default_template_argument`, `type_of` on NTTPs, an `is_dependent`
honesty predicate), with a *library* `clone_template_head` reassembling
`"class T0, size_t V1, class... P2"` token by token.

Rejected because it is tedious AND cannot be general: token reassembly
cannot re-spell anything whose meaning is bound to the original parameters —
interdependent heads (`template <class T, T V>`: V's type names T),
requires-clauses, dependent default arguments, template-template parameters'
nested heads. The library layer would decline those; every consumer inherits
the tedium and the gaps.

## The proposal: `declaration_of`

Key insight: **inside the compiler, cloning a declaration is just
substitution**, not token surgery. Build fresh parameter declarations, remap
old-parameter references to new ones, rebuild everything that mentioned
them. Clang exercises this machinery constantly; the exact in-tree precedent
is **CTAD deduction-guide synthesis** (SemaTemplateDeductionGuide.cpp),
which clones constructor template heads — merged with the class's head! —
remaps parameter references, and synthesizes new declarations. Full
generality is *cheap* at this level.

### API shape

```cpp
namespace std::meta {
  struct clone_naming {
    info name = {};                              // null = keep original name;
                                                 // also accept a token_sequence
                                                 // (operators: ^^{ operator+ })
    string_view template_parameter_prefix = "T"; // -> T0, T1, ...
    string_view parameter_prefix = "p";          // -> p0, p1, ...
  };

  struct declaration_parts {
    token_sequence declaration;  // the whole head: template <...> requires ...
                                 // auto name(params...) cv-ref -> ret
    token_sequence targ_list;    // "T0, T1..."   (empty if not a template)
    token_sequence arg_list;     // "static_cast<decltype(p0)&&>(p0), p1..."
  };

  consteval auto declaration_of(info fn, clone_naming = {}) -> declaration_parts;
}
```

Renaming knobs go *in*; the crucial extra *outputs* are the two forwarding
lists, because the body's forwarding problem needs exactly two facts the
cloner already knows and the user should not re-derive:

- `targ_list`: the template-argument spellings with packs expanded
  (`T1...`). Without it the user needs template-parameter introspection
  again just to know where to put `...` — the API would leak.
- `arg_list`: value-category-correct forwarding with packs expanded.
  `static_cast<decltype(p)&&>(p)` is uniformly right: moves by-value
  parameters, collapses references correctly for `T&&`/`const T&`.

### Usage: the whole LoggingVector generator

```cpp
for (info m : members_of(^^U, ctx)) {
  if (!wrapper_worthy(m)) continue;          // name filters, skip specials,
                                             // skip customization points
  auto d = declaration_of(m, {.parameter_prefix = "p"});
  auto callee = is_function_template(m)
      ? ^^{ impl.template \(id(identifier_of(m)))<\(d.targ_list)> }
      : ^^{ impl.\(id(identifier_of(m))) };
  out += ^^{
    \(d.declaration) {
      ::log_call(\(str_lit(identifier_of(m))));
      return \(callee)(\(d.arg_list));
    }
  };
}
```

Per-*declaration* exact clones fix everything the per-name forwarder loses:

- braced-init-list calls work (real parameter types);
- explicit template arguments work (real heads: `x.get<0>()` binds a
  genuine `size_t` parameter);
- function-parameter default arguments work (cloned);
- cv/ref-qualifiers are per-clone rather than deduced;
- **constraints clone**, so `append_range(3)` fails with
  "container_compatible_range not satisfied" again, not a
  substitution-failure breadcrumb trail.

The behavioral deducing-this forwarder stops being necessary and becomes the
*fallback* for whatever `declaration_of` declines.

### Design decisions (with leanings)

1. **Cloned by default**: template head + requires-clauses + function
   parameters (with their default arguments) + cv/ref-qualifiers + return
   type. **noexcept: NOT by default** — the wrapper body has its own
   exception behavior (`log_call` can throw); silently cloning `noexcept`
   is a correctness trap. Opt-in flag later.
2. **Rendering**: internally clone-then-*print*-then-tokenize. After
   substitution, every name in the cloned declaration is either a fresh
   parameter name or a nameable concrete entity (member templates of a
   specialization already have the enclosing class's arguments substituted
   in — only the template's own parameters remain dependent, and those are
   renamed). Fully-qualified printing + the existing `tokenize` machinery
   produces an honest token sequence — inspectable with `report_tokens`,
   which matters for debugging generated code.
3. **Refusals** (nullopt / constexpr_error, never wrong output):
   variadic-ellipsis parameters (cannot forward); constructors, destructors,
   conversion functions (different problems — though ctors +
   `declaration_of` is deduction-guide-shaped, a future customer);
   unnameable types in the signature (lambdas, local types).
4. **Naming knobs**: prefixes for v1. A consteval callback (`info ->
   string`) is the obvious extension if prefix collisions bite; wait for
   the need.
5. **Operators**: `name` accepts a token sequence so operator cloning is
   not a second API.

### Implementation sketch (this fork)

- Substitution core: adapt the deduction-guide-synthesis recipe — build a
  fresh `TemplateParameterList` with renamed
  `TemplateTypeParmDecl`/`NonTypeTemplateParmDecl`/`TemplateTemplateParmDecl`s,
  build a `MultiLevelTemplateArgumentList` mapping old params to new, run
  `TreeTransform`/`SubstType`/`SubstExpr` over parameter types, default
  arguments, and the trailing requires-clause.
- Rendering: print the cloned declaration with a fully-qualified
  `PrintingPolicy`, feed the string through the existing `tokenize`
  machinery (annotation-token surgery unnecessary).
- Surface: one metafunction returning the three token sequences (the
  `declaration_parts` aggregate lives in `<meta>`).
- Estimate: a few days. Tests: the three vector signatures, `get<size_t>`,
  constrained, defaulted (template and function parameters), operators,
  refusal cases.

### Notes

- This retroactively explains the type-erasure example: its hand-rolled
  `param_tokens`/`inject_interface` loops are a partial reimplementation of
  `declaration_of` for the concrete-function case. One primitive serves
  wrappers, type erasure, mocks, and decorators.
- Member templates of a class template *pattern* remain out of scope for
  the same reason `inject_members` never fires on patterns: their
  signatures reference the enclosing pattern's parameters. Everything here
  operates on members of concrete specializations.
- Names with protocol meaning need filtering by wrapper generators
  regardless of API level: libc++'s `vector` has a public P4340
  `reflect_constant` customization point, and faithfully forwarding it
  makes the wrapper a malformed customization. (Found the hard way;
  filtered in logging-vector.pass.cpp.)
