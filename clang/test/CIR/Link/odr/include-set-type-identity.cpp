// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects -O1 -I%t/with \
// RUN:     -c %t/logger.cpp -o %t/logger.o
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects -O1 \
// RUN:     -c %t/caller.cpp -o %t/caller.o
// RUN: cir-link %t/logger.o %t/caller.o --odr-events=%t/events.json \
// RUN:     -o %t/merged.cir
// RUN: FileCheck %s --input-file=%t/events.json
// RUN: cir-link %t/logger.o %t/caller.o --odr=warning --diagnostic-style=source \
// RUN:     -o %t/diag.cir 2>&1 | FileCheck %s --check-prefix=DIAG --allow-empty
// RUN: cir-opt %t/merged.cir | FileCheck %s --check-prefix=MERGED

// A header picks a type with __has_include, so what the type is depends on the
// include path each TU was compiled with. Two outcomes, both here:
//  - the type's name changes, so a function taking it mangles differently: the
//    definition and the call no longer meet and the link fails on an undefined
//    symbol;
//  - the type keeps its name and changes its members: nothing fails.
// Expected finding: for log_at, a call to a function no input defines while an
// input defines a function of the same name over a different type; for Frame, a
// record divergence.

// Today: Frame is renamed and its inline get_line differs. log_at leaves no
// trace: the definition and the call name two different symbols, so there is
// no choice to record, and the call stays an unresolved declaration (MERGED).
// FIXME: report an unresolved declaration whose name matches a definition
// over a different parameter type.

// CHECK:      "symbols": [
// CHECK:        "symbol": "_ZNK5folly5Frame8get_lineEv",
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
// CHECK-NEXT:     "record": "folly::Frame",
// CHECK-NEXT:     "renamed_to": "folly::Frame.m1"
// CHECK:      "stats": {
// CHECK-NEXT:   "weak_copies_equal": 0,
// CHECK-NEXT:   "weak_copies_differ": 1,
// CHECK-NEXT:   "weak-weak": 1,
// CHECK-NEXT:   "strong-over-weak": 0,
// CHECK-NEXT:   "rtti-over-nortti": 0,
// CHECK-NEXT:   "strong-strong": 0,
// CHECK-NEXT:   "records_renamed": 1

// The definition and the call mangle the parameter type differently.
// MERGED-DAG: cir.func dso_local @_ZN5folly6log_atERKN3exp15source_locationE(
// MERGED-DAG: cir.func private @_ZN5folly6log_atERKNS_15source_locationE(

// --diagnostic-style=source: where each finding points, and the source
// line there.
// DIAG:      {{.*}}sl.h:15:8: warning: 'folly::Frame' is defined differently than in an earlier input
// DIAG-NEXT: struct Frame {
// DIAG:      {{.*}}sl.h:19:12: note: a member differs here: a 32-bit unsigned integer
// DIAG-NEXT: unsigned line;
// DIAG:      {{.*}}sl.h:17:15: note: the earlier definition has: a pointer
// DIAG-NEXT: const char *function;
// DIAG:      {{.*}}sl.h:20:3: warning: 'folly::Frame::get_line() const' (_ZNK5folly5Frame8get_lineEv) has different definitions in '{{.*}}logger.o' ({{.*}}logger.cpp) and '{{.*}}caller.o' ({{.*}}caller.cpp) (they differ in type, attributes, body); keeping the one from '{{.*}}logger.o' ({{.*}}logger.cpp)
// DIAG-NEXT: unsigned get_line() const { return line; }
// DIAG:      {{.*}}sl.h:20:12: note: the type of 'this' differs here: 'folly::Frame' is defined differently in each copy
// DIAG-NEXT: unsigned get_line() const { return line; }
// DIAG:      {{.*}}sl.h:20:12: note: and here in the kept copy
// DIAG-NOT:  {{warning|note}}:

//--- with/experimental_sl.h
namespace exp {
struct source_location {
  const char *file;
  const char *function;
  unsigned line;
};
}
//--- sl.h
#if __has_include(<experimental_sl.h>)
#include <experimental_sl.h>
namespace folly { using source_location = exp::source_location; }
#define FRAME_HAS_FUNCTION 1
#else
namespace folly {
struct source_location {
  const char *file;
  unsigned line;
};
}
#define FRAME_HAS_FUNCTION 0
#endif
namespace folly {
struct Frame {
#if FRAME_HAS_FUNCTION
  const char *function;
#endif
  unsigned line;
  unsigned get_line() const { return line; }
};
void log_at(const source_location &where);
}
//--- logger.cpp
#include "sl.h"
void folly::log_at(const folly::source_location &) {}
unsigned logger_line(const folly::Frame &f) { return f.get_line(); }
//--- caller.cpp
#include "sl.h"
void report(const folly::source_location &where) { folly::log_at(where); }
unsigned caller_line(const folly::Frame &f) { return f.get_line(); }
