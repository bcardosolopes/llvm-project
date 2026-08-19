// RUN: %clang_cc1 -std=c++2d -verify %s

// P4341 v2 (non-transient constexpr allocation), part 2: the rules added
// after the design review (see ideas/non-transient-alloc-v2.md).
//
// - Classification is fixed against the end-of-initialization state, not
//   the live destruction state: the standard destructor idiom (copy the
//   member to a local, null the member, delete the local) must not bypass
//   the check.
// - The hypothetical destruction must not leak allocations of its own.
// - A mutable allocation may persist only for a variable with static
//   storage duration.
// - Persisted contents are subject to the permitted-result checks (no
//   dangling pointers).
// - Pointers into allocations persisted by no-linkage / thread-local owners
//   are not usable as constant template parameters.

template <class T>
struct uptr {
  immutable_if_constexpr(__is_const(T)) T* p;
  constexpr uptr(T* p) : p(p) {}
  uptr(const uptr&) = delete;
  constexpr ~uptr() { delete p; }
  constexpr T& operator*() const { return *p; }
};

// ==== End-of-initialization snapshot: the reset() idiom cannot launder ====

// Same shape as Ex 4, but the destructor erases the mutable path before
// reading through a local copy. Reachability is judged against the
// end-of-initialization state, so this must still be an error.
struct Inner {
  int* q;
  constexpr Inner(int v) : q(new int(v)) {}
  Inner(const Inner&) = delete;
  constexpr ~Inner() { delete q; } // #inner-dtor
};
struct Outer {
  Inner* p;
  constexpr Outer(int v) : p(new Inner(v)) {} // #outer-alloc
  Outer(const Outer&) = delete;
  constexpr ~Outer() {
    Inner* local = p;
    p = nullptr;
    delete local; // #outer-delete
  }
};
constexpr Outer o(7); // #decl-o
// expected-error@#decl-o {{must be initialized by a constant expression}}
// expected-note@#inner-dtor {{read of object in a mutable allocation persisted by 'o' during its constant destruction}}
// expected-note@#outer-delete {{in call}}
// expected-note@#decl-o {{in call}}
// expected-note@#outer-alloc {{heap allocation performed here}}

// ==== Destruction must not leak its own allocations ====

struct Leaky {
  immutable_if_constexpr int* p;
  constexpr Leaky(int v) : p(new int(v)) {}
  Leaky(const Leaky&) = delete;
  constexpr ~Leaky() {
    delete p;
    new int(42); // #leak
  }
};
constexpr Leaky lk(5); // #decl-lk
// expected-error@#decl-lk {{must be initialized by a constant expression}}
// expected-note@#leak {{allocation performed here was not deallocated}}

// ==== Mutable allocations require static storage duration ====

void autos() {
  // Immutable (blessed via the conditional specifier): shareable across
  // invocations like a string literal.
  constexpr uptr<int const> ok(new int(1));
  static_assert(*ok == 1);

  // Mutable: runtime-writable, each invocation's variable would need a
  // distinct allocation; automatic storage duration cannot persist it.
  constexpr uptr<int> bad(new int(2)); // #decl-bad
  // expected-error@#decl-bad {{must be initialized by a constant expression}}
  // expected-note@#decl-bad {{allocation performed here cannot persist: it is mutable (not declared 'immutable_if_constexpr') and 'bad' does not have static storage duration}}

  // Static local: fine.
  static constexpr uptr<int> okstatic(new int(3));
}

// ==== Persisted contents must be permitted results ====

struct Dangling {
  immutable_if_constexpr int** pp;
  constexpr Dangling() : pp(new int*(nullptr)) {
    int* tmp = new int(1);
    *pp = tmp;
    delete tmp; // *pp now dangles
  }
  Dangling(const Dangling&) = delete;
  constexpr ~Dangling() { delete pp; }
};
constexpr Dangling dang; // #decl-dang
// expected-error@#decl-dang {{must be initialized by a constant expression}}
// expected-note@#decl-dang {{pointer to heap-allocated object is not a constant expression}}

// ==== Constant template parameters require an owner with identity ====

template <int const* P> struct X {};

inline constexpr uptr<int const> linked(new int(1));
X<&*linked> x_ok; // ok: external linkage

thread_local constexpr uptr<int const> tls(new int(2));
X<&*tls> x_tls; // #use-tls
// expected-error@#use-tls {{non-type template argument is not a constant expression}}
// expected-note@#use-tls {{pointer into an allocation persisted by 'tls' is not usable as a constant template parameter because 'tls' is thread-local}}

void locals() {
  constexpr uptr<int const> loc(new int(3));
  X<&*loc> x_loc; // #use-loc
  // expected-error@#use-loc {{non-type template argument is not a constant expression}}
  // expected-note@#use-loc {{pointer into an allocation persisted by 'loc' is not usable as a constant template parameter because 'loc' has no linkage}}

  static constexpr uptr<int const> sloc(new int(4));
  X<&*sloc> x_sloc; // ok: static local, identity via the enclosing function
}
