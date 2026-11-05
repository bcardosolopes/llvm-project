// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir -O1
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll -O1
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll -O1
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Verify that volatile loads/stores through pointer subscript get TBAA metadata.

volatile int *pa;

void test_volatile_subscript(void) {
  pa[0] = pa[1] + pa[2];
}

// CIR: #tbaa[[INT:.*]] = #cir.tbaa_scalar<id = "int"
// CIR-LABEL: @test_volatile_subscript
// CIR: cir.load volatile {{.*}} tbaa(#tbaa[[INT]])
// CIR: cir.load volatile {{.*}} tbaa(#tbaa[[INT]])
// CIR: cir.store volatile {{.*}} tbaa(#tbaa[[INT]])

// LLVM-LABEL: @test_volatile_subscript
// LLVM: load volatile i32, {{.*}} !tbaa
// LLVM: load volatile i32, {{.*}} !tbaa
// LLVM: store volatile i32 {{.*}} !tbaa

// OGCG-LABEL: @test_volatile_subscript
// OGCG: load volatile i32, {{.*}} !tbaa
// OGCG: load volatile i32, {{.*}} !tbaa
// OGCG: store volatile i32 {{.*}} !tbaa
