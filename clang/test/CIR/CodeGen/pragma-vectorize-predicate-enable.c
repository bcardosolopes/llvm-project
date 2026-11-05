// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O2 -disable-llvm-passes -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O2 -disable-llvm-passes -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll --check-prefix=OGCG %s
//
// RUN: %clang_cc1 -DWIDTH1 -triple x86_64-unknown-linux-gnu -O2 -disable-llvm-passes -fclangir -emit-llvm %s -o %t.w1.cir.ll
// RUN: FileCheck --input-file=%t.w1.cir.ll --check-prefix=LLVM-W1 %s
// RUN: %clang_cc1 -DWIDTH1 -triple x86_64-unknown-linux-gnu -O2 -disable-llvm-passes -emit-llvm %s -o %t.w1.og.ll
// RUN: FileCheck --input-file=%t.w1.og.ll --check-prefix=OGCG-W1 %s

#ifndef WIDTH1
void pred_enable(int *a, int n) {
#pragma clang loop vectorize_predicate(enable)
  for (int i = 0; i < n; ++i)
    a[i] += 1;
}

// LLVM-LABEL: define{{.*}} @pred_enable
// LLVM: br label %{{.*}}, !llvm.loop ![[LOOP:[0-9]+]]
// LLVM-DAG: ![[LOOP]] = distinct !{![[LOOP]], {{.*}}}
// LLVM-DAG: !{!"llvm.loop.mustprogress"}
// LLVM-DAG: !{!"llvm.loop.vectorize.predicate.enable", i1 true}
// LLVM-DAG: !{!"llvm.loop.vectorize.enable", i1 true}

// OGCG-LABEL: define{{.*}} @pred_enable
// OGCG: br label %{{.*}}, !llvm.loop ![[LOOP:[0-9]+]]
// OGCG-DAG: ![[LOOP]] = distinct !{![[LOOP]], {{.*}}}
// OGCG-DAG: !{!"llvm.loop.mustprogress"}
// OGCG-DAG: !{!"llvm.loop.vectorize.predicate.enable", i1 true}
// OGCG-DAG: !{!"llvm.loop.vectorize.enable", i1 true}
#else
void pred_enable_width_1(int *a, int n) {
#pragma clang loop vectorize_width(1)
#pragma clang loop vectorize_predicate(enable)
  for (int i = 0; i < n; ++i)
    a[i] += 1;
}

// LLVM-W1-LABEL: define{{.*}} @pred_enable_width_1
// LLVM-W1: br label %{{.*}}, !llvm.loop ![[LOOP:[0-9]+]]
// LLVM-W1-DAG: ![[LOOP]] = distinct !{![[LOOP]], {{.*}}}
// LLVM-W1-DAG: !{!"llvm.loop.mustprogress"}
// LLVM-W1-DAG: !{!"llvm.loop.vectorize.predicate.enable", i1 true}
// LLVM-W1-DAG: !{!"llvm.loop.vectorize.width", i32 1}
// LLVM-W1-NOT: !{!"llvm.loop.vectorize.enable", i1 true}
// LLVM-W1-NOT: !{!"llvm.loop.vectorize.enable", i1 false}

// OGCG-W1-LABEL: define{{.*}} @pred_enable_width_1
// OGCG-W1: br label %{{.*}}, !llvm.loop ![[LOOP:[0-9]+]]
// OGCG-W1-DAG: ![[LOOP]] = distinct !{![[LOOP]], {{.*}}}
// OGCG-W1-DAG: !{!"llvm.loop.mustprogress"}
// OGCG-W1-DAG: !{!"llvm.loop.vectorize.predicate.enable", i1 true}
// OGCG-W1-DAG: !{!"llvm.loop.vectorize.width", i32 1}
// OGCG-W1-NOT: !{!"llvm.loop.vectorize.enable", i1 true}
// OGCG-W1-NOT: !{!"llvm.loop.vectorize.enable", i1 false}
#endif
