// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// Test: annotation args with struct APValue kinds are preserved in CIR attrs
// and lowered to typed LLVM metadata globals (no placeholder i32 0 values).

struct S { int a, b; };

// CIR: cir.global {{.*}} @X
// CIR-SAME: #cir.annotation<name = "test", args = [#cir.int<42> : !s32i, #cir.const_record<{#cir.int<1> : !s32i, #cir.int<2> : !s32i}> : !rec_S]{{.*}}>

// LLVM: %struct.S = type { i32, i32 }
// LLVM: @.args.annot = private unnamed_addr constant { i32, %struct.S } { i32 42, %struct.S { i32 1, i32 2 } }, section "llvm.metadata"
int X __attribute__((annotate("test", 42, (struct S){1, 2})));
