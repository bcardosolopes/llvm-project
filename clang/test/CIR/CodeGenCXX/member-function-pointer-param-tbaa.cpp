// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -O1 -disable-llvm-passes -emit-llvm -fclangir %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -O1 -disable-llvm-passes -emit-llvm %s -o %t-ogcg.ll
// RUN: FileCheck --input-file=%t-ogcg.ll %s --check-prefix=OGCG

struct A {
  int f() &;
};

extern void sink(int (A::*&)() &);

void accept(int (A::*pmf)() &) {
  sink(pmf);
}

// CIR-LABEL: define dso_local void @_Z6acceptM1AFivRE(
// CIR: store i64 %{{.*}}, ptr %{{.*}}, align 8, !tbaa ![[TAG:[0-9]+]]
// CIR: store i64 %{{.*}}, ptr %{{.*}}, align 8, !tbaa ![[TAG]]
// CIR-DAG: ![[ROOT:[0-9]+]] = !{!"Simple C++ TBAA"}
// CIR-DAG: ![[CHAR:[0-9]+]] = !{!"omnipotent char", ![[ROOT]], i64 0}
// CIR-DAG: ![[TAG]] = !{![[CHAR]], ![[CHAR]], i64 0}

// OGCG-LABEL: define dso_local void @_Z6acceptM1AFivRE(
// OGCG: load { i64, i64 }, ptr %{{.*}}, align 8, !tbaa ![[TAG:[0-9]+]]
// OGCG: store { i64, i64 } %{{.*}}, ptr %{{.*}}, align 8, !tbaa ![[TAG]]
// OGCG-DAG: ![[ROOT:[0-9]+]] = !{!"Simple C++ TBAA"}
// OGCG-DAG: ![[CHAR:[0-9]+]] = !{!"omnipotent char", ![[ROOT]], i64 0}
// OGCG-DAG: ![[TAG]] = !{![[CHAR]], ![[CHAR]], i64 0}
