// RUN: %clang_cc1 -triple aarch64-none-linux-android21 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple aarch64-none-linux-android21 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

short b() { return (short){}; }

// CIR-LABEL: b
// CIR: {{%.*}} = cir.alloca !s16i, !cir.ptr<!s16i>, [".compoundliteral"] {alignment = 2 : i64}

// LLVM-LABEL: b
// LLVM: ret i16 0
