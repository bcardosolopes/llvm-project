# Selling Token-Sequence Injection

The feature should not be sold as “token-sequence injection.” That is the mechanism. The product is:

> Library-defined, type-aware code generation inside C++, with no external generator and no preprocessor tricks.

The killer apps are the ones that change program shape—creating names, signatures, overloads, and protocol hooks. `enum_to_string` is useful, but reflection can already solve most of it without injection.

My ranking:

1. Rust-style `derive` for existing C++ protocols

   ```cpp
   template<class T>
   struct [[=derive::tuple, =derive::debug]] result {
       T value;
       int error;
   };
   ```

   This could generate:

   - structured-binding support;
   - `std::formatter` integration;
   - comparisons and hashing;
   - serialization customization points;
   - constrained partial specializations covering every `result<T>`.

   The template case is particularly compelling: `on_template_defined` generates the protocol bridge once, before instantiation, rather than playing order-dependent specialization games. The structured-bindings test is probably the clearest existing demonstration of something ordinary reflection cannot do.

2. An interface compiler

   Declare an ordinary C++ interface once, then synthesize a type-erased wrapper, mock, RPC client/server, or ABI table while preserving the original names and signatures.

   ```cpp
   struct Drawable {
       void draw(Canvas&) const;
       Bounds bounds() const;
   };

   using AnyDrawable = dyn<Drawable>;
   ```

   The type-erasure test is already the seed of this. A polished version—qualifiers, overloads, `noexcept`, ownership/SBO—would be a genuine “wait, C++ can do that?” demo.

   The grander version is “an ordinary C++ service declaration becomes the IDL”: generate client stubs, dispatch, mocks, and schema metadata without a separate `.proto` language or build step.

3. Structural mixins

   The iterator example is excellent because iterator boilerplate is notorious: provide three primitive operations and have a library inject the rest as native members and hidden friends. No CRTP, no repeated derived type, no forwarding façade.

   The iterator-interface example demonstrates the larger category: library-defined language features such as iterators, numeric types, views, handles, and state machines.

The pitch should emphasize that token sequences are composable constexpr values. Libraries can quote recognizable C++, interpolate semantic reflections, concatenate fragments, and let the compiler parse and diagnose the result. That is far more approachable than exposing an AST-construction API.

## Expression Macros

I would not block the main feature on expression macros. Derivation, protocol synthesis, structural mixins, and interface compilation are already killer apps.

But yes, we should settle the expression-macro model now, because it determines whether this is merely declaration generation or a credible replacement for the useful portion of the preprocessor.

The important realization from the `macro-experiment` work is:

> The result is token-sequence-shaped, but the arguments should not merely be token sequences.

An argument needs to be an opaque expression fragment retaining:

- its bindings and caller context;
- type and value category;
- source spelling/location;
- the original expression, before parameter conversions;
- safe interpolation as a single expression.

Conceptually:

```cpp
consteval macro fwd(meta::expression e)
    -> meta::token_sequence
{
    return ^^{
        static_cast<decltype(\(e))&&>(\(e))
    };
}
```

The compiler evaluates the macro at translation time, requires its result to parse as exactly one expression, and interpolates `e` as an opaque AST fragment rather than converting it to text and reparsing it. Definition-owned names should use definition context; argument fragments retain caller bindings. Generated identifiers remain explicit through `id`.

I would avoid making ordinary-looking `T&&` parameters secretly denote expression reflections. It gives pleasant syntax, but conflates function calling, overload conversions, and syntax capture. A visible `meta::expression`, `syntax auto`, or comparable parameter category makes the semantics much easier to explain.

Expression macros then have their own convincing examples:

- a correct `FWD(x)`/`MOVE(x)` without preprocessor macros;
- `CHECK(expr)` and `EXPECT_EQ(a, b)` that capture spelling, type, source location, and evaluate operands once;
- expression-valued tracing and instrumentation;
- type-directed pattern matching or query expressions.

`FWD` is the minimal semantic test; `CHECK` is probably the better public demo.

My concrete recommendation is therefore:

1. Lead with “derive + interface compiler,” not enum conversion.
2. Build one polished end-to-end example combining templates, external protocol hooks, and generated native methods.
3. Design expression fragments now so token-sequence interpolation can accommodate them.
4. Present expression macros as the natural second act, not as a prerequisite for selling injection.
