// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM %s < %t-cir.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG %s < %t.ll

// Pointer arithmetic on a pointer-to-VLA must scale the index by the VLA
// element count (the row size), not by the inner element size. Dereferencing
// *(pa + i) where pa has type int(*)[m] must stride by m ints; otherwise the
// row pointer is wrong (previously ClangIR strided by a single int).

int probe(int m, int (*pa)[m], int i) {
  int *row = *(pa + i);
  return row[0];
}
// LLVM-LABEL: @probe(
// LLVM: %[[SCALE:.*]] = mul nsw i64 %{{.*}}, %{{.*}}
// LLVM: getelementptr inbounds i32, ptr %{{.*}}, i64 %[[SCALE]]
// OGCG-LABEL: @probe(
// OGCG: %[[OSCALE:.*]] = mul nsw i64 %{{.*}}, %{{.*}}
// OGCG: getelementptr inbounds i32, ptr %{{.*}}, i64 %[[OSCALE]]
