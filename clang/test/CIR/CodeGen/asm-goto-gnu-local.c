// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

// An `asm goto` targeting a GNU-local label (`__label__`) used to crash the CIR
// GotoSolver pass. The label name was recorded raw ("done") while the matching
// cir.label op was mangled ("done$<id>") by getBlockAddressLabelName, so label
// resolution failed. Separately, GotoSolver runs more than once and re-processed
// the already-resolved asm.goto after its cir.label ops were erased, failing the
// lookup on the second run. Both now resolve cleanly to a callbr.

int test_gnu_local(int cond) {
  __label__ done;
  asm goto("testl $0, $0; jne %l[done];" : : "r"(cond) : : done);
  return 0;
done:
  return 1;
}

// LLVM-LABEL: @test_gnu_local
// LLVM: callbr void asm sideeffect
// LLVM-SAME: "testl $$0, $$0; jne ${1:l};"
// LLVM-SAME: "r,!i,~{dirflag},~{fpsr},~{flags}"

// OGCG-LABEL: @test_gnu_local
// OGCG: callbr void asm sideeffect
