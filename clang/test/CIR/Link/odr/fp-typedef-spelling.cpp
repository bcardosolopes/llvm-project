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

// One TU names a type through a typedef, the other directly. The spelling is
// not part of the type.
// Expected finding: none. Identical weak copies.

// Today: the copies are identical (counted, not listed). This must stay so.

// CHECK:      "symbols": [],
// CHECK:      "records": [],
// CHECK:      "stats": {
// CHECK-NEXT:   "weak_copies_equal": 1,
// CHECK-NEXT:   "weak_copies_differ": 0,
// CHECK-NEXT:   "weak-weak": 0,
// CHECK-NEXT:   "strong-over-weak": 0,
// CHECK-NEXT:   "rtti-over-nortti": 0,
// CHECK-NEXT:   "strong-strong": 0,
// CHECK-NEXT:   "records_renamed": 0

// --diagnostic-style=source: where each finding points, and the source
// line there. Nothing is reported.
// DIAG-NOT:  {{warning|note}}:

//--- tag.h
template <class...> struct Tag {};
namespace feed { enum class Category { Foo }; }
//--- a.cpp
#include "tag.h"
namespace feed { typedef ::feed::Category Category; }
inline int tag_size(Tag<feed::Category>) { return 1; }
int from_a() { return tag_size(Tag<feed::Category>()); }
//--- b.cpp
#include "tag.h"
inline int tag_size(Tag<::feed::Category>) { return 1; }
int from_b() { return tag_size(Tag<::feed::Category>()); }
