// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects -O1 \
// RUN:     -c %t/fork.cpp -o %t/fork.o
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects -O1 \
// RUN:     -c %t/upstream.cpp -o %t/upstream.o
// RUN: cir-link %t/fork.o %t/upstream.o --odr-events=%t/events.json \
// RUN:     -o %t/merged.cir
// RUN: FileCheck %s --input-file=%t/events.json
// RUN: cir-link %t/fork.o %t/upstream.o --odr=warning --diagnostic-style=source \
// RUN:     -o %t/diag.cir 2>&1 | FileCheck %s --check-prefix=DIAG --allow-empty

// Two copies of one library end up in the same binary -- a fork and the upstream
// it came from, both defining the same namespace. The classes share names and
// mangled symbols but not layouts or virtual interfaces, and each copy defines
// its key function, so every vtable, type_info and destructor is a strong
// duplicate. Callers compiled against one copy dispatch through the other's
// vtable.
// Expected finding: strong-strong on the vtable, type_info and destructors with
// different contents, and a record divergence on the class.

// Today: the vtable and two of the destructors are strong-strong pairs with
// different bodies; type_info and the complete-object destructor compare
// equal in body (they only name the class), the destructors differ in type
// because the class is renamed in the second input, and the inline read
// differs as a weak-weak pair.
// The record event says where each class is defined and the first member
// that differs.
// FIXME: nothing ties the duplicates to one library copy.

// CHECK:      "symbols": [
// CHECK:        "symbol": "_ZTVN6planet6saturn9telescope10TTelescopeE",
// CHECK-NEXT:   "op": "cir.global",
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
// CHECK:        "symbol": "_ZTSN6planet6saturn9telescope10TTelescopeE",
// CHECK-NEXT:   "op": "cir.global",
// CHECK:          "kind": "strong-strong",
// CHECK:          "kept": {
// CHECK-NEXT:       "input": 0,
// CHECK-NEXT:       "linkage": "external",
// CHECK:          "dropped": {
// CHECK-NEXT:       "input": 1,
// CHECK-NEXT:       "linkage": "external",
// CHECK:          "same_type": true,
// CHECK-NEXT:     "same_attributes": true,
// CHECK-NEXT:     "same_body": true
// CHECK:        "symbol": "_ZTIN6planet6saturn9telescope10TTelescopeE",
// CHECK-NEXT:   "op": "cir.global",
// CHECK:          "kind": "strong-strong",
// CHECK:          "kept": {
// CHECK-NEXT:       "input": 0,
// CHECK-NEXT:       "linkage": "external",
// CHECK:          "dropped": {
// CHECK-NEXT:       "input": 1,
// CHECK-NEXT:       "linkage": "external",
// CHECK:          "same_type": true,
// CHECK-NEXT:     "same_attributes": true,
// CHECK-NEXT:     "same_body": true
// CHECK:        "symbol": "_ZN6planet6saturn9telescope10TTelescopeD2Ev",
// CHECK-NEXT:   "op": "cir.func",
// CHECK:          "kind": "strong-strong",
// CHECK:          "kept": {
// CHECK-NEXT:       "input": 0,
// CHECK-NEXT:       "linkage": "external",
// CHECK:          "dropped": {
// CHECK-NEXT:       "input": 1,
// CHECK-NEXT:       "linkage": "external",
// CHECK:          "same_type": false,
// CHECK-NEXT:     "same_attributes": false,
// CHECK-NEXT:     "same_body": false
// CHECK:        "symbol": "_ZN6planet6saturn9telescope10TTelescopeD1Ev",
// CHECK-NEXT:   "op": "cir.func",
// CHECK:          "kind": "strong-strong",
// CHECK:          "kept": {
// CHECK-NEXT:       "input": 0,
// CHECK-NEXT:       "linkage": "external",
// CHECK:          "dropped": {
// CHECK-NEXT:       "input": 1,
// CHECK-NEXT:       "linkage": "external",
// CHECK:          "same_type": false,
// CHECK-NEXT:     "same_attributes": false,
// CHECK-NEXT:     "same_body": true
// CHECK:        "symbol": "_ZN6planet6saturn9telescope10TTelescopeD0Ev",
// CHECK-NEXT:   "op": "cir.func",
// CHECK:          "kind": "strong-strong",
// CHECK:          "kept": {
// CHECK-NEXT:       "input": 0,
// CHECK-NEXT:       "linkage": "external",
// CHECK:          "dropped": {
// CHECK-NEXT:       "input": 1,
// CHECK-NEXT:       "linkage": "external",
// CHECK:          "same_type": false,
// CHECK-NEXT:     "same_attributes": false,
// CHECK-NEXT:     "same_body": false
// CHECK:        "symbol": "_ZN6planet6saturn9telescope10TTelescope4readEPhj",
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
// CHECK-NEXT:     "record": "planet::saturn::telescope::TTelescope",
// CHECK-NEXT:     "renamed_to": "planet::saturn::telescope::TTelescope.m1"
// CHECK:      "stats": {
// CHECK-NEXT:   "weak_copies_equal": 0,
// CHECK-NEXT:   "weak_copies_differ": 1,
// CHECK-NEXT:   "weak-weak": 1,
// CHECK-NEXT:   "strong-over-weak": 0,
// CHECK-NEXT:   "rtti-over-nortti": 0,
// CHECK-NEXT:   "strong-strong": 6,
// CHECK-NEXT:   "records_renamed": 1

// --diagnostic-style=source: where each finding points, and the source
// line there.
// DIAG:      {{.*}}upstream.cpp:2:7: warning: 'planet::saturn::telescope::TTelescope' is defined differently than in an earlier input
// DIAG-NEXT: class TTelescope {
// DIAG:      {{.*}}fork.cpp:2:7: note: the earlier definition is here
// DIAG-NEXT: class TTelescope {
// DIAG:      {{.*}}upstream.cpp:8:7: note: an extra member here: a 32-bit signed integer
// DIAG-NEXT: int flags = 0;
// DIAG:      {{.*}}fork.cpp:2:7: note: the earlier definition has: none
// DIAG-NEXT: class TTelescope {
// DIAG:      {{.*}}upstream.cpp:2:1: warning: 'vtable for planet::saturn::telescope::TTelescope' (_ZTVN6planet6saturn9telescope10TTelescopeE) defined in more than one input; keeping the one from '{{.*}}fork.o' ({{.*}}fork.cpp), ignoring '{{.*}}upstream.o' ({{.*}}upstream.cpp); they differ in body
// DIAG-NEXT: class TTelescope {
// DIAG:      {{.*}}fork.cpp:2:1: note: the definition kept is here
// DIAG-NEXT: class TTelescope {
// DIAG:      {{.*}}upstream.cpp:2:1: note: the vtable differs here: 'planet::saturn::telescope::TTelescope::isOpen()'
// DIAG-NEXT: class TTelescope {
// DIAG:      {{.*}}fork.cpp:2:1: note: the kept copy has: 'planet::saturn::telescope::TTelescope::read(unsigned char*, unsigned int)'
// DIAG-NEXT: class TTelescope {
// DIAG:      {{.*}}upstream.cpp:2:1: warning: 'typeinfo name for planet::saturn::telescope::TTelescope' (_ZTSN6planet6saturn9telescope10TTelescopeE) defined in more than one input; keeping the one from '{{.*}}fork.o' ({{.*}}fork.cpp), ignoring '{{.*}}upstream.o' ({{.*}}upstream.cpp); the definitions are identical
// DIAG-NEXT: class TTelescope {
// DIAG:      {{.*}}fork.cpp:2:1: note: the definition kept is here
// DIAG-NEXT: class TTelescope {
// DIAG:      {{.*}}upstream.cpp:2:1: warning: 'typeinfo for planet::saturn::telescope::TTelescope' (_ZTIN6planet6saturn9telescope10TTelescopeE) defined in more than one input; keeping the one from '{{.*}}fork.o' ({{.*}}fork.cpp), ignoring '{{.*}}upstream.o' ({{.*}}upstream.cpp); the definitions are identical
// DIAG-NEXT: class TTelescope {
// DIAG:      {{.*}}fork.cpp:2:1: note: the definition kept is here
// DIAG-NEXT: class TTelescope {
// DIAG:      {{.*}}upstream.cpp:10:1: warning: 'planet::saturn::telescope::TTelescope::~TTelescope()' (_ZN6planet6saturn9telescope10TTelescopeD2Ev) defined in more than one input; keeping the one from '{{.*}}fork.o' ({{.*}}fork.cpp), ignoring '{{.*}}upstream.o' ({{.*}}upstream.cpp); they differ in type, attributes, body
// DIAG-NEXT: TTelescope::~TTelescope() {}
// DIAG:      {{.*}}fork.cpp:8:1: note: the definition kept is here
// DIAG-NEXT: TTelescope::~TTelescope() {}
// DIAG:      {{.*}}upstream.cpp:10:13: note: the type of 'this' differs here: 'planet::saturn::telescope::TTelescope' is defined differently in each copy
// DIAG-NEXT: TTelescope::~TTelescope() {}
// DIAG:      {{.*}}fork.cpp:8:13: note: and here in the kept copy
// DIAG-NEXT: TTelescope::~TTelescope() {}
// DIAG:      {{.*}}upstream.cpp:10:1: warning: 'planet::saturn::telescope::TTelescope::~TTelescope()' (_ZN6planet6saturn9telescope10TTelescopeD1Ev) defined in more than one input; keeping the one from '{{.*}}fork.o' ({{.*}}fork.cpp), ignoring '{{.*}}upstream.o' ({{.*}}upstream.cpp); they differ in type, attributes
// DIAG-NEXT: TTelescope::~TTelescope() {}
// DIAG:      {{.*}}fork.cpp:8:1: note: the definition kept is here
// DIAG-NEXT: TTelescope::~TTelescope() {}
// DIAG:      {{.*}}upstream.cpp:10:1: note: the type of parameter 1 differs here: 'planet::saturn::telescope::TTelescope' is defined differently in each copy
// DIAG-NEXT: TTelescope::~TTelescope() {}
// DIAG:      {{.*}}fork.cpp:8:1: note: and here in the kept copy
// DIAG-NEXT: TTelescope::~TTelescope() {}
// DIAG:      {{.*}}upstream.cpp:10:1: warning: 'planet::saturn::telescope::TTelescope::~TTelescope()' (_ZN6planet6saturn9telescope10TTelescopeD0Ev) defined in more than one input; keeping the one from '{{.*}}fork.o' ({{.*}}fork.cpp), ignoring '{{.*}}upstream.o' ({{.*}}upstream.cpp); they differ in type, attributes, body
// DIAG-NEXT: TTelescope::~TTelescope() {}
// DIAG:      {{.*}}fork.cpp:8:1: note: the definition kept is here
// DIAG-NEXT: TTelescope::~TTelescope() {}
// DIAG:      {{.*}}upstream.cpp:10:13: note: the type of 'this' differs here: 'planet::saturn::telescope::TTelescope' is defined differently in each copy
// DIAG-NEXT: TTelescope::~TTelescope() {}
// DIAG:      {{.*}}fork.cpp:8:13: note: and here in the kept copy
// DIAG-NEXT: TTelescope::~TTelescope() {}
// DIAG:      {{.*}}upstream.cpp:6:3: warning: 'planet::saturn::telescope::TTelescope::read(unsigned char*, unsigned int)' (_ZN6planet6saturn9telescope10TTelescope4readEPhj) has different definitions in '{{.*}}fork.o' ({{.*}}fork.cpp) and '{{.*}}upstream.o' ({{.*}}upstream.cpp) (they differ in type, attributes, body); keeping the one from '{{.*}}fork.o' ({{.*}}fork.cpp)
// DIAG-NEXT: virtual unsigned read(unsigned char *buf, unsigned len) { return 0; }
// DIAG:      {{.*}}fork.cpp:5:3: note: the definition kept is here
// DIAG-NEXT: virtual unsigned read(unsigned char *buf, unsigned len) { return len; }
// DIAG:      {{.*}}upstream.cpp:6:20: note: the type of 'this' differs here: 'planet::saturn::telescope::TTelescope' is defined differently in each copy
// DIAG-NEXT: virtual unsigned read(unsigned char *buf, unsigned len) { return 0; }
// DIAG:      {{.*}}fork.cpp:5:20: note: and here in the kept copy
// DIAG-NEXT: virtual unsigned read(unsigned char *buf, unsigned len) { return len; }
// DIAG-NOT:  {{warning|note}}:

//--- fork.cpp
namespace planet { namespace saturn { namespace telescope {
class TTelescope {
public:
  virtual ~TTelescope();
  virtual unsigned read(unsigned char *buf, unsigned len) { return len; }
  virtual bool peek() { return true; }
};
TTelescope::~TTelescope() {}
} } }
unsigned fork_read(planet::saturn::telescope::TTelescope &t, unsigned char *b) {
  return t.read(b, 4);
}
//--- upstream.cpp
namespace planet { namespace saturn { namespace telescope {
class TTelescope {
public:
  virtual ~TTelescope();
  virtual bool isOpen() { return false; }
  virtual unsigned read(unsigned char *buf, unsigned len) { return 0; }
protected:
  int flags = 0;
};
TTelescope::~TTelescope() {}
} } }
unsigned upstream_read(planet::saturn::telescope::TTelescope &t,
                       unsigned char *b) {
  return t.read(b, 4);
}
