# Review notes on P3549R1 "Diverging Expressions"

Notes for co-authors, organized by how much each point affects the proposal. The core argument (divergence is a property of expressions the language already half-acknowledges via the `throw` special case in `?:`, and the `do`-expression rules are conservative and sensible) is strong. The bottom type section is where the paper is currently underspecified for C++ specifically.

## Substantive issues

### 1. Is `noreturn_t` a complete object type?

Everything downstream depends on this, and the paper doesn't say.

- If it's **incomplete** (like `void`), then `union { T; noreturn_t; }` is ill-formed, so `std::expected<T, noreturn_t>` needs an explicit specialization, as do `variant`, `optional`, `future`, etc.
- If it's **complete**, then `sizeof` is 1, it can be a member subobject, an array element type, and so on. The `expected` layout claim ("simply `struct { T; }`") still needs a specialization because the primary template would happily store it in the union.

Either way the library work is bigger than the paper implies. Note also that `expected<T, E>`'s converting constructor from `expected<T, noreturn_t>` would call `other.error()` on a member that doesn't exist. That is fixable in a cute way: the specialization's `error()` can itself return `noreturn_t` via `std::unreachable()`.

### 2. It cannot be a library class; it has to be a compiler builtin

The paper says "new language/library type". It has to be the former, and the paper should say so. Consider the special member functions:

- If the destructor is deleted, then `std::terminate();` as a discarded-value expression is ill-formed because the temporary must be destroyed.
- If the destructor isn't deleted but the copy constructor is, `noreturn_t* p; f(*p)` is still expressible.
- For `noreturn_t abort()` to link against C's `abort`, the return must be ABI-identical to `void` (no sret slot, no register clobber expectations).

None of that falls out of a class definition. Framing it as a fundamental type (alongside `void` and `std::nullptr_t`) resolves all of it and is more honest about scope.

### 3. Rule 3.2 contradicts the paper's own goal

Under "if the type deduced is not the same in each deduction, the program is ill-formed", this is rejected:

```cpp
do { if (c) do_return 1; else do_return std::terminate(); }
```

We need a rule that `do_return` statements whose operand has type `noreturn_t` don't participate in deduction — exactly the rule pattern matching applies to arms. This is a genuine gap in the proposed wording, not just an omission.

### 4. Lambdas and functions

After this paper, `do { std::terminate(); }` has type `noreturn_t` but `[]{ std::terminate(); }()` has type `void`. Should return-type deduction for functions and lambdas also get rule 3.3 (and the deduction exclusion from item 3)?

I think yes, for consistency and because it makes wrappers like

```cpp
auto fatal = [](auto&&... args) { log(args...); std::abort(); };
```

compose properly — which is the "wrapping" argument the paper uses against `[[noreturn]]`. It's a small breaking change (`decltype(lambda())` changes), so it needs to be called out either way.

On the plus side: `noreturn_t f() { }` flowing off the end is already UB by the existing rule for non-void functions, so the semantics of `[[noreturn]]` fall out for free. Worth saying explicitly.

### 5. Migration story for the existing `[[noreturn]] void` ecosystem

In practice people don't call `std::terminate()` in the fallback arm; they call `LOG(FATAL)`, `fatal_error()`, `unreachable()`, assertion macro internals, etc. Under the pure `noreturn_t` design, none of those diverge until their authors change return types, and callers that take `void(*)()` to them break.

The paper should either:

- (a) recommend the migration explicitly, and note that the `&std::abort` → `void(*)()` compatibility problem is presumably the actual motivation for the constant-expression function pointer conversion (the paper presents it as type theory), or
- (b) also treat calls to `[[noreturn]]` functions as diverging expressions.

If (b), expect EWG to raise attribute ignorability: an attribute affecting whether `int x = c ? 1 : f();` is well-formed is a stronger deviation than `nodiscard` or `no_unique_address`.

### 6. The strongest argument for the type-based approach isn't made

The "Alternative with `[[noreturn]]`" section argues composability and `void` overloading. The better argument is that types are allowed to affect well-formedness and attributes, by EWG policy, mostly aren't. Lead with that.

### 7. Overload resolution rank is unspecified

"Convertible to any type" needs a conversion sequence rank. `f(std::terminate())` with `f(int)` and `f(double)` will be ambiguous under any sane ranking, which is fine, but `std::variant<int, noreturn_t>`'s converting constructor, `common_type`, and `?:` all depend on the answer.

For the conditional operator specifically: [expr.cond]/4 requires the reference to "bind directly to an lvalue" when the other operand is an lvalue, and a prvalue `noreturn_t` doesn't obviously do that. Suggest keeping a special bullet in [expr.cond], generalized from "(possibly parenthesized) throw-expression of type void" to "operand of type `noreturn_t`", rather than promising it's subsumed by general convertibility.

Bonus example for the paper: today `c ? x : (log(), throw 1)` doesn't work because the parenthesization carve-out doesn't cover comma expressions. The type-based rule fixes that too.

### 8. The function pointer conversion rule is a strange object

A conversion whose validity depends on the source being a constant expression has no precedent. "Because of calling convention reasons" is under-explained — the actual problem is sret plus argument shifting (a hidden return pointer in the first argument register moves every real argument over), not return registers, which a non-returning callee never writes anyway.

Either spell that out or mark this bullet as severable so it doesn't sink the rest.

## Smaller things about the divergence rules

- `do { if (c) throw 1; std::terminate(); }` is the most common shape in practice and is correctly handled (last statement diverges). Put it in the examples list; the current list makes the `if`-without-`else` case look like it's always a problem.
- Explicitly list what's conservatively **not** covered: `switch` with all-diverging cases plus `default`, `try` blocks, `for(;;)` / `while(true)` without `break`, labeled statements. A false negative just means writing `-> T`, so that's fine, but say it so nobody thinks it was overlooked.
- `std::unreachable()` is arguably a better headline example than `std::terminate()` for the `_ =>` arm, and an even clearer case for `noreturn_t unreachable()`.
- The paper lists "we keep adding properties to expressions: type, value category, bit-field, and now divergence" as a downside of the first approach, but the second approach still needs "diverging statement" as a property of statements. Worth acknowledging that the type only removes the *expression* half.
- Rule 3.3 could just say "if the *compound-statement* is a diverging statement" and lean on 2.1, rather than repeating "last statement".
- Terminology: `$escape-statement$` in rule 2.2 vs `$escaping-statement$` in rule 1.2 — pick one.
