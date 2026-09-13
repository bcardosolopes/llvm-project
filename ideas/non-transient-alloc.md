This is a design for non-transient allocation. Please first read the following papers:

* P0784R5: https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p0784r5.html
* P1974R0: https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/p1974r0.pdf
* P1974R1: https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2026/p1974r1.pdf
* P2670R0: https://brevzin.github.io/cpp_proposals/2670_constexpr_allocation/p2670r0.html
* P2670R1: https://brevzin.github.io/cpp_proposals/2670_constexpr_allocation/p2670r1.html
* my blog: https://brevzin.github.io/c++/2024/07/24/constexpr-alloc/

My goal is to solve the non-transient constexpr allocation problem, along the lines of P0784R5, taking into account all the insights from Jeff Snyder.

The design I want to propose is:

* An allocation `A` that survives initialization of a constexpr variable `V` may persist if hypothetical constant evaluation of `V`’s destruction deallocates `A` and all other surviving allocations and leaves no allocation behind.
* At the end of `V`'s initialization, each surviving allocation `A` is classified as either **reachable as mutable** or not: `A` is reachable as mutable if the end-of-initialization state of `V` contains a pointer or reference to `A` (or to any object or subobject within `A`, stored in `V` itself or in any surviving allocation) whose declared pointee/referent type is not const-qualified, or is a class type with a `mutable` member. This classification is fixed once, against the end-of-initialization state; the evolving state during the synthesized destruction does not affect it. During that destruction, an object stored in `A` may not be read unless `A` is not reachable as mutable, or `A` has been marked immutable before the read.
* `std::mark_immutable_if_constexpr(A)` may be called during hypothetical destruction, including from recursively invoked destructors. It asserts that `A` was immutable from the end of `V`’s initialization onward, and permits subsequent destruction-time reads despite apparent mutable reachability. This marking can precede reading the allocation within the same destruction of `V` — such a read would then be allowed. `std::mark_immutable_if_constexpr(p)` can be called on a pointer that does not point into an allocation — such a call is a no-op.
* Writes to an allocation `A` marked immutable are allowed during the synthesized destruction of `V`. But outside of that destruction, such a write would fail constant evaluation. The persistent contents are what was there at the end-of-initialization state — it doesn't matter what actually happened during destruction (once we validate that it's constant and that all allocations would have been freed).
* A successfully persisted allocation retains its identity and is promoted to static storage; no destructor is invoked at runtime. Pointers and references into such storage are usable as constant template parameters. The identity rule is a function of `(V, allocation index, offset)` and would require `V` to have linkage. This would probably require an ABI extension. The contents of each persisted allocation are subject to the same permitted-result constraints as the value of `V` itself (no dangling pointers, no pointers to consteval-only entities, etc.). Any violation renders the initialization non-constant.
* A persisted allocation has the storage duration of `V`. An unmarked allocation may persist only if `V` has static storage duration. A pointer (or reference) into a persisted allocation is usable as a constant template parameter only if `V` has linkage.
* The contents of a persistent allocation are usable in later constant expressions only if the allocation was marked immutable. Mutating a marked allocation, or independently deallocating any persistent allocation, is undefined behavior.
* An allocation persisted by a `consteval` variable is never emitted: pointers and references into it are consteval-only values. This applies to all runtime escape routes, including use as a constant template parameter of a specialization that is itself used at runtime.


Notably:

* `std::unique_ptr<T>` will mark its allocation immutable only if `remove_extent_t<T>` is `const`
* `std::vector<T>` and `std::string` will always mark their allocations immutable.

Note that we do not attempt to validate sound-ness in the way that P1974R0 originally proposed. We simply trust the user to mark appropriately. I think this is the right approach because any attempt to prove soundness involves a fairly invasive language change that will be expensive to check anyway — this is a narrow check that I think additionally is easy for humans to ensure that they use correctly.

Note also that an immutable mark doesn't necessarily mean the allocation goes into `.rodata`. A `vector<Cached>` where `Cached` has a `mutable int hits;` could still have its allocation validly marked immutable, that allocation could persist (just not in `.rodata`), and writing to `v[0].hits` could be valid behavior at runtime — just reading `v[0].hits` would never be constant (as reading `mutable` data never is).

Note _also_ that this design is not free from undefined behavior. Element types can launder mutable access to themselves through constant paths (i.e. interior non-const pointers). Such allocations would end up in `.rodata` anyway, and there is no way to verify/validate this short of Jeff's `propconst` model — so any mutations through such a pointer (or reference) would just have to be undefined behavior. Don't do that.

Here are some examples to go through.

Ex 1: This is still ill-formed, because `p` leaks memory.

```cpp
constexpr int* p = new int(1); // error
```

Ex 2: The allocation is fine and persists, but we can't read through it as constant:

```cpp
constexpr unique_ptr<int> p(new int(2)); // ok
static_assert(*p == 2); // error
void bump() { ++*p; } // ok! mutable at runtime
```

Ex 3: Now, we can read through it as constant, because it will have been marked:

```cpp
constexpr unique_ptr<int const> p(new int(3)); // ok
static_assert(*p == 3); // ok
```

Ex 4: This is not okay, because destroying the outer `unique_ptr` must read the inner `unique_ptr` object and that allocation is reachable as mutable:

```cpp
constexpr unique_ptr<unique_ptr<int>> p(new unique_ptr<int>(new int(4))); // error
```

Ex 5: But this recursion works fine:

```cpp
constexpr vector<string> v = {"this", "is", "so", "cool"};
static_assert(v[1] == "is");
```

Ex 6: Marking the inner `unique_ptr const` is fine, it allows the allocation to persist, but the innermost `int` read isn't constant.

```cpp
constexpr unique_ptr<unique_ptr<int> const> p(
    new unique_ptr<int> const(new int(6)));    // ok
static_assert((*p).get() != nullptr);          // ok: outer allocation marked
static_assert(**p == 6);                       // error: inner allocation unmarked
int& r = **p;                                  // ok: and mutable at runtime!
```

Ex 7: Here is a mixed example:

```cpp
constexpr auto v = []{
    vector<unique_ptr<int>> v;
    v.push_back(make_unique<int>(1));
    v.push_back(make_unique<int>(2));
    return v;
}();                                    // ok
static_assert(v.size() == 2);           // ok: buffer marked
static_assert(v[0] != nullptr);         // ok: the unique_ptr objects are readable
static_assert(*v[0] == 1);              // error: pointees unmarked
void f() { *v[1] = 20; }                // ok: pointees runtime-mutable
```

Ex 8: `std::map` would work fine:

```cpp
constexpr std::map<std::string_view, int> m = {{"one",1},{"two",2}};
static_assert(m.at("two") == 2);   // ok: every node allocation marked during the walk
```

Ex 9: `std::shared_ptr` could not work because of the control block (the destructor must read the control block), so it could not be marked, even for `const`:

```cpp
constexpr shared_ptr<int const> sp = make_shared<int>(1);  // error, and rightly so
```

Ex 10: Constant template parameter usage is not content based.

```cpp
template <int const* P> struct X { };
constexpr vector<int> v1 = {1, 2, 3};
constexpr vector<int> v2 = {1, 2, 3};

X<v1.data()> x1; // ok
X<v2.data()> x2; // ok
static_assert(type_of(^^x1) != type_of(^^x2)); // ok: these have different types
```

Ex 11: Persisted allocations are permitted results:

```cpp
constexpr string s = "Some sufficiently long string as to definitely allocate";
constexpr string_view sv = s; // ok
```

---

## Implementation notes (Clang, this branch)

Things a second implementer would want to know; discovered while building it.

* **The allocation type must be recorded as the complete (array) type.** The
  evaluator's `DynAlloc` originally only kept the allocating expression;
  recovering the type from a `CXXNewExpr` yields the *element* type for array
  new (and nothing for `std::allocator` allocations), which breaks
  `unique_ptr<const int[]>` and `vector` persistence. We now store the
  allocation's complete type in `DynAlloc` at `createHeapAlloc` time.
* **The owning variable's destructor must be marked referenced before the
  initializer is evaluated.** Evaluating the initializer may run the
  hypothetical destruction, which needs the destructor (and everything it
  calls) defined. Sema marks it in `CheckCompleteVariableDeclaration` before
  the constant-init check; `FinalizeVarWithDestructor` later re-marks it
  harmlessly.
* **The synthesized destruction runs against a discarded copy.** A fresh
  `EvalInfo` is seeded with copies of the surviving allocations (preserving
  allocation indices so stored pointers resolve); its writes affect nothing.
  The persisted image is the end-of-initialization state.
* **`VarDecl::evaluateDestruction` short-circuits for persisting variables**
  (the destruction was already validated pre-persistence, and no destructor
  may run at runtime). This is keyed off an ASTContext side table
  (`VarsWithPersistentAllocs`).
* **PCH/modules: the side tables must be repopulated at deserialization.**
  `PersistentAllocDecl` registration happens in the ASTReader visit;
  `getCanonicalDecl()` consults the registry so a PCH'd decl and a freshly
  minted one for the same (owner, index) unify. Without this, a use TU
  re-evaluates the owner's initializer, mints duplicate decls, and template
  specializations silently split.
* **Mangling** (vendor extension): a `PersistentAllocDecl` mangles as the
  source-name `__nta_<mangled-owner>_<index>`, and CodeGen emits its storage
  as `<owner>.__nta_<index>` with the owner's linkage (linkonce_odr+comdat
  for external owners, internal otherwise). Verified: two TUs instantiating
  `f<&*shared>` produce one weak specialization symbol.
* **The long tail of decl-kind switches.** New `ValueDecl` kinds need cases
  in: `ExprClassification` (lvalue-ness), `CheckAddressOfOperand`'s
  known-decl assert, `CGExpr`'s DeclRefExpr emission, linkage computation in
  `Decl.cpp` (owner's linkage), `IsGlobalLValue`, `findCompleteObject`
  (read/write permissions live here), plus serialization and
  `RecursiveASTVisitor`/`TemplateDeclInstantiator` stubs.
* **Not implemented**: the new constant interpreter
  (`-fexperimental-new-constant-interpreter`) has no NTA support; `std::map`
  (Ex 8) awaits constexpr node containers in libc++; `shared_ptr` (Ex 9) is
  rejected by construction but untestable until it is constexpr.
