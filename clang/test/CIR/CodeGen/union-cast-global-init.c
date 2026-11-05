// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=gnu11 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=gnu11 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// CK_ToUnion constant cast in global initializer

union U1 {
  int x;
  char y[16];
};

union U1 global_u2_from_cast = (union U1)3;

// CIR: cir.global external @global_u2_from_cast = #cir.const_record<{#cir.int<3> : !s32i
// LLVM: @global_u2_from_cast = global { i32, [12 x i8] } { i32 3, [12 x i8] zeroinitializer }, align 4
