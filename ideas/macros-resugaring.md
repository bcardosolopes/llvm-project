# Macros through the lens of *Resugaring* (Pombrio, 2018)

Justin Pombrio's PhD thesis, *Resugaring: Lifting Languages through Syntactic
Sugar* (Brown, 2018; advisor Shriram Krishnamurthi), recommended by Andrei
Alexandrescu. This note builds on `macros-review-96c8e07cf3c0.md` and uses the
thesis to re-read our token-sequence / expression-macro design.

Probes live in `$HOME/tmp/review/p2/`. Each result below was checked against
the build at `4491327a037d`.

## The thesis in one paragraph

A *sugar* is a construct translated at compile time into the rest of the
language. Once code has been desugared, the programmer sees the **core**
language in everything the tools say: evaluation steps, scope, type errors.
Pombrio's thesis is that tools should report in terms of the **surface**. He
calls this *resugaring*, and does it three times:

- **Ch4, evaluation.** Tag each core term with its origin (the sugar that
  produced it), then unexpand core steps back into surface steps. Three
  properties are required:
  - *Emulation*: every surface step desugars to the real core step.
  - *Abstraction*: code the sugar introduced is never shown.
  - *Coverage*: as many steps as possible are shown.

  Marking a sugar *opaque* or *transparent* trades Abstraction against
  Coverage.
- **Ch5, scope.** Infer the binding structure of the surface language from the
  core scope rules plus the sugar definitions. Hygiene is defined as
  **preservation of α-equivalence**: renaming a user's variable consistently
  can never change what the program means.
- **Ch6, types.** Infer surface *type rules* so that type errors are never
  reported in desugared code. The tools he uses:
  - `calc-type` (§6.4.1), which lets a sugar ask for the type of an argument.
  - Fresh variables with an explicit **capture set** (§6.4.3). Hygiene is the
    default, and capture is opt-in.
  - "Globals" (§6.4.4). A margin note calls these "a poor approximation" of
    macros that can reference anything in scope, and leaves typing that case
    as future work.

The inference in Ch5 and Ch6 only works for **declarative, pattern-based**
sugars. It does not work for sugars defined as arbitrary functions.

The survey in Ch2 is the part most relevant to us:

- It concludes that transformations should *never* operate on text or token
  streams. The C preprocessor is the cautionary example: the `SUB(0, 2-1)`
  parenthesization problem, `do{}while(0)`, and variable capture.
- It notes that C++ templates are "not general-purpose sugars" because they
  cannot take code as a parameter.

Expression macros are exactly the answer to that second point, so the thesis
makes a useful checklist.

---

## 1. Where our design sits in Pombrio's taxonomy

| Axis (Ch2) | Us | Consequence |
|---|---|---|
| Representation | **Bicameral**: expression params are parsed and Sema'd ASTs; the output is a token sequence that is re-parsed and must form one expression or declaration. | Better than CPP, weaker than Racket (see §3). |
| Desugaring order | **Per parameter.** Expression params are *input-then-output*: arguments are expanded and type-checked before the macro runs. Raw `token_sequence` params are *output-then-input*. | A good design point that no system in the survey has. It also causes the "deconstruction sees core" bug (§4). |
| Can inspect/deconstruct code | Yes (`operands_of`, `source_text_of`, `type_of`). | MacroML, MetaHaskell and Wyvern cannot. It also makes static resugaring impossible in general (§6). |
| Can compute | Yes (full consteval). | Same as above. |
| Sugar defines sugar | **Yes.** Checked in `p2/r4_mdm.cpp`: a declaration macro emits `template <class T> __macro \(name)(T&& x) { return ^^{ (\(x) * \\(k)) }; }` and `triple!(5) == 15`. The inward-binding escape rule makes this painless. | Needs a regression test; none exists. |
| Syntactic safety | Checked at **expansion**, not definition. | Like templates. It could be definition-time for template-shaped macros (§6). |
| Hygiene | **Half** (§2). | |
| Type safety | Checked after expansion. | Errors can leak core code (§5). |

## 2. Hygiene = α-equivalence, and we have only half of it

Pombrio's definition gives us a crisp **test oracle**: *renaming any user
identifier consistently must not change the program's meaning.* Apply it to
our design:

- **Expression arguments are already hygienic.** This is not a special
  mechanism. It follows from the input-then-output order: the argument was
  bound in the use-site context before the macro ran. This is the strongest
  argument for making expression params the default kind, and the design
  should say so explicitly.
- **The macro's own free identifiers are not hygienic.** Renaming a user's
  `namespace lib { int m; }` or a user helper changes what `lib::m!` or
  `::helper` inside an expansion means. The previous review lists the
  symptoms: the `lib::m` hijack and helper macros not found at the use site.
  Those are all the same α-equivalence violation.
- **Raw params capture macro locals.** This is the third violation:
  `twice_raw!` gave 200 instead of 101.

Two specific things to borrow:

1. **A capture set instead of a fresh set (SweetT §6.4.3).** Identifiers the
   macro body *introduces* should be fresh by default. A macro that means to
   bind a user-visible name (anaphoric `it`, `TEST_CASE!`'s function name,
   `defer!`'s guard) lists it explicitly, e.g. `__macro m(...) captures(it)`
   or a `[[capture("it")]]` attribute.
   - This is a principled replacement for the `fresh_id` gensym proposed last
     time. The author states the exception rather than remembering the rule.
2. **The origin tag is already there.** Every token spelled literally in a
   macro body has a `SourceLocation` inside the macro definition. Every
   argument token has a location at the call site. That distinction is exactly
   the "mark" in Flatt's sets-of-scopes and exactly Rust's `Span::def_site`
   vs. `call_site`.

A tiered plan that fits what we have:

| Tier | Rule | Fixes |
|---|---|---|
| a | Local declarations introduced by def-site tokens are invisible to call-site tokens (Rust `mixed_site`). | Raw-param capture of macro locals. |
| b | Names spelled in def-site tokens that are not locals are looked up in the definition context first, falling back to the use site (two-phase-lookup semantics). | Helper macros, the `lib::m` hijack, `::helper` hijack. |
| c | Names produced by interpolation (`\(id(...))`, raw params) keep call-site lookup; the capture list whitelists def-site names that should be visible. | Keeps `TEST_CASE!`, `bitflags!` and anaphora working. |

**Add α-equivalence tests.** For each macro in the test suite, rename every
user variable to a name that collides with a macro-internal name (`__v`,
`__l`, `helper`, `fwd`) and assert that the output is unchanged. This is
cheap and catches the whole class at once.

## 3. Raw params are still a token-stream sugar

The thesis's canonical CPP bug, run through both kinds of parameter
(`p2/r1_sub.cpp`):

```cpp
template <class A, class B> __macro sub(A&& a, B&& b) { return ^^{ \(a) - \(b) }; }
__macro sub_raw(token_sequence a, token_sequence b)   { return ^^{ \(a) - \(b) }; }

sub!(0, 2 - 1)      // -1  ✅
sub!(5, 3) * 2      //  4  ✅ outer grouping preserved
sub_raw!(0, 2 - 1)  // -3  ❌ exactly CPP's SUB
sub_raw!(5, 3) * 2  //  4  ✅ outer grouping preserved (single-expression rule)
```

The outer boundary is safe for both kinds. The inner boundary is only safe for
expression params. Options, from cheapest to most thorough:

- A **warning** when a raw param is interpolated directly next to a binary
  operator without surrounding parentheses.
- **Auto-grouping**: when a raw sequence that parses as a complete
  *expression* is interpolated into an expression position, treat it as
  parenthesized. This is awkward at the token level, but the parser already
  re-parses the result, so an `annot_grouped` marker around the spliced range
  could force it to be a primary expression.
- **Fragment specifiers** à la `macro_rules!`
  (`token_sequence<expr>`, `<type>`, `<ident>`, `<block>`). The call site
  checks the category, but the tokens are not Sema'd, so anaphora still works.
  Interpolating an `<expr>` fragment always groups. This also gives clangd the
  syntactic category of each argument (§7).

## 4. Deconstruction sees the core, not the surface (new bug)

Input-then-output order means that when an argument contains a sugar, the
macro receives the *desugared* argument. Pombrio's Abstraction property says
it should not see macro-introduced code. See `p2/r3_core.cpp` and
`p2/r3b.cpp`:

```cpp
struct M { int v; };
__macro operator==(M const& a, M const& b) { return ^^{ (\(a).v == \(b).v) }; }

kind!(a == b)         // binop ==, lhs source "a"          ✅
kind!(a != b)         // binop !=, lhs "a"                 ✅ rewritten != stays surface
kind!(fwd!(x) < y)    // binop <,  lhs "fwd!(x)"           ✅ nested expr macro opaque
kind!(m == n)         // binop ==, lhs source "", type_of(lhs) = int,
                      // source_text_of(c) = "=="          ❌ core leaks
```

With an operator macro, `check!(m == n)` would decompose into `m.v` and `n.v`,
print `int`s, and report the source text as `==`. Every power-assert or
`matches!` over a type whose operators are macros gets this wrong.

**Fix:** keep an opaque wrapper node for the expansion of an operator macro,
the same way `fwd!(x)` apparently already stays opaque. Something like a
`MacroExpansionExpr` that carries the surface operands and source range, with
the expansion as its semantic form, analogous to `CXXRewrittenBinaryOperator`.
Then:

- `operands_of`, `source_text_of` and `is_binary_operation` see the surface
  form.
- A new `expansion_of(expr)` lets a transparent-minded macro peek inside.

This is literally Pombrio's origin tag, and `CXXRewrittenBinaryOperator` is
precedent for doing it in Clang.

## 5. Diagnostics: Abstraction vs. Coverage

What already resugars correctly (opaque by default):

- `std::source_location::current()` in an expansion reports the **invocation**
  line, both directly and through a default argument (`p2/r6_loc.cpp`).
- Constant-evaluation notes: `in call to 'boom(0)'` points at `safe_div!(0)`,
  and the macro body appears only in the "expanded from" note.

What leaks (`p2/r5_diag.cpp`):

```
error: invalid operands to binary expression ('NoPrint' and 'int')
   10 |   show!(NoPrint{1});
note: expanded from macro 'show'
    5 | ... std::printf("%d\n", __v + 0); ...
```

The user wrote `show!(NoPrint{1})`. The error is about `__v + 0`, which they
did not write. This is the Ch6 motivating example. Proposals, in order of
payoff:

1. **Blame the argument.** When an ill-formed expression's offending operand
   is (a copy of, or a reference to) an interpolated argument, the primary
   location should be the argument (`NoPrint{1}` in the source), with the
   expansion as a note. Clang already knows which subtrees came from which
   argument, because it enforces evaluate-once.
2. **Constrain the macro.** Macro overloading by constraints already works.
   The library idiom should be to put the surface type rule in a
   `requires`-clause (`requires std::integral<std::remove_cvref_t<T>>`) so
   that failures are reported as "no matching macro" at the call site. That is
   SoundExt's approach: author-written surface rules, compiler-checked
   consistency. Document it in `macros.md` and use it in the examples.
3. **Per-macro opacity.** `[[transparent]]` shows the full expansion stack.
   The default (opaque) collapses nested "expanded from" notes into one line
   naming the outermost macro. This also subsumes last review's wrong-token
   note bug ("expanded from macro '('").
4. **Debug info and stepping (Ch4 proper, untested).** For `-g`, attribute
   expansion code to the invocation line unless the macro is transparent, so
   `step` in gdb steps *over* `check!`. Pombrio's stepper is the ideal; line
   attribution is the 90% version.

## 6. Pattern macros vs. procedural macros

Pombrio's inference needs declarative sugars. We have procedural ones, so we
cannot resugar in general. However, many of our macros are **template-shaped**:
the body is a single `return ^^{ ... };` whose interpolations are only
parameters, `type_of(param)`, or constants. Examples in the tests are `fwd!`,
`id!`, `twice`, `sub`, the operator macros, `try!`, and the inner half of
`log!`.

For those, the compiler could give guarantees at definition time, the way
`macro_rules!` does and `proc_macro` does not:

| Guarantee | Today | Template-shaped macros could… |
|---|---|---|
| Syntactic safety | expansion time | parse the literal once at definition, with holes as typed placeholders |
| Hygiene tier b | not implemented | resolve non-dependent names at definition, exactly like two-phase lookup in templates |
| Evaluate-once | checked per expansion | check once, statically; also warn on **unused** params (last review wanted per-path; this is the static half) |
| Result category / type | unknown until expanded | infer "`sub!(a,b)` has type `decltype(a - b)`" for clangd hover and completion |

No new syntax is needed; this is a recognizer. It separates "cheap, safe,
toolable" macros from "full consteval power" ones, and lets documentation and
diagnostics say which is which.

## 7. Evaluate-once is stricter than the thesis

Pombrio restricts each pattern variable to one RHS occurrence to avoid code
blowup and double evaluation, but lets **atomic** terms be duplicated freely.
We reject both of these (`p2/r2_dup.cpp`):

```cpp
template <class T> __macro sq(T&& x) { return ^^{ \(x) * \(x) }; }
sq!(v)   // error: would evaluate this argument more than once
sq!(3)   // error
```

**Relax the rule:** allow repeated interpolation when the argument is a
literal, a `constexpr` constant, `this`, or an id-expression naming a
non-volatile variable or parameter (that is, no side effects and no
evaluation cost). Keep the error otherwise, and suggest
`do { auto&& __x = \(x); ... }`, which is the thesis's `let` desugaring and
works today. This removes a real papercut in `min!`/`max!`/`clamp!`-style
examples.

## 8. Types: we already have `calc-type`

Pombrio had to *add* `calc-type` so that `let x = α in β` could name the type
of `α`. Our `type_of(param)` is that feature, and more, because it also works
inside `consteval` logic. Worth stating in `macros.md` as a strength.

What we lack is the other direction: a declared **surface type** that the
compiler checks. Possible syntax is `__macro sub(auto&& a, auto&& b) -> int`,
or `-> auto` meaning "the type of the expansion".

- The expansion is checked against the declared type (SoundExt-style
  consistency check), with an error at the macro definition, not the use.
- `decltype(m!(...))` and concept checks could use the declared type without
  expanding.
- clangd gets a signature to show.

This is P3. Only do it if unevaluated-context expansion ever becomes a cost
problem.

## 9. Scope resugaring for tools

Ch5 exists to give IDEs binding structure on incomplete programs. Our weak
spots:

- **Declaration macros** (`bitflags!`, `TEST_CASE!`, `getter`) introduce names
  that clangd only sees after expansion. When the macro body does not compile
  yet, rename and go-to-definition break.
- **Raw params**: nothing tells a tool whether `x` in `declare!(x, 42)` is a
  binding or a use.

Clang expands eagerly, so complete programs are fine. Cheap mitigations if
this ever matters:

- the fragment specifiers from §3 (`<ident>` marks a binding position);
- a capture list (§2) that doubles as the macro's "exports".

This is lower priority than §2–§5.

## 10. Recursive sugars

The thesis handles `[e | b, Q] ⇒ if b then [e | Q] else []` by allowing any
judgment over a surface term as a premise. Our recursion works (see
`macros-review-96c8e07cf3c0.md`), but there is still **no depth limit**
(segfault after ~1.1s). That remains the top P1. The resugaring view adds one
thing: the depth-exceeded diagnostic should show the *surface* recursion
chain (`m!(3) → m!(2) → …`), capped like `-ftemplate-backtrace-limit`, not
the tokens.

## 11. Answering "never operate on token streams"

The thesis's strongest claim is aimed directly at designs like ours. We should
answer it explicitly in `macros.md`:

- **Inputs:** expression params are ASTs (hygienic, grouped, typed). Only raw
  params are token streams, and they are opt-in.
- **Outputs:** tokens, but re-parsed with a single-construct requirement,
  which closes the outer-grouping hole (`sub_raw!(5,3)*2 == 4`).
- **Remaining exposure:**
  - inner grouping of raw params (§3);
  - free-identifier hygiene (§2);
  - `list_builder` fragments that are not individually well-formed;
  - `constant_of` on string literals (previous review).

Tokens as the output format buy us the ability to generate *declarations*,
including macros (§1), which none of the AST-typed systems in the survey
(MacroML, MetaHaskell) can do.

---

## Missing or under-served examples suggested by the thesis

| Example | Status | Notes |
|---|---|---|
| `SUB` / CPP grouping regression (`sub` vs `sub_raw`) | ⚠️ | Expression params are fine; add a test that locks in the `-3` for raw params, or the fix. |
| Macro-defining macro (`def_scaler!(triple, 3)`) | ✅ untested | Add to `expression-macros.pass.cpp`. |
| α-equivalence suite (rename user vars to `__v`, `helper`, …) | ❌ | Would fail on the helper and `lib::m` cases today; good tracking test. |
| `check!(m == n)` where `==` is an operator macro | ❌ | §4: decomposes into core operands. |
| `sq!`, `min!`, `max!`, `clamp!` over atomic args | ❌ | §7: rejected by evaluate-once. |
| Anaphoric `aif!(expr, it + 1)` with an explicit capture list | ⚠️ | Works via raw params today, but only because raw params are unhygienic. |
| `foreach!` with `break` (thesis §6.6.2) | ⚠️ | `unwrap_or_continue!` shows the ingredients; a loop macro with a captured `break_`/`continue_` name would exercise the capture-set design. |
| List comprehension `comp!(x * x, x : xs, x % 2 == 0)` | ❌ | Recursive and binding-introducing; the canonical Ch5/Ch6 case study, and a demanding test of raw-param scope. |
| `new_type!(Meters, double)` hiding its implementation | ⚠️ | Declaration macro; the diagnostics should never mention the generated internals. |
| `show!(NoPrint{})` diagnostic golden test | ❌ | §5: pin the desired "blame the argument" output. |

## Suggested order

1. **The §4 wrapper node.** It is a real bug, has a contained fix, and has
   precedent in `CXXRewrittenBinaryOperator`.
2. **The §7 atomic-duplication relaxation.** Small change, and it removes a
   papercut.
3. **Hygiene tiers a and b plus the capture list (§2).** This is the same work
   as last review's P1 hygiene item; the thesis supplies the default (fresh)
   and the opt-out (capture). Land the α-equivalence tests first as expected
   failures.
4. **§5.1 "blame the argument" and §5.3 opaque-note collapsing.**
5. **§3 raw-param grouping:** start with the warning, then decide on fragment
   specifiers.
6. **§6 template-shaped recognizer**, once hygiene tier b exists, because it
   reuses the same definition-time lookup.
