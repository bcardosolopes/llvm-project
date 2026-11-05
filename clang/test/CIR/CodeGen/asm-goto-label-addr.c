// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR %s --input-file=%t.cir
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM %s --input-file=%t.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --check-prefix=OGCG %s --input-file=%t.og.ll
// REQUIRES: x86-registered-target

// Asm goto combined with label address (&&label) used to crash in
// MLIR's connectPHINodes because the indirectGotoBlock was placed between
// the asm.goto and its fallthrough block, causing GotoSolver to wire the
// wrong block as the fallthrough successor.

// Matches test7 from clang/test/CodeGen/asm-goto.c.
void *test_label_addr(void) {
  void *p = &&foo;
  asm goto ("# %0\n\t# %l2":"+r"(p):::foo);
foo:
  return p;
}

// CIR-LABEL: @test_label_addr
// CIR:         cir.asm.goto
// CIR:         -> !cir.ptr<!void>
// CIR:         cir.store

// The callbr must produce the pointer result and have a split block.
// LLVM-LABEL: @test_label_addr
// LLVM:         %[[CBR:.*]] = callbr ptr asm "# $0\0A\09# ${2:l}"
// LLVM-SAME:    "=r,0,!i,~{dirflag},~{fpsr},~{flags}"
// LLVM-SAME:    #[[ASM_NONE:[0-9]+]]
// LLVM:         store ptr %[[CBR]], ptr

// OGCG-LABEL: @test_label_addr
// OGCG:         %[[OGCBR:.*]] = callbr ptr asm "# $0\0A\09# ${2:l}"
// OGCG-SAME:    "=r,0,!i,~{dirflag},~{fpsr},~{flags}"
// OGCG-SAME:    #[[OG_ASM_NONE:[0-9]+]]
// OGCG:         store ptr %[[OGCBR]], ptr

// Matches test8 from clang/test/CodeGen/asm-goto.c (symbolic name variant).
void *test_label_addr_symbolic(void) {
  void *p = &&foo;
  asm goto ("# %0\n\t# %l[foo]":"+r"(p):::foo);
foo:
  return p;
}

// CIR-LABEL: @test_label_addr_symbolic
// CIR:         cir.asm.goto
// CIR:         -> !cir.ptr<!void>
// CIR:         cir.store

// LLVM-LABEL: @test_label_addr_symbolic
// LLVM:         callbr ptr asm "# $0\0A\09# ${2:l}"

// OGCG-LABEL: @test_label_addr_symbolic
// OGCG:         callbr ptr asm "# $0\0A\09# ${2:l}"

int g;

// A memory operand must carry elementtype through direct-CIR asm.goto lowering
// so the backend accepts the indirect constraint.
void test_memory_operand(void) {
  asm goto ("# %0" :: "m"(g) :: done);
done:
  return;
}

// CIR-LABEL: @test_memory_operand
// CIR:         %[[G:.*]] = cir.get_global @g : !cir.ptr<!s32i>
// CIR:         cir.asm.goto(x86_att,
// CIR:           in = [%[[G]] : !cir.ptr<!s32i> (maybe_memory)],
// CIR:           {"# $0" "*m,!i,~{dirflag},~{fpsr},~{flags}"}

// LLVM-LABEL: @test_memory_operand
// LLVM:         callbr void asm sideeffect "# $0", "*m,!i,~{dirflag},~{fpsr},~{flags}"(ptr elementtype(i32) @g)

// OGCG-LABEL: @test_memory_operand
// OGCG:         callbr void asm sideeffect "# $0", "*m,!i,~{dirflag},~{fpsr},~{flags}"(ptr elementtype(i32) @g)

int test_memory_read_output(void) {
  int out;
  asm goto ("# %0 %1" : "=r"(out) : "m"(g) :: done);
  return out;
done:
  return out;
}

// LLVM-LABEL: @test_memory_read_output
// LLVM:         callbr i32 asm "# $0 $1", "=r,*m,!i,~{dirflag},~{fpsr},~{flags}"(ptr elementtype(i32) @g) #[[ASM_READ:[0-9]+]]

// OGCG-LABEL: @test_memory_read_output
// OGCG:         callbr i32 asm "# $0 $1", "=r,*m,!i,~{dirflag},~{fpsr},~{flags}"(ptr elementtype(i32) @g) #[[OG_ASM_READ:[0-9]+]]

// LLVM:         attributes #[[ASM_NONE]] = { nounwind memory(none) }
// LLVM:         attributes #[[ASM_READ]] = { nounwind memory(read) }

// OGCG:         attributes #[[OG_ASM_NONE]] = { nounwind memory(none) }
// OGCG:         attributes #[[OG_ASM_READ]] = { nounwind memory(read) }
