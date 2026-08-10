// RUN: %clang_cc1 -std=c++2d -verify %s

// P4341 + P3603: an allocation owned by a consteval variable has
// consteval-only address. Pointers/references into it are consteval-only
// values: they may be materialized during constant evaluation (including
// into constexpr variables, which silently become consteval), but may never
// escape to runtime. Reads that produce clean values (no pointers into the
// allocation) launder wherever constant initialization is attempted:
// namespace scope, static locals, constexpr, and const integral locals.
// A plain local variable's initialization is a runtime operation and does
// not launder (P3496 would be the direction that relaxes this).

namespace std {
  template <class T>
  constexpr void mark_immutable_if_constexpr(T* p) {
    __builtin_mark_immutable_if_constexpr(
        const_cast<void*>(static_cast<const void*>(p)));
  }
}

// Minimal vector-like type owning a persistent allocation.
struct IntVec {
  int* p;
  int n;
  constexpr IntVec(int a, int b, int c) : p(new int[3]{a, b, c}), n(3) {}
  IntVec(const IntVec&) = delete;
  constexpr ~IntVec() {
    std::mark_immutable_if_constexpr(p);
    delete[] p;
  }
  constexpr int operator[](int i) const { return p[i]; }
  constexpr int size() const { return n; }
  constexpr const int* data() const { return p; }
};

consteval IntVec v(1, 2, 3);

// The allocation's contents are constant-readable in consteval contexts.
static_assert(v[0] == 1);
static_assert(*v.data() == 1);

// Namespace scope: constant initialization launders clean reads.
int d = v[0];
int n = v.size();

// A pointer into the allocation is a consteval-only value: a runtime
// variable cannot hold it.
const int* a = v.data(); // expected-error {{expressions involving consteval-only values are only allowed in constant-evaluated contexts}}

// A constexpr variable can (it silently becomes consteval)...
constexpr const int* b = v.data();
static_assert(*b == 1);
static_assert(b == v.data());

// ...which makes it consteval-only itself: reading it at runtime is
// ill-formed.
const int* c = b; // expected-error {{expressions involving consteval-only values are only allowed in constant-evaluated contexts}}
int e = *b;       // ok: dereference during constant init produces a clean int

// An explicitly consteval holder works the same way.
consteval const int* b2 = v.data();
static_assert(*b2 == 2 - 1);

// References into the allocation are consteval-only too.
constexpr const int& r = v.p[1];
static_assert(r == 2);

void local() {
  int bad = v[0];           // expected-error {{expressions involving consteval-only values are only allowed in constant-evaluated contexts}}
  const int good1 = v[0];   // const integral local: constant-init launders
  constexpr int good2 = v[1];
  static int good3 = v[2];
  const int* bad2 = v.data(); // expected-error {{expressions involving consteval-only values are only allowed in constant-evaluated contexts}}
  (void)bad; (void)good1; (void)good2; (void)good3; (void)bad2;
}

// Consteval consumers can use everything freely.
consteval int sum() {
  int r0 = 0;
  for (int i = 0; i != v.size(); ++i)
    r0 += v[i];
  return r0 + *b;
}
static_assert(sum() == 7);
