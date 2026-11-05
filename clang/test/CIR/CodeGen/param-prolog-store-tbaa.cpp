// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -O1 -disable-llvm-passes %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -O1 -disable-llvm-passes %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -O1 -disable-llvm-passes -new-struct-path-tbaa %s -o %t-cir-nsp.ll
// RUN: FileCheck --input-file=%t-cir-nsp.ll %s -check-prefix=CIR-NSP
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -O1 -disable-llvm-passes -new-struct-path-tbaa %s -o %t-nsp.ll
// RUN: FileCheck --input-file=%t-nsp.ll %s -check-prefix=OGCG-NSP

// A class with a user-declared destructor is passed indirectly and *not*
// byval, so the prologue spills the incoming pointer into a local slot. The
// tag on that store is built from the pointer type, not from the record.

class A {
public:
  ~A();
  long a, b, c;
};

long read_indirect_record_param(A x) { return x.a; }

// CIR-LABEL: @_Z26read_indirect_record_param1A
// CIR: store ptr %{{.*}}, ptr %{{.*}}, align 8, !tbaa ![[CIR_PA_TAG:[0-9]+]]

// OGCG-LABEL: @_Z26read_indirect_record_param1A
// OGCG: store ptr %{{.*}}, ptr %{{.*}}, align 8, !tbaa ![[OG_PA_TAG:[0-9]+]]

// CIR-DAG: ![[CIR_ROOT:[0-9]+]] = !{!"Simple C++ TBAA"}
// CIR-DAG: ![[CIR_CHAR:[0-9]+]] = !{!"omnipotent char", ![[CIR_ROOT]], i64 0}
// CIR-DAG: ![[CIR_ANYPTR:[0-9]+]] = !{!"any pointer", ![[CIR_CHAR]], i64 0}
// CIR-DAG: ![[CIR_PA_NODE:[0-9]+]] = !{!"p1 _ZTS1A", ![[CIR_ANYPTR]], i64 0}
// CIR-DAG: ![[CIR_PA_TAG]] = !{![[CIR_PA_NODE]], ![[CIR_PA_NODE]], i64 0}

// OGCG-DAG: ![[OG_ROOT:[0-9]+]] = !{!"Simple C++ TBAA"}
// OGCG-DAG: ![[OG_CHAR:[0-9]+]] = !{!"omnipotent char", ![[OG_ROOT]], i64 0}
// OGCG-DAG: ![[OG_ANYPTR:[0-9]+]] = !{!"any pointer", ![[OG_CHAR]], i64 0}
// OGCG-DAG: ![[OG_PA_NODE:[0-9]+]] = !{!"p1 _ZTS1A", ![[OG_ANYPTR]], i64 0}
// OGCG-DAG: ![[OG_PA_TAG]] = !{![[OG_PA_NODE]], ![[OG_PA_NODE]], i64 0}

// Under -new-struct-path-tbaa the same store carries a sized four-operand
// access tag, so the size on every node is pinned too.

// CIR-NSP-LABEL: @_Z26read_indirect_record_param1A
// CIR-NSP: store ptr %{{.*}}, ptr %{{.*}}, align 8, !tbaa ![[CIR_NSP_TAG:[0-9]+]]

// OGCG-NSP-LABEL: @_Z26read_indirect_record_param1A
// OGCG-NSP: store ptr %{{.*}}, ptr %{{.*}}, align 8, !tbaa ![[OG_NSP_TAG:[0-9]+]]

// CIR-NSP-DAG: ![[CIR_NSP_ROOT:[0-9]+]] = !{!"Simple C++ TBAA"}
// CIR-NSP-DAG: ![[CIR_NSP_CHAR:[0-9]+]] = !{![[CIR_NSP_ROOT]], i64 1, !"omnipotent char"}
// CIR-NSP-DAG: ![[CIR_NSP_ANYPTR:[0-9]+]] = !{![[CIR_NSP_CHAR]], i64 8, !"any pointer"}
// CIR-NSP-DAG: ![[CIR_NSP_NODE:[0-9]+]] = !{![[CIR_NSP_ANYPTR]], i64 8, !"p1 _ZTS1A"}
// CIR-NSP-DAG: ![[CIR_NSP_TAG]] = !{![[CIR_NSP_NODE]], ![[CIR_NSP_NODE]], i64 0, i64 8}

// OGCG-NSP-DAG: ![[OG_NSP_ROOT:[0-9]+]] = !{!"Simple C++ TBAA"}
// OGCG-NSP-DAG: ![[OG_NSP_CHAR:[0-9]+]] = !{![[OG_NSP_ROOT]], i64 1, !"omnipotent char"}
// OGCG-NSP-DAG: ![[OG_NSP_ANYPTR:[0-9]+]] = !{![[OG_NSP_CHAR]], i64 8, !"any pointer"}
// OGCG-NSP-DAG: ![[OG_NSP_NODE:[0-9]+]] = !{![[OG_NSP_ANYPTR]], i64 8, !"p1 _ZTS1A"}
// OGCG-NSP-DAG: ![[OG_NSP_TAG]] = !{![[OG_NSP_NODE]], ![[OG_NSP_NODE]], i64 0, i64 8}
