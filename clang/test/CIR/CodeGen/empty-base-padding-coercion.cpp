// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s -check-prefix=OGCG
//
// Verify that empty base sub-object padding is correctly classified
// as NoClass. Previously, CIR classified it as INTEGER.
//
// B2 layout: [8 bytes padding from empty B at offset 0] [B1.pa (ptr) at offset 8]
// Coerces to ptr (single register, lo=NoClass, hi=ptr)

struct B {};

struct B1 : B {
  int* pa;
};

struct B2 : B {
  B1 b1;
};

// CIR-LABEL: define {{.*}} @_Z3bar2B2(ptr %
// OGCG-LABEL: define {{.*}} @_Z3bar2B2(ptr %
const void *bar(B2 b2) { return b2.b1.pa; }
