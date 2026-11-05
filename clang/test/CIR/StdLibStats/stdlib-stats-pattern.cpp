// A call written in a template PATTERN is not an emitted call. The pattern is
// codegen'd only through its instantiations, and each instantiation is censused
// separately under its own linkage name -- so an entry-point total that does not
// subtract the pattern sites overstates the call surface. isset_bitset::at was
// published as 899,245 call sites on that mistake; 98.4% of it was pattern.
//
// The census reports the split itself so no consumer has to re-derive it by
// counting callers with a null `mangled`.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 \
// RUN:   -fstd-lib-stats=json -emit-obj %s -o %t.o 2>&1 | FileCheck %s

namespace std {
int patternOnly();
int named();
} // namespace std

// The callee resolves, so this is censused rather than counted as `dependent`
// -- but the caller is a template pattern and has no linkage name, so the call
// site is never emitted under it.
template <class T> int neverInstantiated(T t) { return std::patternOnly(); }

// An ordinary function: a real, emitted, attributable call.
int plain() { return std::named(); }

// CHECK: {"version":1,"kind":"stdlib_stats"

// Two censused call sites, exactly one of which sits in a pattern.
// CHECK-SAME: "unevaluated":0,"pattern":1,

// And the same split per entry point, which is the number a consumer subtracts.
// CHECK-SAME: {"record":"","function":"named()","count":1,
// CHECK-SAME: "lib":"std","kind":"runtime","unevaluated":0,"pattern":0,
// CHECK-SAME: {"record":"","function":"patternOnly()","count":1,
// CHECK-SAME: "lib":"std","kind":"runtime","unevaluated":0,"pattern":1,

// The pattern caller is exactly the one with no linkage name. This is what ties
// the compiler-emitted split to the hand-recovered one: same call sites, same
// condition, so old numbers and new ones stay comparable.
// CHECK-SAME: "name":"neverInstantiated","mangled":null
