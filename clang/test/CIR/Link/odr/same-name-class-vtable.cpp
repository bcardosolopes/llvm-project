// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects -O1 \
// RUN:     -c %t/crypto.cpp -o %t/crypto.o
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects -O1 \
// RUN:     -c %t/keychain.cpp -o %t/keychain.o
// RUN: cir-link %t/crypto.o %t/keychain.o --odr-events=%t/events.json \
// RUN:     -o %t/merged.cir
// RUN: FileCheck %s --input-file=%t/events.json
// RUN: cir-link %t/crypto.o %t/keychain.o --odr=warning --diagnostic-style=source \
// RUN:     -o %t/diag.cir 2>&1 | FileCheck %s --check-prefix=DIAG --allow-empty

// Two unrelated targets each define a class with the same name and a different
// virtual interface. The vtables and type_info share one mangled name.
// Expected finding: vtable divergence and record divergence on the class.

// Today: the vtable differs in type, and the constructor and keySize
// differ in body.
// FIXME: as for vtable-layout-macro, the record itself is not renamed.

// CHECK:      "symbols": [
// CHECK:        "symbol": "_ZTV7GcmInfo",
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
// CHECK:        "symbol": "_ZN7GcmInfoC2Ev",
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
// CHECK:        "symbol": "_ZNK7GcmInfo7keySizeEv",
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
// CHECK-NEXT:   "weak_copies_equal": 4,
// CHECK-NEXT:   "weak_copies_differ": 3,
// CHECK-NEXT:   "weak-weak": 3,
// CHECK-NEXT:   "strong-over-weak": 0,
// CHECK-NEXT:   "rtti-over-nortti": 0,
// CHECK-NEXT:   "strong-strong": 0,
// CHECK-NEXT:   "records_renamed": 0

// --diagnostic-style=source: where each finding points, and the source
// line there.
// DIAG:      {{.*}}keychain.cpp:1:1: warning: 'vtable for GcmInfo' (_ZTV7GcmInfo) has different definitions in '{{.*}}crypto.o' ({{.*}}crypto.cpp) and '{{.*}}keychain.o' ({{.*}}keychain.cpp) (they differ in type, body); keeping the one from '{{.*}}crypto.o' ({{.*}}crypto.cpp)
// DIAG-NEXT: struct GcmInfo {
// DIAG:      {{.*}}crypto.cpp:1:1: note: the definition kept is here
// DIAG-NEXT: struct GcmInfo {
// DIAG:      {{.*}}keychain.cpp:1:1: note: the size of the vtable differs here: 6 elements
// DIAG-NEXT: struct GcmInfo {
// DIAG:      {{.*}}crypto.cpp:1:1: note: the kept copy has: 5 elements
// DIAG-NEXT: struct GcmInfo {
// DIAG:      {{.*}}keychain.cpp:1:1: note: the vtable differs here: 'GcmInfo::name() const'
// DIAG-NEXT: struct GcmInfo {
// DIAG:      {{.*}}crypto.cpp:1:1: note: the kept copy has: 'GcmInfo::keySize() const'
// DIAG-NEXT: struct GcmInfo {
// DIAG:      {{.*}}keychain.cpp:1:8: warning: 'GcmInfo::GcmInfo()' (_ZN7GcmInfoC2Ev) has different definitions in '{{.*}}crypto.o' ({{.*}}crypto.cpp) and '{{.*}}keychain.o' ({{.*}}keychain.cpp) (they differ in body); keeping the one from '{{.*}}crypto.o' ({{.*}}crypto.cpp)
// DIAG-NEXT: struct GcmInfo {
// DIAG:      {{.*}}crypto.cpp:1:8: note: the definition kept is here
// DIAG-NEXT: struct GcmInfo {
// DIAG:      {{.*}}keychain.cpp:1:1: note: the copies first differ here
// DIAG-NEXT: struct GcmInfo {
// DIAG:      {{.*}}crypto.cpp:1:1: note: the kept copy has this instead
// DIAG-NEXT: struct GcmInfo {
// DIAG:      {{.*}}keychain.cpp:3:3: warning: 'GcmInfo::keySize() const' (_ZNK7GcmInfo7keySizeEv) has different definitions in '{{.*}}crypto.o' ({{.*}}crypto.cpp) and '{{.*}}keychain.o' ({{.*}}keychain.cpp) (they differ in body); keeping the one from '{{.*}}crypto.o' ({{.*}}crypto.cpp)
// DIAG-NEXT: virtual int keySize() const { return 16; }
// DIAG:      {{.*}}crypto.cpp:2:3: note: the definition kept is here
// DIAG-NEXT: virtual int keySize() const { return 32; }
// DIAG:      {{.*}}keychain.cpp:3:40: note: the copies first differ here
// DIAG-NEXT: virtual int keySize() const { return 16; }
// DIAG:      {{.*}}crypto.cpp:2:40: note: the kept copy has this instead
// DIAG-NEXT: virtual int keySize() const { return 32; }
// DIAG-NOT:  {{warning|note}}:

//--- crypto.cpp
struct GcmInfo {
  virtual int keySize() const { return 32; }
  virtual ~GcmInfo() {}
};
GcmInfo *make_crypto() { return new GcmInfo; }
//--- keychain.cpp
struct GcmInfo {
  virtual const char *name() const { return "aes"; }
  virtual int keySize() const { return 16; }
  virtual ~GcmInfo() {}
};
GcmInfo *make_keychain() { return new GcmInfo; }
