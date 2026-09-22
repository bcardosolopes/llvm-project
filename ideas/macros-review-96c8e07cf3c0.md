# Macros / token-injection review @ 96c8e07cf3c0 — design gaps and missing examples

Unlike the earlier `macros-review-*.md` files, this one is mostly not a bug
hunt. Most findings from 3e817fdf have regression tests now (b5b317f, 1a9487c,
a130a1d, b68c0ac, 02230d6, 3d0f86e, 96c8e07). This review asks three questions:

1. What can be done better?
2. What doesn't work that should?
3. Which compelling examples are poorly supported or missing entirely?

Every claim here comes from a probe compiled against HEAD. The probes are in
`$HOME/tmp/review/p/` (named `a*`/`b*`/`c*`). The snippets below are cut down
from them.

---

## 0. TL;DR, prioritized

| # | Pri | Area | One-liner |
|---|-----|------|-----------|
| 1 | P1 | robustness | Unbounded macro recursion overflows the stack and segfaults within ~1s. There's no `-fmacro-expansion-depth`. |
| 2 | P1 | robustness | `return_type_of` on an undeduced `auto` function hangs the compiler forever (`desugarType` loop). Easy to hit via `macro_expansion_context()` in `auto f()`, i.e. `try_!`. |
| 3 | P1 | design | Free names in an expansion resolve at the call site, and the documented hygiene idioms (`\(^^std::move)`, `\(^^lib::f)(x)`) don't work. |
| 4 | P1 | design | A namespaced macro can't reliably call its own helper macros: `testlib::check!` expanding to `fwd!(…)` fails unless the user can see `fwd`, and `\(^^lib::m)!(…)` isn't supported. |
| 5 | P1 | feature | Pack expansion inside a token sequence (`^^{ f(\(xs)...) }`, `(\(xs) + ...)`) is unsupported, and the diagnostic is misleading. |
| 6 | P1 | feature | Declaration macros don't work at block scope (no `defer!`, `let!`, `TEST_SECTION!`) or in class templates (a hard error that suggests a manual `queue_injection` rewrite). |
| 7 | P2 | feature | `constant_of(expr)` rejects string literals and `string_view("…")` prvalues, which rules out the `fmt!("x={x}")` f-string macro. |
| 8 | P2 | feature | No gensym/unique-name facility. Raw params silently capture macro locals, and `TEST_CASE!` has to derive names from `__LINE__`. |
| 9 | P2 | design | Evaluate-once is per-expansion, not per-path, so `c ? x : -x` is rejected. Also, an argument that is never interpolated is dropped with no warning. |
| 10 | P2 | API | The expression-reflection API only covers binary operators. Unary, call, member, cast, conditional, and subscript are missing, which blocks Swift `#expect`-style capture. |
| 11 | P2 | diag | "expanded from macro 'X'" names the wrong token for qualified (`'N'`) and member (`'('`) invocations. |
| 12 | P2 | diag | Many failures inside the macro body surface only as "did not produce a token sequence". |
| 13 | P3 | lang | A static member macro can't be used from an earlier member function body in the same class, even though that's a complete-class context. |
| 14 | P3 | lang | Reflection-valued arguments (`m!(^^int)`) are unusable in non-consteval code. |
| 15 | P3 | tests | `vec-macro.pass.cpp`'s `#if 0` fold form works now. `vec![]` (empty) fails deep in `common_type`. |

What works well, so it isn't re-litigated:
- operator macros, including unary `-`, postfix `++`, `+=`, `->`, and rewritten `<=>`/`<`/`>=`
- macros in requires-expressions, `noexcept(...)`, and trailing `decltype(...)`
- macros in array bounds, default args, NSDMI, enumerators, and template default args
- overloading on constraints
- higher-order raw-name macros (`apply!(twice, 21)`)
- forwarding a pack into a nested macro via `list_builder`
- statement-ish macros via `do { }`, including `continue` inside a `do -> T` expression (`unwrap_or_continue!`)
- lambdas, generic lambdas, and nested template instantiations
- `source_location::current()` reporting the invocation line
- nested-expansion notes in diagnostics, which are very good (see §11 for the one naming bug)

**Performance is fine.** A syntax-only compile of 5000 `fwd!(x)` calls takes
1.21s, vs 1.11s for the PP `FWD(x)` and 1.25s for `std::forward`. 4000
decomposing `check!` expansions inside explicitly instantiated templates take
3.5s, about 0.6ms each.

---

## 1. P1: no expansion depth limit

```cpp
__macro loop(int x) { return ^^{ loop!(\(x)) }; }
int a = loop!(1);   // SIGSEGV after ~1s
```

`BuildExpressionMacroExpansion` → parse → `ActOnMacroInvocation` →
`BuildExpressionMacroExpansion` recurses on the real stack, with no counter.
Legitimate recursion (`count!` peeling one arg per step) works, so the fix is a
limit, not a ban.

**Suggest:** add `-fmacro-expansion-depth=N` (default 256?), analogous to
`-ftemplate-depth` and `-fconstexpr-depth`. Emit
`err_macro_expansion_depth_exceeded` along with the usual expansion-note stack,
elided the way template backtraces are (`-ftemplate-backtrace-limit`). Also wrap
the expansion in `runWithSufficientStackSpace` like template instantiation does.

## 2. P1: `return_type_of` on undeduced `auto` hangs forever

```cpp
__macro rk() {
  auto r = std::meta::return_type_of(std::meta::macro_expansion_context());
  return ^^{ 0 };
}
auto f() { return rk!(); }        // compiler spins forever

auto g();                          // same without macros:
consteval { (void)return_type_of(^^g); }   // hang
```

`desugarType` (ExprConstantMeta.cpp ~1296) has
`else if (auto *AT = dyn_cast<AutoType>(QT)) QT = AT->desugar();`. For an
undeduced `AutoType`, `desugar()` returns the type itself, so the `while
(true)` loop never ends. The bug is pre-existing P2996 code, but macros make it
far easier to hit: any `try_!`/`?`-style macro that dispatches on the enclosing
function's return type hangs in every `auto` function.

**Suggest:**
- Only desugar a deduced `AutoType` (`AT->isDeduced()`), and break otherwise.
- `return_type_of` should diagnose "return type of 'f' is not yet deduced"
  instead of returning a reflection of `auto`.
- For `try_!`, one option is for `macro_expansion_context()` users to fall back
  to "the type of the first `return` seen". That's probably not worth it; a
  clear diagnostic is enough.

## 3. P1: hygiene of the macro's free names

Here is what macros.md §"Name lookup and hygiene" promises, against what
actually happens:

| Idiom | Result |
|---|---|
| `helper(\(x))` spelled bare | A later `::helper(double)`, or any closer overload at the call site, silently wins (probe `a3`: result -1). |
| `\(^^std::move)(\(x))` | "cannot take the reflection of an overload set" |
| `\(^^lib::twice)(\(x))` (function template) | "expected expression". The `annot_template_name` path in ParseExpr.cpp ~1523 requires a following `<`. |
| `\(^^lib::twice)<int>(3)` | "use of undeclared identifier 'twice'". The template-id is rebuilt with an empty scope spec and re-looked-up unqualified (ParseTemplate.cpp ~1201). |
| `\(^^lib::solo)(x)` (non-template, single) | works |
| `\(^^lib::box)<int>{3}` (class template) | works |
| `::lib::twice(x)` spelled qualified | works, but disables ADL and is still hijackable by a nested `namespace lib` at the call site (probe `c9_splice4`: "'m' is not an expression macro") |

So in practice the only hygiene is "spell it `::fully::qualified`", which
disables ADL and still isn't fully robust.

**Suggest, in increasing ambition:**
1. **Fix the reflection idioms.** Interpolating a function-template reflection
   should produce a token that the parser treats as a *resolved* template name
   (keep the `TemplateName` and don't re-look it up). Then allow
   `\(^^tmpl)(args)` with no `<…>`, deducing as for a normal call.
2. **Let overload sets be interpolated.** Add a token kind
   `annot_overload_set` carrying an `UnresolvedLookupExpr` built at
   *definition* time. `\(overloads_of(^^lib::f))`, or a new
   `^^{ \id(lib::f) }` form, would expand to a call that does definition-context
   lookup plus ADL, which is what templates do for dependent calls. That's the
   right model: a macro body is morally a template, so the two-phase lookup
   rules are the natural precedent.
3. **Definition-context default.** Tokens that come from the macro's *own*
   `^^{…}` literal (not interpolated arguments or raw params) could be looked up
   in the macro's definition context first. This is Rust's `$crate` / Scheme
   hygiene. The token already knows it came from the macro (its location is in
   the macro's body), so this is feasible, but it changes semantics: `fwd!` for
   `std::forward`-less code, `do_return`, and so on. Do it with an opt-out
   (`\unhygienic(…)`, or `__macro [[unhygienic]]`).

## 4. P1: macros calling their own helper macros

```cpp
namespace testlib {
  template <class T> __macro fwd(T&& t) { … }
  template <class T> __macro check(T&& c) {
    auto ops = operands_of(c);
    return ^^{ do { auto&& l = \(ops[0]); auto&& r = \(ops[1]);
                    (void)(fwd!(l) == fwd!(r)); } };
  }
}
void user() { int a=1, b=2; testlib::check!(a == b); }
// error: use of undeclared expression macro 'fwd'
```

Writing `testlib::fwd!` works but has the hijacking issue from §3. The
reflection form `\(^^testlib::fwd)!(…)` gives "expansion of expression macro
must form a single expression" (the `!` after an interpolated reflection isn't
recognized).

This matters: any real library (a test framework, a logging library) will
layer macros.

**Suggest:** accept `annot_reflection_of_macro` followed by `!` as a macro
invocation (`ParseMacroInvocation` should also check for a spliced/interpolated
macro reflection). That also enables generators that emit macro calls from a
`std::meta::info`.

## 5. P1: pack expansion inside token sequences

```cpp
template <class... Ts> __macro s(Ts&&... xs) { return ^^{ (\(xs) + ... + 0) }; }
// error: expression contains unexpanded parameter pack 'xs'
// error: expression macro 's<int,int>' did not produce a token sequence
template <class... A> __macro operator()(this C const&, A&&... a) { return ^^{ (0 + ... + \(a)) }; }
// same
```

The workaround works but is noisy, and it's not what anyone writes first:

```cpp
std::meta::list_builder b(^^{ , }); ((b += ^^{ \(xs) }), ...);
return ^^{ f(\(b)) };
```

**Suggest:** treat `\(pack)` inside `^^{…}` as an unexpanded pack. Then let
`...` *in the token sequence* either:

- **(a)** expand at the token level. `\(xs)...` becomes `x0, x1, x2` (P3294's
  proposed `\(xs)...`). Alternatively, `(\(xs) op ...)` just emits the
  literal fold tokens with each `\(xs_i)` spliced; since the tokens are
  re-parsed, a C++ fold expression over interpolated operands falls out
  naturally.
- **(b)** at minimum, diagnose clearly: "a parameter pack cannot be interpolated
  directly; use a fold over `^^{ \(xs) }` or `list_builder`". Also suppress the
  cascaded "did not produce a token sequence".

(a) is what makes `vec!`, `tuple_of!`, and variadic `check!` one-liners.

## 6. P1: declaration macros at block scope and in templates

### 6a. Block scope

```cpp
void f() {
  declare!(x, 42);   // parsed as an expression statement; not a decl macro
}
```

Declaration macros work only at namespace and class scope. That rules out
the most-requested statement/declaration macros:

- `defer!{ … }` / `SCOPE_EXIT`. Today you have to write `auto _ = defer!{ … };`,
  which needs a user-chosen name and so still needs gensym.
- `let!(x = expr) else { return; }` / guard-let that *declares* `x` in the
  enclosing scope. The `do -> T` + `continue` form only works when the unwrapped
  value is used immediately.
- Catch2 `SECTION("…") { … }` inside a test body.
- `TRY_ASSIGN!(auto v, expr);` (Abseil `ASSIGN_OR_RETURN`), the canonical
  status-macro.

**Suggest:** in `ParseStatementOrDeclaration`, if the statement begins with
`name!(` or `qual::name!(`, look up the name. If it's a macro whose invocation
shape is "declaration" (e.g. the macro returns tokens meant for
`queue_injection`, or is marked `__macro [[decl]]`), inject the tokens as
block-scope declarations/statements rather than parsing an expression. The
depth-0 injected-decls work in 96c8e07 is most of the machinery.

### 6b. Class templates

```cpp
template <class T> struct Box { T x; getter!(x); };
// error: macro 'getter' cannot form declarations in a dependent context; use
//        'consteval { queue_injection(...); }' to defer the injection
```

The suggested workaround works:

```cpp
consteval token_sequence getter(token_sequence n) { … }   // same body, not a macro
template <class T> struct Box { T x; consteval { queue_injection(getter(^^{x})); } };
```

Since the rewrite is mechanical and the compiler already tells the user to do
it, **the compiler should do it itself.** In a dependent class, synthesize a
`ConstevalBlockDecl` whose body calls the macro and queues the result. The
consteval-block instantiation path (SemaTemplateInstantiateDecl.cpp ~2530)
already handles re-evaluation per instantiation. This is exactly the mixin /
`derive!` / `bitflags!` use case from token-seq-*.md, and those are almost
always templates.

## 7. P2: `constant_of` on literal-type arguments

```cpp
template <class T> __macro q(T&& s) { auto r = constant_of(s); return ^^{ 0 }; }
q!(1.5);  q!(P{1,2});  q!(std::array{1,2});  q!(std::string_view{});   // ok
q!("abc");                        // error: did not produce a token sequence
q!(std::string_view("abc"));      // note: cannot query the value of an expression
                                  //       that is not a constant expression
```

Meanwhile `reflect_constant(std::string_view("abc"))` is accepted. The
`ReflectionKind::Expression` branch of `constant_of` (ExprConstantMeta.cpp
~3245) uses `E->isCXX11ConstantExpr`:

- For a string-literal glvalue of array type, that evaluates as an rvalue and
  fails.
- For a class prvalue containing a pointer to a string literal, it rejects the
  string-literal pointer.

**Suggest:**
- Evaluate via the same path `reflect_constant` uses, so both agree.
- For glvalues of array type, return a reflection of the string-literal object
  (or its `define_static_string` equivalent).

**Why it matters: the f-string macro.**

```cpp
int x = 3, y = 4;
std::println(fmt!("x={x}, y={y + 1}"));   // → std::format("x={}, y={}", x, y + 1)
```

This is the single most "wow" macro for C++ users, and it's blocked only by
this. Once `constant_of` works on literals, the macro parses the braces, calls
`tokenize()` on the pieces, and emits the `format` call (probe `c1_fstring.cpp`
has a draft). The same fix unblocks `regex!("…")` (compile-time-validated
regex), `sql!("…")`, `json!("…")`, and `units!("3 m/s")`.

## 8. P2: gensym / unique names

There's no `__COUNTER__` equivalent and no `std::meta::unique_id()`.

- **Raw params capture macro locals** (expression params don't, which is good):

  ```cpp
  __macro twice_raw(token_sequence e) {
    return ^^{ do -> int { int tmp = 100; do_return (\(e)) + tmp; } };
  }
  int tmp = 1; twice_raw!(tmp);   // 200, not 101
  ```

- **`TEST_CASE!` registration** needs a unique function name and a unique
  registrar object. Today (probe `c2_testcase`) you have to use
  `id("__test_", source_location_of(name).line())`. That breaks with two tests
  on one line, the same line in two headers, or a macro that invokes
  `test_case!` twice.

**Suggest:** add `std::meta::fresh_id(string_view hint = "") -> token_sequence`,
which returns an identifier token that is guaranteed unique in the TU and
unspellable (e.g. `__hint.N` with a character the lexer rejects). Pair it with
§3 part 3: tokens produced by `fresh_id` never collide with user names by
construction.

## 9. P2: evaluate-once semantics

- **Too strict:** `(\(c) ? \(x) : -\(x))` is rejected even though `x` is
  evaluated at most once per execution. The rule should be "at most once on any
  path". The conditional/`&&`/`||`/`if`/`switch` branches inside the expansion
  are known after parsing, so this is a CFG-lite check over the expanded AST.
  Even just treating the two arms of `?:` and the two sides of `&&`/`||` as
  alternatives would cover `abs!`, `max!`, `value_or!`, and `unwrap_or!`.
- **Too lax:** `drop!(++n)`, where the macro never interpolates its argument,
  silently discards the side effect. Warn when an expression parameter with
  side effects (`HasSideEffects`) is never interpolated, and let the macro
  author opt out (e.g. `[[maybe_unused]] T&& x`) for `debug_only!`-style
  macros that intentionally drop their argument.

## 10. P2: expression reflection API

Today: `is_binary_operation`, `operands_of`, `operator_of`, `source_text_of`,
`type_of`, `constant_of`, `is_constant_expression`, `as_lvalue`, and
`test_expression`.

To do Swift-`#expect`, Catch2-`REQUIRE`, or power-assert-style
("`a.size() == b[i] + 1` failed: 3 == 7 + 1") decomposition, you also need:

| Query | For |
|---|---|
| `is_unary_operation`, plus operand/operator | `!ok`, `*p`, `-x` |
| `is_call`, `callee_of`, `arguments_of` | `f(a, b)` → show each argument value |
| `is_member_access`, `object_of`, `member_of` | `a.size()` / `p->x` |
| `is_subscript` | `v[i]` |
| `is_conditional` | `c ? a : b` |
| `is_cast`, `cast_kind_of` | skip implicit conversions / see through `static_cast` |
| `value_category_of` / `is_lvalue` | choosing between `auto&&` and `auto` in the rebuilt expression |
| `is_parenthesized` | preserving `((a == b))` warnings |
| `strip_implicit(e)` | most of the time you want to ignore implicit casts |

With those, `expect!` can recursively rebuild the expression, binding each
subexpression once and printing every intermediate value. That's the flagship
demo for "why not just the preprocessor".

Also missing:
- `test_declaration(token_sequence)` and `test_type(token_sequence)`, as
  siblings of `test_expression`, for `derive!` probing ("does `T` already
  declare `operator==`?") and type-position fallbacks.
- `source_location_of(macro_invocation)` for *declaration* macros. For
  expression macros `std::source_location::current()` works, but a declaration
  macro has no expression to anchor to.
- `name<T>!(…)`, explicit template args at invocation, for `offset_of!<T>(x)`,
  `bit_cast!<T>(…)`, and similar macros.

## 11. P2: expansion note names the wrong token

```cpp
struct S { template <class Self> __macro m(this Self&& s) { return ^^{ \(s).nope }; } };
namespace N { __macro q(int x) { return ^^{ \(x).nope }; } }
void f(S s) { s.m!(); N::q!(1); }
// note: expanded from macro '('
// note: expanded from macro 'N'
```

Qualified invocations have the same problem (`testlib::check!` shows as macro
'testlib'). The expansion's `SourceRange` starts at the first token of the
invocation (the qualifier or the object expression), and the note spells
whatever token is at that location. Use the macro *name* location (`Loc` in
`BuildMacroCandidateExpansion`, or `NameLoc` in `ParseMemberMacroInvocation`)
as the expansion's spelling anchor.

## 12. P2: "did not produce a token sequence" hides the real error

Every failure inside the macro body collapses into this message:
- `constant_of` failing
- an unexpanded pack
- a `consteval` exception thrown from a helper
- falling off the end

Sometimes a helpful note follows (e.g. "control reached end of constexpr
function"). Other times, like `q!("abc")`, it's the only line.

**Suggest:**
- Always attach the constexpr-evaluation notes.
- When the primary cause is a Sema error *in the macro body itself* (such as
  the unexpanded pack), suppress the expansion error entirely. The body is
  already invalid.
- Special-case `std::meta::exception` so its `what()` is printed as the error
  text (as `report_tokens` does).

## 13. P3: complete-class context

```cpp
struct S {
  int f() { return m!(1); }                     // error: invoked before it is defined
  static __macro m(int x) { return ^^{ \(x) + 1 }; }
};
```

Member function bodies are complete-class contexts, so the *declaration* of
`m` is visible. The problem is that `m`'s body is also late-parsed, and `f` is
parsed first.

**Suggest:** parse macro bodies eagerly, or on demand when first invoked. The
macro body can't depend on `f`, so there's no cycle.

## 14. P3: reflection-valued macro arguments in runtime code

```cpp
void f() { int c = q!(^^int); }
// error: expressions involving consteval-only values are only allowed in
//        constant-evaluated contexts
```

The argument `^^int` is never evaluated at runtime (it's only reflected), but
the argument expression is checked as if it were. Treat macro-argument
expressions as an immediate-function context, just as the macro body is.
This also covers `m!(std::meta::info{})`, `m!(members_of(^^T, ctx))`, and
similar arguments. They're the natural way to pass "configuration" to a macro,
and the only alternative is the §10 `name<V>!(…)` form.

## 15. P3: test cleanups

- `vec-macro.pass.cpp` has `#if 0` around
  `((pushes += ^^{ …(\(xs)); }), ...);`. Flipping it to `#if 1` now compiles,
  so drop the `for (info expr : {xs...})` fallback.
- `vec![]` fails with a deep `common_type` error. It should either produce an
  error mentioning the macro ("`vec![]` requires an explicit element type") or
  support `vec!<int>[]` (see §10).

---

## Missing or under-served examples

These are the examples I'd add under `libcxx/test/std/experimental/reflection/`,
roughly in order of persuasive value. The ✅ ones are writable today, so we
should add them as tests to lock them in.

| Example | Status | Blocked on |
|---|---|---|
| `fmt!("x={x}, y={y+1}")` f-string | ❌ | §7 |
| `expect!(a.size() == b[i] + 1)`, power assert with every subexpression shown | ❌ | §10 |
| `check!`/`REQUIRE!` from a namespaced test library | ❌ | §4 |
| `TEST_CASE!("name") { … }` with auto-registration | ⚠️ line-based names | §8 |
| `defer!{ … }` / `scope_exit` without naming | ❌ | §6a, §8 |
| `ASSIGN_OR_RETURN!(auto v, expr)` | ❌ | §6a |
| `unwrap_or_continue!(maybe(i))` in a loop | ✅ (probe `c3_guard`) | — |
| `for_each_field!(obj, fn)`, an unrolled per-member call | ✅ (probe `c12_stmt`) | — |
| `getter!(x)` / `derive!(Eq, Hash)` mixin in a **class template** | ❌ | §6b |
| `bitflags!(Perms { R = 1, W = 2, X = 4 })` generating an enum plus operators | ✅ at namespace scope (not tested yet) | — |
| `try_!` in an `auto` function | ❌ hangs | §2 |
| `tuple_of!(xs...)` / `vec!(xs...)` as one-liners | ⚠️ `list_builder` only | §5 |
| `regex!("…")` compile-time validated | ❌ | §7 |
| `matches!(v, 1 | 2 | 3)` | ✅ via raw param + `tokens_of` splitting (write it) | — |
| `dbg!(expr)` that prints `file:line: expr = value` and returns the value | ✅ via `source_location::current()` (write it) | — |
| `offset_of!(T, member.sub[3])` | ⚠️ raw-param form works; `offset_of!<T>(…)` needs §10 | — |
| `apply!(twice, 21)`, higher-order | ✅ (probe `c11_hof`) | — |
| `std::source_location`-accurate `log!` at declaration scope | ❌ | §10 (decl-macro location) |

---

## Suggested order of work

1. §1 depth limit and §2 `desugarType` hang (small, both crash-class).
2. §11 note spelling (small).
3. §7 `constant_of` on literals, then write the `fmt!` test. It's the demo.
4. §5 pack expansion in token sequences (at least the good diagnostic).
5. §6b auto-deferral in class templates (mechanical given consteval blocks).
6. §8 `fresh_id`, then §6a block-scope declaration macros.
7. §3/§4 hygiene: fix the reflection-interpolation parsing bugs first, then
   decide on the definition-context-lookup design.
8. §10 expression API, then the `expect!` power-assert test.
