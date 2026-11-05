// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects -O1 \
// RUN:     -c %t/user.cpp -o %t/user.o
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects -O1 \
// RUN:     -c %t/special.cpp -o %t/special.o
// RUN: cir-link %t/user.o %t/special.o --odr-events=%t/events.json \
// RUN:     -o %t/merged.cir
// RUN: FileCheck %s --input-file=%t/events.json
// RUN: cir-link %t/user.o %t/special.o --odr=warning --diagnostic-style=source \
// RUN:     -o %t/diag.cir 2>&1 | FileCheck %s --check-prefix=DIAG --allow-empty

// One TU explicitly specializes a function template; another TU never sees the
// specialization and implicitly instantiates the primary template. The program
// is ill-formed, no diagnostic required, and which body a caller gets depends
// on the link (and on inlining).
// Expected finding: strong-over-weak, an explicit specialization (strong)
// displacing an implicit instantiation (linkonce_odr) with a different body.

// Today: recorded as strong-over-weak, external over linkonce_odr, bodies
// differ.

// CHECK:      "symbols": [
// CHECK:        "symbol": "_Z3runIiEii",
// CHECK-NEXT:   "op": "cir.func",
// CHECK:          "kind": "strong-over-weak",
// CHECK:          "kept": {
// CHECK-NEXT:       "input": 1,
// CHECK-NEXT:       "linkage": "external",
// CHECK:          "dropped": {
// CHECK-NEXT:       "input": 0,
// CHECK-NEXT:       "linkage": "linkonce_odr",
// CHECK:          "same_type": true,
// CHECK-NEXT:     "same_attributes": true,
// CHECK-NEXT:     "same_body": false
// CHECK:      "records": [],
// CHECK:      "stats": {
// CHECK-NEXT:   "weak_copies_equal": 0,
// CHECK-NEXT:   "weak_copies_differ": 0,
// CHECK-NEXT:   "weak-weak": 0,
// CHECK-NEXT:   "strong-over-weak": 1,
// CHECK-NEXT:   "rtti-over-nortti": 0,
// CHECK-NEXT:   "strong-strong": 0,
// CHECK-NEXT:   "records_renamed": 0

// --diagnostic-style=source: where each finding points, and the source
// line there.
// DIAG:      {{.*}}header.h:1:20: warning: 'int run<int>(int)' (_Z3runIiEii): the strong definition from '{{.*}}special.o' ({{.*}}special.cpp) replaces an inline definition from '{{.*}}user.o' ({{.*}}user.cpp) that differs in body
// DIAG-NEXT: template <class T> int run(int i) { return i == 3 ? 0 : 2; }
// DIAG:      {{.*}}special.cpp:2:1: note: the definition kept is here
// DIAG-NEXT: template <> int run<int>(int) { return 1; }
// DIAG:      {{.*}}header.h:1:44: note: the copies first differ here
// DIAG-NEXT: template <class T> int run(int i) { return i == 3 ? 0 : 2; }
// DIAG:      {{.*}}special.cpp:2:40: note: the kept copy has this instead
// DIAG-NEXT: template <> int run<int>(int) { return 1; }
// DIAG-NOT:  {{warning|note}}:

//--- header.h
template <class T> int run(int i) { return i == 3 ? 0 : 2; }
//--- user.cpp
#include "header.h"
int use_run(int i) { return run<int>(i); }
//--- special.cpp
#include "header.h"
template <> int run<int>(int) { return 1; }
