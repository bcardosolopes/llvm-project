// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects -O1 \
// RUN:     -c %t/ab.cpp -o %t/ab.o
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects -O1 \
// RUN:     -c %t/evil.cpp -o %t/evil.o
// RUN: cir-link %t/ab.o %t/evil.o --odr-events=%t/events.json \
// RUN:     -o %t/merged.cir
// RUN: FileCheck %s --input-file=%t/events.json
// RUN: cir-link %t/ab.o %t/evil.o --odr=warning --diagnostic-style=source \
// RUN:     -o %t/diag.cir 2>&1 | FileCheck %s --check-prefix=DIAG --allow-empty

// Two TUs define a base class with the same name and different members; derived
// classes in each TU inherit their own version.
// Expected finding: record divergence on the base, naming the member that
// differs.

// Today: Base is renamed in the second input, and Base's constructor
// differs in type, attributes and body.
// The record event says where each definition is, and the first member
// that differs (cir.record_locations).

// CHECK:      "symbols": [
// CHECK:        "symbol": "_ZN4BaseC2Ev",
// CHECK-NEXT:   "op": "cir.func",
// CHECK:          "kind": "weak-weak",
// CHECK:          "kept": {
// CHECK-NEXT:       "input": 0,
// CHECK-NEXT:       "linkage": "linkonce_odr",
// CHECK:          "dropped": {
// CHECK-NEXT:       "input": 1,
// CHECK-NEXT:       "linkage": "linkonce_odr",
// CHECK:          "same_type": false,
// CHECK-NEXT:     "same_attributes": false,
// CHECK-NEXT:     "same_body": false
// CHECK:      "records": [
// CHECK:          "kind": "record-renamed",
// CHECK-NEXT:     "input": 1,
// CHECK-NEXT:     "record": "Base",
// CHECK-NEXT:     "renamed_to": "Base.m1"
// CHECK:      "stats": {
// CHECK-NEXT:   "weak_copies_equal": 0,
// CHECK-NEXT:   "weak_copies_differ": 1,
// CHECK-NEXT:   "weak-weak": 1,
// CHECK-NEXT:   "strong-over-weak": 0,
// CHECK-NEXT:   "rtti-over-nortti": 0,
// CHECK-NEXT:   "strong-strong": 0,
// CHECK-NEXT:   "records_renamed": 1

// --diagnostic-style=source: where each finding points, and the source
// line there.
// DIAG:      {{.*}}evil.cpp:1:8: warning: 'Base' is defined differently than in an earlier input
// DIAG-NEXT: struct Base { int evil = 1; float f = 0; };
// DIAG:      {{.*}}ab.cpp:1:8: note: the earlier definition is here
// DIAG-NEXT: struct Base { int i = 0; };
// DIAG:      {{.*}}evil.cpp:1:35: note: an extra member here: a float
// DIAG-NEXT: struct Base { int evil = 1; float f = 0; };
// DIAG:      {{.*}}ab.cpp:1:8: note: the earlier definition has: none
// DIAG-NEXT: struct Base { int i = 0; };
// DIAG:      {{.*}}evil.cpp:1:8: warning: 'Base::Base()' (_ZN4BaseC2Ev) has different definitions in '{{.*}}ab.o' ({{.*}}ab.cpp) and '{{.*}}evil.o' ({{.*}}evil.cpp) (they differ in type, attributes, body); keeping the one from '{{.*}}ab.o' ({{.*}}ab.cpp)
// DIAG-NEXT: struct Base { int evil = 1; float f = 0; };
// DIAG:      {{.*}}ab.cpp:1:8: note: the definition kept is here
// DIAG-NEXT: struct Base { int i = 0; };
// DIAG:      {{.*}}evil.cpp:1:8: note: the type of 'this' differs here: 'Base' is defined differently in each copy
// DIAG-NEXT: struct Base { int evil = 1; float f = 0; };
// DIAG:      {{.*}}ab.cpp:1:8: note: and here in the kept copy
// DIAG-NEXT: struct Base { int i = 0; };
// DIAG-NOT:  {{warning|note}}:

//--- ab.cpp
struct Base { int i = 0; };
struct A : Base { A(); };
A::A() {}
int read_a(A &a) { return a.i; }
//--- evil.cpp
struct Base { int evil = 1; float f = 0; };
struct EvilTwin : Base { EvilTwin(); };
EvilTwin::EvilTwin() {}
int read_evil(EvilTwin &e) { return e.evil; }
