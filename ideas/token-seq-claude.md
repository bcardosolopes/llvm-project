# Selling token sequence injection, and why expression macros are the missing piece

Notes from a conversation on 2026-09-12, after shipping `on_template_defined`
and the `\(tmpl)<Ts...>` pseudotoken syntax.

Short version: expression macros are the piece that turns this from "a powerful
facility for library authors" into "the feature that finally lets you delete the
macros from your codebase," and the design sketch in `ideas/macros.md` is the
right one, for a reason the P3294 text doesn't yet exploit.

## What's genuinely new here

Worth being honest about first: many "reflection killer apps" (enum-to-string,
bindings generation, serialization *readers*) are pull-based and P2996 alone
does them. Token injection earns its keep in exactly two places, and the pitch
should be built around them:

1. **Generating declarations whose *shape* isn't a data member.**
   `define_aggregate` covers layout; injection covers *behavior* — member
   functions with computed names and bodies, operators, constructors, partial
   specializations, friends. Nothing else in the language can produce
   `void draw(int) override { ... }` from a string `"draw"`.
2. **Generating expressions with access to the argument's spelling, type, and
   location.** That's the entire surviving preprocessor use case.

## The narrative: "which `#define`s are still in your codebase?"

Every modern C++ codebase has roughly six kinds of macro left. Mapping them onto
what we have is the sell:

| Surviving macro | What replaces it | Needs |
|---|---|---|
| `CHECK(a == b)` / `ASSERT` / `LOG` | expression macro with stringify + location + value decomposition | **expression macros** |
| `REQUIRE` / `TEST_CASE` (Catch2, gtest) | same, plus injected static registration | expression macros + injection |
| `MOCK_METHOD(int, Foo, (int), (override))` | `mock<Interface>` | injection (already demoed: type-erasure test) |
| X-macros / enum tables | reflection + `derive`-style annotations | P2996 + annotations |
| operator/ctor/forwarder boilerplate, pimpl | injection | injection + `on_complete` / `on_template_defined` |
| `#if PLATFORM` | nothing — that's fine, that's what the preprocessor is for | — |

Five of six covered by one mechanism. That table is the talk.

## Killer demos, ranked

By "impossible without injection" × "how many people feel the pain":

- **`mock<Interface>` and `dyn<Interface>`** — a gmock replacement and a
  type-erasure generator from the *same* ~40 lines. Write an interface as a
  struct with virtuals; get a small-buffer type-erased wrapper, a mock with
  expectations, a logging decorator. Genuinely impossible any other way, and
  every test suite on earth has `MOCK_METHOD`.
- **`CHECK(x < y)` printing `3 < 2 failed`** — Catch2's expression
  decomposition in a page, no operator-overloading trickery. Needs expression
  macros.
- **`derive`-style annotations** — what we just shipped
  (`token-seq-inject-bindings.pass.cpp`). The blog post's argument (pull is
  inherently ambiguous; push resolves it) is the intellectual core, and
  `on_template_defined` makes it work for templates, which Rust's derive does
  and nothing in C++ ever has.
- **`fwd!(x)`, `λ!(_1 > _2)`** — small and joyful; great slide material, not a
  reason to adopt.
- **`try!` / the `?` operator as a library** — impressive but needs storage
  injection; keep as "future" rather than lead with it.

## Expression macros: yes, and the macros.md design has a hidden advantage

P3294's macro sections use raw tokens in (`meta::info` = `^^{ x }`) and inherit
two hard problems the paper itself flags: **parsing** (which comma delimits
arguments?) and **hygiene** (`assert_eq!(42, sa * 2)` finds the macro's own
`sa`). `ideas/macros.md` — typed expression placeholders, arguments analyzed
*in the caller's context before expansion*, overload resolution as for
functions — dissolves both, and the paper should say so loudly:

- **Parsing is free**: arguments are parsed as function arguments. The comma
  problem doesn't exist.
- **Hygiene mostly evaporates**: `\(x)` interpolates an already-*bound*
  expression, not tokens. Name lookup for the argument happened at the call
  site, so a macro's `do { auto va = ...; ... \(x) ... }` cannot capture the
  user's `va`. The remaining hygiene issue (macro-introduced names visible to
  macro-introduced code) is the macro author's own business. You only need
  `\id` / gensym for names you deliberately want callers to see.
- **Types, overloading, constraints, deduction all come for free** —
  `macro fwd(auto&& x) { return ^^{ static_cast<decltype(x)&&>(\(x)) }; }`
  just works.

The proof this model carries the important use case is already in the tree:
**P3951 template strings are an expression macro in disguise** — bound
expressions plus their source text (`interp.expression`) plus their values,
captured at the call site. That's precisely the reflection-on-placeholder API
assertions need (`source_text_of(x)`, `source_location_of(x)`, later
`decompose(x)` for `a == b`). The plumbing exists; expression macros generalize
it. That's also a good committee argument: the demand for `t"..."` is evidence
that people keep asking for one-off syntax because there's no macro mechanism —
ship the mechanism.

Two decisions to settle up front:

1. **Evaluation count.** If `\(x)` appears twice, is `x` evaluated twice?
   Recommendation: yes — per-interpolation, C-like — because laziness
   (`LOG_IF(cond, expensive())`, assertion messages only on failure) and
   control flow are *why* one reaches for a macro over a function. Authors
   wanting once-semantics bind it in a `do` expression. Whatever is picked,
   macros.md's "preserves the expression boundary" is about grouping and should
   be separated from this question.
2. **Keep a raw-token parameter kind as an opt-in escape hatch**
   (`token_sequence` parameter, delimited by top-level commas, C-macro style).
   Anaphoric macros (`λ!(it > 0)`) and DSL-ish things need unbound tokens;
   that's the only reason raw tokens should exist. Don't build a token parser
   library; typed parameters make it unnecessary for everything mainstream.

## Suggested sequencing

Expression macros are probably the *smallest* remaining step for the *largest*
payoff: `do` expressions are the statement-carrying vehicle, template strings
already do bound-expression capture with source text, `\(...)` interpolation now
handles every reflection kind, and injection into function bodies exists.

Level 1 is:

- `macro` declaration syntax with typed parameters;
- body returns `token_sequence`;
- expansion must form a single expression at the call site;
- `\(param)` interpolates the bound argument;
- `source_text_of` / `source_location_of` on placeholders.

That alone yields `CHECK`, `fwd!`, `LOG`, and `λ` with numbered params — and
with it the "delete your macros" story is complete end to end.

Next step if pursued: turn `ideas/macros.md` into a concrete implementation
plan against this codebase (where placeholders live in the AST, how expansion
re-enters the parser, what the template-string code can be reused for).
