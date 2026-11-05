// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects -O1 \
// RUN:     -c %t/def.cpp -o %t/def.o
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects -O1 \
// RUN:     -c %t/use.cpp -o %t/use.o
// RUN: cir-link %t/def.o %t/use.o --odr-events=%t/events.json \
// RUN:     -o %t/merged.cir
// RUN: FileCheck %s --input-file=%t/events.json
// RUN: cir-link %t/def.o %t/use.o --odr=warning --diagnostic-style=source \
// RUN:     -o %t/diag.cir 2>&1 | FileCheck %s --check-prefix=DIAG --allow-empty

// Two TUs disagree on the definition of a struct and on the type of a global of
// that struct.
// Expected finding: record divergence on the struct; the global's type
// differs between the definition and the use.

// Today: only the record rename is recorded.
// FIXME: a declaration whose type differs from the definition it binds to
// (the global `a` here) is not recorded; it is not a choice between two
// definitions, so --odr-events has nowhere to put it.

// CHECK:      "symbols": [],
// CHECK:      "records": [
// CHECK:          "kind": "record-renamed",
// CHECK-NEXT:     "input": 1,
// CHECK-NEXT:     "record": "A",
// CHECK-NEXT:     "renamed_to": "A.m1"
// CHECK:      "stats": {
// CHECK-NEXT:   "weak_copies_equal": 0,
// CHECK-NEXT:   "weak_copies_differ": 0,
// CHECK-NEXT:   "weak-weak": 0,
// CHECK-NEXT:   "strong-over-weak": 0,
// CHECK-NEXT:   "rtti-over-nortti": 0,
// CHECK-NEXT:   "strong-strong": 0,
// CHECK-NEXT:   "records_renamed": 1

// --diagnostic-style=source: where each finding points, and the source
// line there.
// DIAG:      {{.*}}use.cpp:1:8: warning: 'A' is defined differently than in an earlier input
// DIAG-NEXT: struct A {};
// DIAG:      {{.*}}def.cpp:1:8: note: the earlier definition is here
// DIAG-NEXT: struct A { int x; } a;
// DIAG:      {{.*}}use.cpp:1:8: note: a member differs here: an 8-bit unsigned integer
// DIAG-NEXT: struct A {};
// DIAG:      {{.*}}def.cpp:1:16: note: the earlier definition has: a 32-bit signed integer
// DIAG-NEXT: struct A { int x; } a;
// DIAG-NOT:  {{warning|note}}:

//--- def.cpp
struct A { int x; } a;
int read_def() { return a.x; }
//--- use.cpp
struct A {};
extern A a;
A *use_ptr() { return &a; }
