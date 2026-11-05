// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects -O1 \
// RUN:     -fomit-frame-pointer -fstack-protector -c %t/a.cpp -o %t/a.o
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects -O1 \
// RUN:     -fno-omit-frame-pointer -fstack-protector-strong \
// RUN:     -fno-delete-null-pointer-checks -c %t/b.cpp -o %t/b.o
// RUN: cir-link %t/a.o %t/b.o --odr-events=%t/events.json \
// RUN:     -o %t/merged.cir
// RUN: FileCheck %s --input-file=%t/events.json
// RUN: cir-link %t/a.o %t/b.o --odr=warning --diagnostic-style=source \
// RUN:     -o %t/diag.cir 2>&1 | FileCheck %s --check-prefix=DIAG --allow-empty

// The precondition, so the test cannot pass by the options no longer reaching
// the CIR: the two copies of sum() do carry different attributes.
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -O1 \
// RUN:     -fomit-frame-pointer -fstack-protector -Xclang -emit-cir -S \
// RUN:     %t/a.cpp -o %t/a.cir
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -O1 \
// RUN:     -fno-omit-frame-pointer -fstack-protector-strong \
// RUN:     -fno-delete-null-pointer-checks -Xclang -emit-cir -S \
// RUN:     %t/b.cpp -o %t/b.cir
// RUN: FileCheck %s --input-file=%t/a.cir --check-prefix=CIR-A
// RUN: FileCheck %s --input-file=%t/b.cir --check-prefix=CIR-B
// CIR-A:     #fn_attr = #cir<extra({{.*}} ssp,
// CIR-A:     @_Z3sumRK4Node(%arg0: !cir.ptr<!rec_Node> {{{.*}}cir.nonnull}
// CIR-B:     #fn_attr = #cir<extra({"frame-pointer" = "non-leaf-no-reserve", {{.*}}"null-pointer-is-valid" = "true", sspstrong,
// CIR-B-NOT: cir.nonnull

// The same inline function from two TUs built with different frame-pointer,
// stack-protector and null-pointer options. The options are not part of the
// definition.
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

//--- node.h
struct Node {
  Node *next;
  int value;
};
void observe(int);
inline int sum(const Node &head) {
  int total = 0;
  for (const Node *n = &head; n; n = n->next)
    total += n->value;
  observe(total);
  return total;
}
//--- a.cpp
#include "node.h"
int from_a(const Node &n) { return sum(n); }
//--- b.cpp
#include "node.h"
int from_b(const Node &n) { return sum(n); }
