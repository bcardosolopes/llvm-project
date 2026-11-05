// RUN: %clang_cc1 -std=c++20 -O1 -disable-llvm-passes -triple x86_64-linux-gnu -fclangir -emit-llvm %s -o %t.O1.ll
// RUN: FileCheck --check-prefix=O1 --input-file=%t.O1.ll %s
// RUN: %clang_cc1 -std=c++20 -disable-llvm-passes -triple x86_64-linux-gnu -fclangir -emit-llvm %s -o %t.O0.ll
// RUN: FileCheck --check-prefix=O0 --input-file=%t.O0.ll %s

// Verify that CIR emits llvm.invariant.start on the __tls_guard after
// setting it to 1 at -O1+, matching OGCG behavior to allow LLVM to optimize.
// At -O0, OGCG's EmitInvariantStart skips this intrinsic.

int dyn_init();
thread_local int tls_a = dyn_init();
thread_local int tls_b = dyn_init();

int use() { return tls_a + tls_b; }

// O1: define internal void @__tls_init()
// O1:   store i8 1, ptr @__tls_guard
// O1-NEXT:   {{.*}}call ptr @llvm.invariant.start.p0(i64 1, ptr @__tls_guard)

// At -O0, no invariant.start should be emitted.
// O0-NOT: invariant.start
