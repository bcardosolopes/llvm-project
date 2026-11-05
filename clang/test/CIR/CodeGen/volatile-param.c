// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM %s < %t-cir.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG %s < %t.ll

// A volatile-qualified function parameter's prologue argument-spill store must
// be volatile, matching classic CodeGen. Otherwise the observable volatile
// write to the parameter object is dropped (and dead-store-eliminated at -O2).

void f(int *volatile vp) { *vp = 2; }
// LLVM-LABEL: @f(
// LLVM: store volatile ptr %{{.*}}, ptr %{{.*}}
// OGCG-LABEL: @f(
// OGCG: store volatile ptr %{{.*}}, ptr %{{.*}}

int sink;
void g(volatile int v) { sink = v; }
// LLVM-LABEL: @g(
// LLVM: store volatile i32 %{{.*}}, ptr %{{.*}}
// OGCG-LABEL: @g(
// OGCG: store volatile i32 %{{.*}}, ptr %{{.*}}

// A non-volatile parameter must NOT get a volatile spill store.
void h(int *p) { *p = 2; }
// LLVM-LABEL: @h(
// LLVM-NOT: store volatile
// OGCG-LABEL: @h(
// OGCG-NOT: store volatile
