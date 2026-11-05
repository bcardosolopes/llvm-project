// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -emit-llvm -O1 -disable-llvm-passes %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// RUN: %clang_cc1 -triple x86_64-linux-gnu -emit-llvm -O1 -disable-llvm-passes %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Verify vectorize_width emits correct loop metadata matching OGCG.

void test_vectorize_width_1(int *a, int n) {
#pragma clang loop vectorize_width(1)
    for (int i = 0; i < n; ++i)
        a[i] = i;
}

void test_vectorize_width_4(int *a, int n) {
#pragma clang loop vectorize_width(4)
    for (int i = 0; i < n; ++i)
        a[i] = i;
}

// vectorize_width(1): width=1 emitted, NOT vectorize.enable=false
// CHECK-DAG: !{!"llvm.loop.vectorize.width", i32 1}
// CHECK-NOT: !{!"llvm.loop.vectorize.enable", i1 false}

// vectorize_width(4): width=4 + vectorize.enable=true
// CHECK-DAG: !{!"llvm.loop.vectorize.width", i32 4}
// CHECK-DAG: !{!"llvm.loop.vectorize.enable", i1 true}

// OGCG-DAG: !{!"llvm.loop.vectorize.width", i32 1}
// OGCG-NOT: !{!"llvm.loop.vectorize.enable", i1 false}
// OGCG-DAG: !{!"llvm.loop.vectorize.width", i32 4}
// OGCG-DAG: !{!"llvm.loop.vectorize.enable", i1 true}
