// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Verify that inline asm statements are not dropped after structured control
// flow (if, ternary, loop, scope). The asm op previously had
// RecursiveMemoryEffects which made MLIR's greedy rewriter consider it
// side-effect-free (no regions → no effects) and eliminate it as dead code.

volatile int i, j, k;

void test_asm_after_ternary() {
  asm("nop");
  k ? (i = i) : (j = j);
  asm("nop");
}

// LLVM-LABEL: @_Z22test_asm_after_ternaryv
// LLVM: call void asm sideeffect "nop"
// LLVM: call void asm sideeffect "nop"
//
// OGCG-LABEL: @_Z22test_asm_after_ternaryv
// OGCG: call void asm sideeffect "nop"
// OGCG: call void asm sideeffect "nop"

void test_asm_after_if() {
  asm("nop");
  if (k)
    i = j;
  asm("nop");
}

// LLVM-LABEL: @_Z17test_asm_after_ifv
// LLVM: call void asm sideeffect "nop"
// LLVM: call void asm sideeffect "nop"
//
// OGCG-LABEL: @_Z17test_asm_after_ifv
// OGCG: call void asm sideeffect "nop"
// OGCG: call void asm sideeffect "nop"

void test_asm_after_scope() {
  asm("nop");
  { i = j; }
  asm("nop");
}

// LLVM-LABEL: @_Z20test_asm_after_scopev
// LLVM: call void asm sideeffect "nop"
// LLVM: call void asm sideeffect "nop"
//
// OGCG-LABEL: @_Z20test_asm_after_scopev
// OGCG: call void asm sideeffect "nop"
// OGCG: call void asm sideeffect "nop"
