# `declaration_of`: cloning declarations wholesale

Status: design sketch, not implemented. API spellings below are provisional.
Grew out of the `LoggingVector` exercise (2026-09-15); see
[logging-vector.pass.cpp](../libcxx/test/std/experimental/reflection/logging-vector.pass.cpp)
for the current state of the art it improves on.

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

One deducing-this forwarder per member *name* delegates overload resolution
to the wrapped object without reproducing the overload set's spelling:

```cpp
template <class Self, class... Args>
constexpr auto emplace_back(this Self&& self, Args&&... args)
  noexcept(noexcept(log("emplace_back")) &&
           noexcept(((Self&&)self).impl.emplace_back((Args&&)args...)))
  -> decltype(((Self&&)self).impl.emplace_back((Args&&)args...)) {
  log("emplace_back");
  return ((Self&&)self).impl.emplace_back((Args&&)args...);
}
```

- variadic packs: subsumed; mixed concrete+pack parameters (`emplace`): the
  concrete argument rides in the pack, the *inner* call's overload
  resolution converts it;
- constraints: the `decltype` return tests whether the inner call is
  well-formed (SFINAE) — `requires { lv.append_range(3); }` is false.
  As usual, this does not turn errors instantiating the callee's body into
  substitution failures;
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

### The rejected middle: token-level head cloning over introspection primitives

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

Key insight: **the compiler can preserve dependent structure through
substitution**. Build fresh parameter declarations, remap old-parameter
references to new ones, and rebuild everything that mentioned them.
[CTAD deduction-guide synthesis](../clang/lib/Sema/SemaTemplateDeductionGuide.cpp)
is a useful precedent: it clones constructor template heads, merges them
with the class's head, and remaps parameter references.

That supplies machinery, not a complete cloning contract. In particular,
deduction guides only need to know that a function parameter has a default;
the implementation substitutes an `OpaqueValueExpr` placeholder for its
value. A callable clone must retain the actual default expression and its
instantiation behavior.

There are three separate operations:

1. **Describe a cloned declaration head**, preserving bindings and dependent
   structure while recording replacement parameters and allowing a new body.
2. **Generate a forwarding call** from those new parameters to a supplied
   receiver. This has additional restrictions that cloning need not have.
3. **Render the result for inspection**. Readable output is valuable, but
   reparsing that output need not reproduce the semantic fragment.

### API shape

```cpp
namespace std::meta {
  struct clone_naming {
    token_sequence name = ^^{ };                 // empty = keep original name
    string_view template_parameter_prefix = "T"; // -> T0, T1, ...
    string_view parameter_prefix = "p";          // -> p0, p1, ...
  };

  consteval auto declaration_of(info fn, clone_naming = {})
    -> info;

  // declaration describes a cloned head, as returned by declaration_of.
  // receiver is parsed in the generated body, e.g. ^^{ impl }.
  consteval auto forwarding_call_for(info declaration, token_sequence receiver)
    -> token_sequence;
}
```

The name is a declaration-name token sequence, validated for the kind of
declaration being cloned:

```cpp
auto renamed = declaration_of(m, {.name = ^^{ renamed }});
auto plus = declaration_of(m, {.name = ^^{ operator+ }}); // if the signature permits
auto computed = declaration_of(m, {.name = ^^{ \(id("prefix_", identifier_of(m))) }});
```

The result is an `info` describing a declaration to be introduced, analogous
to a data-member specification. It does not yet denote a function declared
in the destination. The compiler-owned description retains the source
function, transformed head, naming choices, and parameter mapping.

Interpolating this description with `\(d)` produces the head ready for a body,
using annotation tokens to retain its semantic bindings. This needs a distinct
reflection kind: interpolating an ordinary function reflection continues to
refer to that function, while interpolating a declaration description produces
a declaration head. Define which reflection queries accept descriptions and
diagnose unsupported queries.

`forwarding_call_for(d, receiver)` reads the description directly. It does not
reparse a declaration or require the caller to repeat its naming options.
Both the head and generated call refer to the description's parameter slots.
Parsing the injected tokens creates the actual declarations and binds those
slots to the new parameters. Reusing the description for another injection
creates fresh declarations with the corresponding bindings. Debug printing
and retokenizing need not preserve this metadata.

Exposing `targ_list` and `arg_list` could provide useful building blocks for
supported cases, but a forwarding recipe also needs to handle legal explicit
template arguments and the receiver expression. Keeping forwarding separate
lets mocks or type erasure reuse a declaration even when a forwarding body
cannot be generated.

Failure reporting is open: use a result carrying a refusal reason, or a
capability query plus a throwing operation. A generator needs to distinguish
unsupported cloning from unsupported forwarding before choosing a fallback.
The usage below assumes both operations support the selected member.

### Usage: LoggingVector generation, schematically

```cpp
for (info m : members_of(^^U, ctx)) {
  if (!wrapper_worthy(m)) continue; // supported members, excluding protocol names
  auto d = declaration_of(m, {.parameter_prefix = "p"});
  auto call = forwarding_call_for(d, ^^{ impl });
  out += ^^{
    \(d) {
      ::log_call(\(str_lit(identifier_of(m))));
      return \(call);
    }
  };
}
```

For supported declarations and forwarding bodies, preserving real heads
recovers the interface properties the per-name forwarder loses:

- braced-init-list calls work (real parameter types);
- explicit template arguments work (real heads: `x.get<0>()` binds a
  genuine `size_t` parameter);
- function-parameter default arguments work (cloned);
- cv/ref-qualifiers are per-clone rather than deduced;
- **constraints clone**, so `append_range(3)` fails with
  "container_compatible_range not satisfied" again, not a
  substitution-failure breadcrumb trail.

The behavioral deducing-this forwarder remains a possible fallback, with its
documented interface differences. A cloned signature alone does not establish
that its new body can forward every accepted call.

### Binding preservation and destination context

The injected declaration belongs to its destination class. Other references
to the source class retain their meaning: cloning a member returning `U&`
does not change that return type to `Wrapper&`. Adapting self types would be
a separate transformation.

Substituting the enclosing class's arguments does not make every remaining
name a fresh parameter or a concrete, fully qualifiable entity. For example:

```cpp
template<class T>
auto f(T t) -> decltype(customize(t));
```

The dependent call retains an ordinary-lookup environment and may use ADL.
Reparsing it unqualified at the destination can change ordinary lookup;
qualifying it can suppress ADL. The clone must preserve the original lookup
information while remapping references to `T` and `t`.

Access is a separate question from nameability:

```cpp
class U {
  static int default_value();
public:
  void f(int = default_value());
};
```

Calling `u.f()` is valid, but spelling `U::default_value()` in an unrelated
wrapper's default argument fails access checking. Decide whether a cloned
default retains the source declaration's access context or is refused in
that destination. Preserving its binding must not silently mean rebinding
it to another accessible function. Private aliases and constraints need the
same treatment.

Preserve the declaration description through annotated interpolation. Give it
a readable debug rendering through `report_tokens` or a companion facility;
document that rendering's limits.
Clang's `PrintingPolicy::FullyQualifiedName` controls function-declaration
names, not qualification of every name inside expressions.

### Forwarding is a separate contract

**Explicit template arguments.** Expanding every template parameter into
an explicit argument list is not generally valid:

```cpp
template<class... Ts, class V>
void f(V, Ts...);
```

For `f(1, 2.0)`, `Ts` is `{double}` and `V` is `int`. Forwarding as
`impl.f<double, int>(p0, p1...)` puts both explicit arguments into `Ts`, so
the inner call expects three arguments. A forwarding recipe must choose a
legal explicit-argument prefix and leave appropriate parameters to deduction,
or bind the specialization semantically. Omitting all explicit arguments is
not sufficient either: `get<0>()` has nothing from which to deduce `0`.
If the recipe uses a call by name, it must also establish that overload
resolution selects the intended member.

**Receiver category.** Inside `void consume() &&`, `impl` is still an lvalue;
`impl.consume()` cannot call an `&&`-qualified member. The forwarding helper
must apply the described member's cv/ref requirements to the receiver,
including when it is obtained by dereferencing a pointer. For an unqualified
implicit object member, `*this` does not reveal whether its caller supplied an
lvalue or rvalue; the recipe must define which inner overload it calls.

**Explicit object and static members.** A cloned `this Self&& self` receives
the wrapper, but the underlying call receives `self.impl`. Their deduced
`Self` types differ, so forwarding the wrapper's template arguments unchanged
is wrong. Supporting this requires an object-parameter policy; refuse it in
the first forwarding implementation if that policy is not ready. Static
members need no receiver and should use a separately specified call form.

**By-value parameters.** `static_cast<decltype(p)&&>(p)` preserves reference
parameter categories, but is not a universally valid way to transfer a
by-value parameter. It can select a deleted move constructor for a copyable
type. More fundamentally:

```cpp
struct X {
  X() = default;
  X(X const&) = delete;
  X(X&&) = delete;
};
void take(X);
// take(X{}) is valid through guaranteed copy elision.
```

A wrapper `take(X p)` cannot pass `p` into another by-value `take`. A function
wrapper adds a parameter-initialization boundary, with possible extra moves,
copies, and exceptions. This is a forwarding limitation, not a reason to
refuse cloning the declaration for a different body. For dependent parameter
types, forwarding failures may only become known at instantiation; the API
must not promise to detect every such failure when the head is cloned.

### Design decisions (with leanings)

1. **Cloned by default**: template head + requires-clauses + function
   parameters (with their default arguments) + cv/ref-qualifiers + return
   type. **noexcept: NOT by default** — the wrapper body has its own
   exception behavior (`log_call` can throw); silently cloning `noexcept`
   is a correctness trap. Opt-in flag later.
2. **Other declaration properties need an explicit policy**: `constexpr`,
   `consteval`, `static`, `virtual`, attributes, and calling conventions.
   Deleted/defaulted functions and pure virtual declarations cannot simply
   have a replacement body appended. Classify each property as preserved,
   deliberately omitted, configurable, or refused. In particular, the logging
   body must satisfy any retained constant-evaluation requirements.
3. **Instantiation and constraints**: preserve lazy default-argument
   instantiation, all sources of associated constraints (including constrained
   parameters and abbreviated templates), their ordering, and the information
   needed for subsumption. Equal truth values alone do not establish equivalent
   overload behavior. Deduced return types also need a rule: retain deduction
   from the new body, or preserve the source's deduced type when available.
4. **Initial scope and refusals**: begin with ordinary named member functions
   and member templates of concrete specializations. Constructors,
   destructors, and conversion functions remain separate problems. Diagnose
   unsupported destination/access combinations. Unnameable types need not be
   inherently unclonable with semantic fragments, but should be refused until
   supported. C-style variadic parameters prevent general forwarding, not
   declaration cloning.
5. **Naming knobs**: prefixes for v1. A consteval callback (`info ->
   string`) is the obvious extension if prefix collisions bite; wait for
   the need. Validate identifiers and collisions, including nested template
   parameter lists; bindings between fragments should not depend on prefixes.
6. **Operators**: keeping the original declaration name supports operators
   without converting it through `identifier_of`. The token-sequence `name`
   also supports explicit operator names, subject to the operator's signature
   requirements. The LoggingVector example deliberately selects ordinary
   identifiers.

### Implementation sketch (this fork)

- Substitution core: adapt the deduction-guide-synthesis recipe — build a
  fresh `TemplateParameterList` with renamed
  `TemplateTypeParmDecl`/`NonTypeTemplateParmDecl`/`TemplateTemplateParmDecl`s,
  build a `MultiLevelTemplateArgumentList` mapping old params to new, run
  `TreeTransform`/`SubstType`/`SubstExpr` over parameter types, default
  arguments, and all associated constraints. Remap function parameters and
  nested template-template parameters as well as the outer template head.
- Representation: add a declaration-description reflection kind with a
  compiler-owned payload for the source, transformed head, naming policy, and
  parameter mapping. `declaration_of` returns an `info` identifying it;
  `forwarding_call_for` consumes that same description.
- Injection: preserve the transformed nodes' bindings and lookup information
  when interpolating the description into annotation tokens. Establish
  ownership and fresh identities at each injection site; resolve access policy
  there. Add debug rendering separately.
- Forwarding: implement a helper for a stated subset, with explicit receiver,
  template-argument, and parameter-transfer rules. Keep failures distinct from
  cloning failures.
- Start with the three vector signatures and `get<size_t>`. Treat that as a
  prototype milestone; broader support depends on the contracts above, not
  just the time needed to adapt CTAD substitution.

### Acceptance examples

These are design requirements for future tests, not claims of implemented
support. Each needs either the specified behavior or a documented refusal:

| Case | What it establishes |
| --- | --- |
| Vector signatures, braced initializer lists, `get<0>()`, `f<int, 3>()` | Concrete parameters and mixed template parameter kinds survive cloning. |
| `template<class T, T V>`, dependent defaults, nested template-template heads | Parameter references are rebound throughout the declaration. |
| A dependent default that is invalid only when used | Cloning does not eagerly instantiate unused defaults. |
| Dependent ADL calls with conflicting names at the destination | Original ordinary lookup is preserved and ADL still participates. |
| A default calling a private helper; a private alias in the signature | Source bindings and destination access follow the chosen policy. |
| Constrained and abbreviated overloads, including subsumption | Viability and overload ordering survive cloning. |
| A nonterminal template pack, as in `f(V, Ts...)` above | The inner call receives a legal template-argument list. |
| `&`, `const &`, `&&`, `const &&`, and unqualified members | The forwarding recipe handles the receiver and selects the intended overload. |
| Explicit object members and static members | Object-parameter handling is supported or explicitly refused. |
| Copyable types with deleted moves; immovable by-value arguments | Forwarding limitations are diagnosed without rejecting unrelated clone uses. |
| A throwing logger wrapping a `noexcept` member | The wrapper does not inherit an invalid exception guarantee. |
| Ordinary function reflections versus declaration descriptions | Interpolation distinguishes a function reference from a new declaration head. |
| Operators, deduced returns, declaration-property policies, repeated injection | Names, body semantics, and fresh declaration identities are covered. |

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

### Type erasure using the proposed primitives

The existing [type-erasure example](../libcxx/test/std/experimental/reflection/token-seq-type-erasure.pass.cpp)
builds three layers: a `VTable` of function pointers, lambdas that cast `void*`
back to `T*` and call the concrete object, and public `Dyn` members that call
through the table. `param_tokens` and the argument loops reconstruct signatures
and parameter references at each layer.

One implementation using the proposed APIs directly lets C++ supply the
vtable: generate an abstract interface, a `Model<T>` implementing it, and the
public `Dyn` forwarders. The same declaration generator serves all three.
Here `interface_functions_of` is the existing filter for ordinary non-static,
non-special member functions; names from `std::meta` are used unqualified.
This sketch targets the example's concrete interface, including `get() const`:

```cpp
consteval auto erased_members(info interface, token_sequence receiver,
                             token_sequence suffix = ^^{ })
    -> token_sequence {
  list_builder out;
  for (info m : interface_functions_of(interface)) {
    info d = declaration_of(m);
    if (receiver == ^^{ }) {
      out += ^^{ virtual \(d) = 0; };
    } else {
      auto call = forwarding_call_for(d, receiver);
      out += ^^{
        \(d) \(suffix) {
          return \(call);
        }
      };
    }
  }
  return out;
}

template<class I>
class Dyn {
  struct Erased {
    virtual ~Erased() = default;
    consteval {
      queue_injection(erased_members(^^I, ^^{ }));
    }
  };

  template<class T>
  struct Model final : Erased {
    T* object;
    explicit Model(T& value) : object(&value) {}

    consteval {
      queue_injection(erased_members(^^I, ^^{ *object }, ^^{ override }));
    }
  };

  std::shared_ptr<Erased> object;

public:
  template<class T>
    requires (!std::is_same_v<std::remove_cv_t<T>, Dyn>)
  Dyn(T& value) : object(std::make_shared<Model<T>>(value)) {}

  consteval {
    queue_injection(erased_members(^^I, ^^{ *object }));
  }
};
```

For the existing `Interface`, the generated path is
`Dyn<Interface>::get() const -> Erased::get() const -> Model<T>::get() const -> T::get()`.
`declaration_of` supplies each head and its parameters; `forwarding_call_for`
supplies the member invocation. Its receiver handling must preserve constness
even though dereferencing a const pointer or smart pointer does not itself
produce a const object. No parameter-type or argument-name loops are needed.
The call must find the described member name on the supplied receiver: `T`
need not derive from `I`. Directly invoking a reflected `I` member on `T`
would not implement this structural interface.

This changes the representation: construction from `T&` allocates a model and
copies of `Dyn` share it, while the referred-to `T` still belongs to the caller and
must outlive its uses through `Dyn`. The existing example stores a raw pointer
and a pointer to a static table. Choosing that representation remains useful;
the virtual version illustrates what the two proposed operations already
express, without introducing a separate signature-transformation API.

To keep the **manual vtable**, `declaration_of` still replaces the head-building
part of `inject_interface`, but the current `forwarding_call_for(d, receiver)`
contract does not express `vtable->slot(data, args...)`. That needs a
callable-target form with explicit leading arguments. Building the table and
its lambdas also needs a signature transformation: remove the implicit object
parameter's member qualifiers and add a `void*` or `void const*` parameter.
Those are additional operations; cloning a member declaration does not
automatically perform them. Distinct slots would also be needed to extend the
existing name-based table to overloaded members.

Neither representation supports arbitrary member templates as runtime virtual
operations. A finite erased interface must identify concrete signatures (or
chosen template specializations) to dispatch. The cloning and by-value
forwarding restrictions elsewhere in this proposal still apply.
