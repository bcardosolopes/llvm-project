// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll --check-prefix=CIR %s
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Explicit non-zero address-space references should not get !nonnull metadata
// on the reference load, but should still carry !align metadata, matching OGCG.

typedef int __attribute__((address_space(1))) as1_int;

// CIR-LABEL: define{{.*}} @_Z1gRU3AS1i
// CIR: load ptr addrspace(1), ptr %{{.*}}, align 8, !align ![[ALIGN4:[0-9]+]]
// CIR-NOT: !nonnull

// OGCG-LABEL: define{{.*}} @_Z1gRU3AS1i
// OGCG: load ptr addrspace(1), ptr %{{.*}}, align 8, !align !{{[0-9]+}}
// OGCG-NOT: load ptr addrspace(1), ptr %{{.*}}, align 8, !nonnull
int g(as1_int &r) { return r; }

// CIR-DAG: ![[ALIGN4]] = !{i64 4}
