// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Inline asm calls must have nounwind, matching OGCG's unconditional
// nounwind from UpdateAsmCallInst (CGStmt.cpp:2655).

void test_asm_nounwind(void) {
  __asm__ volatile("nop" ::: "memory");
}

// LLVM-LABEL: define {{.*}}void @test_asm_nounwind
// LLVM: call void asm sideeffect "nop", "~{memory},~{dirflag},~{fpsr},~{flags}"() #[[NOUNWIND:[0-9]+]]
//
// OGCG-LABEL: define {{.*}}void @test_asm_nounwind
// OGCG: call void asm sideeffect "nop", "~{memory},~{dirflag},~{fpsr},~{flags}"() #[[NOUNWIND:[0-9]+]]

int test_asm_with_output(void) {
  int result;
  __asm__ volatile("movl $42, %0" : "=r"(result));
  return result;
}

// LLVM-LABEL: define {{.*}}i32 @test_asm_with_output
// LLVM: call i32 asm sideeffect "movl $$42, $0", "=r,~{dirflag},~{fpsr},~{flags}"() #[[NOUNWIND]]
//
// OGCG-LABEL: define {{.*}}i32 @test_asm_with_output
// OGCG: call i32 asm sideeffect "movl $$42, $0", "=r,~{dirflag},~{fpsr},~{flags}"() #[[NOUNWIND]]

void test_asm_with_input(int x) {
  __asm__ volatile("" :: "r"(x));
}

// LLVM-LABEL: define {{.*}}void @test_asm_with_input
// LLVM: call void asm sideeffect "", "r,~{dirflag},~{fpsr},~{flags}"(i32 %{{.*}}) #[[NOUNWIND]]
//
// OGCG-LABEL: define {{.*}}void @test_asm_with_input
// OGCG: call void asm sideeffect "", "r,~{dirflag},~{fpsr},~{flags}"(i32 %{{.*}}) #[[NOUNWIND]]

int test_asm_memory_clobber_no_sideeffect(int x) {
  int y;
  __asm__("addl $1, $0" : "=r"(y) : "0"(x) : "memory");
  return y;
}

// LLVM-LABEL: define {{.*}}i32 @test_asm_memory_clobber_no_sideeffect
// LLVM: call i32 asm "addl $$1, $$0", "=r,0,~{memory},~{dirflag},~{fpsr},~{flags}"(i32 %{{.*}}) #[[NOUNWIND]]
// LLVM-NOT: memory(none)
// LLVM-NOT: memory(read)
//
// OGCG-LABEL: define {{.*}}i32 @test_asm_memory_clobber_no_sideeffect
// OGCG: call i32 asm "addl $$1, $$0", "=r,0,~{memory},~{dirflag},~{fpsr},~{flags}"(i32 %{{.*}}) #[[NOUNWIND]]
// OGCG-NOT: memory(none)
// OGCG-NOT: memory(read)

// Verify the nounwind attribute group exists.
// LLVM: attributes #[[NOUNWIND]] = { nounwind }
// OGCG: attributes #[[NOUNWIND]] = { nounwind }
