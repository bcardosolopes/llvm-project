// RUN: %clang_cc1 -triple x86_64-apple-darwin -std=c++11 -fclangir -emit-llvm -O1 -disable-llvm-passes %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Bug 1: when vectorization is enabled and unroll_count is present,
// CIR should emit unroll_count in llvm.loop.vectorize.followup_all, not at
// top-level loop metadata.

void test_followup_unroll_count(int *a, int n) {
#pragma clang loop interleave(enable)
#pragma clang loop interleave_count(4)
#pragma clang loop unroll_count(8)
  for (int i = 0; i < n; ++i)
    a[i] = i;
}

// CHECK: br label {{.*}}, !llvm.loop ![[LOOP:[0-9]+]]
// CHECK-DAG: ![[MP:[0-9]+]] = !{!"llvm.loop.mustprogress"}
// CHECK-DAG: ![[VEC_EN:[0-9]+]] = !{!"llvm.loop.vectorize.enable", i1 true}
// CHECK-DAG: ![[IL4:[0-9]+]] = !{!"llvm.loop.interleave.count", i32 4}
// CHECK-DAG: ![[FOLLOWUP_TAG:[0-9]+]] = !{!"llvm.loop.vectorize.followup_all", ![[MP]], ![[ISV:[0-9]+]], ![[UC8:[0-9]+]]}
// CHECK-DAG: ![[ISV]] = !{!"llvm.loop.isvectorized"}
// CHECK-DAG: ![[UC8]] = !{!"llvm.loop.unroll.count", i32 8}
// CHECK-DAG: ![[LOOP]] = distinct !{![[LOOP]], ![[MP]], ![[VEC_EN]], ![[FOLLOWUP_TAG]], ![[IL4]]}
