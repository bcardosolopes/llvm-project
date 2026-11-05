// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects -O1 -DWITH_EXTRA=1 \
// RUN:     -c %t/with.cpp -o %t/with.o
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects -O1 -DWITH_EXTRA=0 \
// RUN:     -c %t/without.cpp -o %t/without.o
// RUN: cir-link %t/with.o %t/without.o --odr-events=%t/events.json \
// RUN:     -o %t/merged.cir
// RUN: FileCheck %s --input-file=%t/events.json
// RUN: cir-link %t/with.o %t/without.o --odr=warning --diagnostic-style=source \
// RUN:     -o %t/diag.cir 2>&1 | FileCheck %s --check-prefix=DIAG --allow-empty

// A virtual function exists only under a macro, so the vtable layout and every
// virtual call's slot index differ between TUs.
// Expected finding: vtable divergence on the class, and record divergence.

// Today: the vtable global differs in type (one slot more), and the
// constructor that stores the vtable pointer differs in body.
// FIXME: the record is not renamed: both definitions have the same data
// layout (one vptr), so record unification cannot see the difference; only
// the vtable does.

// CHECK:      "symbols": [
// CHECK:        "symbol": "_ZTV6Object",
// CHECK-NEXT:   "op": "cir.global",
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
// CHECK:        "symbol": "_ZN6ObjectC2Ev",
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
// CHECK-NEXT:   "weak_copies_equal": 3,
// CHECK-NEXT:   "weak_copies_differ": 2,
// CHECK-NEXT:   "weak-weak": 2,
// CHECK-NEXT:   "strong-over-weak": 0,
// CHECK-NEXT:   "rtti-over-nortti": 0,
// CHECK-NEXT:   "strong-strong": 0,
// CHECK-NEXT:   "records_renamed": 0

// --diagnostic-style=source: where each finding points, and the source
// line there.
// DIAG:      {{.*}}object.h:1:1: warning: 'vtable for Object' (_ZTV6Object) has different definitions in '{{.*}}with.o' ({{.*}}with.cpp) and '{{.*}}without.o' ({{.*}}without.cpp) (they differ in type, body); keeping the one from '{{.*}}with.o' ({{.*}}with.cpp)
// DIAG-NEXT: struct Object {
// DIAG:      {{.*}}object.h:1:1: note: the size of the vtable differs here: 3 elements
// DIAG:      {{.*}}object.h:1:1: note: the kept copy has: 4 elements
// DIAG:      {{.*}}object.h:1:1: note: the vtable differs here: 'Object::api() const'
// DIAG:      {{.*}}object.h:1:1: note: the kept copy has: 'Object::extra() const'
// DIAG:      {{.*}}object.h:1:8: warning: 'Object::Object()' (_ZN6ObjectC2Ev) has different definitions in '{{.*}}with.o' ({{.*}}with.cpp) and '{{.*}}without.o' ({{.*}}without.cpp) (they differ in body); keeping the one from '{{.*}}with.o' ({{.*}}with.cpp)
// DIAG-NEXT: struct Object {
// DIAG:      {{.*}}object.h:1:1: note: the copies first differ here
// DIAG-NEXT: struct Object {
// DIAG:      {{.*}}object.h:1:1: note: the kept copy has this instead
// DIAG-NOT:  {{warning|note}}:

//--- object.h
struct Object {
#if WITH_EXTRA
  virtual int extra() const { return 2; }
#endif
  virtual int api() const { return 1; }
};
//--- with.cpp
#include "object.h"
int call_with(const Object &o) { return o.api(); }
Object *make_with() { return new Object; }
//--- without.cpp
#include "object.h"
int call_without(const Object &o) { return o.api(); }
Object *make_without() { return new Object; }
