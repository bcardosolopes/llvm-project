// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fno-delete-null-pointer-checks -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll --check-prefix=CIR %s
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fno-delete-null-pointer-checks -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Under -fno-delete-null-pointer-checks (NullPointerIsValid), reference loads
// should not carry !nonnull metadata. They should still carry !align metadata,
// matching OGCG behavior.

// CIR-LABEL: define{{.*}} @_Z1fRi
// CIR: load ptr, ptr %{{.*}}, align 8, !align ![[ALIGN4:[0-9]+]]
// CIR-NOT: !nonnull

// OGCG-LABEL: define{{.*}} @_Z1fRi
// OGCG: load ptr, ptr %{{.*}}, align 8, !align !{{[0-9]+}}
// OGCG-NOT: load ptr, ptr %{{.*}}, align 8, !nonnull
int f(int &r) { return r; }

// Alignment-1 references should also omit !nonnull under NullPointerIsValid
// and should not carry !align metadata.

// CIR-LABEL: define{{.*}} @_Z2fcRc
// CIR: load ptr, ptr %{{.*}}, align 8
// CIR-NOT: !align
// CIR-NOT: !nonnull

// OGCG-LABEL: define{{.*}} @_Z2fcRc
// OGCG: load ptr, ptr %{{.*}}, align 8
// OGCG-NOT: load ptr, ptr %{{.*}}, align 8, !align
// OGCG-NOT: load ptr, ptr %{{.*}}, align 8, !nonnull
void fc(char &c) { c = 'x'; }

// Explicit AS0 type spelling under NullPointerIsValid should still omit
// !nonnull and keep !align, matching OGCG.
typedef int __attribute__((address_space(0))) as0_int;

// CIR-LABEL: define{{.*}} @_Z5f_as0Ri
// CIR: load ptr, ptr %{{.*}}, align 8, !align ![[ALIGN4]]
// CIR-NOT: !nonnull

// OGCG-LABEL: define{{.*}} @_Z5f_as0Ri
// OGCG: load ptr, ptr %{{.*}}, align 8, !align !{{[0-9]+}}
// OGCG-NOT: load ptr, ptr %{{.*}}, align 8, !nonnull
int f_as0(as0_int &r) { return r; }

// Explicit AS1 alignment-1 references under NullPointerIsValid should omit
// both !nonnull and !align, matching OGCG.
typedef char __attribute__((address_space(1))) as1_char;

// CIR-LABEL: define{{.*}} @_Z1hRU3AS1c
// CIR: load ptr addrspace(1), ptr %{{.*}}, align 8
// CIR-NOT: !align
// CIR-NOT: !nonnull

// OGCG-LABEL: define{{.*}} @_Z1hRU3AS1c
// OGCG: load ptr addrspace(1), ptr %{{.*}}, align 8{{$}}
// OGCG-NOT: load ptr addrspace(1), ptr %{{.*}}, align 8, !align
// OGCG-NOT: load ptr addrspace(1), ptr %{{.*}}, align 8, !nonnull
void h(as1_char &c) { c = 'y'; }

// CIR-DAG: ![[ALIGN4]] = !{i64 4}
