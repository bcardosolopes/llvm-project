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

// The precondition, so the test cannot pass by the two TUs no longer emitting
// forward() differently: a.cpp calls peek() plainly, b.cpp through a
// synthetic try that terminates.
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -O1 \
// RUN:     -Xclang -emit-cir -S %t/a.cpp -o %t/a.cir
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -O1 \
// RUN:     -Xclang -emit-cir -S %t/b.cpp -o %t/b.cir
// RUN: FileCheck %s --input-file=%t/a.cir --check-prefix=CIR-A
// RUN: FileCheck %s --input-file=%t/b.cir --check-prefix=CIR-B
// CIR-A:      cir.func {{.*}}@_Z7forwardRK6Handle(
// CIR-A:        cir.call @_Z4peekRK6Handle(
// CIR-B:      cir.func {{.*}}@_Z7forwardRK6Handle(
// CIR-B:        cir.try synthetic {
// CIR-B-NEXT:     cir.call exception @_Z4peekRK6Handle(

// A noexcept inline function calls another inline function. A TU that has
// emitted the callee before the caller knows it cannot throw and calls it
// plainly; a TU that emits the caller first wraps the call in a try that
// terminates. Which it is depends on the order the TU uses them in, not on
// the source.
// Expected finding: none. Identical weak copies.

// Today: the copies are identical (counted, not listed). This must stay so.

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

//--- handle.h
struct Handle {
  int *slot;
};
inline int *peek(const Handle &h) { return h.slot; }
int consume(int *);
inline int forward(const Handle &h) noexcept { return consume(peek(h)); }
//--- a.cpp
#include "handle.h"
int *first_a(const Handle &h) { return peek(h); }
int then_a(const Handle &h) { return forward(h); }
//--- b.cpp
#include "handle.h"
int first_b(const Handle &h) { return forward(h); }
int *then_b(const Handle &h) { return peek(h); }
