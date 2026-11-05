// RUN: %clang --target=x86_64-unknown-linux-gnu -S -emit-llvm -O2 -fno-clangir %s -o %t.og.ll
// RUN: %clang --target=x86_64-unknown-linux-gnu -S -emit-llvm -O2 -fclangir %s -o %t.cir.ll
// RUN: FileCheck --check-prefix=COMMON --input-file=%t.og.ll %s
// RUN: FileCheck --check-prefix=COMMON --input-file=%t.cir.ll %s
//
// Was XFAIL — fixed by aligning scalar TBAA tag shape.
//
// Verify scalar loads use the shared int tag and that errno TBAA references
// the same node, matching OGCG.

int f(int *p, int *q) { return *p + *q; }

// COMMON-LABEL: define dso_local i32 @f(
// COMMON: load i32, ptr {{.*}}, align 4, !tbaa ![[INT_TAG:[0-9]+]]
// COMMON: load i32, ptr {{.*}}, align 4, !tbaa ![[INT_TAG]]
// COMMON: !llvm.errno.tbaa = !{![[INT_TAG]]}
// COMMON-DAG: ![[INT_TAG]] = !{![[INT:[0-9]+]], ![[INT]], i64 0}
// COMMON-DAG: ![[INT]] = !{!"int", ![[CHAR:[0-9]+]], i64 0}
// COMMON-DAG: ![[CHAR]] = !{!"omnipotent char", ![[ROOT:[0-9]+]], i64 0}
// COMMON-DAG: ![[ROOT]] = !{!"Simple C/C++ TBAA"}
