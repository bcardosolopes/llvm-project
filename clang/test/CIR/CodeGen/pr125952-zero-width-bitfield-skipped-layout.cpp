// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Regression: avoid asserting in skipped-layout replay when a record contains
// a zero-width bitfield and a function-pointer field that initially references
// an incomplete type.

struct Fwd;

struct Crash {
  int (*fp)(Fwd);
  int : 0;
  int y;
};

// Forces function type arrangement while Fwd is incomplete.
// CIR-LABEL: define{{.*}} @_Z3use5Crash(ptr %{{.*}}, i32 %{{.*}})
// OGCG-LABEL: define{{.*}} @_Z3use5Crash(ptr %{{.*}}, i32 %{{.*}})
int use(Crash c) { return c.y; }

struct Fwd {
  int z;
};
