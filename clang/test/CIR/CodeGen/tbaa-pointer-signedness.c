// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s \
// RUN:   -o %t.cir.ll -O1 -disable-llvm-passes -pointer-tbaa
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s \
// RUN:   -o %t.ogcg.ll -O1 -disable-llvm-passes -pointer-tbaa
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Verify that pointer TBAA names canonicalize signed/unsigned variants
// and map char types to "omnipotent char", matching OGCG behavior.

void unsigned_int_ptr(int *a, unsigned int *b) {
  // Both int* and unsigned int* should use "p1 int".
  *a = *b;
}

// LLVM-LABEL: @unsigned_int_ptr
// LLVM: load ptr, {{.*}}!tbaa ![[P1_INT_TAG:.*]]
// LLVM: load ptr, {{.*}}!tbaa ![[P1_INT_TAG]]

// OGCG-LABEL: @unsigned_int_ptr
// OGCG: load ptr, {{.*}}!tbaa ![[P1_INT_TAG:.*]]
// OGCG: load ptr, {{.*}}!tbaa ![[P1_INT_TAG]]

void unsigned_long_ptr(long *a, unsigned long *b) {
  // Both long* and unsigned long* should use "p1 long".
  *a = *b;
}

// LLVM-LABEL: @unsigned_long_ptr
// LLVM: load ptr, {{.*}}!tbaa ![[P1_LONG_TAG:.*]]
// LLVM: load ptr, {{.*}}!tbaa ![[P1_LONG_TAG]]

// OGCG-LABEL: @unsigned_long_ptr
// OGCG: load ptr, {{.*}}!tbaa ![[P1_LONG_TAG:.*]]
// OGCG: load ptr, {{.*}}!tbaa ![[P1_LONG_TAG]]

void char_ptrs(char *a, unsigned char *b) {
  // Both char* and unsigned char* should use "p1 omnipotent char".
  *a = *b;
}

// LLVM-LABEL: @char_ptrs
// LLVM: load ptr, {{.*}}!tbaa ![[P1_OMNI_TAG:.*]]
// LLVM: load ptr, {{.*}}!tbaa ![[P1_OMNI_TAG]]

// OGCG-LABEL: @char_ptrs
// OGCG: load ptr, {{.*}}!tbaa ![[P1_OMNI_TAG:.*]]
// OGCG: load ptr, {{.*}}!tbaa ![[P1_OMNI_TAG]]

// LLVM-DAG: ![[P1_INT_TAG]] = !{![[P1_INT:.*]], ![[P1_INT]], i64 0}
// LLVM-DAG: ![[P1_INT]] = !{!"p1 int", ![[ANY_PTR:.*]], i64 0}
// LLVM-DAG: ![[P1_LONG_TAG]] = !{![[P1_LONG:.*]], ![[P1_LONG]], i64 0}
// LLVM-DAG: ![[P1_LONG]] = !{!"p1 long", ![[ANY_PTR]], i64 0}
// LLVM-DAG: ![[P1_OMNI_TAG]] = !{![[P1_OMNI:.*]], ![[P1_OMNI]], i64 0}
// LLVM-DAG: ![[P1_OMNI]] = !{!"p1 omnipotent char", ![[ANY_PTR]], i64 0}

// OGCG-DAG: ![[P1_INT_TAG]] = !{![[P1_INT:.*]], ![[P1_INT]], i64 0}
// OGCG-DAG: ![[P1_INT]] = !{!"p1 int", ![[ANY_PTR:.*]], i64 0}
// OGCG-DAG: ![[P1_LONG_TAG]] = !{![[P1_LONG:.*]], ![[P1_LONG]], i64 0}
// OGCG-DAG: ![[P1_LONG]] = !{!"p1 long", ![[ANY_PTR]], i64 0}
// OGCG-DAG: ![[P1_OMNI_TAG]] = !{![[P1_OMNI:.*]], ![[P1_OMNI]], i64 0}
// OGCG-DAG: ![[P1_OMNI]] = !{!"p1 omnipotent char", ![[ANY_PTR]], i64 0}
