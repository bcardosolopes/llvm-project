// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// RUN: %clang_cc1 -triple x86_64-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Verify inline asm with "unwind" clobber compiles (previously crashed
// with 'cir.asm' op requires attribute 'operand_attrs').

extern "C" void trap();
void caller() {
  asm volatile("call trap" ::: "unwind");
}

// CHECK: call void asm sideeffect "call trap"
// OGCG: call void asm sideeffect {{.*}}"call trap"
