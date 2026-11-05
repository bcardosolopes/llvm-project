// RUN: %clang_cc1 -O2 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s
//
// Was XFAIL — fixed by using non-distinct TBAA nodes for
// errno TBAA so the tree is shared with instruction-level TBAA (matching OGCG).
//
// Verify CIR emits a single shared TBAA tree for struct field accesses and
// errno at -O2, with no duplicate distinct nodes.

struct P {
  int a;
  float b;
};

int g(struct P *p, int *q) {
  p->a += *q;
  return p->a;
}

// Verify loads/stores use TBAA tags from a shared tree.
// CHECK: load i32, ptr %{{.*}}, align 4, !tbaa ![[INT_TAG:[0-9]+]]
// CHECK: load i32, ptr %{{.*}}, align 4, !tbaa ![[PA_TAG:[0-9]+]]
// CHECK: store i32 %{{.*}}, ptr %{{.*}}, align 4, !tbaa ![[PA_TAG]]

// Verify errno TBAA shares the same int tag as instruction-level TBAA.
// CHECK: !llvm.errno.tbaa = !{![[INT_TAG]]}

// Verify the TBAA tree structure (non-distinct, shared).
// CHECK-DAG: ![[INT_TAG]] = !{![[INT:[0-9]+]], ![[INT]], i64 0}
// CHECK-DAG: ![[PA_TAG]] = !{![[STRUCT_P:[0-9]+]], ![[INT]], i64 0}
// CHECK-DAG: ![[INT]] = !{!"int", ![[CHAR:[0-9]+]], i64 0}
// CHECK-DAG: ![[CHAR]] = !{!"omnipotent char", ![[ROOT:[0-9]+]], i64 0}
// CHECK-DAG: ![[ROOT]] = !{!"Simple C/C++ TBAA"}
