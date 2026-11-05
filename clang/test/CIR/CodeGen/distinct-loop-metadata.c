// RUN: %clang_cc1 -std=c17 -triple x86_64-unknown-linux-gnu \
// RUN:     -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s
// RUN: %clang_cc1 -std=c17 -triple x86_64-unknown-linux-gnu \
// RUN:     -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Verify that each loop gets its own distinct !llvm.loop metadata node.

void test(int *p, int n) {
  for (int i = 0; i < n; i++)
    p[i] = 1;
  for (int j = 0; j < n; j++)
    p[j] = 2;
  for (int k = 0; k < n; k++)
    p[k] = 3;
}

// Each loop backedge must reference a distinct metadata node.
// CHECK-LABEL: define {{.*}} @test(
// CHECK:   br label %{{.*}}, !llvm.loop ![[L1:[0-9]+]]
// CHECK:   br label %{{.*}}, !llvm.loop ![[L2:[0-9]+]]
// CHECK:   br label %{{.*}}, !llvm.loop ![[L3:[0-9]+]]

// Verify each is a distinct self-referencing node with mustprogress.
// CHECK-DAG: ![[L1]] = distinct !{![[L1]], ![[MP:[0-9]+]]}
// CHECK-DAG: ![[L2]] = distinct !{![[L2]], ![[MP]]}
// CHECK-DAG: ![[L3]] = distinct !{![[L3]], ![[MP]]}
// CHECK-DAG: ![[MP]] = !{!"llvm.loop.mustprogress"}

// OGCG-LABEL: define {{.*}} @test(
// OGCG:   br label %{{.*}}, !llvm.loop ![[L1:[0-9]+]]
// OGCG:   br label %{{.*}}, !llvm.loop ![[L2:[0-9]+]]
// OGCG:   br label %{{.*}}, !llvm.loop ![[L3:[0-9]+]]
// OGCG-DAG: ![[L1]] = distinct !{![[L1]], ![[MP:[0-9]+]]}
// OGCG-DAG: ![[L2]] = distinct !{![[L2]], ![[MP]]}
// OGCG-DAG: ![[L3]] = distinct !{![[L3]], ![[MP]]}
// OGCG-DAG: ![[MP]] = !{!"llvm.loop.mustprogress"}
