// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.cir.ll %s
// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

int a;
int b;

void trivial_for() {
  for (;;)
    ;
}

void trivial_while() {
  while (1) {
  }
}

void nontrivial_for() {
  for (; a == b;)
    ;
}

// LLVM: ; Function Attrs: noinline nounwind optnone
// LLVM-LABEL: define dso_local void @_Z11trivial_forv()

// LLVM: ; Function Attrs: noinline nounwind optnone
// LLVM-LABEL: define dso_local void @_Z13trivial_whilev()

// LLVM: ; Function Attrs: mustprogress noinline nounwind optnone
// LLVM-LABEL: define dso_local void @_Z14nontrivial_forv()
// LLVM: br label %{{.*}}, !llvm.loop ![[LOOP:[0-9]+]]
// LLVM: ![[LOOP]] = distinct !{![[LOOP]], ![[MP:[0-9]+]]}
// LLVM: ![[MP]] = !{!"llvm.loop.mustprogress"}

// OGCG: ; Function Attrs: noinline nounwind optnone
// OGCG-LABEL: define dso_local void @_Z11trivial_forv()
//
// OGCG: ; Function Attrs: noinline nounwind optnone
// OGCG-LABEL: define dso_local void @_Z13trivial_whilev()
//
// OGCG: ; Function Attrs: mustprogress noinline nounwind optnone
// OGCG-LABEL: define dso_local void @_Z14nontrivial_forv()
// OGCG: br label %{{.*}}, !llvm.loop ![[OGCG_LOOP:[0-9]+]]
// OGCG: ![[OGCG_LOOP]] = distinct !{![[OGCG_LOOP]], ![[OGCG_MP:[0-9]+]]}
// OGCG: ![[OGCG_MP]] = !{!"llvm.loop.mustprogress"}
