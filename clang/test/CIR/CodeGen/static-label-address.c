// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR %s --input-file=%t.cir
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM %s --input-file=%t.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --check-prefix=OGCG %s --input-file=%t.og.ll

// Static local variable initialized with a label address (GNU &&label extension).
// Previously failed verification: 'cir.global' op global with type
// '!cir.ptr<!cir.void>' not yet supported.

void func(void) {
L:;
  static void *label_ptr = &&L;
}

// CIR: cir.global {{.*}} @func.label_ptr = #cir.block_addr
// CIR-SAME: !cir.ptr<!void>

// LLVM: @func.label_ptr = internal global ptr blockaddress(@func,
// OGCG: @func.label_ptr = internal global ptr blockaddress(@func,
