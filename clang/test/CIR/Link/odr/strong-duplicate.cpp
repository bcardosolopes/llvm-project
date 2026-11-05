// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects -O1 \
// RUN:     -c %t/ab.cpp -o %t/ab.o
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects -O1 \
// RUN:     -c %t/evil.cpp -o %t/evil.o
// RUN: cir-link %t/ab.o %t/evil.o --odr-events=%t/events.json \
// RUN:     -o %t/merged.cir
// RUN: FileCheck %s --input-file=%t/events.json
// RUN: cir-link %t/ab.o %t/evil.o --odr=warning --diagnostic-style=source \
// RUN:     -o %t/diag.cir 2>&1 | FileCheck %s --check-prefix=DIAG --allow-empty

// Two TUs each define the same external function. In a static link from two
// archives the linker pulls only the member it needs first, so the duplicate is
// never diagnosed and the winner depends on link order.
// Expected finding: strong-strong with different bodies (latent when the copies
// arrive through archives).

// Today: recorded as strong-strong with different bodies.
// FIXME: through two archives only one member is pulled in and cir-link
// never sees the second; reporting the latent duplicate needs the archive
// contents.

// CHECK:      "symbols": [
// CHECK:        "symbol": "_Z9get_valuev",
// CHECK-NEXT:   "op": "cir.func",
// CHECK:          "kind": "strong-strong",
// CHECK:          "kept": {
// CHECK-NEXT:       "input": 0,
// CHECK-NEXT:       "linkage": "external",
// CHECK:          "dropped": {
// CHECK-NEXT:       "input": 1,
// CHECK-NEXT:       "linkage": "external",
// CHECK:          "same_type": true,
// CHECK-NEXT:     "same_attributes": true,
// CHECK-NEXT:     "same_body": false
// CHECK:      "records": [],
// CHECK:      "stats": {
// CHECK-NEXT:   "weak_copies_equal": 0,
// CHECK-NEXT:   "weak_copies_differ": 0,
// CHECK-NEXT:   "weak-weak": 0,
// CHECK-NEXT:   "strong-over-weak": 0,
// CHECK-NEXT:   "rtti-over-nortti": 0,
// CHECK-NEXT:   "strong-strong": 1,
// CHECK-NEXT:   "records_renamed": 0

// --diagnostic-style=source: where each finding points, and the source
// line there.
// DIAG:      {{.*}}evil.cpp:1:1: warning: 'get_value()' (_Z9get_valuev) defined in more than one input; keeping the one from '{{.*}}ab.o' ({{.*}}ab.cpp), ignoring '{{.*}}evil.o' ({{.*}}evil.cpp); they differ in body
// DIAG-NEXT: int get_value() { return 1; }
// DIAG:      {{.*}}ab.cpp:1:1: note: the definition kept is here
// DIAG-NEXT: int get_value() { return 0; }
// DIAG:      {{.*}}evil.cpp:1:26: note: the copies first differ here
// DIAG-NEXT: int get_value() { return 1; }
// DIAG:      {{.*}}ab.cpp:1:26: note: the kept copy has this instead
// DIAG-NEXT: int get_value() { return 0; }
// DIAG-NOT:  {{warning|note}}:

//--- ab.cpp
int get_value() { return 0; }
//--- evil.cpp
int get_value() { return 1; }
