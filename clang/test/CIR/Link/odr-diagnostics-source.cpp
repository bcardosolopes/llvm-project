// ODR findings point back at the source, so that whoever reads them -- a
// person or a tool -- can go straight to the definitions and to what differs.
//
// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects -O1 \
// RUN:     -DWIDTH_T=short -c %t/a.cpp -o %t/a.o
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects -O1 \
// RUN:     -DMODE=1 -DWIDTH_T=int -c %t/b.cpp -o %t/b.o

// --diagnostic-style=source: each finding at the dropped definition in the
// source, the kept one in a note, and each copy's first difference -- the
// statement, the parameter -- with the source line printed under it, in
// source terms only: no CIR. Inputs are named with their translation unit.
// RUN: cir-link %t/a.o %t/b.o --odr=warning --diagnostic-style=source \
// RUN:     -o %t/source.cir 2>&1 | FileCheck %s --check-prefix=SOURCE
// RUN: cir-link %t/a.o %t/b.o --odr=warning --diagnostic-style=source \
// RUN:     -o %t/source-again.cir 2>&1 | not grep -E '%%[0-9]+ =|[ (]cir[.][a-z]|!cir[.]|#cir[.]'

// SOURCE:      header.h:5:1: warning: 'scale(int)' (_Z5scalei) has different definitions in '{{.*}}a.o' ({{.*}}a.cpp) and '{{.*}}b.o' ({{.*}}b.cpp) (they differ in body); keeping the one from '{{.*}}a.o' ({{.*}}a.cpp)
// SOURCE-NEXT: inline int scale(int x) {
// SOURCE-NEXT: ^
// SOURCE-NEXT: header.h:7:14: note: the copies first differ here
// SOURCE-NEXT:   return x + 1;
// SOURCE-NEXT:              ^
// SOURCE-NEXT: header.h:9:14: note: the kept copy has this instead
// SOURCE-NEXT:   return x * 2;
// SOURCE-NEXT:              ^
// SOURCE-NEXT: header.h:14:1: warning: 'widen(Width)' (_Z5widen5Width) has different definitions in '{{.*}}a.o' ({{.*}}a.cpp) and '{{.*}}b.o' ({{.*}}b.cpp) (they differ in type, body); keeping the one from '{{.*}}a.o' ({{.*}}a.cpp)
// SOURCE-NEXT: inline int widen(Width w) { return (int)w; }
// SOURCE-NEXT: ^
// SOURCE-NEXT: header.h:14:18: note: the type of parameter 1 differs here: a 32-bit signed integer
// SOURCE-NEXT: inline int widen(Width w) { return (int)w; }
// SOURCE-NEXT:                  ^
// SOURCE-NEXT: header.h:14:18: note: the kept copy has: a 16-bit signed integer
// SOURCE-NEXT: header.h:16:20: warning: 'int pick<char>(char)' (_Z4pickIcEiT_): the strong definition from '{{.*}}a.o' ({{.*}}a.cpp) replaces an inline definition from '{{.*}}b.o' ({{.*}}b.cpp) that differs in body
// SOURCE-NEXT: template <class T> int pick(T) { return 0; }
// SOURCE-NEXT:                    ^
// SOURCE-NEXT: a.cpp:2:1: note: the definition kept is here
// SOURCE-NEXT: template <> int pick<char>(char) { return 1; }
// SOURCE-NEXT: ^
// SOURCE-NEXT: header.h:16:41: note: the copies first differ here
// SOURCE-NEXT: template <class T> int pick(T) { return 0; }
// SOURCE-NEXT:                                         ^
// SOURCE-NEXT: a.cpp:2:43: note: the kept copy has this instead
// SOURCE-NEXT: template <> int pick<char>(char) { return 1; }
// SOURCE-NEXT:                                           ^
// SOURCE-NOT:  {{warning|note}}:

// --diagnostic-style=mixed: the same positions, showing the CIR that differs
// -- for whoever works on the compiler.
// RUN: cir-link %t/a.o %t/b.o --odr=warning --diagnostic-style=mixed \
// RUN:     -o %t/mixed.cir 2>&1 | FileCheck %s --check-prefix=MIXED

// MIXED:      header.h:5:1: warning: 'scale(int)' (_Z5scalei) has different definitions in '{{.*}}a.o' ({{.*}}a.cpp) and '{{.*}}b.o' ({{.*}}b.cpp) (they differ in body); keeping the one from '{{.*}}a.o' ({{.*}}a.cpp)
// MIXED-NEXT: inline int scale(int x) {
// MIXED-NEXT: ^
// MIXED-NEXT: header.h:7:14: note: the body differs here: %{{.*}} = cir.const #cir.int<1> : !cir.int<s, 32>
// MIXED-NEXT:   return x + 1;
// MIXED-NEXT:              ^
// MIXED-NEXT: header.h:9:14: note: the kept copy has: %{{.*}} = cir.const #cir.int<2> : !cir.int<s, 32>
// MIXED-NEXT:   return x * 2;
// MIXED-NEXT:              ^
// MIXED-NEXT: header.h:14:1: warning: 'widen(Width)' (_Z5widen5Width) has different definitions in '{{.*}}a.o' ({{.*}}a.cpp) and '{{.*}}b.o' ({{.*}}b.cpp) (they differ in type, body); keeping the one from '{{.*}}a.o' ({{.*}}a.cpp)
// MIXED-NEXT: inline int widen(Width w) { return (int)w; }
// MIXED-NEXT: ^
// MIXED-NEXT: header.h:14:18: note: parameter 1 differs here: '!cir.int<s, 32>'
// MIXED-NEXT: inline int widen(Width w) { return (int)w; }
// MIXED-NEXT:                  ^
// MIXED-NEXT: header.h:14:18: note: the kept copy has: '!cir.int<s, 16>'
// MIXED-NEXT: header.h:16:20: warning: 'int pick<char>(char)' (_Z4pickIcEiT_): the strong definition from '{{.*}}a.o' ({{.*}}a.cpp) replaces a linkonce_odr definition from '{{.*}}b.o' ({{.*}}b.cpp) that differs in body
// MIXED-NEXT: template <class T> int pick(T) { return 0; }
// MIXED-NEXT:                    ^
// MIXED-NEXT: a.cpp:2:1: note: the definition kept is here
// MIXED-NEXT: template <> int pick<char>(char) { return 1; }
// MIXED-NEXT: ^
// MIXED-NEXT: header.h:16:41: note: the body differs here: %{{.*}} = cir.const #cir.int<0> : !cir.int<s, 32>
// MIXED-NEXT: template <class T> int pick(T) { return 0; }
// MIXED-NEXT:                                         ^
// MIXED-NEXT: a.cpp:2:43: note: the kept copy has: %{{.*}} = cir.const #cir.int<1> : !cir.int<s, 32>
// MIXED-NEXT: template <> int pick<char>(char) { return 1; }
// MIXED-NEXT:                                           ^
// MIXED-NOT:  {{warning|note}}:

// The default, cir: there may be no source to point back to, so nothing is
// said about one -- every position is the input's own -- and what differs is
// still shown.
// RUN: cir-link %t/a.o %t/b.o --odr=warning -o %t/cir.cir 2>&1 \
// RUN:     | FileCheck %s --check-prefix=CIR
// CIR-NOT: {{\.cpp:|\.h:|\(.*\.cpp\)}}
// CIR:     b.o:0:0: warning: 'scale(int)' (_Z5scalei) has different definitions in '{{.*}}a.o' and '{{.*}}b.o' (they differ in body); keeping the one from '{{.*}}a.o'
// CIR:     b.o:0:0: note: the body differs here: %{{.*}} = cir.const #cir.int<1> : !cir.int<s, 32>
// CIR:     a.o:0:0: note: the kept copy has: %{{.*}} = cir.const #cir.int<2> : !cir.int<s, 32>
// CIR:     b.o:0:0: warning: 'widen(Width)'
// CIR:     b.o:0:0: note: parameter 1 differs here: '!cir.int<s, 32>'
// CIR:     b.o:0:0: warning: 'int pick<char>(char)'
// CIR:     a.o:0:0: note: the definition kept is here

// --diagnostic-path-prefix-map rewrites the paths, in the diagnostics and in
// the record, as -fdebug-prefix-map does. A file that cannot be read at its
// new path still gets its position; there is just no source line under it.
// RUN: cir-link %t/a.o %t/b.o --odr=warning --diagnostic-style=source \
// RUN:     --diagnostic-path-prefix-map=%t=/elsewhere/src \
// RUN:     --odr-events=%t/mapped.json -o %t/mapped.cir 2>&1 \
// RUN:     | FileCheck %s --check-prefix=MAPPED
// MAPPED:      /elsewhere/src/header.h:5:1: warning: 'scale(int)' (_Z5scalei) has different definitions in '/elsewhere/src/a.o' (/elsewhere/src/a.cpp) and '/elsewhere/src/b.o' (/elsewhere/src/b.cpp)
// MAPPED-NEXT: /elsewhere/src/header.h:7:14: note: the copies first differ here
// MAPPED-NEXT: /elsewhere/src/header.h:9:14: note: the kept copy has this instead
// RUN: FileCheck %s --check-prefix=MAPPEDJSON --input-file=%t/mapped.json
// MAPPEDJSON: "source": "/elsewhere/src/a.cpp"
// MAPPEDJSON: "file": "/elsewhere/src/header.h"
// MAPPEDJSON-NOT: "file": "{{.*}}Output

// A mapping needs both halves.
// RUN: not cir-link %t/a.o %t/b.o --diagnostic-path-prefix-map=nothing \
// RUN:     -o %t/bad.cir 2>&1 | FileCheck %s --check-prefix=BADMAP
// BADMAP: for the --diagnostic-path-prefix-map option: expected <old>=<new>, got 'nothing'

// The record carries the same locations, and the differences in both terms
// whatever the style, for a tool to act on without parsing the messages.
// RUN: cir-link %t/a.o %t/b.o --diagnostic-style=source \
// RUN:     --odr-events=%t/events.json -o %t/events.cir
// RUN: FileCheck %s --check-prefix=JSON --input-file=%t/events.json
// JSON:      "path": "{{.*}}a.o",
// JSON:      "source": "{{.*}}a.cpp"
// JSON:      "symbol": "_Z5scalei",
// JSON:        "kept": {
// JSON:          "location": {
// JSON-NEXT:       "file": "{{.*}}header.h",
// JSON-NEXT:       "line": 5,
// JSON-NEXT:       "column": 1
// JSON:        "body_difference": {
// JSON-NEXT:     "what": "the body",
// JSON-NEXT:     "source_what": "the copies first differ",
// JSON-NEXT:     "kept": {
// JSON-NEXT:       "location": {
// JSON-NEXT:         "file": "{{.*}}header.h",
// JSON-NEXT:         "line": 9,
// JSON-NEXT:         "column": 14
// JSON-NEXT:       },
// JSON-NEXT:       "text": "%{{.*}} = cir.const #cir.int<2> : !cir.int<s, 32>"
// JSON-NEXT:     },
// JSON-NEXT:     "dropped": {
// JSON-NEXT:       "location": {
// JSON-NEXT:         "file": "{{.*}}header.h",
// JSON-NEXT:         "line": 7,
// JSON-NEXT:         "column": 14
// JSON:      "symbol": "_Z5widen5Width",
// JSON:        "type_difference": {
// JSON-NEXT:     "what": "parameter 1",
// JSON-NEXT:     "source_what": "the type of parameter 1 differs",
// JSON:          "dropped": {
// JSON:            "text": "'!cir.int<s, 32>'",
// JSON-NEXT:       "source_text": "a 32-bit signed integer"

// The same findings, in the same order, however many inputs are read at once.
// RUN: cir-link %t/a.o %t/b.o --odr=warning --diagnostic-style=source \
// RUN:     -j 1 -o %t/j1.cir 2> %t/j1.txt
// RUN: cir-link %t/a.o %t/b.o --odr=warning --diagnostic-style=source \
// RUN:     -j 4 -o %t/j4.cir 2> %t/j4.txt
// RUN: diff %t/j1.txt %t/j4.txt

//--- header.h
#ifndef MODE
#define MODE 0
#endif

inline int scale(int x) {
#if MODE
  return x + 1;
#else
  return x * 2;
#endif
}

enum class Width : WIDTH_T { Narrow };
inline int widen(Width w) { return (int)w; }

template <class T> int pick(T) { return 0; }

//--- a.cpp
#include "header.h"
template <> int pick<char>(char) { return 1; }
int useA(int i) { return scale(i) + widen(Width::Narrow) + pick('a'); }

//--- b.cpp
#include "header.h"
int useB(int i) { return scale(i) + widen(Width::Narrow) + pick('b'); }
