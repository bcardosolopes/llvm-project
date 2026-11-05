// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Test that [[clang::nomerge]] propagates to inline asm when the asm
// is inside an attributed statement (e.g. a compound statement with
// [[clang::nomerge]]).

void foo(void);

void test_asm_no_nomerge() {
  __asm__ volatile("nop");
}

// CIR-LABEL: @_Z19test_asm_no_nomergev
// CIR: cir.asm
// CIR-NOT: cir.nomerge
// CIR: cir.return

// LLVM-LABEL: @_Z19test_asm_no_nomergev
// LLVM: call void asm sideeffect "nop"
// LLVM-NOT: nomerge
// LLVM: ret void

// OGCG-LABEL: @_Z19test_asm_no_nomergev
// OGCG: call void asm sideeffect "nop"
// OGCG-NOT: nomerge
// OGCG: ret void

void test_asm_nomerge() {
  [[clang::nomerge]] {
    __asm__ volatile("nop");
    foo();
  }
}

// CIR-LABEL: @_Z16test_asm_nomergev
// CIR: side_effects {cir.nomerge}

// LLVM-LABEL: @_Z16test_asm_nomergev
// LLVM: call void asm sideeffect "nop", {{.*}} #[[NOMERGE:[0-9]+]]
// LLVM: call void @_Z3foov() #[[NOMERGE_CALL:[0-9]+]]

// OGCG-LABEL: @_Z16test_asm_nomergev
// OGCG: call void asm sideeffect "nop", {{.*}} #[[OGCG_NOMERGE:[0-9]+]]
// OGCG: call void @_Z3foov() #[[OGCG_NOMERGE_CALL:[0-9]+]]

// LLVM-DAG: attributes #[[NOMERGE]] = {{{.*}}nomerge{{.*}}}
// LLVM-DAG: attributes #[[NOMERGE_CALL]] = {{{.*}}nomerge{{.*}}}
// OGCG-DAG: attributes #[[OGCG_NOMERGE]] = {{{.*}}nomerge{{.*}}}
// OGCG-DAG: attributes #[[OGCG_NOMERGE_CALL]] = {{{.*}}nomerge{{.*}}}
