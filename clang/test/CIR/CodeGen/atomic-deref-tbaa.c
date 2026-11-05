// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c11 -O2 -disable-llvm-passes -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c11 -O2 -disable-llvm-passes -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll --check-prefix=OGCG %s

int load_deref_i(_Atomic(int) *p) { return *p; }

void store_deref_i(_Atomic(int) *p, int v) { *p = v; }

// LLVM-LABEL: define{{.*}} @load_deref_i
// LLVM: load atomic i32, ptr {{.*}} seq_cst, align 4, !tbaa ![[AT:[0-9]+]]
// LLVM-LABEL: define{{.*}} @store_deref_i
// LLVM: store atomic i32 {{.*}}, ptr {{.*}} seq_cst, align 4, !tbaa ![[AT]]
// LLVM-DAG: ![[AT]] = !{![[CHAR:[0-9]+]], ![[CHAR]], i64 0}
// LLVM-DAG: ![[CHAR]] = !{!"omnipotent char", ![[ROOT:[0-9]+]], i64 0}
// LLVM-DAG: ![[ROOT]] = !{!"Simple C/C++ TBAA"}

// OGCG-LABEL: define{{.*}} @load_deref_i
// OGCG: load atomic i32, ptr {{.*}} seq_cst, align 4, !tbaa ![[AT:[0-9]+]]
// OGCG-LABEL: define{{.*}} @store_deref_i
// OGCG: store atomic i32 {{.*}}, ptr {{.*}} seq_cst, align 4, !tbaa ![[AT]]
// OGCG-DAG: ![[AT]] = !{![[CHAR:[0-9]+]], ![[CHAR]], i64 0}
// OGCG-DAG: ![[CHAR]] = !{!"omnipotent char", ![[ROOT:[0-9]+]], i64 0}
// OGCG-DAG: ![[ROOT]] = !{!"Simple C/C++ TBAA"}
