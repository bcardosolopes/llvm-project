// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects -O1 -DNEW_ABI=1 \
// RUN:     -c %t/new.cpp -o %t/new.o
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects -O1 -DNEW_ABI=0 \
// RUN:     -c %t/old.cpp -o %t/old.o
// RUN: cir-link %t/new.o %t/old.o --odr-events=%t/events.json \
// RUN:     -o %t/merged.cir
// RUN: FileCheck %s --input-file=%t/events.json
// RUN: cir-link %t/new.o %t/old.o --odr=warning --diagnostic-style=source \
// RUN:     -o %t/diag.cir 2>&1 | FileCheck %s --check-prefix=DIAG --allow-empty

// A library type switches representation on an ABI macro (the shape of
// _GLIBCXX_USE_CXX11_ABI mixing): same mangled names for its inline members,
// different layout.
// Expected finding: record layout divergence, and the inline members that read
// it.

// Today: the record is renamed in the second input, and the inline member
// differs in type, attributes and body.
// The record event says where each definition is, and the first member
// that differs (cir.record_locations).

// CHECK:      "symbols": [
// CHECK:        "symbol": "_ZNK3lib3Str4sizeEv",
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
// CHECK-NEXT:     "record": "lib::Str",
// CHECK-NEXT:     "renamed_to": "lib::Str.m1"
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
// DIAG:      {{.*}}str.h:2:8: warning: 'lib::Str' is defined differently than in an earlier input
// DIAG-NEXT: struct Str {
// DIAG:      {{.*}}str.h:2:8: note: a member is missing here
// DIAG:      {{.*}}str.h:5:17: note: the earlier definition has: a 64-bit unsigned integer
// DIAG-NEXT: unsigned long len;
// DIAG:      {{.*}}str.h:10:3: warning: 'lib::Str::size() const' (_ZNK3lib3Str4sizeEv) has different definitions in '{{.*}}new.o' ({{.*}}new.cpp) and '{{.*}}old.o' ({{.*}}old.cpp) (they differ in type, attributes, body); keeping the one from '{{.*}}new.o' ({{.*}}new.cpp)
// DIAG-NEXT: unsigned long size() const { return ((unsigned long *)ptr)[-3]; }
// DIAG:      {{.*}}str.h:7:3: note: the definition kept is here
// DIAG-NEXT: unsigned long size() const { return len; }
// DIAG:      {{.*}}str.h:10:17: note: the type of 'this' differs here: 'lib::Str' is defined differently in each copy
// DIAG-NEXT: unsigned long size() const { return ((unsigned long *)ptr)[-3]; }
// DIAG:      {{.*}}str.h:7:17: note: and here in the kept copy
// DIAG-NEXT: unsigned long size() const { return len; }
// DIAG-NOT:  {{warning|note}}:

//--- str.h
namespace lib {
struct Str {
#if NEW_ABI
  char *ptr;
  unsigned long len;
  char local[16];
  unsigned long size() const { return len; }
#else
  char *ptr;
  unsigned long size() const { return ((unsigned long *)ptr)[-3]; }
#endif
};
}
//--- new.cpp
#include "str.h"
unsigned long new_size(const lib::Str &s) { return s.size(); }
//--- old.cpp
#include "str.h"
unsigned long old_size(const lib::Str &s) { return s.size(); }
