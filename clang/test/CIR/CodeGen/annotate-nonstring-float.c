// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

// Test that __attribute__((annotate)) with float args works end-to-end.

__attribute__((annotate("test_float", 3.14f))) int x;
// CIR: cir.annotation<name = "test_float", args = [#cir.fp<3.140000e+00> : !cir.float]{{.*}}>
// LLVM: private unnamed_addr constant { float } { float 0x40091EB860000000 }, section "llvm.metadata"

__attribute__((annotate("test_int", 42))) int z;
// CIR: cir.annotation<name = "test_int", args = [#cir.int<42> : !s32i]{{.*}}>
// LLVM: private unnamed_addr constant { i32 } { i32 42 }, section "llvm.metadata"
