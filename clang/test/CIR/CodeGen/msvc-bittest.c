// RUN: %clang_cc1 -fclangir -emit-llvm -triple x86_64-unknown-linux-gnu \
// RUN:   -fms-extensions %s -o %t.cir.ll
// RUN: FileCheck --check-prefix=CIR %s --input-file=%t.cir.ll
// RUN: %clang_cc1 -emit-llvm -triple x86_64-unknown-linux-gnu \
// RUN:   -fms-extensions %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG %s --input-file=%t.ogcg.ll

// On x86 targets, direct-CIR should match OGCG's dedicated bt/btc/btr/bts path.

volatile unsigned char sink = 0;

// _bittest — read-only, no store needed

// CIR-LABEL: @test_bittest(
// CIR: call i8 asm sideeffect "btl $2, ($1)", "={@ccc},r,r,~{cc},~{memory}

// OGCG-LABEL: @test_bittest(
// OGCG: call i8 asm sideeffect "btl $2, ($1)", "={@ccc}
void test_bittest(int const *a, int b) {
  sink = _bittest(a, b);
}

// _bittestandset — load, or with mask, store

// CIR-LABEL: @test_bittestandset(
// CIR: call i8 asm sideeffect "btsl $2, ($1)", "={@ccc},r,r,~{cc},~{memory}

// OGCG-LABEL: @test_bittestandset(
// OGCG: call i8 asm sideeffect "btsl $2, ($1)", "={@ccc}
void test_bittestandset(int *a, int b) {
  sink = _bittestandset(a, b);
}

// _bittestandreset — load, and with ~mask, store

// CIR-LABEL: @test_bittestandreset(
// CIR: call i8 asm sideeffect "btrl $2, ($1)", "={@ccc},r,r,~{cc},~{memory}

// OGCG-LABEL: @test_bittestandreset(
// OGCG: call i8 asm sideeffect "btrl $2, ($1)", "={@ccc}
void test_bittestandreset(int *a, int b) {
  sink = _bittestandreset(a, b);
}

// _bittestandcomplement — load, xor with mask, store

// CIR-LABEL: @test_bittestandcomplement(
// CIR: call i8 asm sideeffect "btcl $2, ($1)", "={@ccc},r,r,~{cc},~{memory}

// OGCG-LABEL: @test_bittestandcomplement(
// OGCG: call i8 asm sideeffect "btcl $2, ($1)", "={@ccc}
void test_bittestandcomplement(int *a, int b) {
  sink = _bittestandcomplement(a, b);
}

// _interlockedbittestandset — atomicrmw or

// CIR-LABEL: @test_interlockedbittestandset(
// CIR: call i8 asm sideeffect "lock btsl $2, ($1)", "={@ccc},r,r,~{cc},~{memory}

// OGCG-LABEL: @test_interlockedbittestandset(
// OGCG: call i8 asm sideeffect "lock btsl $2, ($1)", "={@ccc}
void test_interlockedbittestandset(volatile int *a, int b) {
  sink = _interlockedbittestandset(a, b);
}

// _interlockedbittestandreset — atomicrmw and with ~mask

// CIR-LABEL: @test_interlockedbittestandreset(
// CIR: call i8 asm sideeffect "lock btrl $2, ($1)", "={@ccc},r,r,~{cc},~{memory}

// OGCG-LABEL: @test_interlockedbittestandreset(
// OGCG: call i8 asm sideeffect "lock btrl $2, ($1)", "={@ccc}
void test_interlockedbittestandreset(volatile int *a, int b) {
  sink = _interlockedbittestandreset(a, b);
}

// _interlockedbittestandset64 — atomicrmw or (64-bit index variant)

// CIR-LABEL: @test_interlockedbittestandset64(
// CIR: call i8 asm sideeffect "lock btsq $2, ($1)", "={@ccc},r,r,~{cc},~{memory}
void test_interlockedbittestandset64(volatile long *a, long b) {
  sink = _interlockedbittestandset64(a, b);
}

// 64-bit variant

// CIR-LABEL: @test_bittest64(
// CIR: call i8 asm sideeffect "btq $2, ($1)", "={@ccc},r,r,~{cc},~{memory}

// OGCG-LABEL: @test_bittest64(
// OGCG: call i8 asm sideeffect "btq $2, ($1)", "={@ccc}
void test_bittest64(long const *a, long b) {
  sink = _bittest64(a, b);
}
