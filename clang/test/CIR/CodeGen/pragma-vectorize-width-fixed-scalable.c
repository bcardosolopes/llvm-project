// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -emit-llvm -O1 -disable-llvm-passes %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-linux-gnu -emit-llvm -O1 -disable-llvm-passes %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

void fixed_width_unspecified(int *a, int n) {
#pragma clang loop vectorize_width(fixed)
  for (int i = 0; i < n; ++i)
    a[i] = i;
}

void scalable_width_unspecified(int *a, int n) {
#pragma clang loop vectorize_width(scalable)
  for (int i = 0; i < n; ++i)
    a[i] = i;
}

// CIR-DAG: !{!"llvm.loop.vectorize.scalable.enable", i1 false}
// CIR-DAG: !{!"llvm.loop.vectorize.scalable.enable", i1 true}
// CIR-DAG: !{!"llvm.loop.vectorize.enable", i1 true}
// CIR-NOT: !{!"llvm.loop.vectorize.width", i32 1}

// OGCG-DAG: !{!"llvm.loop.vectorize.scalable.enable", i1 false}
// OGCG-DAG: !{!"llvm.loop.vectorize.scalable.enable", i1 true}
// OGCG-DAG: !{!"llvm.loop.vectorize.enable", i1 true}
// OGCG-NOT: !{!"llvm.loop.vectorize.width", i32 1}
