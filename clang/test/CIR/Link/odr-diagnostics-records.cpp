// A record has no position of its own in CIR. CIRGen records where each one
// is defined, and where each member is (cir.record_locations, in fat
// objects), so a record two TUs define differently is reported at both
// definitions and at the first member that differs.
//
// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects -O1 \
// RUN:     -c %t/flat.cpp -o %t/flat.o
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects -O1 \
// RUN:     -DTHREE_D=1 -c %t/deep.cpp -o %t/deep.o

// RUN: cir-link %t/flat.o %t/deep.o --odr=warning --diagnostic-style=source \
// RUN:     -o %t/source.cir 2>&1 | FileCheck %s --check-prefix=SOURCE
// SOURCE:      point.h:1:8: warning: 'Point' is defined differently than in an earlier input
// SOURCE-NEXT: struct Point {
// SOURCE-NEXT: ^
// SOURCE-NEXT: point.h:4:9: note: a member differs here: a float
// SOURCE-NEXT:   float z;
// SOURCE-NEXT:   ^
// SOURCE-NEXT: point.h:6:7: note: the earlier definition has: a 32-bit signed integer
// SOURCE-NEXT:   int y;
// SOURCE-NEXT:   ^
// A function that takes the record says so.
// SOURCE-NEXT: point.h:9:1: warning: 'sum(Point const&)' (_Z3sumRK5Point) has different definitions in '{{.*}}flat.o' ({{.*}}flat.cpp) and '{{.*}}deep.o' ({{.*}}deep.cpp) (they differ in type, attributes, body)
// SOURCE:      point.h:9:16: note: the type of parameter 1 differs here: 'Point' is defined differently in each copy
// SOURCE:      point.h:9:16: note: and here in the kept copy
// SOURCE-NOT:  {{warning|note}}:

// RUN: cir-link %t/flat.o %t/deep.o --odr=warning --diagnostic-style=mixed \
// RUN:     -o %t/mixed.cir 2>&1 | FileCheck %s --check-prefix=MIXED
// MIXED:      point.h:1:8: warning: record 'Point' has a different body than in an earlier input; renamed to 'Point.m1'
// MIXED:      point.h:4:9: note: member 2 differs here: '!cir.float'
// MIXED:      point.h:6:7: note: the earlier definition has: '!cir.int<s, 32>'

// The default names no source; the member is still shown.
// RUN: cir-link %t/flat.o %t/deep.o --odr=warning -o %t/cir.cir 2>&1 \
// RUN:     | FileCheck %s --check-prefix=CIR
// CIR-NOT: point.h
// CIR:     deep.o:0:0: warning: record 'Point' has a different body than in an earlier input; renamed to 'Point.m1'
// CIR-NEXT: flat.o:0:0: note: the earlier definition is here
// CIR-NEXT: deep.o:0:0: note: member 2 differs here: '!cir.float'
// CIR-NEXT: flat.o:0:0: note: the earlier definition has: '!cir.int<s, 32>'

// RUN: cir-link %t/flat.o %t/deep.o --diagnostic-style=source \
// RUN:     --odr-events=%t/events.json -o %t/events.cir
// RUN: FileCheck %s --check-prefix=JSON --input-file=%t/events.json
// JSON:      "records": [
// JSON:        "record": "Point",
// JSON-NEXT:   "renamed_to": "Point.m1",
// JSON-NEXT:   "severity": "ignore",
// JSON-NEXT:   "kept_input": 0,
// JSON-NEXT:   "location": {
// JSON-NEXT:     "file": "{{.*}}point.h",
// JSON-NEXT:     "line": 1,
// JSON-NEXT:     "column": 8
// JSON:        "member_difference": {
// JSON-NEXT:     "what": "member 2",
// JSON-NEXT:     "source_what": "a member differs",
// JSON-NEXT:     "kept": {
// JSON-NEXT:       "location": {
// JSON-NEXT:         "file": "{{.*}}point.h",
// JSON-NEXT:         "line": 6,
// JSON-NEXT:         "column": 7
// JSON-NEXT:       },
// JSON-NEXT:       "text": "'!cir.int<s, 32>'",
// JSON-NEXT:       "source_text": "a 32-bit signed integer"
// JSON-NEXT:     },
// JSON-NEXT:     "dropped": {
// JSON-NEXT:       "location": {
// JSON-NEXT:         "file": "{{.*}}point.h",
// JSON-NEXT:         "line": 4,
// JSON-NEXT:         "column": 9

//--- point.h
struct Point {
  int x;
#if THREE_D
  float z;
#endif
  int y;
};

inline int sum(const Point &p) { return p.x + p.y; }

//--- flat.cpp
#include "point.h"
int flat() { return sum(Point{1, 2}); }

//--- deep.cpp
#include "point.h"
int deep() { Point p{}; return sum(p); }
