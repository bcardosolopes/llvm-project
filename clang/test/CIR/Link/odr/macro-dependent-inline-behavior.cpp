// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects -O1 -DBATCH_DRIVER \
// RUN:     -c %t/batch.cpp -o %t/batch.o
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects -O1 \
// RUN:     -c %t/online.cpp -o %t/online.o
// RUN: cir-link %t/batch.o %t/online.o --odr-events=%t/events.json \
// RUN:     -o %t/merged.cir
// RUN: FileCheck %s --input-file=%t/events.json
// RUN: cir-link %t/batch.o %t/online.o --odr=warning --diagnostic-style=source \
// RUN:     -o %t/diag.cir 2>&1 | FileCheck %s --check-prefix=DIAG --allow-empty

// The same source compiled twice with a different -D: the inline function's
// behavior depends on the macro, and both copies share one mangled name.
// Expected finding: weak-weak, same type, different body.

// Today: recorded as a weak-weak pair whose bodies differ.

// CHECK:      "symbols": [
// CHECK:        "symbol": "_Z7processi",
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
// DIAG:      {{.*}}process.h:1:1: warning: 'process(int)' (_Z7processi) has different definitions in '{{.*}}batch.o' ({{.*}}batch.cpp) and '{{.*}}online.o' ({{.*}}online.cpp) (they differ in body); keeping the one from '{{.*}}batch.o' ({{.*}}batch.cpp)
// DIAG-NEXT: inline int process(int x) {
// DIAG:      {{.*}}process.h:5:14: note: the copies first differ here
// DIAG-NEXT: return x + 1;
// DIAG:      {{.*}}process.h:3:14: note: the kept copy has this instead
// DIAG-NEXT: return x * 2;
// DIAG-NOT:  {{warning|note}}:

//--- process.h
inline int process(int x) {
#ifdef BATCH_DRIVER
  return x * 2;
#else
  return x + 1;
#endif
}
//--- batch.cpp
#include "process.h"
int batch(int x) { return process(x); }
//--- online.cpp
#include "process.h"
int online(int x) { return process(x); }
