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

// Same enum name, same values, different enumerator names.
// Expected finding: enum divergence (names). Only a check that sees the
// enumerators can report it; nothing about layout or code differs.

// Today: nothing is recorded; the copies are identical.
// FIXME: only a summary that keeps the enumerators can report this.

// CHECK:      "symbols": [],
// CHECK:      "records": [],
// CHECK:      "stats": {
// CHECK-NEXT:   "weak_copies_equal": 1,
// CHECK-NEXT:   "weak_copies_differ": 0,
// CHECK-NEXT:   "weak-weak": 0,
// CHECK-NEXT:   "strong-over-weak": 0,
// CHECK-NEXT:   "rtti-over-nortti": 0,
// CHECK-NEXT:   "strong-strong": 0,
// CHECK-NEXT:   "records_renamed": 0

// --diagnostic-style=source: where each finding points, and the source
// line there. Nothing is reported.
// DIAG-NOT:  {{warning|note}}:

//--- a.cpp
enum Mode { First };
inline int encode(Mode m) { return m; }
int from_a(Mode m) { return encode(m); }
//--- b.cpp
enum Mode { Only };
inline int encode(Mode m) { return m; }
int from_b(Mode m) { return encode(m); }
