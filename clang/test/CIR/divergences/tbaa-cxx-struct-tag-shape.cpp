// RUN: %clang --target=x86_64-unknown-linux-gnu -S -emit-llvm -O2 -fno-clangir %s -o %t.og.ll
// RUN: %clang --target=x86_64-unknown-linux-gnu -S -emit-llvm -O2 -fclangir %s -o %t.cir.ll
// RUN: FileCheck --check-prefix=COMMON --input-file=%t.og.ll %s
// RUN: FileCheck --check-prefix=COMMON --input-file=%t.cir.ll %s
//
// Was XFAIL — fixed by aligning C++ struct-field TBAA
// tag/tree shape at -O2.

struct S { int x; };
int f(S *s) { return s->x; }

// COMMON-LABEL: define dso_local noundef i32 @_Z1fP1S(
// COMMON: load i32, ptr {{.*}}, align 4, !tbaa ![[S_TAG:[0-9]+]]
// COMMON: !llvm.errno.tbaa = !{![[INT_TAG:[0-9]+]]}
// COMMON-DAG: ![[INT_TAG]] = !{![[INT:[0-9]+]], ![[INT]], i64 0}
// COMMON-DAG: ![[S_TAG]] = !{![[S_TYPE:[0-9]+]], ![[INT]], i64 0}
// COMMON-DAG: ![[INT]] = !{!"int", ![[CHAR:[0-9]+]], i64 0}
// COMMON-DAG: ![[CHAR]] = !{!"omnipotent char", ![[ROOT:[0-9]+]], i64 0}
