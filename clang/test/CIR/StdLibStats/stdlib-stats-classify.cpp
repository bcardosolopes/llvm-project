// A census of call expressions is not a census of emitted calls. Check the
// two things that tell them apart: the callee kind, and whether the call site
// sits in an unevaluated operand.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 \
// RUN:   -fstd-lib-stats=json -emit-obj %s -o %t.o 2>&1 | FileCheck %s

// -fstd-lib-stats-libs adds non-std namespaces to the census.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 \
// RUN:   -fstd-lib-stats=json -fstd-lib-stats-libs=folly,absl \
// RUN:   -emit-obj %s -o %t.o 2>&1 | FileCheck %s -check-prefix=LIBS

// Without the extra libraries, folly and absl are just "not std".
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 \
// RUN:   -fstd-lib-stats=json -emit-obj %s -o %t.o 2>&1 \
// RUN:   | FileCheck %s -check-prefix=NOLIBS

namespace std {
template <class T> struct remove_reference { using type = T; };
template <class T> constexpr T &&forward(typename remove_reference<T>::type &t);
template <class T> T &&declval();
template <class T> struct numeric_limits {
  static constexpr T max();
};
consteval int alwaysFolded() { return 3; }
int reallyCalled();
} // namespace std

namespace folly {
struct fbstring {
  unsigned size() const;
};
} // namespace folly

namespace absl {
inline namespace lts_20240116 {
unsigned strLen(const char *);
} // namespace lts_20240116
} // namespace absl

int sink(int);

int classify(int &x) {
  // constexpr: may fold, may not.
  int a = std::numeric_limits<int>::max();
  int b = sink(std::forward<int>(x));
  // consteval: never emits a call.
  int c = std::alwaysFolded();
  // runtime: a real call.
  int d = std::reallyCalled();
  return a + b + c + d;
}

// declval is only ever spelled in an unevaluated operand, so every one of its
// call sites is free. The decltype cases arrive as TypeLocs, not QualTypes.
using DeclType = decltype(std::declval<int>());
unsigned sizeofUse = sizeof(std::declval<int>());
bool noexceptUse = noexcept(std::declval<int>());
auto trailing() -> decltype(std::declval<int>());

unsigned useOtherLibs(const folly::fbstring &s, const char *p) {
  return s.size() + absl::strLen(p);
}

// A call with no callee declaration. This is what "indirect" counts, and it is
// counted over every call, not just std ones -- it is the devirtualization
// surface, so a std-only denominator would understate it.
int throughPointer(int (*fp)(int), int x) { return fp(x); }

// A virtual call is NOT indirect by this census: it resolves to the base
// method's declaration.
struct Base { virtual int f(); };
int throughVtable(Base &b) { return b.f(); }

// Calls in a template pattern that is never instantiated resolve to nothing
// either, but no code is ever generated for them. In header-heavy code they
// swamp the real indirect calls, so they are counted apart.
template <class T> auto neverInstantiated(T t) { return t.method(); }
template <class T> auto neverInstantiatedOp(T t) { return t + t; }

// The same shape, instantiated: the instantiation resolves, so it is an
// ordinary non-std call rather than either of the above.
struct Has { int method(); };
template <class T> auto instantiated(T t) { return t.method(); }
int useInstantiated(Has h) { return instantiated(h); }

// Overload resolution is deferred whenever an *argument* is dependent, so this
// callee is unresolved even though the name being called is not itself
// dependent. It is `dependent` too, not `indirect`.
int over(int);
int over(double);
template <class T> auto overloadedOnDependentArg(T t) { return over(t); }

// CHECK: {"version":1,"kind":"stdlib_stats"
// The call through `fp` is the only real indirect one: the virtual call and
// the instantiated template both resolve, and the three calls in the
// never-instantiated patterns are counted as `dependent` instead.
// CHECK-SAME: "not_std":6,"indirect":1,"dependent":3,"unnamed":0,"unevaluated":4,

// Each kind carries its classification. Entries are emitted sorted by record
// then function, and CHECK-SAME is ordered, so these follow that order.
// CHECK-SAME: {"record":"","function":"alwaysFolded()","count":1,
// CHECK-SAME: "lib":"std","kind":"consteval","unevaluated":0,

// Every declval call site is unevaluated: decltype, sizeof, noexcept,
// trailing return type.
// CHECK-SAME: {"record":"","function":"declval()","count":4,
// CHECK-SAME: "lib":"std","kind":"runtime","unevaluated":4,

// CHECK-SAME: {"record":"","function":"forward(typename remove_reference<int>::type &)","count":1,
// CHECK-SAME: "lib":"std","kind":"constexpr","unevaluated":0,
// CHECK-SAME: {"record":"","function":"reallyCalled()","count":1,
// CHECK-SAME: "lib":"std","kind":"runtime","unevaluated":0,
// CHECK-SAME: {"record":"numeric_limits","function":"max()","count":1,
// CHECK-SAME: "lib":"std","kind":"constexpr","unevaluated":0,

// LIBS: {"version":1,"kind":"stdlib_stats"
// LIBS-SAME: "libs":{"absl":1,"folly":1,"std":8}

// Censusing folly and absl moves them out of the namespace histogram, leaving
// only the global-scope calls.
// LIBS-SAME: "namespaces":{"(global)":4}

// LIBS-SAME: {"record":"","function":"strLen(const char *)","count":1,
// LIBS-SAME: "lib":"absl","kind":"runtime",
// LIBS-SAME: {"record":"fbstring","function":"size()","count":1,
// LIBS-SAME: "lib":"folly","kind":"runtime",

// NOLIBS: "libs":{"std":8}
// NOLIBS-NOT: "lib":"folly"
// NOLIBS-NOT: "lib":"absl"

// Without them, the histogram is what says they are worth censusing. This is
// the discovery pass: run it first, then pick -fstd-lib-stats-libs from it.
// NOLIBS: "namespaces":{"(global)":4,"absl":1,"folly":1}
