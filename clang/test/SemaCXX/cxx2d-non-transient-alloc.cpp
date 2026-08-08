// RUN: %clang_cc1 -std=c++2d -verify %s
// RUN: %clang_cc1 -std=c++2c -verify=cxx26 %s

// P4341 (non-transient constexpr allocation), Sema/constant-evaluator side.
// See ideas/non-transient-alloc.md. Under C++26, none of this persists and
// the classic diagnostics apply; under C++2d, allocations may persist when
// the variable's hypothetical constant destruction deallocates them.

namespace std {
  template <class T>
  constexpr void mark_immutable_if_constexpr(T* p) {
    __builtin_mark_immutable_if_constexpr(
        const_cast<void*>(static_cast<const void*>(p)));
  }
}

template <class T>
struct uptr {
  T* p;
  constexpr uptr(T* p) : p(p) {}
  uptr(const uptr&) = delete;
  constexpr ~uptr() {
    if constexpr (__is_const(T))
      std::mark_immutable_if_constexpr(p);
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

// ==== Ex 2: persists, but not constant-readable; runtime-mutable ====
constexpr uptr<int> e2(new int(2)); // #decl-e2
// cxx26-error@#decl-e2 {{must be initialized by a constant expression}}
// cxx26-note@#decl-e2 {{pointer to heap-allocated object is not a constant expression}}
// cxx26-note@#decl-e2 {{heap allocation performed here}}
static_assert(*e2 == 2); // #read-e2
// expected-error@#read-e2 {{static assertion expression is not an integral constant expression}}
// expected-note@#uptr-deref {{read of object in a persistent allocation that was not marked immutable with 'std::mark_immutable_if_constexpr'}}
// expected-note@#read-e2 {{in call}}
// cxx26-error@#read-e2 {{static assertion expression is not an integral constant expression}}
// cxx26-note@#read-e2 {{initializer of 'e2' is not a constant expression}}
// cxx26-note@#decl-e2 {{declared here}}
void bump() { ++*e2; } // ok: mutable at runtime

// ==== Ex 3: marked (const element): constant-readable ====
constexpr uptr<int const> e3(new int(3)); // #decl-e3
// cxx26-error@#decl-e3 {{must be initialized by a constant expression}}
// cxx26-note@#decl-e3 {{pointer to heap-allocated object is not a constant expression}}
// cxx26-note@#decl-e3 {{heap allocation performed here}}
static_assert(*e3 == 3); // #read-e3
// cxx26-error@#read-e3 {{static assertion expression is not an integral constant expression}}
// cxx26-note@#read-e3 {{initializer of 'e3' is not a constant expression}}
// cxx26-note@#decl-e3 {{declared here}}

// ==== Ex 4: outer allocation reachable-as-mutable; dtor must read it ====
constexpr uptr<uptr<int>> e4(new uptr<int>(new int(4))); // #decl-e4
// expected-error@#decl-e4 {{must be initialized by a constant expression}}
// expected-note@#uptr-dtor-delete {{read of object in an allocation that is reachable as mutable from 'e4' during its constant destruction}}
// expected-note@#uptr-dtor-delete {{in call}}
// expected-note@#decl-e4 {{in call}}
// expected-note@#decl-e4 {{heap allocation performed here}}
// cxx26-error@#decl-e4 {{must be initialized by a constant expression}}
// cxx26-note@#decl-e4 {{pointer to heap-allocated object is not a constant expression}}
// cxx26-note@#decl-e4 {{heap allocation performed here}}

// ==== Ex 6: inner unique_ptr const: outer marked, inner unmarked ====
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
// expected-note@#uptr-deref {{read of object in a persistent allocation that was not marked immutable}}
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

// ==== No-op marks: interior objects and null pointers ====
struct SelfMark {
  int buf[4];
  int* p;
  constexpr SelfMark() : buf{1,2,3,4}, p(new int(9)) {} // #selfmark-alloc
  SelfMark(const SelfMark&) = delete;
  constexpr ~SelfMark() {
    std::mark_immutable_if_constexpr(buf);           // interior object: no-op
    std::mark_immutable_if_constexpr((int*)nullptr); // null: no-op
    std::mark_immutable_if_constexpr(p);             // real allocation: marks
    delete p;
  }
};
constexpr SelfMark sm; // #decl-sm
// cxx26-error@#decl-sm {{must be initialized by a constant expression}}
// cxx26-note@#decl-sm {{pointer to heap-allocated object is not a constant expression}}
// cxx26-note@#selfmark-alloc {{heap allocation performed here}}
static_assert(*sm.p == 9); // #read-sm
// cxx26-error@#read-sm {{static assertion expression is not an integral constant expression}}
// cxx26-note@#read-sm {{initializer of 'sm' is not a constant expression}}
// cxx26-note@#decl-sm {{declared here}}
