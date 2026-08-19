// RUN: %clang_cc1 -std=c++2d -verify %s
// RUN: %clang_cc1 -std=c++2c -verify=cxx26 %s

// P4341 v2 (non-transient constexpr allocation), Sema/constant-evaluator
// side. See ideas/non-transient-alloc-v2.md. Under C++26, nothing persists
// and the classic diagnostics apply (immutable_if_constexpr is not a keyword
// there, so the test type spells it via a macro). Under C++2d, allocations
// may persist when the variable's hypothetical constant destruction
// deallocates them; immutability is declared structurally on data members.

#if __cplusplus > 202400L
#define IIC immutable_if_constexpr
#define IIC_IF(...) immutable_if_constexpr(__VA_ARGS__)
#else
#define IIC
#define IIC_IF(...)
#endif

template <class T>
struct uptr {
  IIC_IF(__is_const(T)) T* p;
  constexpr uptr(T* p) : p(p) {}
  uptr(const uptr&) = delete;
  constexpr ~uptr() {
    delete p; // #uptr-dtor-delete
  }
  constexpr T& operator*() const { return *p; } // #uptr-deref
  constexpr T* get() const { return p; }
};

// ==== Ex 1: a bare pointer leaks; no destructor deallocates it ====
constexpr int* e1 = new int(1); // #decl-e1
// expected-error@#decl-e1 {{must be initialized by a constant expression}}
// expected-note@#decl-e1 {{pointer to heap-allocated object is not a constant expression}}
// expected-note@#decl-e1 {{heap allocation performed here}}
// cxx26-error@#decl-e1 {{must be initialized by a constant expression}}
// cxx26-note@#decl-e1 {{pointer to heap-allocated object is not a constant expression}}
// cxx26-note@#decl-e1 {{heap allocation performed here}}

// ==== Ex 2: persists mutable; not constant-readable; runtime-mutable ====
constexpr uptr<int> e2(new int(2)); // #decl-e2
// cxx26-error@#decl-e2 {{must be initialized by a constant expression}}
// cxx26-note@#decl-e2 {{pointer to heap-allocated object is not a constant expression}}
// cxx26-note@#decl-e2 {{heap allocation performed here}}
static_assert(*e2 == 2); // #read-e2
// expected-error@#read-e2 {{static assertion expression is not an integral constant expression}}
// expected-note@#uptr-deref {{read of object in a persistent allocation that is not immutable (its holders are not declared 'immutable_if_constexpr')}}
// expected-note@#read-e2 {{in call}}
// cxx26-error@#read-e2 {{static assertion expression is not an integral constant expression}}
// cxx26-note@#read-e2 {{initializer of 'e2' is not a constant expression}}
// cxx26-note@#decl-e2 {{declared here}}
void bump() { ++*e2; } // ok: mutable at runtime

// ==== Ex 3: blessed via the conditional specifier: constant-readable ====
constexpr uptr<int const> e3(new int(3)); // #decl-e3
// cxx26-error@#decl-e3 {{must be initialized by a constant expression}}
// cxx26-note@#decl-e3 {{pointer to heap-allocated object is not a constant expression}}
// cxx26-note@#decl-e3 {{heap allocation performed here}}
static_assert(*e3 == 3); // #read-e3
// cxx26-error@#read-e3 {{static assertion expression is not an integral constant expression}}
// cxx26-note@#read-e3 {{initializer of 'e3' is not a constant expression}}
// cxx26-note@#decl-e3 {{declared here}}

// ==== Row 1: const-allocated object needs no blessing at all ====
#if __cplusplus > 202400L
struct rawc {
  int const* p; // unannotated const path over a const-allocated object
  constexpr rawc(int v) : p(new int const(v)) {}
  rawc(const rawc&) = delete;
  constexpr ~rawc() { delete p; }
};
constexpr rawc rc(7);
static_assert(*rc.p == 7); // ok: immutability inferred from the object type
#endif

// ==== Ex 4: mutable allocation, dtor must read it: still ill-formed ====
constexpr uptr<uptr<int>> e4(new uptr<int>(new int(4))); // #decl-e4
// expected-error@#decl-e4 {{must be initialized by a constant expression}}
// expected-note@#uptr-dtor-delete {{read of object in a mutable allocation persisted by 'e4' during its constant destruction}}
// expected-note@#uptr-dtor-delete {{in call}}
// expected-note@#decl-e4 {{in call}}
// expected-note@#decl-e4 {{heap allocation performed here}}
// cxx26-error@#decl-e4 {{must be initialized by a constant expression}}
// cxx26-note@#decl-e4 {{pointer to heap-allocated object is not a constant expression}}
// cxx26-note@#decl-e4 {{heap allocation performed here}}

// ==== Ex 6: inner uptr const: outer blessed, inner mutable ====
constexpr uptr<uptr<int> const> e6(new uptr<int> const(new int(6))); // #decl-e6
// cxx26-error@#decl-e6 {{must be initialized by a constant expression}}
// cxx26-note@#decl-e6 {{pointer to heap-allocated object is not a constant expression}}
// cxx26-note@#decl-e6 {{heap allocation performed here}}
static_assert((*e6).get() != nullptr); // #read-e6a
// cxx26-error@#read-e6a {{static assertion expression is not an integral constant expression}}
// cxx26-note@#read-e6a {{initializer of 'e6' is not a constant expression}}
// cxx26-note@#decl-e6 {{declared here}}
static_assert(**e6 == 6); // #read-e6b
// expected-error@#read-e6b {{static assertion expression is not an integral constant expression}}
// expected-note@#uptr-deref {{read of object in a persistent allocation that is not immutable}}
// expected-note@#read-e6b {{in call}}
// cxx26-error@#read-e6b {{static assertion expression is not an integral constant expression}}
// cxx26-note@#read-e6b {{initializer of 'e6' is not a constant expression}}
// cxx26-note@#decl-e6 {{declared here}}
int& r6 = **e6; // ok: mutable at runtime

// ==== Ex 10: identity, not content ====
template <int const* P> struct X {};
constexpr uptr<int const> v1(new int(1)); // #decl-v1
// cxx26-error@#decl-v1 {{must be initialized by a constant expression}}
// cxx26-note@#decl-v1 {{pointer to heap-allocated object is not a constant expression}}
// cxx26-note@#decl-v1 {{heap allocation performed here}}
constexpr uptr<int const> v2(new int(1)); // #decl-v2
// cxx26-error@#decl-v2 {{must be initialized by a constant expression}}
// cxx26-note@#decl-v2 {{pointer to heap-allocated object is not a constant expression}}
// cxx26-note@#decl-v2 {{heap allocation performed here}}
#if __cplusplus > 202400L
X<&*v1> x1;
X<&*v2> x2;
X<&*v1> x1b;
static_assert(!__is_same(decltype(x1), decltype(x2)));
static_assert(__is_same(decltype(x1), decltype(x1b)));

// ==== Ex 11: pointers into another variable's persistent allocation ====
constexpr int const* view = &*e3;
static_assert(*view == 3);
#endif

// ==== Row 5: conflicting intent — blessed AND mutably reachable ====
#if __cplusplus > 202400L
struct Conflicted {
  immutable_if_constexpr int* a; // blesses the allocation
  int* b;                        // #conflict-member — unblessed path to it
  constexpr Conflicted(int v) : a(new int(v)), b(a) {} // #conflict-alloc
  Conflicted(const Conflicted&) = delete;
  constexpr ~Conflicted() { delete a; }
};
constexpr Conflicted cf(1); // #decl-cf
// expected-error@#decl-cf {{must be initialized by a constant expression}}
// expected-note@#conflict-alloc {{allocation persisted by 'cf' is declared immutable via 'immutable_if_constexpr' but is also reachable as mutable through 'b'}}

// ==== Composition: blessing an enclosing member distributes down ====
struct Wrapper {
  immutable_if_constexpr uptr<int> inner; // blesses inner's paths wholesale
  constexpr Wrapper(int v) : inner(new int(v)) {}
  Wrapper(const Wrapper&) = delete;
  constexpr ~Wrapper() {}
};
constexpr Wrapper w(5);
static_assert(*w.inner == 5); // ok: blessed via Wrapper::inner

// ==== Specifier placement errors ====
immutable_if_constexpr int global = 1;
// expected-error@-1 {{'immutable_if_constexpr' attribute only applies to non-bit-field non-static data members}}
struct BadUses {
  static immutable_if_constexpr int* s; // expected-error {{'immutable_if_constexpr' attribute only applies to non-bit-field non-static data members}}
  mutable immutable_if_constexpr int* m; // expected-error {{'immutable_if_constexpr' cannot be combined with 'mutable'}}
  immutable_if_constexpr int bf : 3; // expected-error {{'immutable_if_constexpr' attribute only applies to non-bit-field non-static data members}}
};
#endif

// ==== Partially-initialized allocations (raw tail) ====
#if __cplusplus > 202400L
struct MiniVec {
  immutable_if_constexpr int* p;
  int sz;
  int cap;
  constexpr MiniVec(int n, int c) : p(new int[c]), sz(n), cap(c) { // #minivec-alloc
    for (int i = 0; i < n; ++i)
      p[i] = i + 1;
    // Elements [n, c) are raw storage, deliberately uninitialized.
  }
  MiniVec(const MiniVec&) = delete;
  constexpr ~MiniVec() { delete[] p; }
};

constexpr MiniVec mv(3, 5); // #decl-mv
static_assert(mv.p[0] == 1 && mv.p[2] == 3);
static_assert(mv.sz == 3 && mv.cap == 5);

// Reading into the raw tail fails at read time, not at mv's declaration.
constexpr int tail = mv.p[4]; // #read-tail
// expected-error@#read-tail {{must be initialized by a constant expression}}
// expected-note@#read-tail {{read of uninitialized object is not allowed in a constant expression}}
// expected-note@#decl-mv {{declared here}}
#endif
