// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s

// Wide string literal array with explicit size larger than the literal.
// The array should be zero-padded to the declared size.

typedef __WCHAR_TYPE__ wchar_t;

wchar_t ws[10] = L"hello";

// CIR: cir.global external @ws = #cir.const_array<[#cir.int<104> : !s32i, #cir.int<101> : !s32i, #cir.int<108> : !s32i, #cir.int<108> : !s32i, #cir.int<111> : !s32i, #cir.int<0> : !s32i, #cir.int<0> : !s32i, #cir.int<0> : !s32i, #cir.int<0> : !s32i, #cir.int<0> : !s32i]> : !cir.array<!s32i x 10>
// LLVM: @ws = global [10 x i32] [i32 104, i32 101, i32 108, i32 108, i32 111, i32 0, i32 0, i32 0, i32 0, i32 0]
// OGCG: @ws = global [10 x i32] [i32 104, i32 101, i32 108, i32 108, i32 111, i32 0, i32 0, i32 0, i32 0, i32 0]
