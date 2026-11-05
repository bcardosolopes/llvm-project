// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// An `asm goto` whose labels are backward/self references, with no natural
// fallthrough successor at the source level (a self-loop, or a backward label
// as the last statement before a return), must still get a materialized
// fallthrough block. Otherwise the GotoSolver pass asserts ("asm.goto must have
// a fallthrough block"). Classic CodeGen (OGCG) always emits an asm.fallthrough
// successor; ClangIR must too.

void selfloop(void) {
L:
  asm goto("jmp %l[L]" : : : : L);
}
// LLVM-LABEL: @selfloop(
// LLVM: callbr void asm sideeffect "jmp ${0:l}"
// LLVM: to label %{{.*}} [label %{{.*}}]
// OGCG-LABEL: @selfloop(
// OGCG: callbr void asm sideeffect "jmp ${0:l}"
// OGCG: to label %asm.fallthrough [label %{{.*}}]

int backloop(int x) {
back:
  asm goto("test %0,%0\n\tjnz %l[back]" : : "r"(x) : : back);
  return 1;
}
// LLVM-LABEL: @backloop(
// LLVM: callbr void asm sideeffect
// LLVM: to label %{{.*}} [label %{{.*}}]
// OGCG-LABEL: @backloop(
// OGCG: to label %asm.fallthrough [label %{{.*}}]
