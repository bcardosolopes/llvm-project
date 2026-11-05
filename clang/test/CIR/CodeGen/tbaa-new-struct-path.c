// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -O1 -disable-llvm-passes -new-struct-path-tbaa %s -o - | FileCheck %s --check-prefix=OGCG
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -O1 -disable-llvm-passes -new-struct-path-tbaa %s -o - | FileCheck %s --check-prefix=CIR

struct S {
  int a;
  float b;
};

int load_a(struct S *s) {
  // OGCG-LABEL: define{{.*}} i32 @load_a(
  // OGCG: load i32, ptr %{{.*}}, align 4, !tbaa ![[OG_LOAD_TAG:[0-9]+]]

  // CIR-LABEL: define{{.*}} i32 @load_a(
  // CIR: load i32, ptr %{{.*}}, align 4, !tbaa ![[CIR_LOAD_TAG:[0-9]+]]
  return s->a;
}

void store_b(struct S *s, float v) {
  // OGCG-LABEL: define{{.*}} void @store_b(
  // OGCG: %[[OG_B:.*]] = getelementptr inbounds nuw %struct.S, ptr %{{.*}}, i32 0, i32 1
  // OGCG-NEXT: store float %{{.*}}, ptr %[[OG_B]], align 4, !tbaa ![[OG_STORE_TAG:[0-9]+]]
  // OGCG-DAG: ![[OG_LOAD_TAG]] = !{![[OG_BASE:[0-9]+]], ![[OG_INT:[0-9]+]], i64 0, i64 4}
  // OGCG-DAG: ![[OG_STORE_TAG]] = !{![[OG_BASE]], ![[OG_FLOAT:[0-9]+]], i64 4, i64 4}
  // OGCG-DAG: ![[OG_BASE]] = !{![[OG_CHAR:[0-9]+]], i64 8, !"S", ![[OG_INT]], i64 0, i64 4, ![[OG_FLOAT]], i64 4, i64 4}
  // OGCG-DAG: ![[OG_INT]] = !{![[OG_CHAR]], i64 4, !"int"}
  // OGCG-DAG: ![[OG_FLOAT]] = !{![[OG_CHAR]], i64 4, !"float"}

  // CIR-LABEL: define{{.*}} void @store_b(
  // CIR: %[[CIR_B:.*]] = getelementptr inbounds nuw %struct.S, ptr %{{.*}}, i32 0, i32 1
  // CIR-NEXT: store float %{{.*}}, ptr %[[CIR_B]], align 4, !tbaa ![[CIR_STORE_TAG:[0-9]+]]
  // CIR-DAG: ![[CIR_LOAD_TAG]] = !{![[CIR_BASE:[0-9]+]], ![[CIR_INT:[0-9]+]], i64 0, i64 4}
  // CIR-DAG: ![[CIR_STORE_TAG]] = !{![[CIR_BASE]], ![[CIR_FLOAT:[0-9]+]], i64 4, i64 4}
  // CIR-DAG: ![[CIR_BASE]] = !{![[CIR_CHAR:[0-9]+]], i64 8, !"S", ![[CIR_INT]], i64 0, i64 4, ![[CIR_FLOAT]], i64 4, i64 4}
  // CIR-DAG: ![[CIR_INT]] = !{![[CIR_CHAR]], i64 4, !"int"}
  // CIR-DAG: ![[CIR_FLOAT]] = !{![[CIR_CHAR]], i64 4, !"float"}

  // Verify errno TBAA uses new-struct-path format too.
  // OGCG-DAG: !llvm.errno.tbaa = !{![[OG_ERRNO:[0-9]+]]}
  // OGCG-DAG: ![[OG_ERRNO]] = !{![[OG_INT]], ![[OG_INT]], i64 0, i64 4}

  // CIR-DAG: !llvm.errno.tbaa = !{![[CIR_ERRNO:[0-9]+]]}
  // CIR-DAG: ![[CIR_ERRNO]] = !{![[CIR_INT]], ![[CIR_INT]], i64 0, i64 4}
  s->b = v;
}
