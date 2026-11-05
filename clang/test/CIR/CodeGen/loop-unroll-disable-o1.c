// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O1 -disable-llvm-passes -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O1 -disable-llvm-passes -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll --check-prefix=OGCG %s

int f(int n) {
  int i = 0;
  while (i < n)
    ++i;
  return i;
}

// LLVM-LABEL: define{{.*}} @f
// LLVM: br label %{{.*}}, !llvm.loop ![[LOOP:[0-9]+]]
// LLVM: ![[LOOP]] = distinct !{![[LOOP]], ![[MP:[0-9]+]], ![[UNROLL:[0-9]+]]}
// LLVM: ![[MP]] = !{!"llvm.loop.mustprogress"}
// LLVM: ![[UNROLL]] = !{!"llvm.loop.unroll.disable"}

// OGCG-LABEL: define{{.*}} @f
// OGCG: br label %{{.*}}, !llvm.loop ![[LOOP:[0-9]+]]
// OGCG: ![[LOOP]] = distinct !{![[LOOP]], ![[MP:[0-9]+]], ![[UNROLL:[0-9]+]]}
// OGCG: ![[MP]] = !{!"llvm.loop.mustprogress"}
// OGCG: ![[UNROLL]] = !{!"llvm.loop.unroll.disable"}

