// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects -O1 \
// RUN:     -c %t/a.cpp -o %t/a.o
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects -O1 \
// RUN:     -c %t/b.cpp -o %t/b.o
// RUN: cir-link %t/a.o %t/b.o --odr-events=%t/events.json \
// RUN:     -o %t/merged.cir
// RUN: FileCheck %s --input-file=%t/events.json
// RUN: cir-link %t/a.o %t/b.o --odr=warning --diagnostic-style=source \
// RUN:     -o %t/diag.cir 2>&1 | FileCheck %s --check-prefix=DIAG --allow-empty

// A lambda-holding local named through __COUNTER__ gets a different name in each
// TU that expands the macro a different number of times before it. Local names
// are not part of the ODR definition.
// Expected finding: none. Identical weak copies.

// Today: the copies are identical (counted, not listed): the name a local
// is given on its cir.alloca is not part of the definition. This must stay so.

// CHECK:      "symbols": [],
// CHECK:      "records": [],
// CHECK:      "stats": {
// CHECK-NEXT:   "weak_copies_equal": 2,
// CHECK-NEXT:   "weak_copies_differ": 0,
// CHECK-NEXT:   "weak-weak": 0,
// CHECK-NEXT:   "strong-over-weak": 0,
// CHECK-NEXT:   "rtti-over-nortti": 0,
// CHECK-NEXT:   "strong-strong": 0,
// CHECK-NEXT:   "records_renamed": 0

// --diagnostic-style=source: where each finding points, and the source
// line there. Nothing is reported.
// DIAG-NOT:  {{warning|note}}:

//--- scope.h
#define CONCAT_IMPL(a, b) a##b
#define CONCAT(a, b) CONCAT_IMPL(a, b)
#define ANON(str) CONCAT(str, __COUNTER__)
#define GUARD_IMPL(v) auto v = [&]() noexcept { state = 1; }; v()
#define GUARD GUARD_IMPL(ANON(guard))
struct Holder {
  static int state;
  template <class F> static void run() { GUARD; }
};
//--- a.cpp
#include "scope.h"
int Holder::state;
void from_a() { Holder::run<int>(); }
//--- b.cpp
#define BUMP __COUNTER__
int bump() { return BUMP; }
#include "scope.h"
void from_b() { Holder::run<int>(); }
