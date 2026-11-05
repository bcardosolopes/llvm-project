// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

void takes_noescape(__attribute__((noescape)) int *p) { *p = 1; }
void call_it(int *q) { takes_noescape(q); }

// CIR-LABEL: cir.func{{.*}} @takes_noescape(
// CIR-SAME: !cir.ptr<!s32i> {cir.noescape, cir.noundef}

// LLVM-LABEL: define {{.*}} @takes_noescape
// LLVM-SAME: ptr noundef captures(none)

// LLVM-LABEL: define {{.*}} @call_it
// LLVM: call void @takes_noescape(ptr noundef captures(none)
