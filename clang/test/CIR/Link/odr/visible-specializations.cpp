// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects -O1 \
// RUN:     -c %t/plain.cpp -o %t/plain.o
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects -O1 \
// RUN:     -c %t/special.cpp -o %t/special.o
// RUN: cir-link %t/plain.o %t/special.o --odr-events=%t/events.json \
// RUN:     -o %t/merged.cir
// RUN: FileCheck %s --input-file=%t/events.json
// RUN: cir-link %t/plain.o %t/special.o --odr=warning --diagnostic-style=source \
// RUN:     -o %t/diag.cir 2>&1 | FileCheck %s --check-prefix=DIAG --allow-empty

// Same header text, but one TU also sees a specialization of a helper that the
// other does not, so the inline member that calls the helper resolves
// differently in each TU.
// Expected finding: strong-over-weak on describe<char> -- the explicit
// specialization one TU sees displaces the instantiation the other TU made, with
// a different body; Box<char>::kind is the same definition in both.

// Today: the specialization shows up as strong-over-weak on describe<char>.
// Box<char>::kind calls the same mangled name in both TUs, so its bodies
// compare equal, and it is an identical copy: special.cpp infers it does not
// throw and plain.cpp does not, and an inferred nothrow is not part of the
// definition.

// CHECK:      "symbols": [
// CHECK:        "symbol": "_Z8describeIcEiT_",
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
// CHECK-NOT:    "symbol": "_ZNK3BoxIcE4kindEv",
// CHECK:      "records": [],
// CHECK:      "stats": {
// CHECK-NEXT:   "weak_copies_equal": 1,
// CHECK-NEXT:   "weak_copies_differ": 0,
// CHECK-NEXT:   "weak-weak": 0,
// CHECK-NEXT:   "strong-over-weak": 1,
// CHECK-NEXT:   "rtti-over-nortti": 0,
// CHECK-NEXT:   "strong-strong": 0,
// CHECK-NEXT:   "records_renamed": 0

// --diagnostic-style=source: where each finding points, and the source
// line there.
// DIAG:      {{.*}}box.h:1:20: warning: 'int describe<char>(char)' (_Z8describeIcEiT_): the strong definition from '{{.*}}special.o' ({{.*}}special.cpp) replaces an inline definition from '{{.*}}plain.o' ({{.*}}plain.cpp) that differs in body
// DIAG-NEXT: template <class T> int describe(T) { return 0; }
// DIAG:      {{.*}}special.cpp:2:1: note: the definition kept is here
// DIAG-NEXT: template <> int describe<char>(char) { return 1; }
// DIAG:      {{.*}}box.h:1:45: note: the copies first differ here
// DIAG-NEXT: template <class T> int describe(T) { return 0; }
// DIAG:      {{.*}}special.cpp:2:47: note: the kept copy has this instead
// DIAG-NEXT: template <> int describe<char>(char) { return 1; }
// DIAG-NOT:  {{warning|note}}:

//--- box.h
template <class T> int describe(T) { return 0; }
template <class T> struct Box {
  T v;
  int kind() const { return describe(v); }
};
//--- plain.cpp
#include "box.h"
int plain_kind(const Box<char> &b) { return b.kind(); }
//--- special.cpp
#include "box.h"
template <> int describe<char>(char) { return 1; }
int special_kind(const Box<char> &b) { return b.kind(); }
