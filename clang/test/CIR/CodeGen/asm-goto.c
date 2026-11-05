// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

int test_one_label(int cond) {
  //      CIR: cir.func{{.*}}@test_one_label
  //      CIR: cir.asm.goto(x86_att,
  // CIR-NEXT:   out = [],
  // CIR-NEXT:   in = [{{.*}} : !s32i],
  // CIR-NEXT:   in_out = [],
  // CIR-NEXT:   {"testl $0, $0; jne ${1:l};" "r,!i,~{dirflag},~{fpsr},~{flags}"}) side_effects labels ["label_true"]

  // LLVM-LABEL: @test_one_label
  // LLVM: callbr void asm sideeffect
  // LLVM-SAME: "testl $0, $0; jne ${1:l};"
  // LLVM-SAME: "r,!i,~{dirflag},~{fpsr},~{flags}"
  // LLVM: !srcloc ![[SRCLOC1:[0-9]+]]

  // OGCG-LABEL: @test_one_label
  // OGCG: callbr void asm sideeffect
  // OGCG-SAME: "testl $0, $0; jne ${1:l};"
  // OGCG-SAME: "r,!i,~{dirflag},~{fpsr},~{flags}"
  asm volatile goto("testl %0, %0; jne %l1;" :: "r"(cond)::label_true);
  return 0;
label_true:
  return 1;
}

int test_two_labels(int cond) {
  //      CIR: cir.func{{.*}}@test_two_labels
  //      CIR: cir.asm.goto(x86_att,
  // CIR-NEXT:   out = [],
  // CIR-NEXT:   in = [{{.*}} : !s32i],
  // CIR-NEXT:   in_out = [],
  // CIR-NEXT:   {"testl $0, $0; jne ${1:l};" "r,!i,!i,~{dirflag},~{fpsr},~{flags}"}) side_effects labels ["label_true", "loop"]

  // LLVM-LABEL: @test_two_labels
  // LLVM: callbr void asm sideeffect
  // LLVM-SAME: "testl $0, $0; jne ${1:l};"
  // LLVM-SAME: "r,!i,!i,~{dirflag},~{fpsr},~{flags}"
  // LLVM: !srcloc ![[SRCLOC2:[0-9]+]]

  // OGCG-LABEL: @test_two_labels
  // OGCG: callbr void asm sideeffect
  // OGCG-SAME: "testl $0, $0; jne ${1:l};"
  // OGCG-SAME: "r,!i,!i,~{dirflag},~{fpsr},~{flags}"
  asm volatile goto("testl %0, %0; jne %l1;" :: "r"(cond)::label_true, loop);
  return 0;
loop:
  return 0;
label_true:
  return 1;
}

int test_two_asm_goto(int cond) {
  //      CIR: cir.func{{.*}}@test_two_asm_goto
  //      CIR: cir.asm.goto
  //      CIR: cir.asm.goto

  // LLVM-LABEL: @test_two_asm_goto
  // LLVM: callbr void asm sideeffect
  // LLVM: !srcloc ![[SRCLOC3:[0-9]+]]
  // LLVM: callbr void asm sideeffect
  // LLVM: !srcloc ![[SRCLOC4:[0-9]+]]

  // OGCG-LABEL: @test_two_asm_goto
  // OGCG: callbr void asm sideeffect
  // OGCG: callbr void asm sideeffect
  asm volatile goto("testl %0, %0; jne %l1;" :: "r"(cond)::label_true, loop);
  asm volatile goto("testl %0, %0; jne %l2;" :: "r"(cond)::label_true, loop);
  return 0;
loop:
  return 0;
label_true:
  return 1;
}

int test_no_inputs(void) {
  //      CIR: cir.func{{.*}}@test_no_inputs
  //      CIR: cir.asm.goto(x86_att,
  // CIR-NEXT:   out = [],
  // CIR-NEXT:   in = [],
  // CIR-NEXT:   in_out = [],
  // CIR-NEXT:   {"jmp ${0:l}" "!i,~{dirflag},~{fpsr},~{flags}"}) side_effects labels ["done"]

  // LLVM-LABEL: @test_no_inputs
  // LLVM: callbr void asm sideeffect "jmp ${0:l}", "!i,~{dirflag},~{fpsr},~{flags}"()
  // LLVM: !srcloc ![[SRCLOC5:[0-9]+]]

  // OGCG-LABEL: @test_no_inputs
  // OGCG: callbr void asm sideeffect "jmp ${0:l}", "!i,~{dirflag},~{fpsr},~{flags}"()
  asm volatile goto("jmp %l0" ::::done);
  return 0;
done:
  return 1;
}
