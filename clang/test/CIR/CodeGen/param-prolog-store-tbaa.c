// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -O1 -disable-llvm-passes %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -O1 -disable-llvm-passes %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

void scalar_param_stores(float x, float *p) {
  *p = x;
}

// CIR-LABEL: @scalar_param_stores
// CIR: store float %{{.*}}, ptr %{{.*}}, align 4, !tbaa ![[CIR_FLOAT_TAG:[0-9]+]]
// CIR: store ptr %{{.*}}, ptr %{{.*}}, align 8, !tbaa ![[CIR_PFLOAT_TAG:[0-9]+]]

// OGCG-LABEL: @scalar_param_stores
// OGCG: store float %{{.*}}, ptr %{{.*}}, align 4, !tbaa ![[OG_FLOAT_TAG:[0-9]+]]
// OGCG: store ptr %{{.*}}, ptr %{{.*}}, align 8, !tbaa ![[OG_PFLOAT_TAG:[0-9]+]]

// CIR-DAG: ![[CIR_ROOT:[0-9]+]] = !{!"Simple C/C++ TBAA"}
// CIR-DAG: ![[CIR_CHAR:[0-9]+]] = !{!"omnipotent char", ![[CIR_ROOT]], i64 0}
// CIR-DAG: ![[CIR_FLOAT_NODE:[0-9]+]] = !{!"float", ![[CIR_CHAR]], i64 0}
// CIR-DAG: ![[CIR_ANYPTR:[0-9]+]] = !{!"any pointer", ![[CIR_CHAR]], i64 0}
// CIR-DAG: ![[CIR_PFLOAT_NODE:[0-9]+]] = !{!"p1 float", ![[CIR_ANYPTR]], i64 0}
// CIR-DAG: ![[CIR_FLOAT_TAG]] = !{![[CIR_FLOAT_NODE]], ![[CIR_FLOAT_NODE]], i64 0}
// CIR-DAG: ![[CIR_PFLOAT_TAG]] = !{![[CIR_PFLOAT_NODE]], ![[CIR_PFLOAT_NODE]], i64 0}

// OGCG-DAG: ![[OG_ROOT:[0-9]+]] = !{!"Simple C/C++ TBAA"}
// OGCG-DAG: ![[OG_CHAR:[0-9]+]] = !{!"omnipotent char", ![[OG_ROOT]], i64 0}
// OGCG-DAG: ![[OG_FLOAT_NODE:[0-9]+]] = !{!"float", ![[OG_CHAR]], i64 0}
// OGCG-DAG: ![[OG_ANYPTR:[0-9]+]] = !{!"any pointer", ![[OG_CHAR]], i64 0}
// OGCG-DAG: ![[OG_PFLOAT_NODE:[0-9]+]] = !{!"p1 float", ![[OG_ANYPTR]], i64 0}
// OGCG-DAG: ![[OG_FLOAT_TAG]] = !{![[OG_FLOAT_NODE]], ![[OG_FLOAT_NODE]], i64 0}
// OGCG-DAG: ![[OG_PFLOAT_TAG]] = !{![[OG_PFLOAT_NODE]], ![[OG_PFLOAT_NODE]], i64 0}
