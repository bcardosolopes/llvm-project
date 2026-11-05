// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Test that empty base classes don't cause extra ABI parameters.
// The empty base B produces padding in CIR's record layout, which should
// be classified as NoClass (not INTEGER) in the x86_64 ABI classifier.

struct B {};
struct B1 : B { int* pa; };
struct B2 : B { B1 b1; };

const void *bar(B2 b2) { return b2.b1.pa; }

// Both should coerce B2 to a single ptr (the lo half is empty padding).
// CHECK: define {{.*}} ptr @_Z3bar2B2(ptr %{{.*}})
// OGCG: define {{.*}} ptr @_Z3bar2B2(ptr %{{.*}})
