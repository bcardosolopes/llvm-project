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

// Same enum name, different underlying type: the size differs.
// Expected finding: enum divergence (underlying type), and the inline functions
// taking it differ in type.

// Today: the inline function differs in type (its parameter's width).
// FIXME: nothing names the enum.

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
// CHECK:          "same_type": false,
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
// DIAG:      {{.*}}b.cpp:2:1: warning: 'encode(Mode)' (_Z6encode4Mode) has different definitions in '{{.*}}a.o' ({{.*}}a.cpp) and '{{.*}}b.o' ({{.*}}b.cpp) (they differ in type, body); keeping the one from '{{.*}}a.o' ({{.*}}a.cpp)
// DIAG-NEXT: inline int encode(Mode m) { return m; }
// DIAG:      {{.*}}a.cpp:2:1: note: the definition kept is here
// DIAG-NEXT: inline int encode(Mode m) { return m; }
// DIAG:      {{.*}}b.cpp:2:19: note: the type of parameter 1 differs here: a 32-bit signed integer
// DIAG-NEXT: inline int encode(Mode m) { return m; }
// DIAG:      {{.*}}a.cpp:2:19: note: the kept copy has: an 8-bit unsigned integer
// DIAG-NEXT: inline int encode(Mode m) { return m; }
// DIAG-NOT:  {{warning|note}}:

//--- a.cpp
enum Mode : unsigned char { Off, On };
inline int encode(Mode m) { return m; }
int from_a(Mode m) { return encode(m); }
//--- b.cpp
enum Mode : int { Off, On };
inline int encode(Mode m) { return m; }
int from_b(Mode m) { return encode(m); }
