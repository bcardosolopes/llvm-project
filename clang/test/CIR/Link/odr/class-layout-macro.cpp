// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects -O1 -DWITH_COUNTER=1 \
// RUN:     -c %t/with.cpp -o %t/with.o
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects -O1 -DWITH_COUNTER=0 \
// RUN:     -c %t/without.cpp -o %t/without.o
// RUN: cir-link %t/with.o %t/without.o --odr-events=%t/events.json \
// RUN:     -o %t/merged.cir
// RUN: FileCheck %s --input-file=%t/events.json
// RUN: cir-link %t/with.o %t/without.o --odr=warning --diagnostic-style=source \
// RUN:     -o %t/diag.cir 2>&1 | FileCheck %s --check-prefix=DIAG --allow-empty

// A class gains a member under a macro that only some TUs define. Its member
// functions are inline and shared, but each TU computes field offsets from its
// own layout.
// Expected finding: record layout divergence on the class (with the member that
// is missing), and the inline members that touch it.

// Today: the record is renamed in the second input, and the inline member
// differs in type (it takes the renamed record), attributes (its
// dereferenceable size) and body.
// The record event says where each definition is, and the first member
// that differs (cir.record_locations).

// CHECK:      "symbols": [
// CHECK:        "symbol": "_ZNK6Object3getEv",
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
// CHECK-NEXT:     "record": "Object",
// CHECK-NEXT:     "renamed_to": "Object.m1"
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
// DIAG:      {{.*}}object.h:1:8: warning: 'Object' is defined differently than in an earlier input
// DIAG-NEXT: struct Object {
// DIAG:      {{.*}}object.h:1:8: note: a member is missing here
// DIAG:      {{.*}}object.h:5:7: note: the earlier definition has: a 32-bit signed integer
// DIAG-NEXT: int value;
// DIAG:      {{.*}}object.h:6:3: warning: 'Object::get() const' (_ZNK6Object3getEv) has different definitions in '{{.*}}with.o' ({{.*}}with.cpp) and '{{.*}}without.o' ({{.*}}without.cpp) (they differ in type, attributes, body); keeping the one from '{{.*}}with.o' ({{.*}}with.cpp)
// DIAG-NEXT: int get() const { return value; }
// DIAG:      {{.*}}object.h:6:7: note: the type of 'this' differs here: 'Object' is defined differently in each copy
// DIAG-NEXT: int get() const { return value; }
// DIAG:      {{.*}}object.h:6:7: note: and here in the kept copy
// DIAG-NOT:  {{warning|note}}:

//--- object.h
struct Object {
#if WITH_COUNTER
  int counter;
#endif
  int value;
  int get() const { return value; }
};
//--- with.cpp
#include "object.h"
int read_with(const Object &o) { return o.get(); }
//--- without.cpp
#include "object.h"
int read_without(const Object &o) { return o.get(); }
