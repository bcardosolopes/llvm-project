// Computed goto (labels-as-values) whose target label lives inside a nested
// lexical region (an if-block, a switch case, a loop body, or a plain nested
// compound block). At CIR codegen time the label sits in a nested cir.scope
// region, so it cannot be listed as a cir.indirect_br successor there (MLIR
// forbids cross-region branches). FlattenCFG hoists the block into the single
// function-entry region and GotoSolver then wires it up, so after lowering the
// indirectbr must list the label just like classic codegen does.

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-flat %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIRFLAT
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=OGCG

// Target label inside an if-block.
int in_if(int n) {
  void *l = &&A;
  goto *l;
  if (n > 0) {
  A:
    return n + 1;
  }
  return 0;
}

// After FlattenCFG + GotoSolver the label block is a successor of the
// indirect branch, even though it originated inside the if's nested scope.
// CIRFLAT-LABEL: cir.func {{.*}} @in_if
// CIRFLAT:         cir.indirect_br {{.*}} : !cir.ptr<!void>, [
// CIRFLAT-NEXT:    ^[[LBL:.*]]
// CIRFLAT:         ]
// CIRFLAT:       ^[[LBL]]:
// CIRFLAT:         cir.label "A"

// LLVM-LABEL: define {{.*}} @in_if
// LLVM:         indirectbr ptr %{{.*}}, [label %[[A:.*]]]
// LLVM:       [[A]]:

// OGCG-LABEL: define {{.*}} @in_if
// OGCG:         indirectbr ptr %{{.*}}, [label %[[A:.*]]]

// Target label inside a switch case.
int in_switch(int n) {
  void *l = &&A;
  goto *l;
  switch (n) {
  A:
    return 1;
  case 0:
    return 2;
  }
  return 0;
}

// CIRFLAT-LABEL: cir.func {{.*}} @in_switch
// CIRFLAT:         cir.indirect_br {{.*}} : !cir.ptr<!void>, [
// CIRFLAT:         cir.label "A"

// LLVM-LABEL: define {{.*}} @in_switch
// LLVM:         indirectbr ptr %{{.*}}, [label %{{.*}}]

// OGCG-LABEL: define {{.*}} @in_switch
// OGCG:         indirectbr ptr %{{.*}}, [label %{{.*}}]

// Target label inside a loop body.
int in_loop(int n) {
  void *l = &&A;
  goto *l;
  while (n) {
  A:
    return n + 1;
  }
  return 0;
}

// CIRFLAT-LABEL: cir.func {{.*}} @in_loop
// CIRFLAT:         cir.indirect_br {{.*}} : !cir.ptr<!void>, [
// CIRFLAT:         cir.label "A"

// LLVM-LABEL: define {{.*}} @in_loop
// LLVM:         indirectbr ptr %{{.*}}, [label %{{.*}}]

// OGCG-LABEL: define {{.*}} @in_loop
// OGCG:         indirectbr ptr %{{.*}}, [label %{{.*}}]

// Target label inside a doubly-nested plain compound block.
int in_block(int n) {
  void *l = &&A;
  goto *l;
  {
    {
    A:
      return n + 1;
    }
  }
  return 0;
}

// CIRFLAT-LABEL: cir.func {{.*}} @in_block
// CIRFLAT:         cir.indirect_br {{.*}} : !cir.ptr<!void>, [
// CIRFLAT:         cir.label "A"

// LLVM-LABEL: define {{.*}} @in_block
// LLVM:         indirectbr ptr %{{.*}}, [label %{{.*}}]

// OGCG-LABEL: define {{.*}} @in_block
// OGCG:         indirectbr ptr %{{.*}}, [label %{{.*}}]
