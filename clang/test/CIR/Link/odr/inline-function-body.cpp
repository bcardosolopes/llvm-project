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

// Two TUs define the same inline function with different bodies. Each TU's
// callers were compiled against its own body, and the link keeps one.
// Expected finding: weak-weak, same type, different body.

// Today: recorded as a weak-weak pair whose bodies differ.

// CHECK:      "symbols": [
// CHECK:        "symbol": "_Z5valuev",
// CHECK-NEXT:   "op": "cir.func",
// CHECK:          "kind": "weak-weak",
// CHECK:          "kept": {
// CHECK-NEXT:       "input": 0,
// CHECK-NEXT:       "linkage": "linkonce_odr",
// CHECK:          "dropped": {
// CHECK-NEXT:       "input": 1,
// CHECK-NEXT:       "linkage": "linkonce_odr",
// CHECK:          "same_type": true,
// CHECK-NEXT:     "same_attributes": true,
// CHECK-NEXT:     "same_body": false
// CHECK:      "records": [],
// CHECK:      "stats": {
// CHECK-NEXT:   "weak_copies_equal": 0,
// CHECK-NEXT:   "weak_copies_differ": 1,
// CHECK-NEXT:   "weak-weak": 1,
// CHECK-NEXT:   "strong-over-weak": 0,
// CHECK-NEXT:   "rtti-over-nortti": 0,
// CHECK-NEXT:   "strong-strong": 0,
// CHECK-NEXT:   "records_renamed": 0

// --diagnostic-style=source: where each finding points, and the source
// line there.
// DIAG:      {{.*}}b.cpp:1:1: warning: 'value()' (_Z5valuev) has different definitions in '{{.*}}a.o' ({{.*}}a.cpp) and '{{.*}}b.o' ({{.*}}b.cpp) (they differ in body); keeping the one from '{{.*}}a.o' ({{.*}}a.cpp)
// DIAG-NEXT: inline int value() { return 1; }
// DIAG:      {{.*}}a.cpp:1:1: note: the definition kept is here
// DIAG-NEXT: inline int value() { return 0; }
// DIAG:      {{.*}}b.cpp:1:29: note: the copies first differ here
// DIAG-NEXT: inline int value() { return 1; }
// DIAG:      {{.*}}a.cpp:1:29: note: the kept copy has this instead
// DIAG-NEXT: inline int value() { return 0; }
// DIAG-NOT:  {{warning|note}}:

//--- a.cpp
inline int value() { return 0; }
int from_a() { return value(); }
//--- b.cpp
inline int value() { return 1; }
int from_b() { return value(); }
