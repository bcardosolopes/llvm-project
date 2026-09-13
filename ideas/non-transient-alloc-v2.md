# Non-transient allocation, v2: `immutable_if_constexpr`

This is a revision of the design in [non-transient-alloc.md](non-transient-alloc.md).
Read that first; the persistence machinery (hypothetical destruction, identity,
linkage, consteval-only escapes) carries over unchanged. What changes is the
blessing mechanism: instead of an operational call —
`std::mark_immutable_if_constexpr(p)` executed during the synthesized
destruction — immutability is declared structurally, on the data members
through which allocations are reached.

The prior model blesses *allocations* (a mark waives the reachable-as-mutable
check for the whole allocation, no matter who points into it). This model
blesses *paths*: each pointer- or reference-valued member declares whether
runtime mutation can happen through it. The compiler then classifies each
allocation from the actual end-of-initialization value graph. The result
catches strictly more misuses (interior pointers far removed from the
allocating type become hard errors instead of silent `.rodata` timebombs),
permits the same uses, and eliminates the mark-ordering lore that container
destructors currently have to know.

## The specifier

```
immutable_if_constexpr             // means immutable_if_constexpr(true)
immutable_if_constexpr(cond)      // cond: constant expression, contextually
                                   // converted to bool; may be value- or
                                   // type-dependent in a template
```

A *decl-specifier* that may appear only on the declaration of a **non-static,
non-`mutable` data member**. Anywhere else (variables, function parameters,
static members, `mutable` members, bit-fields of non-pointer type, function
declarations) it is ill-formed. It has **no effect on the member's type**: no
new qualifier, no change to overload resolution, conversions, mangling of the
enclosing class, or template argument deduction. `decltype(m)` is unaffected.

In a class template, a dependent `cond` is evaluated when the member's
declaration is instantiated. A member of a class template specialization is
an immutable-if-constexpr member iff its instantiated `cond` is `true`.

Semantically, `immutable_if_constexpr` on member `m` is a declaration by the
class author that:

> No runtime write ever occurs through a pointer or reference derived from
> the value stored in `m` (or in any subobject of `m`, or in any allocation
> owned through `m`) after the initialization of an enclosing constexpr
> variable completes.

This is a *promise*, with the same trust semantics the mark had: the
compiler consults it during classification (below) but does not — cannot —
verify it against the class's interface. Breaking the promise (e.g. a `const`
member function that returns mutable access into `m`'s allocation, later
written through at runtime) is undefined behavior, exactly as mutating a
marked allocation is UB today. What the promise buys: the allocation may be
placed in read-only storage and its contents become usable in later constant
expressions.

Expected library usage:

```cpp
template <class T, class Allocator>
class vector {
  immutable_if_constexpr pointer __begin_;   // deep-const container:
  immutable_if_constexpr pointer __end_;     // unconditional
  immutable_if_constexpr pointer __cap_;
};

template <class T, class D>
class unique_ptr {
  // shallow-const handle: blessed only when the element type is const
  immutable_if_constexpr(is_const_v<remove_extent_t<T>>) pointer __ptr_;
};

class basic_string {
  struct __long {
    immutable_if_constexpr pointer __data_;  // union active-member handles SSO
    ...
  };
};
```

Note what is *absent*: no destructor changes, no `__is_long()` guard, no
mark-before-read ordering. The instance-conditionality that `string` needed a
runtime guard for (SSO vs long) falls out of the classification walk for free:
a short string simply contributes no pointer-into-allocation through
`__long::__data_`, because that union member isn't active.

## Classification rules

Everything happens at one moment: **the end of initialization of the
constexpr variable `V`**, against the frozen value graph, before the
hypothetical destruction runs. (This is already how the reachable-as-mutable
classification works in v1; the specifier slots into that same walk.)

For each surviving allocation `A`, the walk visits every pointer and
reference into `A` (or into any subobject of an object in `A`) stored
anywhere in the graph — in `V` itself or in any surviving allocation — and
classifies each such *path* as **blessed** or **unblessed**:

* A path is **blessed** if the pointer/reference is stored in (a subobject
  of) a data member declared `immutable_if_constexpr` with a true condition,
  **or** if any member on the containment chain from `V` down to the pointer
  is so declared (blessing distributes downward; see "Composition" below),
  **or** if the pointer/reference's declared pointee/referent type is
  const-qualified and not a class type with `mutable` members.
* Otherwise the path is **unblessed**. A pointer stored directly in an
  allocation with no member declaration on its containment chain (e.g. an
  element of a `vector<int*>`'s buffer) is classified by its type alone:
  const pointee → blessed, otherwise unblessed.

Then `A` itself is classified:

| Allocated object type | Paths into `A` | Result |
|---|---|---|
| const (and no `mutable` members) | all necessarily blessed | **immutable** — inferred; no declaration needed anywhere |
| non-const | all blessed, ≥ 1 via an `immutable_if_constexpr` member | **immutable** — declared |
| non-const | all blessed, but only via const-typed paths | **mutable persistence** (see below) |
| non-const | some unblessed, none via `immutable_if_constexpr` | **mutable persistence** |
| non-const | mixed: some path via `immutable_if_constexpr`, some unblessed | **ill-formed** — conflicting declared intent |

Rationale for the corners:

* Row 1: `new int const(1)` — the object itself is const; writes through any
  laundering are UB by the object's own constness, so immutability is a
  theorem, not a promise. No blessing needed. (v1 gets this wrong today: it
  demands a mark even for const-allocated objects.)
* Row 3: `new int(1)` viewed only through `int const*` paths — the *paths*
  are const but the *object* is not; `const_cast` + write is a **legal**
  runtime operation. Inferring immutability would put legitimately-writable
  data in `.rodata`. So constness of paths alone never promotes; promotion to
  immutable requires either object constness (row 1) or an explicit
  declaration (row 2). Freezing is always either a theorem or a stated
  promise — never an inference.
* Row 5 is the payoff. In v1 this case silently persists as immutable (the
  mark waives the whole allocation) and the stray pointer is a runtime
  timebomb:

  ```cpp
  struct S { vector<int> v; int* p; };
  constexpr S s = []{
    vector<int> v = {1, 2, 3};
    int* p = v.data();
    return S{std::move(v), p};
  }();               // v1: ok, buffer → .rodata, *s.p = 42 segfaults at runtime
                     // v2: ill-formed — S::p is an unblessed path into an
                     //     allocation that vector's members declare immutable
  ```

  The fix is spelled in the diagnostic: declare `S::p` as `int const*`, or
  declare it `immutable_if_constexpr` (taking on the promise), or don't
  store it.

The distance of the offending pointer from the allocating type is
irrelevant: the walk sees values, not scopes. A lambda capture, a member of
a third-party aggregate, an element of a sibling allocation — every path is
found and judged. What this model deliberately does **not** catch is a *lying
class*: a type whose `immutable_if_constexpr` member is handed out mutably by
its own const interface. That residue is local to one class definition,
reviewable by its author, and mechanically lintable ("const member function
returns non-const access derived from an immutable_if_constexpr member") —
whereas the global property (who, anywhere, points into this allocation) is
what the compiler now proves. propconst would catch the lying class too, at
the cost of a type-system qualifier and its adoption cascade; see the
comparison at the end.

## Composition: blessing distributes through members

The specifier may be applied to a member of **class type** (not just
pointers), meaning: every pointer/reference contributed by this member's
value — its subobjects, recursively, and the contents of allocations owned
through it — is blessed along this containment path. This is the deep-const
adapter that neither v1 nor propconst can express declaratively:

```cpp
class C {
  immutable_if_constexpr unique_ptr<int> p_;   // C is deep-const even though
                                                // unique_ptr<int> is not
public:
  constexpr C(int i) : p_(new int(i)) {}
  constexpr auto get() -> int&             { return *p_; }
  constexpr auto get() const -> int const& { return *p_; }
};

constexpr auto c = C(2);
static_assert(c.get() == 2);   // ok: the int allocation is blessed via C::p_,
                               // overriding unique_ptr<int>'s own
                               // (false-conditioned) declaration on __ptr_
```

Under v1, `C` must either declare a destructor solely to call the mark
(knowing to call it *before* any read), or switch storage to
`unique_ptr<int const>` and `const_cast` in the non-const accessor. Under
propconst, `C` must abandon `unique_ptr` for a propconst-qualified raw
pointer or a parallel smart-pointer vocabulary type. Here, the deep-const
claim is written by `C`'s author, on `C`'s member, at the single place where
it is true.

Precedence rules:

* Blessing anywhere on the containment chain suffices (outermost wins; it is
  a ∨ over the chain). There is deliberately no "unbless": like `const`,
  the property only accumulates. `immutable_if_constexpr(false)` means "this
  declaration does not bless" — it does not veto an ancestor's blessing.
* The blessing follows the containment chain **through owned allocations**:
  `immutable_if_constexpr map<K,V> m_;` blesses the node allocations and
  everything they in turn own along that chain. (This makes the specifier on
  a class-type member strictly stronger than making the member const-typed,
  and is what "this member is deep-const" must mean for containers of
  containers.)

## Reads during hypothetical destruction

The v1 read rule — during the synthesized destruction of `V`, an object in a
surviving allocation may not be read unless the allocation is not reachable
as mutable or has been marked immutable *before the read* — simplifies. There
is no longer a during-destruction event: classification is complete before
destruction starts, so

> during the hypothetical destruction of `V`, an object in a surviving
> allocation may be read iff the allocation was classified **immutable**
> (rows 1–2), and may be written unconditionally (the destruction runs
> against a discarded copy).

`vector<string>`'s destructor illustrates why this is a simplification and
not a restriction. `clear()` reads each string's rep out of the vector's
buffer allocation; in v1 this works only because the mark call is sequenced
before `clear()` in `__destroy_vector`. In v2 the buffer is classified
immutable up front (via `__begin_`), so the reads pass regardless of
destructor statement order. The copy-members-to-locals-then-null destructor
idiom also just works, for the same reason it does in v1: classification is
fixed against the end-of-initialization state, so nothing the destructor does
to the members can change it. Likewise, destructors that copy the pointers
along (`destroy(begin_, end_)`) track correctly for free: the read check
keys on *allocation identity*, which pointer copies preserve — the specifier
never travels with values and never needs to.

One consequence worth stating: an allocation that persists **mutable**
(rows 3–4) still may not be read during the destruction. So mutable
persistence remains restricted to element types whose destruction reads
nothing (trivially destructible contents, or pointees held by handles whose
destructor only deallocates). `unique_ptr<unique_ptr<int>>` stays ill-formed
(Ex 4 of v1) with an unchanged rationale: destroying the outer handle must
read the inner `unique_ptr` object, which lives in an allocation classified
mutable.

## What carries over from v1 unchanged

* Persistence condition: hypothetical constant destruction of `V` deallocates
  every surviving allocation and leaks nothing. Only attempted for variables
  with nontrivial constexpr destructors.
* The persisted image is the end-of-initialization state; destruction-time
  writes are discarded.
* Identity `(V, allocation index)`, owner's linkage, static-storage
  promotion, no runtime destructor, permitted-result checks on the persisted
  contents (with uninitialized subobjects permitted — spare `vector` capacity
  — reads of raw storage diagnosed at read time).
* Mutable persistence requires `V` to have static storage duration.
  Immutable allocations may be shared (like string literals).
* Contents of immutable allocations are usable in later constant
  expressions; contents of mutable ones are not. Runtime writes to immutable
  allocations, and any independent deallocation, are UB.
* `mutable` members inside an allocation keep their v1 semantics: they don't
  prevent persistence, but they force the allocation out of `.rodata` and
  reads of them are never constant.
* consteval variables: persisted allocations have consteval-only address;
  all runtime escape routes (including via template-argument identity) are
  ill-formed.
* Constant-template-parameter identity is by `(V, index, offset)`, not
  content (Ex 10 of v1).

Under this model `std::mark_immutable_if_constexpr` is no longer needed by
any type in our libc++ (`vector`, `string`, `unique_ptr` all convert to the
specifier as shown above, deleting their destructor marks and `string`'s
`__is_long()` guard). Whether to keep the builtin at all is an open question
below.

## Examples (v1 set, revisited)

Same numbering as v1; behavior identical except where noted.

```cpp
// Ex 1: unchanged — leaks, ill-formed.
constexpr int* p = new int(1);                       // error

// Ex 2: unchanged — persists mutable (row 4: path unblessed, since
// unique_ptr<int>'s condition is false). No constant reads; runtime-mutable.
constexpr unique_ptr<int> p(new int(2));             // ok
static_assert(*p == 2);                              // error
void bump() { ++*p; }                                // ok

// Ex 3: unchanged result, different derivation: row 2 via the
// true-conditioned __ptr_ declaration. (Had the user written
// new int const(3), it would be row 1 and need no declaration at all —
// an improvement over v1, which demands a mark even then.)
constexpr unique_ptr<int const> p(new int(3));       // ok
static_assert(*p == 3);                              // ok

// Ex 4: unchanged — inner allocation classified mutable, and destroying the
// outer handle must read it.
constexpr unique_ptr<unique_ptr<int>> p(
    new unique_ptr<int>(new int(4)));                // error

// Ex 5: unchanged, minus the ordering lore: buffer and char allocations all
// classified immutable up front via __begin_ / __long::__data_.
constexpr vector<string> v = {"this", "is", "so", "cool"};
static_assert(v[1] == "is");                         // ok

// Ex 6: unchanged. Outer allocation: row 2 (unique_ptr<T const>'s __ptr_
// condition true). Inner int allocation: row 4, runtime-mutable.
constexpr unique_ptr<unique_ptr<int> const> p(
    new unique_ptr<int> const(new int(6)));          // ok
static_assert((*p).get() != nullptr);                // ok
static_assert(**p == 6);                             // error
int& r = **p;                                        // ok at runtime

// Ex 7: unchanged — the mixed case the path model was designed to keep.
// Buffer: immutable via vector::__begin_. Pointee allocations: reached via
// the element unique_ptr<int> objects' __ptr_ (condition false) → mutable.
constexpr auto v = []{
    vector<unique_ptr<int>> v;
    v.push_back(make_unique<int>(1));
    v.push_back(make_unique<int>(2));
    return v;
}();                                                 // ok
static_assert(v.size() == 2);                        // ok
static_assert(v[0] != nullptr);                      // ok
static_assert(*v[0] == 1);                           // error
void f() { *v[1] = 20; }                             // ok

// Ex 8–11: unchanged (map via specifiers on its node types' link members;
// shared_ptr still impossible — the runtime destructor must *write* the
// control block, so no honest declaration exists for it).
```

New examples specific to v2:

```cpp
// Ex 12: the interior-pointer hole, now closed (row 5). See S above.

// Ex 12b: the same hole with the mutable path stored INSIDE the allocation —
// a self-referential element type. Under v1 this compiles, the buffer goes
// to .rodata, and `++bs[0].p[0]` at runtime crashes. Under v2, B::p is an
// unannotated int* into the (vector-blessed) buffer allocation: row 5,
// ill-formed at bs's declaration. The walk sees pointers stored in
// allocations, not just in V, so element types can't smuggle paths.
struct B {
  int i;
  int* p;
  constexpr B(int i) : i(i), p(&this->i) {}
  constexpr B(B const& rhs) : i(rhs.i), p(&i) {}
};
constexpr std::vector<B> bs = {1, 2, 3};   // v1: ok (timebomb). v2: error.

// Ex 13: deep-const adapter. See C above.

// Ex 14: the residual trust hole (the "lying class") — unchanged from v1 in
// kind, but now local and lintable:
struct Liar {
  immutable_if_constexpr int* p;
  constexpr Liar(int v) : p(new int(v)) {}
  constexpr ~Liar() { delete p; }
  constexpr int* get() const { return p; }   // hands out mutable access!
};
constexpr Liar l(1);                          // ok: promise accepted
int main() { *l.get() = 2; }                  // compiles; UB (writes .rodata)
// propconst would reject get()'s body at Liar's definition. We accept the
// hole in exchange for zero type-system footprint, and lint for the pattern.

// Ex 15: pointers in allocation storage have no member declaration; their
// type decides (const pointee = blessed, else unblessed):
constexpr auto vp = []{
    vector<int const*> v;                     // elements blessed by type
    v.push_back(new int const(1));            // pointee: row 1, immutable
    return v;
}();                                          // ok... except the int const
                                              // allocation leaks: no one
                                              // deallocates it in the dtor,
                                              // so this is ill-formed by the
                                              // persistence condition, not
                                              // by classification. Use an
                                              // owning element type.
```

## Comparison summary

| | propconst (P1974) | mark (v1) | `immutable_if_constexpr` (v2) |
|---|---|---|---|
| mechanism | type qualifier, per-access enforcement | call during destruction, per-allocation waiver | declaration on members, per-path classification at end of init |
| interior pointer far from container (`S::p`) | rejected (type error at the store) | **silently accepted** → runtime UB | rejected (row 5, at `V`'s declaration) |
| lying class (`Liar::get`) | rejected at class definition | accepted (UB) | accepted (UB), lintable |
| deep-const adapter (`C` over `unique_ptr<int>`) | change storage type / parallel vocabulary types | destructor + mark + ordering lore | one specifier on `C::p_` |
| `vector<unique_ptr<int>>` mixed mutability | expressible, needs propconst plumbed through | works (mark buffer only) | works (row-per-allocation) |
| instance-conditional immutability (runtime-flag freeze) | no | yes (mark is a call) | no — type-level conditions only; union active-member covers `string` |
| fancy pointers (`allocator_traits<A>::pointer` class types) | needs propconst adoption inside them | works (mark takes a raw address) | **open question** — see below |
| type-system footprint | qualifier: mangling, deduction, conversions, ecosystem cascade | none | none (specifier, not part of the type) |
| `const_cast` + write through blessed path | UB (both models converge here) | UB | UB |

## Decided

* **Specifier, not attribute** (it affects program validity and object-file
  placement, which sits badly with ignorable attributes). Grammar slot: the
  decl-specifier-seq of a member-declaration, alongside `mutable` — with
  which it is mutually exclusive.
* **References allowed.** `immutable_if_constexpr T& m;` follows the same
  rules; references and pointers are the same kind of path.
* **Diagnostics carry the path.** Row-5 errors name the member chain:
  "allocation persisted by 's' is reachable as mutable through 'S::p';
  declare it 'int const*' or 'immutable_if_constexpr'". The classification
  walk returns a path description, not a bool.
* **`[[no_unique_address]]`, anonymous unions/structs, bases, bit-fields**
  need tests, not design — the member-wise walk already handles them.

## Open questions

1. **Fancy pointers.** Classification attributes a pointer to the member
   holding it; a class-type `pointer` stores its address bits in *its own*
   members, one level removed from `vector::__begin_`. The composition rule
   ("blessing distributes through subobjects") handles this: an
   `immutable_if_constexpr offset_ptr<T> __begin_;` blesses the raw pointer
   inside the `offset_ptr` subobject. What it cannot handle is an offset-based
   representation storing something the walk can't recognize as designating
   the allocation at all. Possibly the answer is: representations the
   evaluator can't trace already can't be constexpr allocators, so the
   problem excludes itself. If a genuine case emerges, retain
   `mark_immutable_if_constexpr` as the operational escape hatch (blessing by
   address, not by declaration) — otherwise deprecate it to a no-op.
2. **Migration.** Removing the blanket mark-waiver is a hard break for
   unannotated code that persists today. Library conversion (three types in
   libc++) lands in the same change as the rule switch. Out-of-tree users of
   the builtin get a deprecation period where a mark is treated as blessing
   every path into the marked allocation (i.e. v1 semantics per-allocation),
   before it becomes a no-op or is removed.

## Implementation sketch (Clang, this branch)

Small delta over the v1 machinery, which already does the hard part:

* Parse the specifier into a bit + optional condition expr on `FieldDecl`
  (instantiate the condition with the class; store the evaluated bit on the
  instantiated field). Serialization: one bit + one optional Stmt.
* `valueReferencesAllocAsMutable` ([ExprConstant.cpp:4848](../clang/lib/AST/ExprConstant.cpp))
  already recurses member-decl-wise with the `FieldDecl` in hand at exactly
  the right spot; it grows: (a) consult the field's bit, (b) thread a
  "blessed-by-ancestor" flag down the recursion (and through
  `DynamicAllocLValue` contents for the ownership-chain rule), (c) return a
  path description instead of bool, (d) apply the row-1 const-allocated-type
  inference (the complete `AllocType` is already stored on `DynAlloc`).
* The classification pass (ExprConstant.cpp:23208) computes the five-row
  verdict per allocation before `HandleDestruction`; the read check
  (ExprConstant.cpp:5168) keys off the precomputed verdict — the
  `ImmutableAllocs` grows-during-destruction set is deleted (or retained
  solely for the deprecated builtin during migration).
* libc++: add the specifier to `vector`'s three pointers,
  `basic_string::__long::__data_`, `unique_ptr`'s `__ptr_` (conditioned);
  delete the destructor marks and `__destroy_vector`'s ordering comment.
* Tests: v1 suites should pass with only Ex-2/3-style diagnostics reworded;
  new tests for rows 1/3/5, the `S` error, the `C` adapter, `Liar` lint
  (if implemented), conditional-specifier instantiation, and union/SSO.
