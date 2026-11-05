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

// Same enum name, different enumerators: same layout, different meaning.
// Expected finding: enum divergence (enumerators differ). Needs the CIRGen
// summary: enums are lowered to their integer type before cir-link sees them.

// Today: the inline function's bodies differ (the value of On differs).
// FIXME: nothing names the enum: enums are integers by the time cir-link
// sees them.

// CHECK:      "symbols": [
// CHECK:        "symbol": "_Z6encode4Mode",
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
// DIAG:      {{.*}}b.cpp:2:1: warning: 'encode(Mode)' (_Z6encode4Mode) has different definitions in '{{.*}}a.o' ({{.*}}a.cpp) and '{{.*}}b.o' ({{.*}}b.cpp) (they differ in body); keeping the one from '{{.*}}a.o' ({{.*}}a.cpp)
// DIAG-NEXT: inline int encode(Mode m) { return m == On ? 7 : 3; }
// DIAG:      {{.*}}a.cpp:2:1: note: the definition kept is here
// DIAG-NEXT: inline int encode(Mode m) { return m == On ? 7 : 3; }
// DIAG:      {{.*}}b.cpp:2:41: note: the copies first differ here
// DIAG-NEXT: inline int encode(Mode m) { return m == On ? 7 : 3; }
// DIAG:      {{.*}}a.cpp:2:41: note: the kept copy has this instead
// DIAG-NEXT: inline int encode(Mode m) { return m == On ? 7 : 3; }
// DIAG-NOT:  {{warning|note}}:

//--- a.cpp
enum Mode : int { Off, On };
inline int encode(Mode m) { return m == On ? 7 : 3; }
int from_a(Mode m) { return encode(m); }
//--- b.cpp
enum Mode : int { On };
inline int encode(Mode m) { return m == On ? 7 : 3; }
int from_b(Mode m) { return encode(m); }
